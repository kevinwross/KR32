#pragma once

/*
 * K32I initrd on-disk layout.
 *
 * All fields are little-endian 32-bit values, matching the KR32 target.
 * The archive is:
 *
 *   initrd_header
 *   initrd_entry[count]
 *   payload bytes
 *
 * Entry offsets are relative to the start of the archive. Names are
 * NUL-padded to 32 bytes.
 */
struct initrd_header {
    unsigned int magic;
    unsigned int version;
    unsigned int count;
};

struct initrd_entry {
    char name[32];
    unsigned int offset;
    unsigned int size;
    unsigned int entry;
};
