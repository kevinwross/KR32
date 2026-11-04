import struct
import sys

user_path, fault_path, wx_path, archive_path, assembly_path = sys.argv[1:6]
programs = [
    ("init", open(user_path, "rb").read()),
    ("fault", open(fault_path, "rb").read()),
    ("wx", open(wx_path, "rb").read()),
]
header = struct.pack("<III", 0x4932334B, 1, len(programs))
payload_offset = 12 + len(programs) * 44
table = b""
payload = b""
for name, data in programs:
    table += name.encode("ascii").ljust(32, b"\0")
    table += struct.pack("<III", payload_offset + len(payload), len(data), 0x00400000)
    payload += data
archive = header + table + payload
with open(archive_path, "wb") as handle:
    handle.write(archive)

lines = [".section .rodata", ".global __initrd_start", "__initrd_start:"]
for offset in range(0, len(archive), 16):
    chunk = archive[offset:offset + 16]
    lines.append(".byte " + ", ".join(f"0x{byte:02X}" for byte in chunk))
lines.extend([".global __initrd_end", "__initrd_end:"])
with open(assembly_path, "w", encoding="ascii") as handle:
    handle.write("\n".join(lines) + "\n")
