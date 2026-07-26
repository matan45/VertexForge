#include "MsdfShapeBridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <msdfgen.h>
#include <core/ShapeDistanceFinder.h>
#include <core/pixel-conversion.hpp>

namespace types
{
    namespace
    {
        constexpr double MTSDF_ANGLE_THRESHOLD = 3.0;
        constexpr double MTSDF_MITER_LIMIT = 1.0;
        constexpr size_t MAX_MTSDF_GLYPH_PIXELS = 4096u * 4096u;

        struct FtOutlineContext
        {
            double scale = 1.0;
            msdfgen::Point2 position;
            msdfgen::Shape* shape = nullptr;
            msdfgen::Contour* contour = nullptr;
        };

        msdfgen::Point2 ftPoint(const FT_Vector& vector, double scale)
        {
            return {scale * static_cast<double>(vector.x),
                    scale * static_cast<double>(vector.y)};
        }

        int ftMoveTo(const FT_Vector* to, void* user)
        {
            auto* context = static_cast<FtOutlineContext*>(user);
            if (!(context->contour && context->contour->edges.empty()))
                context->contour = &context->shape->addContour();
            context->position = ftPoint(*to, context->scale);
            return 0;
        }

        int ftLineTo(const FT_Vector* to, void* user)
        {
            auto* context = static_cast<FtOutlineContext*>(user);
            const msdfgen::Point2 endpoint = ftPoint(*to, context->scale);
            if (endpoint != context->position)
            {
                context->contour->addEdge(msdfgen::EdgeHolder(context->position, endpoint));
                context->position = endpoint;
            }
            return 0;
        }

        int ftConicTo(const FT_Vector* control, const FT_Vector* to, void* user)
        {
            auto* context = static_cast<FtOutlineContext*>(user);
            const msdfgen::Point2 endpoint = ftPoint(*to, context->scale);
            if (endpoint != context->position)
            {
                context->contour->addEdge(msdfgen::EdgeHolder(
                    context->position, ftPoint(*control, context->scale), endpoint));
                context->position = endpoint;
            }
            return 0;
        }

        int ftCubicTo(const FT_Vector* control1, const FT_Vector* control2,
                      const FT_Vector* to, void* user)
        {
            auto* context = static_cast<FtOutlineContext*>(user);
            const msdfgen::Point2 endpoint = ftPoint(*to, context->scale);
            const msdfgen::Point2 c1 = ftPoint(*control1, context->scale);
            const msdfgen::Point2 c2 = ftPoint(*control2, context->scale);
            if (endpoint != context->position ||
                msdfgen::crossProduct(c1 - endpoint, c2 - endpoint) != 0.0)
            {
                context->contour->addEdge(
                    msdfgen::EdgeHolder(context->position, c1, c2, endpoint));
                context->position = endpoint;
            }
            return 0;
        }

        bool finiteBounds(const msdfgen::Shape::Bounds& bounds)
        {
            return std::isfinite(bounds.l) && std::isfinite(bounds.b) &&
                   std::isfinite(bounds.r) && std::isfinite(bounds.t);
        }

        bool calculateBox(const msdfgen::Shape& shape, double scale, double pxRange,
                          int& width, int& height, msdfgen::Vector2& translate)
        {
            const msdfgen::Range range(pxRange / scale);
            msdfgen::Shape::Bounds bounds = shape.getBounds();
            if (!finiteBounds(bounds) || bounds.l > bounds.r || bounds.b > bounds.t)
                return false;

            bounds.l += range.lower;
            bounds.b += range.lower;
            bounds.r -= range.lower;
            bounds.t -= range.lower;
            shape.boundMiters(bounds.l, bounds.b, bounds.r, bounds.t,
                              -range.lower, MTSDF_MITER_LIMIT, 1);

            if (!finiteBounds(bounds) || bounds.l >= bounds.r || bounds.b >= bounds.t)
                return false;

            const double boxWidth = scale * (bounds.r - bounds.l);
            const double boxHeight = scale * (bounds.t - bounds.b);
            constexpr double MAX_DIMENSION =
                static_cast<double>((std::numeric_limits<int>::max)() - 1);
            if (!std::isfinite(boxWidth) || !std::isfinite(boxHeight) ||
                boxWidth < 0.0 || boxHeight < 0.0 ||
                boxWidth > MAX_DIMENSION || boxHeight > MAX_DIMENSION)
            {
                return false;
            }

            width = static_cast<int>(std::ceil(boxWidth)) + 1;
            height = static_cast<int>(std::ceil(boxHeight)) + 1;
            translate.x = -bounds.l + 0.5 * (static_cast<double>(width) - boxWidth) / scale;
            translate.y = -bounds.b + 0.5 * (static_cast<double>(height) - boxHeight) / scale;
            return width > 0 && height > 0 &&
                   std::isfinite(translate.x) && std::isfinite(translate.y);
        }
    }

    MtsdfGlyphResult generateMtsdfGlyph(FT_Face face, FT_UInt glyphIndex,
                                         double pxPerEm, double pxRange)
    {
        MtsdfGlyphResult result;
        if (!face || !FT_IS_SCALABLE(face) || FT_IS_TRICKY(face) ||
            face->units_per_EM == 0 || !std::isfinite(pxPerEm) ||
            !std::isfinite(pxRange) || pxPerEm <= 0.0 || pxRange <= 0.0 ||
            pxPerEm > static_cast<double>((std::numeric_limits<FT_UInt>::max)()) ||
            pxPerEm > static_cast<double>((std::numeric_limits<long>::max)()))
        {
            return result;
        }

        const FT_UInt pixelSize = static_cast<FT_UInt>(std::lround(pxPerEm));
        if (pixelSize == 0 || FT_Set_Pixel_Sizes(face, 0, pixelSize) != 0 ||
            FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT | FT_LOAD_NO_SVG) != 0)
        {
            return result;
        }
        result.advanceX = static_cast<float>(face->glyph->advance.x) / 64.0f;
        if (!std::isfinite(result.advanceX))
            return result;

        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE | FT_LOAD_NO_SVG) != 0 ||
            face->glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        {
            return result;
        }

        msdfgen::Shape shape;
        shape.setYAxisOrientation(msdfgen::Y_UPWARD);
        FtOutlineContext context;
        context.scale = 1.0 / static_cast<double>(face->units_per_EM);
        context.shape = &shape;

        FT_Outline_Funcs functions{};
        functions.move_to = &ftMoveTo;
        functions.line_to = &ftLineTo;
        functions.conic_to = &ftConicTo;
        functions.cubic_to = &ftCubicTo;
        functions.shift = 0;
        functions.delta = 0;
        if (FT_Outline_Decompose(&face->glyph->outline, &functions, &context) != 0)
            return result;

        if (!shape.contours.empty() && shape.contours.back().edges.empty())
            shape.contours.pop_back();
        if (shape.contours.empty())
        {
            result.valid = true;
            result.empty = true;
            return result;
        }
        if (!shape.validate())
            return result;

        shape.normalize();
        const msdfgen::Shape::Bounds bounds = shape.getBounds();
        if (!finiteBounds(bounds))
            return result;

        const msdfgen::Point2 outside(
            bounds.l - (bounds.r - bounds.l) - 1.0,
            bounds.b - (bounds.t - bounds.b) - 1.0);
        if (msdfgen::SimpleTrueShapeDistanceFinder::oneShotDistance(shape, outside) > 0.0)
        {
            for (auto& contour : shape.contours)
                contour.reverse();
        }
        msdfgen::edgeColoringSimple(shape, MTSDF_ANGLE_THRESHOLD, 0);

        msdfgen::Vector2 translate;
        if (!calculateBox(shape, pxPerEm, pxRange, result.width, result.height, translate))
            return result;

        const size_t width = static_cast<size_t>(result.width);
        const size_t height = static_cast<size_t>(result.height);
        if (result.width > (std::numeric_limits<int>::max)() / 4 ||
            width > (std::numeric_limits<size_t>::max)() / height)
        {
            return result;
        }
        const size_t pixelCount = width * height;
        if (pixelCount > MAX_MTSDF_GLYPH_PIXELS ||
            pixelCount > (std::numeric_limits<size_t>::max)() /
                             (4 * sizeof(float)))
        {
            return result;
        }

        std::vector<float> floatPixels(pixelCount * 4);
        msdfgen::BitmapSection<float, 4> bitmap(
            floatPixels.data(), result.width, result.height, 4 * result.width,
            msdfgen::Y_DOWNWARD);
        const msdfgen::SDFTransformation transformation(
            msdfgen::Projection(msdfgen::Vector2(pxPerEm), translate),
            msdfgen::Range(pxRange / pxPerEm));
        msdfgen::generateMTSDF(bitmap, shape, transformation, msdfgen::MSDFGeneratorConfig{});

        result.pixels.resize(floatPixels.size());
        std::transform(floatPixels.begin(), floatPixels.end(), result.pixels.begin(),
                       [](float value) { return msdfgen::pixelFloatToByte(value); });

        result.bearingX = static_cast<float>(-pxPerEm * translate.x);
        result.bearingY =
            static_cast<float>(static_cast<double>(result.height) - pxPerEm * translate.y);
        if (!std::isfinite(result.bearingX) || !std::isfinite(result.bearingY))
        {
            result = {};
            return result;
        }

        result.valid = true;
        return result;
    }
}
