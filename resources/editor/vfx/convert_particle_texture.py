from PIL import Image
import sys
#White/colored shape on a TRANSPARENT background (PNG with alpha channel)

def convert_to_particle_texture(input_path, output_path=None):
    """
    Convert a black-on-white image to white-on-transparent for particle systems.
    Black becomes white, white becomes transparent.
    """
    if output_path is None:
        output_path = input_path.rsplit('.', 1)[0] + '_particle.png'

    # Load image
    img = Image.open(input_path).convert('RGBA')
    pixels = img.load()

    width, height = img.size

    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]

            # Calculate brightness (0 = black, 255 = white)
            brightness = (r + g + b) // 3

            # Invert: dark pixels become white+opaque, light pixels become transparent
            # Alpha = 255 - brightness (black=full opacity, white=transparent)
            new_alpha = 255 - brightness

            # Set color to white, alpha based on original darkness
            pixels[x, y] = (255, 255, 255, new_alpha)

    img.save(output_path, 'PNG')
    print(f"Saved particle texture to: {output_path}")
    return output_path

if __name__ == "__main__":
    input_file = sys.argv[1] if len(sys.argv) > 1 else r"C:\matan\dry-clean.png"
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    convert_to_particle_texture(input_file, output_file)
