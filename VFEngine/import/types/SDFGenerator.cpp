#include "SDFGenerator.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace types
{
    // Large distance sentinel
    static constexpr float INF_DIST = 1e10f;

    void SDFGenerator::computeDistanceField(
        const std::vector<bool>& binaryImage,
        int width, int height,
        std::vector<float>& distanceField)
    {
        // 8SSEDT: two-pass sweep using (dx, dy) vectors for accurate Euclidean distance
        struct Point { int dx, dy; };

        const int size = width * height;
        std::vector<Point> grid(size);

        // Initialize: boundary pixels get (0,0), others get large sentinel
        for (int i = 0; i < size; i++)
        {
            if (binaryImage[i])
            {
                grid[i] = {0, 0};
            }
            else
            {
                grid[i] = {9999, 9999};
            }
        }

        auto dist = [](const Point& p) -> float
        {
            return std::sqrt(static_cast<float>(p.dx * p.dx + p.dy * p.dy));
        };

        // Forward pass: top-left to bottom-right
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                int idx = y * width + x;
                Point& p = grid[idx];

                auto tryNeighbor = [&](int nx, int ny, int odx, int ody)
                {
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height)
                    {
                        Point& n = grid[ny * width + nx];
                        int ndx = n.dx + odx;
                        int ndy = n.dy + ody;
                        if (ndx * ndx + ndy * ndy < p.dx * p.dx + p.dy * p.dy)
                        {
                            p.dx = ndx;
                            p.dy = ndy;
                        }
                    }
                };

                tryNeighbor(x - 1, y - 1, 1, 1);
                tryNeighbor(x,     y - 1, 0, 1);
                tryNeighbor(x + 1, y - 1, -1, 1);
                tryNeighbor(x - 1, y,     1, 0);
            }
        }

        // Backward pass: bottom-right to top-left
        for (int y = height - 1; y >= 0; y--)
        {
            for (int x = width - 1; x >= 0; x--)
            {
                int idx = y * width + x;
                Point& p = grid[idx];

                auto tryNeighbor = [&](int nx, int ny, int odx, int ody)
                {
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height)
                    {
                        Point& n = grid[ny * width + nx];
                        int ndx = n.dx + odx;
                        int ndy = n.dy + ody;
                        if (ndx * ndx + ndy * ndy < p.dx * p.dx + p.dy * p.dy)
                        {
                            p.dx = ndx;
                            p.dy = ndy;
                        }
                    }
                };

                tryNeighbor(x + 1, y + 1, -1, -1);
                tryNeighbor(x,     y + 1, 0, -1);
                tryNeighbor(x - 1, y + 1, 1, -1);
                tryNeighbor(x + 1, y,     -1, 0);
            }
        }

        // Convert to float distances
        distanceField.resize(size);
        for (int i = 0; i < size; i++)
        {
            distanceField[i] = dist(grid[i]);
        }
    }

    SDFResult SDFGenerator::generateFromBitmap(
        const unsigned char* srcBitmap,
        int srcWidth, int srcHeight,
        int targetWidth, int targetHeight,
        float spread,
        uint8_t onEdgeValue,
        uint8_t threshold)
    {
        SDFResult result;

        if (!srcBitmap || srcWidth <= 0 || srcHeight <= 0 ||
            targetWidth <= 0 || targetHeight <= 0 || spread <= 0.0f)
        {
            return result;
        }

        const int srcSize = srcWidth * srcHeight;

        // Step 1: Threshold the source bitmap to binary
        std::vector<bool> insideImage(srcSize, false);
        std::vector<bool> outsideImage(srcSize, false);

        for (int i = 0; i < srcSize; i++)
        {
            bool inside = srcBitmap[i] >= threshold;
            // For inside distance: mark inside pixels as "on the feature"
            insideImage[i] = inside;
            // For outside distance: mark outside pixels as "on the feature"
            outsideImage[i] = !inside;
        }

        // Step 2: Compute distance fields for both inside and outside
        std::vector<float> insideDist, outsideDist;
        computeDistanceField(insideImage, srcWidth, srcHeight, outsideDist);   // distance from outside pixels to nearest inside
        computeDistanceField(outsideImage, srcWidth, srcHeight, insideDist);   // distance from inside pixels to nearest outside

        // Step 3: Compute signed distance at source resolution
        // Positive = inside, Negative = outside
        std::vector<float> signedDist(srcSize);
        for (int i = 0; i < srcSize; i++)
        {
            if (insideImage[i])
            {
                // Inside the glyph: distance to nearest outside edge
                signedDist[i] = insideDist[i];
            }
            else
            {
                // Outside the glyph: negative distance to nearest inside edge
                signedDist[i] = -outsideDist[i];
            }
        }

        // Step 4: Downsample from source resolution to target resolution
        // and normalize to [0, 255]
        float scaleX = static_cast<float>(srcWidth) / static_cast<float>(targetWidth);
        float scaleY = static_cast<float>(srcHeight) / static_cast<float>(targetHeight);

        // Spread in source pixels (spread is specified in target pixels)
        float spreadSrc = spread * scaleX;

        result.width = targetWidth;
        result.height = targetHeight;
        result.pixels.resize(targetWidth * targetHeight);

        float pixelDistScale = static_cast<float>(onEdgeValue) / spreadSrc;

        for (int ty = 0; ty < targetHeight; ty++)
        {
            for (int tx = 0; tx < targetWidth; tx++)
            {
                // Map target pixel center to source coordinates
                float sx = (static_cast<float>(tx) + 0.5f) * scaleX;
                float sy = (static_cast<float>(ty) + 0.5f) * scaleY;

                // Bilinear sample from the signed distance field
                int x0 = static_cast<int>(sx - 0.5f);
                int y0 = static_cast<int>(sy - 0.5f);
                int x1 = x0 + 1;
                int y1 = y0 + 1;

                x0 = std::clamp(x0, 0, srcWidth - 1);
                x1 = std::clamp(x1, 0, srcWidth - 1);
                y0 = std::clamp(y0, 0, srcHeight - 1);
                y1 = std::clamp(y1, 0, srcHeight - 1);

                float fx = sx - 0.5f - static_cast<float>(static_cast<int>(sx - 0.5f));
                float fy = sy - 0.5f - static_cast<float>(static_cast<int>(sy - 0.5f));
                fx = std::clamp(fx, 0.0f, 1.0f);
                fy = std::clamp(fy, 0.0f, 1.0f);

                float d00 = signedDist[y0 * srcWidth + x0];
                float d10 = signedDist[y0 * srcWidth + x1];
                float d01 = signedDist[y1 * srcWidth + x0];
                float d11 = signedDist[y1 * srcWidth + x1];

                float d = d00 * (1 - fx) * (1 - fy)
                        + d10 * fx * (1 - fy)
                        + d01 * (1 - fx) * fy
                        + d11 * fx * fy;

                // Convert signed distance to pixel value
                float value = d * pixelDistScale + static_cast<float>(onEdgeValue);
                value = std::clamp(value, 0.0f, 255.0f);

                result.pixels[ty * targetWidth + tx] = static_cast<unsigned char>(value + 0.5f);
            }
        }

        return result;
    }
}
