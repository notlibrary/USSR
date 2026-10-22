from PIL import Image, ImageDraw

def generate_ural_icon():
    # 1. Draw at 4x and scale down so the slopes come out antialiased
    scale = 4
    size = 256 * scale
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    def s(points):
        return [(x * scale, y * scale) for x, y in points]

    # 2. Dark red circular sky with a golden border, like ussr.ico
    center = size // 2
    radius = 120 * scale
    draw.ellipse([center - radius, center - radius, center + radius, center + radius],
                 fill=(190, 20, 20, 255))

    # 3. Back range: two low peaks in a darker stone colour
    draw.polygon(s([(0, 256), (0, 216), (70, 118), (104, 160), (176, 92), (256, 214), (256, 256)]),
                 fill=(95, 60, 55, 255))

    # 4. Main peak in front, with its shadowed right face
    draw.polygon(s([(4, 256), (128, 58), (252, 256)]), fill=(140, 95, 80, 255))
    draw.polygon(s([(128, 58), (252, 256), (158, 256)]), fill=(110, 72, 62, 255))

    # 5. Gold snow cap with a jagged snow line
    draw.polygon(s([(128, 58), (161, 110), (146, 102), (136, 116),
                    (124, 100), (110, 114), (102, 100), (95, 110)]),
                 fill=(235, 180, 40, 255))

    # 6. Clip everything to the disc, then draw the border on top
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).ellipse(
        [center - radius, center - radius, center + radius, center + radius], fill=255)
    clipped = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    clipped.paste(img, (0, 0), mask)
    border = 6 * scale
    ImageDraw.Draw(clipped).ellipse(
        [center - radius, center - radius, center + radius, center + radius],
        outline=(235, 180, 40, 255), width=border)

    img = clipped.resize((256, 256), Image.LANCZOS)

    # 7. Save as a multi-resolution Windows .ico file
    icon_sizes = [(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    img.save("ural.ico", format="ICO", sizes=icon_sizes)
    print("Success! 'ural.ico' has been successfully created.")

if __name__ == "__main__":
    generate_ural_icon()
