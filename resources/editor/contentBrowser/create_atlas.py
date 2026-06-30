"""
Creates a transparent icon atlas from individual PNG icons.
Atlas layout: 4 columns x 5 rows, 64x64 pixels per tile, 256x320 total


Usage: python create_atlas.py
"""

from PIL import Image
import os

# Configuration
TILE_SIZE = 64
GRID_COLS = 5
GRID_ROWS = 5
ATLAS_WIDTH = TILE_SIZE * GRID_COLS   # 256
ATLAS_HEIGHT = TILE_SIZE * GRID_ROWS  # 320

# Icon mapping: index -> filename (without extension)
# Update these filenames to match your actual icon files
ICON_FILES = {
    0: "animation-file",      # animation
    1: "mType",        # image/texture
    2: "folder",              # folder
    3: "scene",               # scene
    4: "audio-file",          # audio
    5: "file",          # audio
    6: "Prefab",                # other/file
    7: "font",                # other/file
    8: "project",                # other/file
    9: "animator",                # other/file
    10: "particle",                # other/file
    11: "terrain",                # terrain
    12: "navmesh",               # navmesh
    13: "physAnim",              # physics animation
    14: "sea",              # physics animation
    15: "ai-tree",              # physics animation
    16: "plugin",              # physics animation
    17: "input",             # physics animation
    18: "animation-retarget",             # physics animation
    19: "VFXSequence",             # physics animation
    20: "material",
    21: "collider",
}

def find_icon_file(base_name):
    """Find icon file with common extensions."""
    extensions = ['.png']
    for ext in extensions:
        path = f"{base_name}{ext}"
        if os.path.exists(path):
            return path
    return None

def create_atlas():
    # Create transparent atlas image (RGBA)
    atlas = Image.new('RGBA', (ATLAS_WIDTH, ATLAS_HEIGHT), (0, 0, 0, 0))

    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    print(f"Creating {ATLAS_WIDTH}x{ATLAS_HEIGHT} atlas with {TILE_SIZE}x{TILE_SIZE} tiles...")
    print(f"Working directory: {script_dir}")

    for index, base_name in ICON_FILES.items():
        icon_path = find_icon_file(base_name)

        if icon_path is None:
            print(f"  [{index}] WARNING: Icon not found: {base_name}.png - leaving empty")
            continue

        try:
            # Load icon
            icon = Image.open(icon_path)

            # Convert to RGBA if needed
            if icon.mode != 'RGBA':
                icon = icon.convert('RGBA')

            # Resize to tile size if needed
            if icon.size != (TILE_SIZE, TILE_SIZE):
                icon = icon.resize((TILE_SIZE, TILE_SIZE), Image.Resampling.LANCZOS)

            # Make white pixels transparent (optional - uncomment if needed)
            # icon = make_white_transparent(icon)

            # Calculate position in atlas
            col = index % GRID_COLS
            row = index // GRID_COLS
            x = col * TILE_SIZE
            y = row * TILE_SIZE

            # Paste icon into atlas
            atlas.paste(icon, (x, y), icon)  # Use icon as mask to preserve transparency

            print(f"  [{index}] Added {base_name} at ({col}, {row}) -> pixel ({x}, {y})")

        except Exception as e:
            print(f"  [{index}] ERROR loading {icon_path}: {e}")

    # Save atlas
    output_path = "atlasIcons.png"
    atlas.save(output_path, 'PNG')
    print(f"\nAtlas saved to: {output_path}")
    print("Now convert to .vfImage using your import tool.")

    return atlas

def make_white_transparent(img, threshold=250):
    """Convert white/near-white pixels to transparent."""
    data = img.getdata()
    new_data = []
    for pixel in data:
        r, g, b, a = pixel
        # If pixel is white or near-white, make it transparent
        if r > threshold and g > threshold and b > threshold:
            new_data.append((r, g, b, 0))
        else:
            new_data.append(pixel)
    img.putdata(new_data)
    return img

if __name__ == "__main__":
    create_atlas()
