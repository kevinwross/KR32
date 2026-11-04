import re
import struct
import sys
import zlib

kernel_path, map_path, meta_path = sys.argv[1:4]
addresses = {}
with open(map_path, "r", encoding="utf-8") as handle:
    for line in handle:
        match = re.match(r"\s*(__initrd_(?:start|end))\s+0x([0-9A-Fa-f]+)", line)
        if match:
            addresses[match.group(1)] = int(match.group(2), 16)
start = addresses["__initrd_start"]
end = addresses["__initrd_end"]
image = open(kernel_path, "rb").read()
meta = struct.pack(
    "<IIIIII",
    0x4D32334B,
    1,
    0x00020000,
    start - 0x00020000,
    end - start,
    zlib.crc32(image) & 0xFFFFFFFF,
)
with open(meta_path, "wb") as handle:
    handle.write(meta)
