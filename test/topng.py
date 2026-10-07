import sys
from PIL import Image
for name in sys.argv[1:]:
    b = open(name, "rb").read(); im = Image.new("L", (128, 64), 0)
    for p in range(8):
        for x in range(128):
            v = b[p*128+x]
            for k in range(8):
                if v >> k & 1: im.putpixel((x, p*8+k), 255)
    im.resize((512, 256), Image.NEAREST).save(name.replace(".bin", ".png"))
