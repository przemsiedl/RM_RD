#ifndef COMPRESSION_H
#define COMPRESSION_H

class Compression {
public:
    static void encrypt(char* dataIn, int inLength, char* dataOut, int& outLength);
    static void decrypt(char* dataIn, int inLength, char* dataOut, int& outLength);
};

#endif // COMPRESSION_H