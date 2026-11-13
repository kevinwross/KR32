#pragma once

/* Fills cntWords 32-bit words at mem with fillValue. mem must be 4-byte
   aligned. */
void fast_WordFill(int cntWords, void *mem, unsigned int fillValue);

/* Fills cntBytes bytes at mem with fillValue. The value is replicated into a
   word, written cntBytes/4 times, and the final 0-3 bytes use byte stores.
   mem may be unaligned: leading bytes up to the next word boundary use byte
   stores. */
void fast_ByteFill(int cntBytes, void *mem, unsigned char fillValue);
