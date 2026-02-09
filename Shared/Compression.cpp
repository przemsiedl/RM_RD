#include "../Shared/Compression.h"
#include <cstring>

const char ESC = (char)0xF0;
const char CMD_COPY = 0x01;

const int MIN_MATCH = 4;
const int MAX_OFFSET = 0x0FFF; // 4095
const int MAX_LENGTH = 0x0FFF; // 4095

// Tablica haszująca: 2^16 wpisów (64KB pamięci)
// Przechowuje indeksy ostatnio widzianych sekwencji 4-bajtowych
static int hashTable[65536];

inline unsigned int getHash(const char* p) {
    // Szybki hash dla 4 bajtów
    unsigned int v = *reinterpret_cast<const unsigned int*>(p);
    return ((v >> 16) ^ v) & 0xFFFF;
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

void Compression::encrypt(char* dataIn, int inLength, char* dataOut, int& outLength) {
    if (inLength < MIN_MATCH) {
        // Zbyt małe dane na kompresję - po prostu kopiuj z obsługą ESC
        char* out = dataOut;
        for(int i=0; i<inLength; ++i) {
            if (dataIn[i] == ESC) { *out++ = ESC; *out++ = ESC; }
            else *out++ = dataIn[i];
        }
        outLength = (int)(out - dataOut);
        return;
    }

    // Inicjalizacja tablicy haszującej wartością -1
    memset(hashTable, -1, sizeof(hashTable));

    char* out = dataOut;
    int i = 0;

    // Kompresujemy do momentu, gdy zostanie nam miejsce na MIN_MATCH
    while (i < inLength - MIN_MATCH) {
        unsigned int h = getHash(&dataIn[i]);
        int matchPos = hashTable[h];
        hashTable[h] = i;

        short bestOffset = 0;
        short bestLength = 0;

        // Jeśli znaleziono potencjalne dopasowanie w zasięgu okna
        if (matchPos != -1 && (i - matchPos) <= MAX_OFFSET) {
            int len = 0;
            // Sprawdzanie długości dopasowania
            while (i + len < inLength && 
                   dataIn[matchPos + len] == dataIn[i + len] && 
                   len < MAX_LENGTH) {
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
            
            // Zaktualizuj hashe dla pominiętych bajtów (opcjonalne, ale zwiększa stopień kompresji)
            // Dla maksymalnej prędkości przy dużej ilości zer można to ograniczyć
            for (int k = 1; k < bestLength; ++k) {
                if (i + k < inLength - MIN_MATCH)
                    hashTable[getHash(&dataIn[i + k])] = i + k;
            }
            i += bestLength;
        } else {
            unsigned char b = (unsigned char)dataIn[i++];
            if (b == (unsigned char)ESC) {
                *out++ = ESC;
                *out++ = ESC;
            } else {
                *out++ = b;
            }
        }
    }

    // Dokończenie pozostałych bajtów
    while (i < inLength) {
        unsigned char b = (unsigned char)dataIn[i++];
        if (b == (unsigned char)ESC) {
            *out++ = ESC;
            *out++ = ESC;
        } else {
            *out++ = b;
        }
    }

    outLength = (int)(out - dataOut);
}

void Compression::decrypt(char* dataIn, int inLength, char* dataOut, int& outLength) {
    const char* in = dataIn;
    const char* end = dataIn + inLength;
    char* out = dataOut;

    while (in < end) {
        unsigned char b = (unsigned char)*in++;

        if (b != (unsigned char)ESC) {
            *out++ = b;
            continue;
        }

        // Sprawdzamy czy to koniec danych (bezpieczeństwo)
        if (in >= end) break; 
        
        unsigned char cmd = (unsigned char)*in++;

        if (cmd == (unsigned char)ESC) {
            *out++ = ESC;
        } else if (cmd == (unsigned char)CMD_COPY) {
            short offset, length;
            decodeOffsetLength(in, offset, length);

            char* src = out - offset;
            // Używamy prostej pętli, ponieważ zakresy mogą na siebie nachodzić (np. przy zerach)
            for (short j = 0; j < length; ++j) {
                *out++ = src[j];
            }
        }
    }

    outLength = (int)(out - dataOut);
}