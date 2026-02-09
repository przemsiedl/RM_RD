#include "../Shared/Compression.h"
#include "../Shared/BmpStream.h"
#include <stdio.h>
#include <string.h>

static bool BuffersEqual(const char* a, const char* b, int length) {
    return (memcmp(a, b, length) == 0);
}

static int TestCompressionRoundTrip() {
    const int dataSize = 256;
    char input[dataSize];
    char compressed[dataSize * 2];
    char decompressed[dataSize];
    int compressedLength = 0;
    int decompressedLength = 0;

    for (int i = 0; i < dataSize; ++i) {
        input[i] = (char)(i ^ 0x5A);
    }

    Compression::encrypt(input, dataSize, compressed, compressedLength);
    Compression::decrypt(compressed, compressedLength, decompressed, decompressedLength);

    if (decompressedLength != dataSize || !BuffersEqual(input, decompressed, dataSize)) {
        printf("Compression round-trip failed.\n");
        return 1;
    }

    printf("Compression round-trip OK.\n");
    return 0;
}

static int TestApplyDiffXOR() {
    const int width = 2;
    const int height = 2;
    const int bytesPerPixel = 3;
    const int stride = width * bytesPerPixel;
    const int dataSize = stride * height;

    char previous[dataSize];
    char current[dataSize];
    char diff[dataSize];
    char target[dataSize];

    for (int i = 0; i < dataSize; ++i) {
        previous[i] = (char)i;
        current[i] = (char)(previous[i] ^ 0x5A);
        diff[i] = (char)(previous[i] ^ current[i]);
        target[i] = previous[i];
    }

    ImageData diffView;
    diffView.width = width;
    diffView.height = height;
    diffView.bitsPerPixel = bytesPerPixel * 8;
    diffView.stride = stride;
    diffView.dataSize = dataSize;
    diffView.pData = diff;
    diffView.isFullFrame = false;
    diffView.isEmptyDiff = false;

    if (!BmpStream::ApplyDiffXOR(target, &diffView) || !BuffersEqual(target, current, dataSize)) {
        diffView.pData = NULL;
        printf("ApplyDiffXOR diff test failed.\n");
        return 1;
    }

    diffView.pData = NULL;

    char targetFull[dataSize];
    for (int i = 0; i < dataSize; ++i) {
        targetFull[i] = 0;
    }

    ImageData fullView;
    fullView.width = width;
    fullView.height = height;
    fullView.bitsPerPixel = bytesPerPixel * 8;
    fullView.stride = stride;
    fullView.dataSize = dataSize;
    fullView.pData = current;
    fullView.isFullFrame = true;
    fullView.isEmptyDiff = false;

    if (!BmpStream::ApplyDiffXOR(targetFull, &fullView) || !BuffersEqual(targetFull, current, dataSize)) {
        fullView.pData = NULL;
        printf("ApplyDiffXOR full-frame test failed.\n");
        return 1;
    }

    fullView.pData = NULL;

    printf("ApplyDiffXOR tests OK.\n");
    return 0;
}

int main() {
    int failures = 0;
    failures += TestCompressionRoundTrip();
    failures += TestApplyDiffXOR();
    return failures;
}