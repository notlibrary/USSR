from PIL import Image, ImageDraw

def generate_ussr_icon():
    # 1. Create a base canvas (256x256) with a transparent background
    size = 256
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # 2. Draw a dark red circular background with a golden border (Flat Modern Style)
    center = size // 2
    radius = 120
    draw.ellipse([center - radius, center - radius, center + radius, center + radius], 
                 fill=(190, 20, 20, 255), outline=(235, 180, 40, 255), width=6)

    # 3. Define coordinates for a beautiful 5-point star
    # Outer radius 95, Inner radius 38
    points = [
        (128, 40), (150, 102), (218, 102), (163, 142), 
        (184, 206), (128, 166), (72, 206), (93, 142), 
        (38, 102), (106, 102)
    ]
    
    # Draw the main gold star
    draw.polygon(points, fill=(235, 180, 40, 255))
    
    # Draw a slightly smaller red inner star for that classic Soviet contrast
    inner_points = [
        (128, 48), (148, 104), (206, 104), (159, 140), 
        (177, 196), (128, 161), (79, 196), (97, 140), 
        (50, 104), (108, 104)
    ]
    draw.polygon(inner_points, fill=(215, 25, 25, 255))

    # 4. Save as a multi-resolution Windows .ico file
    # This embeds all standard sizes for desktop scaling
    icon_sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    img.save("ussr.ico", format="ICO", sizes=icon_sizes)
    print("Success! 'ussr.ico' has been successfully created.")

if __name__ == "__main__":
    generate_ussr_icon()
