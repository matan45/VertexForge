"""
Removes black background from images and makes it transparent.
Works great for icons with dark/black backgrounds.

Usage: python remove_black_bg.py input.png output.png
   or: python remove_black_bg.py input.png  (overwrites input)
"""

from PIL import Image
import sys
import os

def remove_black_background(input_path, output_path=None, threshold=30):
    """
    Remove black/near-black pixels and make them transparent.

    Args:
        input_path: Path to input image
        output_path: Path to output image (optional, defaults to input)
        threshold: Pixels with R, G, B all below this value become transparent (0-255)
    """
    if output_path is None:
        output_path = input_path

    # Load image
    img = Image.open(input_path)

    # Convert to RGBA if needed
    if img.mode != 'RGBA':
        img = img.convert('RGBA')

    # Get pixel data
    data = img.getdata()
    new_data = []

    for pixel in data:
        r, g, b, a = pixel

        # If pixel is black or near-black, make it transparent
        if r <= threshold and g <= threshold and b <= threshold:
            new_data.append((r, g, b, 0))  # Fully transparent
        else:
            new_data.append(pixel)  # Keep original

    img.putdata(new_data)

    # Save result
    img.save(output_path, 'PNG')
    print(f"Saved: {output_path}")

    return img

def process_folder(folder_path, threshold=30):
    """Process all PNG files in a folder."""
    for filename in os.listdir(folder_path):
        if filename.lower().endswith('.png'):
            filepath = os.path.join(folder_path, filename)
            print(f"Processing: {filename}")
            remove_black_background(filepath, threshold=threshold)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage:")
        print("  python remove_black_bg.py image.png              # Process single file")
        print("  python remove_black_bg.py image.png output.png   # Save to new file")
        print("  python remove_black_bg.py --folder               # Process all PNGs in current folder")
        print("")
        print("Options:")
        print("  --threshold=50  # Adjust black detection (default: 30)")
        sys.exit(1)

    threshold = 30

    # Parse threshold if provided
    for arg in sys.argv:
        if arg.startswith('--threshold='):
            threshold = int(arg.split('=')[1])

    if sys.argv[1] == '--folder':
        process_folder('.', threshold=threshold)
    elif len(sys.argv) >= 3 and not sys.argv[2].startswith('--'):
        remove_black_background(sys.argv[1], sys.argv[2], threshold=threshold)
    else:
        remove_black_background(sys.argv[1], threshold=threshold)
