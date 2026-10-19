#ifndef COMPRESSION_H
#define COMPRESSION_H

class Compression {
public:
    /* outBufferBytes = rozmiar przydzielonego dataOut; dla pelnej sciezki LZ >= inLength*2+4096.
       abortAt > 0: przerwij, gdy wyjscie nie jest mniejsze niz abortAt (outLength = -1). */
    static void encrypt(const char* dataIn, int inLength, char* dataOut, int& outLength, int outBufferBytes,
        int abortAt = 0);
    static void decrypt(const char* dataIn, int inLength, char* dataOut, int& outLength);

    // Region (x,y,width,height); dataOutCapacity jak przy encrypt (plain region *2 + 4096).
    static void encryptRegion(const char* src, int srcStride, int x, int y,
        int width, int height, int bytesPerPixel, char* dataOut, int& outLength, int dataOutCapacity);
};

#endif // COMPRESSION_H