import struct
import sys

user_path, archive_path, assembly_path = sys.argv[1:4]
user = open(user_path, "rb").read()
header = struct.pack("<III", 0x4932334B, 1, 1)
name = b"init" + b"\0" * 28
entry = struct.pack("<III", 56, len(user), 0x00400000)
archive = header + name + entry + user
with open(archive_path, "wb") as handle:
    handle.write(archive)

lines = [".section .rodata", ".global __initrd_start", "__initrd_start:"]
for offset in range(0, len(archive), 16):
    chunk = archive[offset:offset + 16]
    lines.append(".byte " + ", ".join(f"0x{byte:02X}" for byte in chunk))
lines.extend([".global __initrd_end", "__initrd_end:"])
with open(assembly_path, "w", encoding="ascii") as handle:
    handle.write("\n".join(lines) + "\n")
