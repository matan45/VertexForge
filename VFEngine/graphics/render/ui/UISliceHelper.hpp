#pragma once

#include "UIRenderTypes.hpp"
#include <components/UIComponents.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

namespace render::ui
{
    inline void generateSlicedInstances(
        glm::vec2 elementPos, glm::vec2 elementSize,
        glm::vec4 border, // left, right, top, bottom (source texture pixels)
        uint32_t texWidth, uint32_t texHeight,
        glm::vec4 colorTint,
        glm::vec4 scissorRect,
        const std::string& texturePath,
        components::UIImageType imageType,
        std::vector<UIImageRenderData>& outDrawList)
    {
        float tw = static_cast<float>(texWidth);
        float th = static_cast<float>(texHeight);
        float ew = elementSize.x;
        float eh = elementSize.y;

        float bL = border.x;
        float bR = border.y;
        float bT = border.z;
        float bB = border.w;

        // Clamp borders if they exceed element size
        if (bL + bR > ew)
        {
            float scale = ew / (bL + bR);
            bL *= scale;
            bR *= scale;
        }
        if (bT + bB > eh)
        {
            float scale = eh / (bT + bB);
            bT *= scale;
            bB *= scale;
        }

        // UV borders (normalized 0-1)
        float uL = bL / tw;
        float uR = bR / tw;
        float vT = bT / th;
        float vB = bB / th;

        // Screen-space column widths and x-offsets
        float colW[3] = {bL, ew - bL - bR, bR};
        float colX[3] = {0.0f, bL, ew - bR};

        // Screen-space row heights and y-offsets
        float rowH[3] = {bT, eh - bT - bB, bB};
        float rowY[3] = {0.0f, bT, eh - bB};

        // UV column boundaries
        float uCol[4] = {0.0f, uL, 1.0f - uR, 1.0f};
        // UV row boundaries
        float vRow[4] = {0.0f, vT, 1.0f - vB, 1.0f};

        auto emitPatch = [&](float px, float py, float pw, float ph,
                             float u0, float v0, float u1, float v1)
        {
            if (pw <= 0.0f || ph <= 0.0f)
                return;

            UIImageRenderData rd;
            rd.texturePath = texturePath;
            rd.position = glm::vec2(px, py);
            rd.size = glm::vec2(pw, ph);
            rd.colorTint = colorTint;
            rd.scissorRect = scissorRect;
            rd.uvRect = glm::vec4(u0, v0, u1, v1);
            outDrawList.push_back(std::move(rd));
        };

        if (imageType == components::UIImageType::Sliced)
        {
            // Emit 9 patches (corners, edges, center) with stretched UVs
            for (int row = 0; row < 3; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    emitPatch(
                        elementPos.x + colX[col],
                        elementPos.y + rowY[row],
                        colW[col],
                        rowH[row],
                        uCol[col], vRow[row],
                        uCol[col + 1], vRow[row + 1]);
                }
            }
        }
        else if (imageType == components::UIImageType::Tiled)
        {
            // Corners: same as sliced (fixed size, no tiling)
            // [0,0] top-left corner
            emitPatch(elementPos.x + colX[0], elementPos.y + rowY[0],
                      colW[0], rowH[0], uCol[0], vRow[0], uCol[1], vRow[1]);
            // [0,2] top-right corner
            emitPatch(elementPos.x + colX[2], elementPos.y + rowY[0],
                      colW[2], rowH[0], uCol[2], vRow[0], uCol[3], vRow[1]);
            // [2,0] bottom-left corner
            emitPatch(elementPos.x + colX[0], elementPos.y + rowY[2],
                      colW[0], rowH[2], uCol[0], vRow[2], uCol[1], vRow[3]);
            // [2,2] bottom-right corner
            emitPatch(elementPos.x + colX[2], elementPos.y + rowY[2],
                      colW[2], rowH[2], uCol[2], vRow[2], uCol[3], vRow[3]);

            // Source sizes for tiling regions (in pixels)
            float srcCenterW = tw * (uCol[2] - uCol[1]);
            float srcCenterH = th * (vRow[2] - vRow[1]);

            // Helper: tile a region along one or both axes
            auto emitTiled = [&](float startX, float startY, float totalW, float totalH,
                                 float tileW, float tileH,
                                 float u0, float v0, float u1, float v1)
            {
                if (totalW <= 0.0f || totalH <= 0.0f || tileW <= 0.0f || tileH <= 0.0f)
                    return;

                // Cap tile count to prevent extreme instance counts
                int tilesX = std::min(static_cast<int>(std::ceil(totalW / tileW)), 256);
                int tilesY = std::min(static_cast<int>(std::ceil(totalH / tileH)), 256);

                for (int ty = 0; ty < tilesY; ++ty)
                {
                    float py = startY + ty * tileH;
                    float ph = std::min(tileH, totalH - ty * tileH);
                    float tv1 = v0 + (v1 - v0) * (ph / tileH);

                    for (int tx = 0; tx < tilesX; ++tx)
                    {
                        float px = startX + tx * tileW;
                        float pw = std::min(tileW, totalW - tx * tileW);
                        float tu1 = u0 + (u1 - u0) * (pw / tileW);

                        emitPatch(px, py, pw, ph, u0, v0, tu1, tv1);
                    }
                }
            };

            // Top edge [0,1]: tile horizontally
            emitTiled(elementPos.x + colX[1], elementPos.y + rowY[0],
                      colW[1], rowH[0],
                      srcCenterW, rowH[0],
                      uCol[1], vRow[0], uCol[2], vRow[1]);

            // Bottom edge [2,1]: tile horizontally
            emitTiled(elementPos.x + colX[1], elementPos.y + rowY[2],
                      colW[1], rowH[2],
                      srcCenterW, rowH[2],
                      uCol[1], vRow[2], uCol[2], vRow[3]);

            // Left edge [1,0]: tile vertically
            emitTiled(elementPos.x + colX[0], elementPos.y + rowY[1],
                      colW[0], rowH[1],
                      colW[0], srcCenterH,
                      uCol[0], vRow[1], uCol[1], vRow[2]);

            // Right edge [1,2]: tile vertically
            emitTiled(elementPos.x + colX[2], elementPos.y + rowY[1],
                      colW[2], rowH[1],
                      colW[2], srcCenterH,
                      uCol[2], vRow[1], uCol[3], vRow[2]);

            // Center [1,1]: tile both axes
            emitTiled(elementPos.x + colX[1], elementPos.y + rowY[1],
                      colW[1], rowH[1],
                      srcCenterW, srcCenterH,
                      uCol[1], vRow[1], uCol[2], vRow[2]);
        }
    }
}
