#include "../Shared/compression.h"
#include <windows.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

const char ESC = (char)0xF0;
const char CMD_COPY = 0x01;

const int MIN_MATCH = 4;
const int MAX_OFFSET = 0x0FFF; // 4095
const int MAX_LENGTH = 0x0FFF; // 4095

inline unsigned int getHash(const char* p) {
    unsigned int v;
    memcpy(&v, p, 4);
    return ((v >> 16) ^ v) & 0xFFFF;
}

/* Tablica hasza zyje przez caly proces. Znacznik generacji zamiast memset 256 KB co klatke. */
struct LzHash {
    CRITICAL_SECTION cs;
    int pos[65536];
    unsigned short stamp[65536];
    unsigned short generation;

    LzHash() {
        InitializeCriticalSection(&cs);
        memset(stamp, 0, sizeof(stamp));
        generation = 1;
    }

    ~LzHash() {
        DeleteCriticalSection(&cs);
    }
};

static LzHash g_lz;

static int LookupHash(unsigned int h, int i) {
    int matchPos = -1;
    if (g_lz.stamp[h] == g_lz.generation)
        matchPos = g_lz.pos[h];
    g_lz.stamp[h] = g_lz.generation;
    g_lz.pos[h] = i;
    return matchPos;
}

inline void encodeOffsetLength(short offset, short length, char*& out) {
    out[0] = (offset >> 4) & 0xFF;
    out[1] = ((offset & 0x0F) << 4) | ((length >> 8) & 0x0F);
    out[2] = length & 0xFF;
    out += 3;
}

inline void decodeOffsetLength(const char*& in, short& offset, short& length) {
    unsigned char b0 = (unsigned char)in[0];
    unsigned char b1 = (unsigned char)in[1];
    unsigned char b2 = (unsigned char)in[2];
    offset = (b0 << 4) | (b1 >> 4);
    length = ((b1 & 0x0F) << 8) | b2;
    in += 3;
}

/* Tylko literaly (ESC podwojony) — bez tablicy hasz; max wyjscie ~2 * inLength. */
static void EncryptLiteralsOnly(const char* dataIn, int inLength, char* dataOut, int& outLength) {
    char* out = dataOut;
    for (int j = 0; j < inLength; j++) {
        unsigned char b = (unsigned char)dataIn[j];
        if (b == (unsigned char)ESC) {
            *out++ = ESC;
            *out++ = ESC;
        } else
            *out++ = (char)b;
    }
    outLength = (int)(out - dataOut);
}

static bool PastAbort(const char* dataOut, const char* out, int abortAt) {
    return abortAt > 0 && (int)(out - dataOut) >= abortAt;
}

void Compression::encrypt(const char* dataIn, int inLength, char* dataOut, int& outLength,
    int outBufferBytes, int abortAt) {
    outLength = 0;
    if (!dataIn || !dataOut || inLength < 0 || outBufferBytes < 1)
        return;

    if (inLength < MIN_MATCH) {
        EncryptLiteralsOnly(dataIn, inLength, dataOut, outLength);
        if (outLength > outBufferBytes)
            outLength = 0;
        else if (PastAbort(dataOut, dataOut + outLength, abortAt))
            outLength = -1;
        return;
    }

    /* Pelna sciezka LZ wymaga bufora ~2x + zapas (literal 0xF0 -> 2 bajty). */
    if (inLength > (INT_MAX - 4096) / 2) {
        EncryptLiteralsOnly(dataIn, inLength, dataOut, outLength);
        if (outLength > outBufferBytes)
            outLength = 0;
        else if (PastAbort(dataOut, dataOut + outLength, abortAt))
            outLength = -1;
        return;
    }
    int minForLz = inLength * 2 + 4096;
    if (outBufferBytes < minForLz) {
        EncryptLiteralsOnly(dataIn, inLength, dataOut, outLength);
        if (outLength > outBufferBytes)
            outLength = 0;
        else if (PastAbort(dataOut, dataOut + outLength, abortAt))
            outLength = -1;
        return;
    }

    EnterCriticalSection(&g_lz.cs);
    g_lz.generation++;
    if (g_lz.generation == 0) {
        memset(g_lz.stamp, 0, sizeof(g_lz.stamp));
        g_lz.generation = 1;
    }

    char* out = dataOut;
    int i = 0;
    bool aborted = false;

    while (i < inLength - MIN_MATCH) {
        unsigned int h = getHash(&dataIn[i]);
        int matchPos = LookupHash(h, i);

        short bestOffset = 0;
        short bestLength = 0;

        if (matchPos != -1 && (i - matchPos) <= MAX_OFFSET) {
            int len = 0;
            while (i + len < inLength && dataIn[matchPos + len] == dataIn[i + len] && len < MAX_LENGTH) {
                len++;
            }
            if (len >= MIN_MATCH) {
                bestLength = (short)len;
                bestOffset = (short)(i - matchPos);
            }
        }

        if (bestLength >= MIN_MATCH) {
            *out++ = ESC;
            *out++ = CMD_COPY;
            encodeOffsetLength(bestOffset, bestLength, out);
            for (int k = 1; k < bestLength; k++) {
                if (i + k < inLength - MIN_MATCH)
                    LookupHash(getHash(&dataIn[i + k]), i + k);
            }
            i += bestLength;
        } else {
            unsigned char b = (unsigned char)dataIn[i++];
            if (b == (unsigned char)ESC) {
                *out++ = ESC;
                *out++ = ESC;
            } else
                *out++ = (char)b;
        }

        if (PastAbort(dataOut, out, abortAt)) {
            aborted = true;
            break;
        }
    }

    if (!aborted) {
        while (i < inLength) {
            unsigned char b = (unsigned char)dataIn[i++];
            if (b == (unsigned char)ESC) {
                *out++ = ESC;
                *out++ = ESC;
            } else
                *out++ = (char)b;
            if (PastAbort(dataOut, out, abortAt)) {
                aborted = true;
                break;
            }
        }
    }

    LeaveCriticalSection(&g_lz.cs);

    if (aborted) {
        outLength = -1;
        return;
    }

    outLength = (int)(out - dataOut);
    if (outLength > outBufferBytes) {
        EncryptLiteralsOnly(dataIn, inLength, dataOut, outLength);
        if (outLength > outBufferBytes)
            outLength = 0;
        else if (PastAbort(dataOut, dataOut + outLength, abortAt))
            outLength = -1;
    }
}

void Compression::encryptRegion(const char* src, int srcStride, int x, int y,
    int width, int height, int bytesPerPixel, char* dataOut, int& outLength, int dataOutCapacity) {
    outLength = 0;
    int rowBytes = width * bytesPerPixel;
    int regionStride = ((rowBytes + 3) / 4) * 4;
    long regionSize = (long)regionStride * height;

    if (regionSize <= 0 || regionSize > INT_MAX) {
        return;
    }
    int plain = (int)regionSize;

    char* temp = (char*)malloc((size_t)plain);
    if (!temp) {
        return;
    }

    for (int row = 0; row < height; row++) {
        const char* srcRow = src + ((y + row) * srcStride) + (x * bytesPerPixel);
        char* dstRow = temp + (row * regionStride);
        memcpy(dstRow, srcRow, (size_t)rowBytes);
        if (regionStride > rowBytes) {
            memset(dstRow + rowBytes, 0, (size_t)(regionStride - rowBytes));
        }
    }

    encrypt(temp, plain, dataOut, outLength, dataOutCapacity);
    free(temp);
}

void Compression::decrypt(const char* dataIn, int inLength, char* dataOut, int& outLength) {
    const char* in = dataIn;
    const char* end = dataIn + inLength;
    char* out = dataOut;

    while (in < end) {
        unsigned char b = (unsigned char)*in++;

        if (b != (unsigned char)ESC) {
            *out++ = b;
            continue;
        }
        if (in >= end)
            break;

        unsigned char cmd = (unsigned char)*in++;
        if (cmd == (unsigned char)ESC) {
            *out++ = ESC;
        } else if (cmd == (unsigned char)CMD_COPY) {
            short offset, length;
            decodeOffsetLength(in, offset, length);
            char* src = out - offset;
            for (short j = 0; j < length; j++) {
                *out++ = src[j];
            }
        }
    }
    outLength = (int)(out - dataOut);
}
