#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// reports a failed runtime check, like assert, with the line it's on, and stops the program
_Noreturn void cheri_trap(const char* message, long long line) {
    fprintf(stderr, "trap at line %lld: %s\n", line, message);
    abort();
}

// reports an index past the end of an array, with the index, the array as it's written, and how many it holds, and stops the program
_Noreturn void cheri_trap_index(unsigned long long index, unsigned long long length, const char* array, long long line) {
    fprintf(stderr, "trap at line %lld: index %llu is past the end of '%s', which holds %llu\n", line, index, array, length);
    abort();
}

// allocates count values of size bytes each on the heap, zeroed when asked, and traps with the message when they wouldn't fit in memory at all or malloc fails, where nothing at all needs no memory, so it's null, which free takes
void* cheri_alloc(unsigned long long count, unsigned long long size, int zeroed, const char* message, long long line) {
    if (count == 0 || size == 0) return NULL;
    if (count > (unsigned long long)-1 / size) cheri_trap(message, line);
    void* memory = zeroed ? calloc(count, size) : malloc(count * size);
    if (memory == NULL) cheri_trap(message, line);
    return memory;
}

// the memory a conversion between kinds of string makes, which traps with the out-of-memory message when malloc fails
static void* convertedMemory(unsigned long long count, unsigned long long size, const char* full, long long line) {
    return cheri_alloc(count, size, 0, full, line);
}

// decodes UTF-8 bytes into runes in memory of their own, setting *count to how many there are, and traps with bad when the bytes
// aren't valid UTF-8: a sequence cut short, a continuation byte where none goes, a longer encoding than the character needs, a
// surrogate, or a code point past 10FFFF
unsigned int* cheri_decode_utf8(const unsigned char* bytes, unsigned long long length, unsigned long long* count, const char* full, const char* bad, long long line) {
    unsigned int* runes = convertedMemory(length, sizeof(unsigned int), full, line);
    unsigned long long made = 0;
    for (unsigned long long i = 0; i < length;) {
        unsigned char lead = bytes[i];
        unsigned int point;
        unsigned long long extra;
        if (lead < 0x80) {
            point = lead;
            extra = 0;
        } else if ((lead & 0xE0) == 0xC0) {
            point = lead & 0x1F;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            point = lead & 0x0F;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            point = lead & 0x07;
            extra = 3;
        } else {
            cheri_trap(bad, line);
        }
        if (i + extra >= length) cheri_trap(bad, line);
        for (unsigned long long j = 1; j <= extra; j++) {
            if ((bytes[i + j] & 0xC0) != 0x80) cheri_trap(bad, line);
            point = (point << 6) | (bytes[i + j] & 0x3F);
        }
        // the smallest code point each length can hold, so a longer encoding than a character needs is caught
        static const unsigned int smallest[] = {0, 0x80, 0x800, 0x10000};
        if (point < smallest[extra] || (point >= 0xD800 && point <= 0xDFFF) || point > 0x10FFFF) cheri_trap(bad, line);
        runes[made++] = point;
        i += extra + 1;
    }
    *count = made;
    return runes;
}

// encodes runes into UTF-8 bytes in memory of their own, setting *count to how many there are, which can't fail, since a rune is always a Unicode scalar value
unsigned char* cheri_encode_utf8(const unsigned int* runes, unsigned long long length, unsigned long long* count, const char* full, long long line) {
    unsigned char* bytes = convertedMemory(length, 4, full, line);
    unsigned long long made = 0;
    for (unsigned long long i = 0; i < length; i++) {
        unsigned int point = runes[i];
        if (point < 0x80) {
            bytes[made++] = point;
        } else if (point < 0x800) {
            bytes[made++] = 0xC0 | (point >> 6);
            bytes[made++] = 0x80 | (point & 0x3F);
        } else if (point < 0x10000) {
            bytes[made++] = 0xE0 | (point >> 12);
            bytes[made++] = 0x80 | ((point >> 6) & 0x3F);
            bytes[made++] = 0x80 | (point & 0x3F);
        } else {
            bytes[made++] = 0xF0 | (point >> 18);
            bytes[made++] = 0x80 | ((point >> 12) & 0x3F);
            bytes[made++] = 0x80 | ((point >> 6) & 0x3F);
            bytes[made++] = 0x80 | (point & 0x3F);
        }
    }
    *count = made;
    return bytes;
}

// copies bytes or runes, each width bytes wide, into chars in memory of their own, trapping with bad when one isn't ASCII
unsigned char* cheri_to_ascii(const void* source, unsigned long long length, unsigned long long width, const char* full, const char* bad, long long line) {
    unsigned char* chars = convertedMemory(length, 1, full, line);
    for (unsigned long long i = 0; i < length; i++) {
        unsigned int point = width == 4 ? ((const unsigned int*)source)[i] : ((const unsigned char*)source)[i];
        if (point > 0x7F) cheri_trap(bad, line);
        chars[i] = point;
    }
    return chars;
}
