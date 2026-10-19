/*
 * Testy kompresji (Shared/Compression) i symulacji sciezki serwer -> "wire" -> klient
 * (jak RemoteClient::ReceiveImageData + FrameProvider::ImageDataToFrameBmp).
 * Uruchomienie: test_mingw.bat lub Tests\tests.exe z katalogu glownego repo.
 * Opcje: tests.exe -v  (statystyki kompresji na stdout)
 */

#include "../Shared/compression.h"
#include "../Shared/BmpStream.h"
#include "../Shared/Frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_verbose = 0;

static bool BuffersEqual(const char* a, const char* b, int length) {
    return (memcmp(a, b, (size_t)length) == 0);
}

static void LogVerbose(const char* name, int raw, int compressed) {
    if (!g_verbose) return;
    if (raw <= 0) {
        printf("  [cmp] %s raw=%d compressed=%d\n", name, raw, compressed);
        return;
    }
    double ratio = 100.0 * (double)compressed / (double)raw;
    printf("  [cmp] %s raw=%d compressed=%d (%.2f%% of raw)\n",
           name, raw, compressed, ratio);
}

/* ---- Round-trip: surowy bufor ---- */
static bool RoundTripRaw(const char* name, const char* in, int n) {
    if (n < 0) return false;
    int maxOut = n * 2 + 4096;
    if (maxOut < 64) maxOut = 64;
    char* compressed = (char*)malloc((size_t)maxOut);
    char* decompressed = (char*)malloc((size_t)(n + maxOut));
    if (!compressed || !decompressed) {
        free(compressed);
        free(decompressed);
        printf("FAIL %s: malloc\n", name);
        return false;
    }
    int clen = 0, dlen = 0;
    Compression::encrypt(in, n, compressed, clen, maxOut);
    LogVerbose(name, n, clen);
    Compression::decrypt(compressed, clen, decompressed, dlen);
    bool ok = (dlen == n) && BuffersEqual(in, decompressed, n);
    free(compressed);
    free(decompressed);
    if (!ok) {
        printf("FAIL %s: round-trip len in=%d out=%d\n", name, n, dlen);
    }
    return ok;
}

/* LCG deterministyczny */
static void FillPseudoRandom(char* buf, int n, unsigned seed) {
    unsigned x = seed;
    for (int i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        buf[i] = (char)((x >> 16) & 0xFF);
    }
}

/* ---- Odbior jak klient (bez socketu) ---- */
static bool SimulateReceiveImageData(ImageData* img, const HeaderBmp& header,
                                     const char* wirePayload) {
    if (!img || !img->pData) return false;

    bool isFullFrame = (header.flags & FRAME_FLAG_FULL) != 0;
    bool isCompressed = (header.flags & FRAME_FLAG_COMPRESSED) != 0;
    bool isNoChange = (header.flags & FRAME_FLAG_NOCHANGE) != 0;
    bool isBlocks = (header.flags & FRAME_FLAG_BLOCKS) != 0;
    bool isTrans = (header.flags & FRAME_FLAG_TRANSLATION) != 0;

    if (isNoChange) {
        if (isTrans) {
            if (header.length != 8 || !wirePayload) return false;
            int dx, dy;
            memcpy(&dx, wirePayload, 4);
            memcpy(&dy, wirePayload + 4, 4);
            if (!BmpStream::ApplyTranslationResidual(img, dx, dy, NULL, 0, 0, 0))
                return false;
            img->isFullFrame = false;
            return true;
        }
        if (header.length != 0) return false;
        return true;
    }

    if (header.length <= 0) return false;
    if (!wirePayload) return false;

    if (isFullFrame && isTrans)
        return false;

    const char* receivedBuffer = wirePayload;
    int transDx = 0, transDy = 0;
    const char* payloadStart = receivedBuffer;
    int payloadLen = header.length;
    if (isTrans) {
        if (payloadLen < 8) return false;
        memcpy(&transDx, payloadStart, 4);
        memcpy(&transDy, payloadStart + 4, 4);
        payloadStart += 8;
        payloadLen -= 8;
    }

    if (!isFullFrame && isBlocks) {
        if (!isCompressed) return false;
        if (payloadLen < 4) return false;
        if (isTrans) {
            if (!BmpStream::ApplyTranslationResidual(img, transDx, transDy, NULL, 0, 0, 0))
                return false;
        }
        if (!BmpStream::ApplyPackedBlockDiffs(img, payloadStart, payloadLen))
            return false;
        img->isFullFrame = false;
        return true;
    }

    int expectedDataSize = (int)img->dataSize;

    char* workingBuffer = NULL;
    int workingSize = 0;
    char* allocatedDecrypt = NULL;

    if (isCompressed) {
        int maxDecompressed = expectedDataSize + 4096;
        workingBuffer = (char*)malloc((size_t)maxDecompressed);
        if (!workingBuffer) return false;
        allocatedDecrypt = workingBuffer;
        Compression::decrypt(payloadStart, payloadLen, workingBuffer, workingSize);
        if (workingSize != expectedDataSize) {
            free(allocatedDecrypt);
            return false;
        }
    } else {
        workingBuffer = (char*)payloadStart;
        workingSize = payloadLen;
        if (workingSize != expectedDataSize) {
            return false;
        }
    }

    bool ok = true;
    if (isFullFrame) {
        memcpy(img->pData, workingBuffer, (size_t)img->dataSize);
        img->isFullFrame = true;
    } else {
        if (isTrans) {
            ok = BmpStream::ApplyTranslationResidual(img, transDx, transDy,
                workingBuffer, img->stride, img->width, img->height);
        } else {
            ok = BmpStream::ApplyDiff(img->pData, img->stride, img->bitsPerPixel,
                workingBuffer, img->stride, img->width, img->height, 0, 0, false);
        }
        img->isFullFrame = false;
    }

    free(allocatedDecrypt);
    return ok;
}

/* ---- Pakowanie jak FrameProvider::ImageDataToFrameBmp (siatka diff jak na serwerze) ---- */
static bool BuildWireFromImage(const ImageData* img, HeaderBmp* outHdr,
                               char** outPayload, int* outPayloadLen,
                               int diffBlockW, int diffBlockH) {
    if (!img || !img->pData || !outHdr || !outPayload || !outPayloadLen) return false;
    *outPayload = NULL;
    *outPayloadLen = 0;

    memset(outHdr, 0, sizeof(HeaderBmp));
    memcpy(outHdr->magic, "BMP", 3);
    outHdr->bitsPerPixel = img->bitsPerPixel;
    outHdr->stride = img->stride;

    if (img->isFullFrame) {
        outHdr->flags = (char)FRAME_FLAG_FULL;
        outHdr->x = 0;
        outHdr->y = 0;
        outHdr->width = img->width;
        outHdr->height = img->height;
    } else {
        outHdr->flags = (char)FRAME_FLAG_DIFF;
        if (img->isEmptyDiff) {
            outHdr->flags |= (char)FRAME_FLAG_NOCHANGE;
            outHdr->x = 0;
            outHdr->y = 0;
            outHdr->width = img->width;
            outHdr->height = img->height;
            outHdr->length = 0;
            return true;
        }

        outHdr->x = 0;
        outHdr->y = 0;
        outHdr->width = img->width;
        outHdr->height = img->height;

        char* blockPayload = NULL;
        int blockLen = 0;
        if (BmpStream::PackDiffAsBlocks(img, diffBlockW, diffBlockH, 0.7f, &blockPayload, &blockLen)) {
            outHdr->flags |= (char)FRAME_FLAG_COMPRESSED | (char)FRAME_FLAG_BLOCKS;
            outHdr->length = blockLen;
            *outPayload = blockPayload;
            *outPayloadLen = blockLen;
            return true;
        }
    }

    int maxCompressedSize = (int)img->dataSize * 2 + 4096;
    if (maxCompressedSize < 64)
        maxCompressedSize = 64;
    char* compressedData = (char*)malloc((size_t)maxCompressedSize);
    if (!compressedData) return false;
    int compressedSize = 0;
    Compression::encrypt(img->pData, (int)img->dataSize, compressedData, compressedSize, maxCompressedSize);
    outHdr->flags |= (char)FRAME_FLAG_COMPRESSED;
    outHdr->length = compressedSize;
    *outPayload = compressedData;
    *outPayloadLen = compressedSize;
    return true;
}

static void FreeWirePayload(char* p) {
    free(p);
}

/* Alokacja obrazu DIB (stride wyrownany do 4) */
static bool AllocImage(ImageData* img, int w, int h, int bpp) {
    if (w <= 0 || h <= 0 || (bpp != 16 && bpp != 24 && bpp != 32)) return false;
    int stride = ((w * bpp + 31) / 32) * 4;
    long ds = (long)stride * h;
    img->width = w;
    img->height = h;
    img->bitsPerPixel = bpp;
    img->stride = stride;
    img->dataSize = ds;
    img->pData = new char[ds];
    if (!img->pData) return false;
    memset(img->pData, 0, (size_t)ds);
    img->isFullFrame = true;
    img->isEmptyDiff = false;
    return true;
}

static void FillSolid(ImageData* img, unsigned char b0, unsigned char b1, unsigned char b2) {
    int bpp = img->bitsPerPixel / 8;
    for (int y = 0; y < img->height; y++) {
        char* row = img->pData + y * img->stride;
        for (int x = 0; x < img->width; x++) {
            row[x * bpp + 0] = (char)b0;
            row[x * bpp + 1] = (char)b1;
            row[x * bpp + 2] = (char)b2;
        }
    }
}

/* ---- Testy kompresji ---- */
static int TestCompressionEdgeCases() {
    int fail = 0;
    const char* n;

    n = "empty"; if (!RoundTripRaw(n, "", 0)) fail++;

    char b[4];
    n = "one_byte"; b[0] = 0; if (!RoundTripRaw(n, b, 1)) fail++;
    n = "two_bytes"; b[0] = 1; b[1] = 2; if (!RoundTripRaw(n, b, 2)) fail++;
    n = "three_bytes"; b[0] = 3; b[1] = 4; b[2] = 5; if (!RoundTripRaw(n, b, 3)) fail++;

    n = "all_ESC"; memset(b, (char)0xF0, 4); if (!RoundTripRaw(n, b, 4)) fail++;

    char escRun[256];
    memset(escRun, (char)0xF0, sizeof(escRun));
    n = "ESC_x256"; if (!RoundTripRaw(n, escRun, 256)) fail++;

    char rep[8192];
    memset(rep, 'A', sizeof(rep));
    n = "repeat_A_8k"; if (!RoundTripRaw(n, rep, (int)sizeof(rep))) fail++;

    char* big = (char*)malloc(120000);
    if (big) {
        for (int i = 0; i < 120000; i++) big[i] = (char)(i & 0x7F);
        n = "pattern_120k"; if (!RoundTripRaw(n, big, 120000)) fail++;
        FillPseudoRandom(big, 120000, 0xC0DEC0DEu);
        n = "random_120k"; if (!RoundTripRaw(n, big, 120000)) fail++;
        free(big);
    } else {
        printf("FAIL: malloc 120k\n");
        fail++;
    }

    /* Granice LZ: dlugi ciag identycznych bajtow (offset+length) */
    char* lz = (char*)malloc(5000);
    if (lz) {
        memset(lz, 'Z', 5000);
        n = "LZ_long_run"; if (!RoundTripRaw(n, lz, 5000)) fail++;
        free(lz);
    } else {
        fail++;
    }

    /* Na przemian – slabo sie kompresuje, musi round-trip */
    char alt[2000];
    for (int i = 0; i < 2000; i++) alt[i] = (char)((i & 1) ? 0x55 : 0xAA);
    n = "alt_bits"; if (!RoundTripRaw(n, alt, 2000)) fail++;

    if (fail == 0) printf("Compression edge cases OK.\n");
    else printf("Compression edge cases: %d failure(s).\n", fail);
    return fail;
}

static int TestEncryptRegionStride() {
    /* Szerokosc 5 px * 3 bpp = 15 -> stride regionu 16; padding zerami (jak encryptRegion) */
    const int W = 5, H = 3, bpp = 3;
    /* bpp to bajty na piksel; stride DIB liczymy z bitow, inaczej bufor jest za maly. */
    int srcStride = ((W * (bpp * 8) + 31) / 32) * 4;
    long srcSize = (long)srcStride * H;
    char* src = (char*)malloc((size_t)srcSize);
    if (!src) {
        printf("FAIL encryptRegion: malloc\n");
        return 1;
    }
    memset(src, 0, (size_t)srcSize);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int o = y * srcStride + x * bpp;
            src[o + 0] = (char)(x + y * 17);
            src[o + 1] = (char)(x * 3);
            src[o + 2] = (char)(y * 5);
        }
    }

    int regionStride = ((W * bpp + 3) / 4) * 4;
    int expectedPlain = regionStride * H;
    char* expect = (char*)malloc((size_t)expectedPlain);
    if (!expect) {
        free(src);
        return 1;
    }
    for (int row = 0; row < H; row++) {
        const char* srcRow = src + row * srcStride;
        char* dstRow = expect + row * regionStride;
        memcpy(dstRow, srcRow, (size_t)(W * bpp));
        if (regionStride > W * bpp)
            memset(dstRow + W * bpp, 0, (size_t)(regionStride - W * bpp));
    }

    int maxOut = expectedPlain * 2 + 4096;
    char* comp = (char*)malloc((size_t)maxOut);
    char* plain = (char*)malloc((size_t)expectedPlain + 4096);
    if (!comp || !plain) {
        free(src);
        free(expect);
        free(comp);
        free(plain);
        printf("FAIL encryptRegion: malloc2\n");
        return 1;
    }

    int clen = 0;
    Compression::encryptRegion(src, srcStride, 0, 0, W, H, bpp, comp, clen, maxOut);

    int dlen = 0;
    Compression::decrypt(comp, clen, plain, dlen);

    free(src);
    free(comp);

    if (dlen != expectedPlain) {
        printf("FAIL encryptRegion stride: dlen=%d expected=%d\n", dlen, expectedPlain);
        free(expect);
        free(plain);
        return 1;
    }

    bool ok = BuffersEqual(plain, expect, expectedPlain);
    free(expect);
    free(plain);

    if (!ok) {
        printf("FAIL encryptRegion stride: content mismatch\n");
        return 1;
    }
    printf("encryptRegion stride OK.\n");
    return 0;
}

/* Uproszczone: porownanie plain po decrypt z powtorna ekstrakcja z src – weryfikacja przez re-encryptRegion temp */
static int TestEncryptRegionSubRect() {
    const int W = 20, H = 20, bpp = 3;
    int stride = ((W * (bpp * 8) + 31) / 32) * 4;
    long sz = (long)stride * H;
    char* src = (char*)malloc((size_t)sz);
    if (!src) return 1;
    FillPseudoRandom(src, (int)sz, 0x12345678u);

    int x0 = 5, y0 = 7, rw = 6, rh = 4;
    int regionStride = ((rw * bpp + 3) / 4) * 4;
    int expectedPlain = regionStride * rh;
    int maxOut = expectedPlain * 2 + 4096;
    char* comp = (char*)malloc((size_t)maxOut);
    char* dec = (char*)malloc((size_t)expectedPlain + 256);
    if (!comp || !dec) {
        free(src);
        free(comp);
        free(dec);
        return 1;
    }
    int clen = 0;
    Compression::encryptRegion(src, stride, x0, y0, rw, rh, bpp, comp, clen, maxOut);
    int dlen = 0;
    Compression::decrypt(comp, clen, dec, dlen);
    free(comp);

    if (dlen != expectedPlain) {
        printf("FAIL subrect: dlen=%d exp=%d\n", dlen, expectedPlain);
        free(src);
        free(dec);
        return 1;
    }

    char* expect = (char*)malloc((size_t)expectedPlain);
    if (!expect) {
        free(src);
        free(dec);
        return 1;
    }
    for (int row = 0; row < rh; row++) {
        const char* srow = src + (y0 + row) * stride + x0 * bpp;
        char* drow = expect + row * regionStride;
        memcpy(drow, srow, (size_t)(rw * bpp));
        if (regionStride > rw * bpp)
            memset(drow + rw * bpp, 0, (size_t)(regionStride - rw * bpp));
    }

    bool ok = BuffersEqual(dec, expect, expectedPlain);
    free(expect);
    free(src);
    free(dec);
    if (!ok) {
        printf("FAIL encryptRegion subrect content\n");
        return 1;
    }
    printf("encryptRegion subrect OK.\n");
    return 0;
}

/* ---- Sciezka "wire" ---- */
static int TestPipelineFullFrame() {
    ImageData cur;
    if (!AllocImage(&cur, 48, 36, 24)) return 1;
    FillPseudoRandom(cur.pData, (int)cur.dataSize, 0xBEEFu);

    HeaderBmp hdr;
    char* payload = NULL;
    int plen = 0;
    if (!BuildWireFromImage(&cur, &hdr, &payload, &plen, 32, 32)) {
        printf("FAIL pipeline full: build\n");
        return 1;
    }
    if (g_verbose)
        printf("  [wire] FULL compressed len=%d\n", plen);

    ImageData recv;
    if (!AllocImage(&recv, cur.width, cur.height, cur.bitsPerPixel)) {
        FreeWirePayload(payload);
        return 1;
    }
    memset(recv.pData, 0, (size_t)recv.dataSize);

    if (!SimulateReceiveImageData(&recv, hdr, payload)) {
        printf("FAIL pipeline full: receive\n");
        FreeWirePayload(payload);
        return 1;
    }
    FreeWirePayload(payload);

    bool ok = BuffersEqual(cur.pData, recv.pData, (int)cur.dataSize);
    if (!ok) {
        printf("FAIL pipeline full: pixel mismatch\n");
        return 1;
    }
    printf("Pipeline full frame OK.\n");
    return 0;
}

static int TestPipelineDiffFull() {
    ImageData prev, cur;
    if (!AllocImage(&prev, 24, 20, 24) || !AllocImage(&cur, 24, 20, 24)) {
        return 1;
    }
    FillSolid(&prev, 10, 20, 30);
    FillPseudoRandom(cur.pData, (int)cur.dataSize, 0x41414141u);

    ImageData diff;
    memset(&diff, 0, sizeof(diff));
    if (!BmpStream::ComputeDiff(&cur, &prev, &diff, 0, 0, cur.width, cur.height)) {
        printf("FAIL pipeline diff: ComputeDiff\n");
        return 1;
    }

    HeaderBmp hdr;
    char* payload = NULL;
    int plen = 0;
    if (!BuildWireFromImage(&diff, &hdr, &payload, &plen, 32, 32)) {
        printf("FAIL pipeline diff: build\n");
        return 1;
    }
    if (g_verbose)
        printf("  [wire] DIFF full compressed len=%d\n", plen);

    ImageData recv;
    if (!AllocImage(&recv, prev.width, prev.height, prev.bitsPerPixel)) {
        FreeWirePayload(payload);
        return 1;
    }
    memcpy(recv.pData, prev.pData, (size_t)recv.dataSize);

    if (!SimulateReceiveImageData(&recv, hdr, payload)) {
        printf("FAIL pipeline diff: receive\n");
        FreeWirePayload(payload);
        return 1;
    }
    FreeWirePayload(payload);

    bool ok = BuffersEqual(cur.pData, recv.pData, (int)cur.dataSize);
    if (!ok) {
        printf("FAIL pipeline diff: mismatch after ApplyDiff\n");
        return 1;
    }
    printf("Pipeline diff (full xor) OK.\n");
    return 0;
}

static int TestPipelineNoChange() {
    ImageData prev;
    if (!AllocImage(&prev, 16, 16, 24)) return 1;
    FillSolid(&prev, 5, 6, 7);

    ImageData diff;
    memset(&diff, 0, sizeof(diff));
    if (!BmpStream::ComputeDiff(&prev, &prev, &diff, 0, 0, prev.width, prev.height)) {
        return 1;
    }
    /* cur == prev -> pusta roznica */
    if (!diff.isEmptyDiff) {
        printf("FAIL NOCHANGE: expected empty diff\n");
        return 1;
    }

    HeaderBmp hdr;
    char* payload = NULL;
    int plen = 0;
    if (!BuildWireFromImage(&diff, &hdr, &payload, &plen, 32, 32)) {
        return 1;
    }
    if (payload) FreeWirePayload(payload);

    if ((hdr.flags & FRAME_FLAG_NOCHANGE) == 0 || hdr.length != 0) {
        printf("FAIL NOCHANGE: header\n");
        return 1;
    }

    ImageData recv;
    if (!AllocImage(&recv, prev.width, prev.height, prev.bitsPerPixel)) {
        return 1;
    }
    memcpy(recv.pData, prev.pData, (size_t)recv.dataSize);
    char snapshot[512];
    int snapLen = (recv.dataSize < (long)sizeof(snapshot)) ? (int)recv.dataSize : (int)sizeof(snapshot);
    memcpy(snapshot, recv.pData, (size_t)snapLen);

    if (!SimulateReceiveImageData(&recv, hdr, NULL)) {
        printf("FAIL NOCHANGE: receive\n");
        return 1;
    }
    if (!BuffersEqual(snapshot, recv.pData, snapLen)) {
        printf("FAIL NOCHANGE: buffer changed\n");
        return 1;
    }
    printf("Pipeline NOCHANGE OK.\n");
    return 0;
}

static int TestPipelineBlocksSmallMotion() {
    const int W = 64, H = 64;
    ImageData prev, cur;
    if (!AllocImage(&prev, W, H, 24) || !AllocImage(&cur, W, H, 24)) {
        return 1;
    }
    FillSolid(&prev, 0, 0, 0);
    memcpy(cur.pData, prev.pData, (size_t)cur.dataSize);
    /* Maly kwadrat 8x8 – tylko kilka bloczkow 32x32 z ruchem */
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            int x = 10 + dx, y = 12 + dy;
            char* p = cur.pData + y * cur.stride + x * 3;
            p[0] = (char)200;
            p[1] = (char)200;
            p[2] = (char)200;
        }
    }

    ImageData diff;
    memset(&diff, 0, sizeof(diff));
    if (!BmpStream::ComputeDiff(&cur, &prev, &diff, 0, 0, W, H)) {
        return 1;
    }

    HeaderBmp hdr;
    char* payload = NULL;
    int plen = 0;
    if (!BuildWireFromImage(&diff, &hdr, &payload, &plen, 32, 32)) {
        return 1;
    }

    if ((hdr.flags & FRAME_FLAG_BLOCKS) == 0) {
        printf("FAIL BLOCKS motion: expected FRAME_FLAG_BLOCKS (got 0x%02x)\n",
               (unsigned char)hdr.flags);
        FreeWirePayload(payload);
        return 1;
    }
    if (g_verbose)
        printf("  [wire] BLOCKS small motion payload=%d\n", plen);

    ImageData recv;
    if (!AllocImage(&recv, W, H, 24)) {
        FreeWirePayload(payload);
        return 1;
    }
    memcpy(recv.pData, prev.pData, (size_t)recv.dataSize);

    if (!SimulateReceiveImageData(&recv, hdr, payload)) {
        printf("FAIL BLOCKS motion: receive\n");
        FreeWirePayload(payload);
        return 1;
    }
    FreeWirePayload(payload);

    if (!BuffersEqual(cur.pData, recv.pData, (int)cur.dataSize)) {
        printf("FAIL BLOCKS motion: pixel mismatch\n");
        return 1;
    }
    printf("Pipeline BLOCKS (small motion) OK.\n");
    return 0;
}

/* Duzy ruch (>70%% pikseli) — jeden skompresowany XOR bez BLOCKS */
static int TestPipelineFullDiffWhenMostlyChanged() {
    const int W = 48, H = 48;
    ImageData prev, cur;
    if (!AllocImage(&prev, W, H, 24) || !AllocImage(&cur, W, H, 24)) {
        return 1;
    }
    FillSolid(&prev, 1, 2, 3);
    FillPseudoRandom(cur.pData, (int)cur.dataSize, 0x99u);

    ImageData diff;
    memset(&diff, 0, sizeof(diff));
    if (!BmpStream::ComputeDiff(&cur, &prev, &diff, 0, 0, W, H)) {
        return 1;
    }

    HeaderBmp hdr;
    char* payload = NULL;
    int plen = 0;
    if (!BuildWireFromImage(&diff, &hdr, &payload, &plen, 32, 32)) {
        return 1;
    }

    if ((hdr.flags & FRAME_FLAG_BLOCKS) != 0) {
        printf("FAIL fallback diff: expected no BLOCKS when scene mostly changed\n");
        FreeWirePayload(payload);
        return 1;
    }
    if ((hdr.flags & FRAME_FLAG_COMPRESSED) == 0) {
        printf("FAIL fallback diff: expected compressed blob\n");
        FreeWirePayload(payload);
        return 1;
    }

    ImageData recv;
    if (!AllocImage(&recv, W, H, 24)) {
        FreeWirePayload(payload);
        return 1;
    }
    memcpy(recv.pData, prev.pData, (size_t)recv.dataSize);

    if (!SimulateReceiveImageData(&recv, hdr, payload)) {
        printf("FAIL fallback diff: receive\n");
        FreeWirePayload(payload);
        return 1;
    }
    FreeWirePayload(payload);

    if (!BuffersEqual(cur.pData, recv.pData, (int)cur.dataSize)) {
        printf("FAIL fallback diff: pixel mismatch\n");
        return 1;
    }
    printf("Pipeline full XOR blob (heavy motion fallback) OK.\n");
    return 0;
}

/* Zly rozmiar po dekompresji – klient powinien odrzucic */
static int TestRejectBadDecompressedSize() {
    ImageData img;
    if (!AllocImage(&img, 4, 4, 24)) return 1;
    char plain[64];
    char comp[8192];
    memset(plain, 'x', 4);
    int cl = 0;
    Compression::encrypt(plain, 4, comp, cl, (int)sizeof(comp));

    HeaderBmp hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "BMP", 3);
    hdr.flags = (char)(FRAME_FLAG_FULL | FRAME_FLAG_COMPRESSED);
    hdr.width = 4;
    hdr.height = 4;
    hdr.bitsPerPixel = 24;
    hdr.stride = img.stride;
    hdr.length = cl;

    /* Oczekiwany rozmiar po dekompresji = img.dataSize, a nie 4 */
    bool rejected = !SimulateReceiveImageData(&img, hdr, comp);
    if (!rejected) {
        printf("FAIL bad size: expected reject\n");
        return 1;
    }
    printf("Reject overshort decompressed payload OK.\n");
    return 0;
}

/* Pliki z dysku */
static int TestLoadBinRoundTrip(const char* path, const char* label) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0; /* brak pliku — OK */
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        fclose(f);
        printf("FAIL fixture %s: bad size %ld\n", label, sz);
        return 1;
    }
    char* buf = (char*)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return 1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        printf("FAIL fixture %s: read\n", label);
        return 1;
    }
    fclose(f);
    char name[256];
    sprintf(name, "file:%s", label);
    bool ok = RoundTripRaw(name, buf, (int)sz);
    free(buf);
    if (!ok) return 1;
    printf("Fixture %s OK (%ld bytes).\n", label, sz);
    return 0;
}

static int TestLoadGradientRaw() {
    const char* path = "Tests/data/gradient_32x32.raw";
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    const int W = 32, H = 32, bpp = 24;
    int stride = ((W * bpp + 31) / 32) * 4;
    long expected = (long)stride * H;
    if (sz != expected) {
        fclose(f);
        printf("FAIL gradient raw: size %ld expected %ld\n", sz, expected);
        return 1;
    }

    ImageData img;
    if (!AllocImage(&img, W, H, bpp)) {
        fclose(f);
        return 1;
    }
    if (fread(img.pData, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        printf("FAIL gradient raw: fread\n");
        return 1;
    }
    fclose(f);
    img.isFullFrame = true;
    img.isEmptyDiff = false;

    HeaderBmp hdr;
    char* payload = NULL;
    int plen = 0;
    if (!BuildWireFromImage(&img, &hdr, &payload, &plen, 32, 32)) {
        return 1;
    }

    ImageData recv;
    if (!AllocImage(&recv, W, H, bpp)) {
        FreeWirePayload(payload);
        return 1;
    }
    memset(recv.pData, 0, (size_t)recv.dataSize);

    if (!SimulateReceiveImageData(&recv, hdr, payload)) {
        printf("FAIL gradient wire\n");
        FreeWirePayload(payload);
        return 1;
    }
    FreeWirePayload(payload);

    bool ok = BuffersEqual(img.pData, recv.pData, (int)sz);
    if (!ok) {
        printf("FAIL gradient raw pipeline\n");
        return 1;
    }
    printf("Fixture gradient_32x32.raw pipeline OK.\n");
    return 0;
}

static int TestApplyDiffLegacy() {
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

    if (!BmpStream::ApplyDiff(target, stride, bytesPerPixel * 8,
            diff, stride, width, height, 0, 0, false) ||
        !BuffersEqual(target, current, dataSize)) {
        printf("ApplyDiff XOR test failed.\n");
        return 1;
    }

    char targetFull[dataSize];
    for (int i = 0; i < dataSize; ++i) targetFull[i] = 0;

    if (!BmpStream::ApplyDiff(targetFull, stride, bytesPerPixel * 8,
            current, stride, width, height, 0, 0, true) ||
        !BuffersEqual(targetFull, current, dataSize)) {
        printf("ApplyDiff full-frame test failed.\n");
        return 1;
    }

    printf("ApplyDiff tests OK.\n");
    return 0;
}

static int ReadPackedBlockCount(const char* payload) {
    int n;
    memcpy(&n, payload, 4);
    return n;
}

/* Silnie powtarzalne dane — kompresja << surowy rozmiar */
static int TestCompressionLevelHighlyRedundant() {
    const int raw = 12000;
    char* buf = (char*)malloc((size_t)raw);
    memset(buf, 0, (size_t)raw);
    int maxOut = raw * 2 + 4096;
    char* cmp = (char*)malloc((size_t)maxOut);
    int cl = 0;
    Compression::encrypt(buf, raw, cmp, cl, maxOut);
    free(buf);
    double pct = (raw > 0) ? (100.0 * (double)cl / (double)raw) : 0.0;
    if (g_verbose)
        printf("  [level] zeros %d -> compressed %d (%.3f%% of raw)\n", raw, cl, pct);
    free(cmp);
    if (cl * 50 > raw) {
        printf("FAIL compression level: zeros should compress to well below 2%% of raw (got %d/%d)\n",
               cl, raw);
        return 1;
    }
    printf("Compression level (highly redundant) OK.\n");
    return 0;
}

/* Mały ruch: skompresowany XOR << pełny bufor diff */
static int TestCompressionLevelSmallMotionVsRawDiff() {
    const int W = 80, H = 80;
    ImageData prev, cur, diff;
    if (!AllocImage(&prev, W, H, 24) || !AllocImage(&cur, W, H, 24)) {
        return 1;
    }
    FillSolid(&prev, 0, 0, 0);
    memcpy(cur.pData, prev.pData, (size_t)cur.dataSize);
    cur.pData[10 * cur.stride + 40 * 3 + 1] = (char)0xFF;

    memset(&diff, 0, sizeof(diff));
    if (!BmpStream::ComputeDiff(&cur, &prev, &diff, 0, 0, W, H)) {
        return 1;
    }

    char* blockPayload = NULL;
    int blockLen = 0;
    if (!BmpStream::PackDiffAsBlocks(&diff, 32, 32, 0.7f, &blockPayload, &blockLen)) {
        printf("FAIL compression level motion: expected BLOCKS pack\n");
        return 1;
    }
    int maxC = (int)diff.dataSize * 2 + 4096;
    if (maxC < 64)
        maxC = 64;
    char* fullC = (char*)malloc((size_t)maxC);
    int fullLen = 0;
    Compression::encrypt(diff.pData, (int)diff.dataSize, fullC, fullLen, maxC);
    if (g_verbose) {
        printf("  [level] small motion BLOCKS wire=%d vs monolithic XOR=%d (raw diff=%ld)\n",
               blockLen, fullLen, diff.dataSize);
    }
    /* Wire bloczkowy powinien byc znacznie mniejszy niz surowy bufor XOR (bez kompresji). */
    bool ok = (blockLen < (int)diff.dataSize / 8);
    free(blockPayload);
    free(fullC);
    if (!ok) {
        printf("FAIL compression level: BLOCKS wire should be << raw diff size (%d vs %ld)\n",
               blockLen, diff.dataSize);
        return 1;
    }
    if (g_verbose)
        printf("  [level] BLOCKS %d vs monolithic compressed XOR %d (raw diff %ld)\n",
               blockLen, fullLen, diff.dataSize);
    printf("Compression level (small motion vs raw diff) OK.\n");
    return 0;
}

/* BuildWire z inną siatką == bezpośredni Pack (konfiguracja zgodna z FrameProvider) */
static int TestConfigurableGridWireMatchesPack() {
    const int W = 96, H = 96;
    ImageData prev, cur, diff;
    if (!AllocImage(&prev, W, H, 24) || !AllocImage(&cur, W, H, 24)) {
        return 1;
    }
    FillSolid(&prev, 3, 3, 3);
    memcpy(cur.pData, prev.pData, (size_t)cur.dataSize);
    cur.pData[50 * cur.stride + 50 * 3] = (char)99;

    memset(&diff, 0, sizeof(diff));
    if (!BmpStream::ComputeDiff(&cur, &prev, &diff, 0, 0, W, H)) {
        return 1;
    }

    const int bw = 48, bh = 24;
    char* p1 = NULL;
    int l1 = 0;
    if (!BmpStream::PackDiffAsBlocks(&diff, bw, bh, 0.7f, &p1, &l1)) {
        printf("FAIL grid config: PackDiffAsBlocks\n");
        return 1;
    }

    HeaderBmp hdr;
    char* p2 = NULL;
    int l2 = 0;
    if (!BuildWireFromImage(&diff, &hdr, &p2, &l2, bw, bh)) {
        free(p1);
        return 1;
    }

    bool same = (l1 == l2) && (memcmp(p1, p2, (size_t)l1) == 0);
    free(p1);
    free(p2);
    if (!same) {
        printf("FAIL grid config: BuildWire must match PackDiffAsBlocks for same block size\n");
        return 1;
    }
    printf("Configurable grid (wire vs Pack) OK.\n");
    return 0;
}

/* Drobniejsza siatka — wiecej komorek z ruchem niz przy grubszej (te same dwa piksele) */
static int TestFinerGridMoreBlocksAndOverhead() {
    const int W = 80, H = 80;
    ImageData prev, cur, diff;
    if (!AllocImage(&prev, W, H, 24) || !AllocImage(&cur, W, H, 24)) {
        return 1;
    }
    FillSolid(&prev, 0, 0, 0);
    memcpy(cur.pData, prev.pData, (size_t)cur.dataSize);
    /* (10,10) i (25,10): w siatce 32 jedna komorka [0,32), w siatce 16 dwie [0,16) i [16,32). */
    int pts[2][2] = { {10, 10}, {25, 10} };
    for (int i = 0; i < 2; i++) {
        int x = pts[i][0], y = pts[i][1];
        char* p = cur.pData + y * cur.stride + x * 3;
        p[0] = (char)0xFF;
        p[1] = (char)0xFF;
        p[2] = (char)0xFF;
    }

    memset(&diff, 0, sizeof(diff));
    if (!BmpStream::ComputeDiff(&cur, &prev, &diff, 0, 0, W, H)) {
        return 1;
    }

    char* pay16 = NULL;
    int len16 = 0;
    char* pay32 = NULL;
    int len32 = 0;
    if (!BmpStream::PackDiffAsBlocks(&diff, 16, 16, 0.7f, &pay16, &len16) ||
        !BmpStream::PackDiffAsBlocks(&diff, 32, 32, 0.7f, &pay32, &len32)) {
        printf("FAIL grid overhead: pack\n");
        return 1;
    }

    int n16 = ReadPackedBlockCount(pay16);
    int n32 = ReadPackedBlockCount(pay32);
    if (g_verbose) {
        printf("  [level] two dots: grid16 blocks=%d len=%d | grid32 blocks=%d len=%d\n",
               n16, len16, n32, len32);
    }

    free(pay16);
    free(pay32);

    if (n16 <= n32) {
        printf("FAIL grid overhead: 16px grid should split into more blocks than 32px for this pattern\n");
        return 1;
    }
    printf("Compression/grid block count (16 vs 32) OK.\n");
    return 0;
}

/* Przesuniecie globalne: wykrycie (dx,dy) i rekonstrukcja z reszty XOR */
static int TestTranslationDetectAndApply() {
    const int W = 64, H = 48;
    ImageData prev, cur, diff, client;
    memset(&diff, 0, sizeof(diff));
    memset(&client, 0, sizeof(client));
    if (!AllocImage(&prev, W, H, 24) || !AllocImage(&cur, W, H, 24)) {
        return 1;
    }
    FillSolid(&prev, 0, 0, 0);
    for (int y = 20; y < 28; y++) {
        for (int x = 10; x < 22; x++) {
            char* p = prev.pData + y * prev.stride + x * 3;
            p[0] = p[1] = p[2] = (char)0xFF;
        }
    }
    FillSolid(&cur, 0, 0, 0);
    for (int y = 20; y < 28; y++) {
        for (int x = 16; x < 28; x++) {
            char* p = cur.pData + y * cur.stride + x * 3;
            p[0] = p[1] = p[2] = (char)0xFF;
        }
    }

    int dx = 0, dy = 0;
    if (!BmpStream::TryGlobalTranslationDiff(&cur, &prev, &diff, &dx, &dy)) {
        printf("FAIL translation: TryGlobalTranslationDiff expected true\n");
        delete[] prev.pData;
        delete[] cur.pData;
        if (diff.pData) delete[] diff.pData;
        return 1;
    }

    if (!AllocImage(&client, W, H, 24)) {
        delete[] prev.pData;
        delete[] cur.pData;
        if (diff.pData) delete[] diff.pData;
        return 1;
    }
    memcpy(client.pData, prev.pData, (size_t)prev.dataSize);
    if (!BmpStream::ApplyTranslationResidual(&client, dx, dy,
            diff.pData, diff.stride, diff.width, diff.height)) {
        printf("FAIL translation: ApplyTranslationResidual\n");
        delete[] prev.pData;
        delete[] cur.pData;
        delete[] client.pData;
        if (diff.pData) delete[] diff.pData;
        return 1;
    }
    if (memcmp(client.pData, cur.pData, (size_t)cur.dataSize) != 0) {
        printf("FAIL translation: reconstructed mismatch (dx=%d dy=%d)\n", dx, dy);
        delete[] prev.pData;
        delete[] cur.pData;
        delete[] client.pData;
        if (diff.pData) delete[] diff.pData;
        return 1;
    }
    delete[] prev.pData;
    delete[] cur.pData;
    delete[] client.pData;
    if (diff.pData) delete[] diff.pData;
    printf("Translation detect + residual apply OK.\n");
    return 0;
}

/* TRANSLATION + NOCHANGE: samo przesuniecie bufora (8 B na kablu) */
static int TestPackChangedBlocksRoundTrip() {
    const int W = 64, H = 48;
    ImageData prev, cur;
    if (!AllocImage(&prev, W, H, 24) || !AllocImage(&cur, W, H, 24))
        return 1;
    FillSolid(&prev, 1, 2, 3);
    memcpy(cur.pData, prev.pData, (size_t)cur.dataSize);
    cur.pData[8 * cur.stride + 9 * 3 + 1] = (char)0xAB;

    if (BmpStream::MeasureDirtyRatio(&cur, &prev, 32, 32) <= 0.0f) {
        printf("FAIL PackChangedBlocks: ratio\n");
        return 1;
    }

    char* payload = NULL;
    int plen = 0;
    int st = BmpStream::PackChangedBlocks(&cur, &prev, 32, 32, 0.7f, &payload, &plen);
    if (st != BLOCK_PACK_OK || !payload) {
        printf("FAIL PackChangedBlocks: status %d\n", st);
        delete[] payload;
        return 1;
    }

    ImageData recv;
    if (!AllocImage(&recv, W, H, 24)) {
        delete[] payload;
        return 1;
    }
    memcpy(recv.pData, prev.pData, (size_t)prev.dataSize);
    if (!BmpStream::ApplyPackedBlockDiffs(&recv, payload, plen, NULL)) {
        printf("FAIL PackChangedBlocks: apply\n");
        delete[] payload;
        return 1;
    }
    delete[] payload;
    if (!BuffersEqual(cur.pData, recv.pData, (int)cur.dataSize)) {
        printf("FAIL PackChangedBlocks: pixels\n");
        return 1;
    }

    int stSame = BmpStream::PackChangedBlocks(&cur, &cur, 32, 32, 0.7f, &payload, &plen);
    if (stSame != BLOCK_PACK_NOCHANGE) {
        printf("FAIL PackChangedBlocks: expected NOCHANGE\n");
        delete[] payload;
        return 1;
    }

    printf("PackChangedBlocks round-trip OK.\n");
    return 0;
}

static int TestPipeline16bpp() {
    ImageData prev, cur;
    if (!AllocImage(&prev, 32, 16, 16) || !AllocImage(&cur, 32, 16, 16))
        return 1;
    FillPseudoRandom(prev.pData, (int)prev.dataSize, 0x1616u);
    memcpy(cur.pData, prev.pData, (size_t)prev.dataSize);
    cur.pData[4 * cur.stride + 6 * 2] ^= (char)0xFF;

    ImageData diff;
    if (!BmpStream::ComputeDiff(&cur, &prev, &diff, 0, 0, cur.width, cur.height))
        return 1;

    HeaderBmp hdr;
    char* payload = NULL;
    int plen = 0;
    if (!BuildWireFromImage(&diff, &hdr, &payload, &plen, 16, 16))
        return 1;

    ImageData recv;
    if (!AllocImage(&recv, 32, 16, 16)) {
        FreeWirePayload(payload);
        return 1;
    }
    memcpy(recv.pData, prev.pData, (size_t)prev.dataSize);
    if (!SimulateReceiveImageData(&recv, hdr, payload)) {
        printf("FAIL 16bpp pipeline receive\n");
        FreeWirePayload(payload);
        return 1;
    }
    FreeWirePayload(payload);
    if (!BuffersEqual(cur.pData, recv.pData, (int)cur.dataSize)) {
        printf("FAIL 16bpp pipeline pixels\n");
        return 1;
    }
    printf("Pipeline 16bpp OK.\n");
    return 0;
}

static int TestEncryptAbort() {
    const int n = 8000;
    char* buf = (char*)malloc((size_t)n);
    if (!buf) return 1;
    FillPseudoRandom(buf, n, 0xA5A5A5A5u);
    int maxOut = n * 2 + 4096;
    char* out = (char*)malloc((size_t)maxOut);
    int cl = 0;
    Compression::encrypt(buf, n, out, cl, maxOut, n / 4);
    free(buf);
    free(out);
    if (cl != -1) {
        printf("FAIL encrypt abort: expected -1, got %d\n", cl);
        return 1;
    }
    printf("Encrypt abort OK.\n");
    return 0;
}

static int TestTranslationNoChangeWire() {
    const int W = 48, H = 48;
    ImageData client, expect;
    if (!AllocImage(&client, W, H, 24) || !AllocImage(&expect, W, H, 24)) {
        return 1;
    }
    FillSolid(&client, 0, 0, 0);
    FillSolid(&expect, 0, 0, 0);
    for (int y = 10; y < 18; y++) {
        for (int x = 10; x < 18; x++) {
            char* pc = client.pData + y * client.stride + x * 3;
            pc[0] = pc[1] = pc[2] = (char)0xFF;
        }
    }
    for (int y = 10; y < 18; y++) {
        for (int x = 13; x < 21; x++) {
            char* pe = expect.pData + y * expect.stride + x * 3;
            pe[0] = pe[1] = pe[2] = (char)0xFF;
        }
    }

    HeaderBmp hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "BMP", 3);
    hdr.flags = (char)(FRAME_FLAG_DIFF | FRAME_FLAG_NOCHANGE | FRAME_FLAG_TRANSLATION);
    hdr.width = W;
    hdr.height = H;
    hdr.bitsPerPixel = 24;
    hdr.stride = client.stride;
    hdr.length = 8;
    char pay[8];
    int tdx = 3, tdy = 0;
    memcpy(pay, &tdx, 4);
    memcpy(pay + 4, &tdy, 4);

    if (!SimulateReceiveImageData(&client, hdr, pay)) {
        printf("FAIL translation wire: SimulateReceiveImageData\n");
        delete[] client.pData;
        delete[] expect.pData;
        return 1;
    }
    if (memcmp(client.pData, expect.pData, (size_t)client.dataSize) != 0) {
        printf("FAIL translation wire: pixel mismatch\n");
        delete[] client.pData;
        delete[] expect.pData;
        return 1;
    }
    delete[] client.pData;
    delete[] expect.pData;
    printf("Translation NOCHANGE wire OK.\n");
    return 0;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_verbose = 1;
    }

    int failures = 0;
    failures += TestCompressionEdgeCases();
    failures += TestEncryptRegionStride();
    failures += TestEncryptRegionSubRect();
    failures += TestApplyDiffLegacy();
    failures += TestPipelineFullFrame();
    failures += TestPipelineDiffFull();
    failures += TestPipelineNoChange();
    failures += TestPipelineBlocksSmallMotion();
    failures += TestPipelineFullDiffWhenMostlyChanged();
    failures += TestRejectBadDecompressedSize();
    failures += TestCompressionLevelHighlyRedundant();
    failures += TestCompressionLevelSmallMotionVsRawDiff();
    failures += TestConfigurableGridWireMatchesPack();
    failures += TestFinerGridMoreBlocksAndOverhead();
    failures += TestTranslationDetectAndApply();
    failures += TestTranslationNoChangeWire();
    failures += TestPackChangedBlocksRoundTrip();
    failures += TestPipeline16bpp();
    failures += TestEncryptAbort();
    failures += TestLoadBinRoundTrip("Tests/data/ramp4k.bin", "ramp4k.bin");
    failures += TestLoadGradientRaw();

    if (failures == 0)
        printf("\nAll tests passed.\n");
    else
        printf("\nTests finished with %d failure(s).\n", failures);
    return failures;
}
