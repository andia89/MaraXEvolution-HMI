import svgwrite
import cairosvg
import math

# --- 1. Parameters You Can Change ---

# File names


# Canvas settings
canvas_width = 480
canvas_height = 480

# Rectangle settings
rect_width = 20
rect_height = 8
corner_radius = 4  # rx and ry for rounded corners

# Rotation and copy settings
num_copies = 31                     # How many rectangles to create
rotation_center = (240, 240)      # The (x, y) point to rotate around
# How far from the center the rectangles are
distance_from_center = 240 - rect_width

# --- 2. SVG Creation with svgwrite ---


for j in range(num_copies + 1):
    # Create an SVG drawing object
    output_svg_filename = f'pictures/brew_{j}.svg'
    output_png_filename = f'pictures/brew_{j}.png'
    dwg = svgwrite.Drawing(
        output_svg_filename,
        size=(f'{canvas_width}px', f'{canvas_height}px'),
        profile='full'  # Use 'full' profile for transforms and gradients
    )

    # Use a group to easily apply styles or transforms to all elements if needed
    group = dwg.g(id='rect_group')
    dwg.add(group)
    angle = 3.75

    # Loop to create and rotate each rectangle
    for i in range(num_copies):
        # Calculate the angle for this rectangle
        # We distribute them evenly around a 360-degree circle
        angle_deg = angle * (i + 1) - 90
        angle_rad = math.radians(angle_deg)

        # Calculate the rectangle's center position before rotation
        # This places the center of the rectangle on the circle's circumference
        rect_cx = rotation_center[0] + \
            distance_from_center * math.cos(angle_rad)
        rect_cy = rotation_center[1] + \
            distance_from_center * math.sin(angle_rad)

        # Calculate the top-left corner (insert point) from the center position
        top_left_x = rect_cx - (rect_width / 2)
        top_left_y = rect_cy - (rect_height / 2)

        # --- Coloring ---
        # We use the HSL color model to cycle through hues, which looks great.
        # The hue (first value) maps directly to our angle.
        if i >= j:
            color = f'#8c8c8c'
        else:
            color = f'#c35252'

        # Create the rectangle

        rect = dwg.rect(
            insert=(top_left_x, top_left_y),
            size=(rect_width, rect_height),
            rx=corner_radius,  # Horizontal corner radius
            ry=corner_radius,  # Vertical corner radius
            fill=color,
            stroke='none',
            stroke_width=2
        )

        # --- Rotation ---
        # Apply a rotation transform around the rectangle's own center
        rect.rotate(angle_deg, center=(rect_cx, rect_cy))

        # Add the final, transformed rectangle to the group
        group.add(rect)

    # Save the SVG file
    dwg.save()

    # --- 3. PNG Conversion with cairosvg ---

    # Convert the SVG to PNG with a transparent background
    cairosvg.svg2png(
        url=output_svg_filename,
        write_to=output_png_filename,
        # Set the background to a fully transparent color
        background_color="rgba(0, 0, 0, 0)",
        output_width=canvas_width,
        output_height=canvas_height
    )
