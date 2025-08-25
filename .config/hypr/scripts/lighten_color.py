#!/usr/bin/env python3
import sys
import colorsys

def lighten_hex_color(hex_color, factor):
    """
    Lightens a hex color by a given factor.
    Factor > 1.0 will lighten the color.
    Factor < 1.0 will darken the color.
    """
    try:
        # 1. Sanitize and parse the hex string
        hex_color = hex_color.lstrip('#')
        if len(hex_color) != 6:
            raise ValueError("Invalid hex color format")
        
        r, g, b = int(hex_color[0:2], 16), int(hex_color[2:4], 16), int(hex_color[4:6], 16)

        # 2. Convert RGB to HLS (Hue, Lightness, Saturation)
        # We normalize to 0-1 range for colorsys
        h, l, s = colorsys.rgb_to_hls(r / 255.0, g / 255.0, b / 255.0)

        # 3. Modify the lightness
        # Multiply lightness by the factor, but clamp it at 1.0 (max lightness)
        l = min(1.0, l * factor)

        # 4. Convert back to RGB
        r_new, g_new, b_new = colorsys.hls_to_rgb(h, l, s)

        # 5. Format back to a hex string
        # De-normalize from 0-1 to 0-255 and format as 2-digit hex
        return f"#{int(r_new * 255):02x}{int(g_new * 255):02x}{int(b_new * 255):02x}"

    except (ValueError, IndexError) as e:
        # If anything goes wrong, return the original color
        # print(f"Error processing color '{hex_color}': {e}", file=sys.stderr)
        return f"#{hex_color}"

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: ./lighten_color.py <hex_color> <factor>", file=sys.stderr)
        # Fallback to a default color if misused, so the script doesn't break
        print("#000000", file=sys.stdout)
        sys.exit(1)

    input_color = sys.argv[1]
    try:
        lightness_factor = float(sys.argv[2])
    except ValueError:
        print("Error: Factor must be a number (e.g., 1.2)", file=sys.stderr)
        print(input_color, file=sys.stdout) # Return original color
        sys.exit(1)

    new_color = lighten_hex_color(input_color, lightness_factor)
    print(new_color, file=sys.stdout)