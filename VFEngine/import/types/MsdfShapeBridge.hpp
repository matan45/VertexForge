#pragma once

#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace types
{
    // Import-internal value type. Keep msdfgen out of this header because projects
    // that consume Import headers do not inherit msdfgen's include directory.
    struct MtsdfGlyphResult
    {
        std::vector<unsigned char> pixels;
        int width = 0;
        int height = 0;
        float bearingX = 0.0f;
        float bearingY = 0.0f;
        float advanceX = 0.0f;
        bool valid = false;
        bool empty = false;
    };

    MtsdfGlyphResult generateMtsdfGlyph(FT_Face face, FT_UInt glyphIndex,
                                         double pxPerEm, double pxRange);
}
