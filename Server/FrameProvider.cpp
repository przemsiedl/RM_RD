#include "FrameProvider.h"
#include "../Shared/compression.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool AttachTranslationPrefix(FrameBmp& frame, int dx, int dy) {
    int oldLen = frame.header.length;
    int newLen = 8 + oldLen;
    char* neu = (char*)malloc((size_t)newLen);
    if (!neu)
        return false;
    memcpy(neu, &dx, 4);
    memcpy(neu + 4, &dy, 4);
    if (oldLen > 0 && frame.data.data)
        memcpy(neu + 8, frame.data.data, (size_t)oldLen);
    frame.data.SetReferenceWithOwn(neu);
    frame.header.length = newLen;
    frame.header.flags |= FRAME_FLAG_TRANSLATION;
    return true;
}

FrameProvider::FrameProvider(int bpp) {
    frameCallback = NULL;
    callbackUserData = NULL;
    pBmpStream = new BmpStream(bpp);
    pCurrentFrame = NULL;
    captureThread = NULL;
    captureRunning = 0;
    lastConsumerTick = 0;
    InitializeCriticalSection(&frameSection);
}

FrameProvider::~FrameProvider() {
    StopCaptureThread();
    DeleteCriticalSection(&frameSection);
    if (pCurrentFrame) {
        delete pCurrentFrame;
        pCurrentFrame = NULL;
    }
    if (pBmpStream) {
        delete pBmpStream;
        pBmpStream = NULL;
    }
}

void FrameProvider::SetFrameGenerator(FrameGeneratorCallback callback, void* userData) {
    frameCallback = callback;
    callbackUserData = userData;
}

void FrameProvider::CaptureLoop() {
    if (!pBmpStream->Capture())
        return;
    EnterCriticalSection(&frameSection);
    if (!pCurrentFrame) {
        pCurrentFrame = new ImageData();
    }
    pBmpStream->CopyCurrentTo(pCurrentFrame, 0, 0, pBmpStream->GetWidth(), pBmpStream->GetHeight());
    LeaveCriticalSection(&frameSection);
}

DWORD FrameProvider::SuggestedCaptureSleep() const {
    DWORD last = lastConsumerTick;
    DWORD now = GetTickCount();
    /* Brak klienta: rzadki zrzut, zeby nie grzac CPU. */
    if (last == 0 || (now - last) > 800)
        return 400;
    return 35;
}

DWORD WINAPI FrameProvider::CaptureThreadProc(LPVOID param) {
    FrameProvider* self = (FrameProvider*)param;
    while (self->captureRunning) {
        self->CaptureLoop();
        DWORD ms = self->SuggestedCaptureSleep();
        DWORD slept = 0;
        while (slept < ms && self->captureRunning) {
            Sleep(10);
            slept += 10;
        }
    }
    return 0;
}

void FrameProvider::StartCaptureThread() {
    if (captureThread) return;
    if (!pBmpStream->Capture())
        return;
    pCurrentFrame = new ImageData();
    pBmpStream->CopyCurrentTo(pCurrentFrame, 0, 0, pBmpStream->GetWidth(), pBmpStream->GetHeight());
    captureRunning = 1;
    DWORD threadId = 0;
    captureThread = CreateThread(NULL, 0, CaptureThreadProc, this, 0, &threadId);
    if (!captureThread) {
        captureRunning = 0;
    }
}

void FrameProvider::StopCaptureThread() {
    captureRunning = 0;
    if (captureThread) {
        WaitForSingleObject(captureThread, 5000);
        CloseHandle(captureThread);
        captureThread = NULL;
    }
}

static bool CopyImageInto(const ImageData* src, ImageData* dst) {
    if (!src || !src->pData || !dst) return false;
    if (!dst->pData || dst->width != src->width || dst->height != src->height ||
        dst->stride != src->stride || dst->bitsPerPixel != src->bitsPerPixel ||
        dst->dataSize != src->dataSize) {
        if (dst->pData) delete[] dst->pData;
        dst->width = src->width;
        dst->height = src->height;
        dst->bitsPerPixel = src->bitsPerPixel;
        dst->stride = src->stride;
        dst->dataSize = src->dataSize;
        dst->pData = new char[src->dataSize];
        if (!dst->pData) return false;
    }
    CopyMemory(dst->pData, src->pData, src->dataSize);
    dst->isFullFrame = src->isFullFrame;
    dst->isEmptyDiff = src->isEmptyDiff;
    return true;
}

static void FillNoChange(FrameBmp& frame, int w, int h, int bpp, int stride) {
    frame.header.bitsPerPixel = bpp;
    frame.header.stride = stride;
    frame.header.flags = FRAME_FLAG_DIFF | FRAME_FLAG_NOCHANGE;
    frame.header.x = 0;
    frame.header.y = 0;
    frame.header.width = w;
    frame.header.height = h;
    frame.header.length = 0;
    frame.data.Reset();
}

static bool SameFrameGeom(const ImageData* a, const ImageData* b) {
    return a && b && a->pData && b->pData &&
        a->width == b->width && a->height == b->height &&
        a->stride == b->stride && a->bitsPerPixel == b->bitsPerPixel;
}

bool FrameProvider::GetFrame(FrameBmp& frame, ImageData* lastSentFrame, ImageData* scratch,
        int diffBlockW, int diffBlockH) {
    if (frameCallback) {
        return frameCallback(frame, callbackUserData);
    }
    if (!scratch)
        return false;

    lastConsumerTick = GetTickCount();

    EnterCriticalSection(&frameSection);
    if (!pCurrentFrame || !pCurrentFrame->pData) {
        LeaveCriticalSection(&frameSection);
        return false;
    }

    int w = pCurrentFrame->width;
    int h = pCurrentFrame->height;
    int bpp = pCurrentFrame->bitsPerPixel;
    int stride = pCurrentFrame->stride;
    long dataSize = pCurrentFrame->dataSize;

    bool same = SameFrameGeom(pCurrentFrame, lastSentFrame);
    if (same && memcmp(pCurrentFrame->pData, lastSentFrame->pData, (size_t)dataSize) == 0) {
        LeaveCriticalSection(&frameSection);
        FillNoChange(frame, w, h, bpp, stride);
        return true;
    }

    if (!CopyImageInto(pCurrentFrame, scratch)) {
        LeaveCriticalSection(&frameSection);
        return false;
    }
    LeaveCriticalSection(&frameSection);

    bool ok = BuildFromSnapshot(scratch, same ? lastSentFrame : NULL, frame, diffBlockW, diffBlockH);
    if (ok && lastSentFrame)
        ok = CopyImageInto(scratch, lastSentFrame);
    return ok;
}

bool FrameProvider::BuildFromSnapshot(ImageData* current, ImageData* lastSent, FrameBmp& frame,
        int diffBlockW, int diffBlockH) {
    if (!current || !current->pData)
        return false;

    if (!lastSent) {
        current->isFullFrame = true;
        current->isEmptyDiff = false;
        return ImageDataToFrameBmp(current, frame, diffBlockW, diffBlockH);
    }

    float ratio = BmpStream::MeasureDirtyRatio(current, lastSent, diffBlockW, diffBlockH);
    if (ratio <= 0.0f) {
        FillNoChange(frame, current->width, current->height, current->bitsPerPixel, current->stride);
        return true;
    }

    /* Translacja tylko przy srednim ruchu. Kursor jej nie potrzebuje, a szukanie jest drogie. */
    bool useTrans = false;
    int tdx = 0, tdy = 0;
    ImageData diff;
    if (ratio >= 0.12f && ratio < 1.0f) {
        useTrans = BmpStream::TryGlobalTranslationDiff(current, lastSent, &diff, &tdx, &tdy);
    }

    if (useTrans && diff.isEmptyDiff) {
        frame.header.bitsPerPixel = current->bitsPerPixel;
        frame.header.stride = current->stride;
        frame.header.flags = FRAME_FLAG_DIFF | FRAME_FLAG_NOCHANGE | FRAME_FLAG_TRANSLATION;
        frame.header.x = 0;
        frame.header.y = 0;
        frame.header.width = current->width;
        frame.header.height = current->height;
        frame.header.length = 8;
        char* prefix = (char*)malloc(8);
        if (!prefix)
            return false;
        memcpy(prefix, &tdx, 4);
        memcpy(prefix + 4, &tdy, 4);
        frame.data.SetReferenceWithOwn(prefix);
        return true;
    }

    if (useTrans) {
        bool ok = ImageDataToFrameBmp(&diff, frame, diffBlockW, diffBlockH);
        if (ok && !AttachTranslationPrefix(frame, tdx, tdy))
            ok = false;
        return ok;
    }

    if (ratio < 1.0f) {
        char* payload = NULL;
        int plen = 0;
        int st = BmpStream::PackChangedBlocks(current, lastSent, diffBlockW, diffBlockH,
            0.7f, &payload, &plen);
        if (st == BLOCK_PACK_NOCHANGE) {
            FillNoChange(frame, current->width, current->height, current->bitsPerPixel, current->stride);
            return true;
        }
        if (st == BLOCK_PACK_OK && payload && plen > 0) {
            frame.header.bitsPerPixel = current->bitsPerPixel;
            frame.header.stride = current->stride;
            frame.header.flags = FRAME_FLAG_DIFF | FRAME_FLAG_COMPRESSED | FRAME_FLAG_BLOCKS;
            frame.header.x = 0;
            frame.header.y = 0;
            frame.header.width = current->width;
            frame.header.height = current->height;
            frame.header.length = plen;
            frame.data.SetReferenceWithOwn(payload);
            return true;
        }
        if (payload)
            delete[] payload;
        if (st == BLOCK_PACK_ERROR)
            return false;
    }

    if (!BmpStream::ComputeDiff(current, lastSent, &diff, 0, 0, current->width, current->height))
        return false;
    return ImageDataToFrameBmp(&diff, frame, diffBlockW, diffBlockH);
}

bool FrameProvider::ImageDataToFrameBmp(const ImageData* img, FrameBmp& frame,
        int diffBlockW, int diffBlockH) {
    frame.header.bitsPerPixel = img->bitsPerPixel;
    frame.header.stride = img->stride;

    if (img->isFullFrame) {
        frame.header.flags = FRAME_FLAG_FULL;
        frame.header.x = 0;
        frame.header.y = 0;
        frame.header.width = img->width;
        frame.header.height = img->height;
    } else {
        frame.header.flags = FRAME_FLAG_DIFF;
        if (img->isEmptyDiff) {
            frame.header.flags |= FRAME_FLAG_NOCHANGE;
            frame.header.x = 0;
            frame.header.y = 0;
            frame.header.width = img->width;
            frame.header.height = img->height;
            frame.header.length = 0;
            frame.data.Reset();
            return true;
        }

        frame.header.x = 0;
        frame.header.y = 0;
        frame.header.width = img->width;
        frame.header.height = img->height;

        /* Ruch: zmienione bloczki (siatka z klienta CMD_SET_DIFF_GRID); przy duzym % zmian — jeden blob XOR. */
        char* blockPayload = NULL;
        int blockLen = 0;
        if (BmpStream::PackDiffAsBlocks(img, diffBlockW, diffBlockH, 0.7f, &blockPayload, &blockLen)) {
            frame.data.SetReferenceWithOwn(blockPayload);
            frame.header.flags |= FRAME_FLAG_COMPRESSED | FRAME_FLAG_BLOCKS;
            frame.header.length = blockLen;
            return true;
        }
    }

    /* encrypt: bufor >= dataSize*2+4096 (patrz Compression::encrypt, sciezka LZ). */
    if (img->dataSize > 0 && img->dataSize > (INT_MAX - 4096) / 2)
        return false;
    int maxCompressedSize = (int)img->dataSize * 2 + 4096;
    if (maxCompressedSize < 64)
        maxCompressedSize = 64;
    char* compressedData = new char[maxCompressedSize];
    if (!compressedData) return false;
    int compressedSize = 0;

    /* Przerywamy, gdy LZ nie schodzi ponizej surowego rozmiaru — klient umie odebrac dane bez flagi COMPRESSED. */
    Compression::encrypt(img->pData, (int)img->dataSize, compressedData, compressedSize, maxCompressedSize,
        (int)img->dataSize);

    if (compressedSize > 0 && compressedSize < (int)img->dataSize) {
        frame.data.SetReferenceWithOwn(compressedData);
        frame.header.flags |= FRAME_FLAG_COMPRESSED;
        frame.header.length = compressedSize;
        return true;
    }

    delete[] compressedData;
    if (img->dataSize <= 0)
        return false;
    char* raw = new char[img->dataSize];
    CopyMemory(raw, img->pData, img->dataSize);
    frame.data.SetReferenceWithOwn(raw);
    frame.header.length = (int)img->dataSize;
    return true;
}
