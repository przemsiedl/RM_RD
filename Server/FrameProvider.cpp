#include "FrameProvider.h"
#include "../Shared/Compression.h"
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
    pBmpStream->Capture();
    EnterCriticalSection(&frameSection);
    if (!pCurrentFrame) {
        pCurrentFrame = new ImageData();
    }
    pBmpStream->CopyCurrentTo(pCurrentFrame, 0, 0, pBmpStream->GetWidth(), pBmpStream->GetHeight());
    LeaveCriticalSection(&frameSection);
}

DWORD WINAPI FrameProvider::CaptureThreadProc(LPVOID param) {
    FrameProvider* self = (FrameProvider*)param;
    while (self->captureRunning) {
        self->CaptureLoop();
        for (int i = 0; i < 5 && self->captureRunning; i++) {
            Sleep(10);
        }
    }
    return 0;
}

void FrameProvider::StartCaptureThread() {
    if (captureThread) return;
    pBmpStream->Capture();
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

bool FrameProvider::GetFrame(FrameBmp& frame, ImageData* lastSentFrame,
        int diffBlockW, int diffBlockH) {
    if (frameCallback) {
        return frameCallback(frame, callbackUserData);
    }

    EnterCriticalSection(&frameSection);
    if (!pCurrentFrame || !pCurrentFrame->pData) {
        LeaveCriticalSection(&frameSection);
        return false;
    }

    ImageData diff;
    bool useTrans = false;
    int tdx = 0, tdy = 0;

    if (lastSentFrame && lastSentFrame->pData &&
        lastSentFrame->width == pCurrentFrame->width &&
        lastSentFrame->height == pCurrentFrame->height &&
        lastSentFrame->stride == pCurrentFrame->stride &&
        lastSentFrame->bitsPerPixel == pCurrentFrame->bitsPerPixel) {
        useTrans = BmpStream::TryGlobalTranslationDiff(
            pCurrentFrame, lastSentFrame, &diff, &tdx, &tdy);
    }

    if (!useTrans) {
        if (!BmpStream::ComputeDiff(pCurrentFrame, lastSentFrame, &diff,
                0, 0, pCurrentFrame->width, pCurrentFrame->height)) {
            LeaveCriticalSection(&frameSection);
            return false;
        }
    }

    bool ok = false;
    if (useTrans && diff.isEmptyDiff) {
        frame.header.bitsPerPixel = pCurrentFrame->bitsPerPixel;
        frame.header.stride = pCurrentFrame->stride;
        frame.header.flags = FRAME_FLAG_DIFF | FRAME_FLAG_NOCHANGE | FRAME_FLAG_TRANSLATION;
        frame.header.x = 0;
        frame.header.y = 0;
        frame.header.width = pCurrentFrame->width;
        frame.header.height = pCurrentFrame->height;
        frame.header.length = 8;
        char* prefix = (char*)malloc(8);
        if (!prefix) {
            LeaveCriticalSection(&frameSection);
            return false;
        }
        memcpy(prefix, &tdx, 4);
        memcpy(prefix + 4, &tdy, 4);
        frame.data.SetReferenceWithOwn(prefix);
        ok = true;
    } else {
        ok = ImageDataToFrameBmp(&diff, frame, diffBlockW, diffBlockH);
        if (ok && useTrans && !AttachTranslationPrefix(frame, tdx, tdy))
            ok = false;
    }

    if (ok && lastSentFrame) {
        if (lastSentFrame->width != pCurrentFrame->width ||
            lastSentFrame->height != pCurrentFrame->height ||
            lastSentFrame->stride != pCurrentFrame->stride) {
            if (lastSentFrame->pData) delete[] lastSentFrame->pData;
            lastSentFrame->width = pCurrentFrame->width;
            lastSentFrame->height = pCurrentFrame->height;
            lastSentFrame->bitsPerPixel = pCurrentFrame->bitsPerPixel;
            lastSentFrame->stride = pCurrentFrame->stride;
            lastSentFrame->dataSize = pCurrentFrame->dataSize;
            lastSentFrame->pData = new char[pCurrentFrame->dataSize];
        }
        if (lastSentFrame->pData) {
            CopyMemory(lastSentFrame->pData, pCurrentFrame->pData, pCurrentFrame->dataSize);
        } else {
            ok = false;
        }
    }

    LeaveCriticalSection(&frameSection);
    return ok;
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

    Compression::encrypt(img->pData, img->dataSize, compressedData, compressedSize, maxCompressedSize);

    frame.data.SetReferenceWithOwn(compressedData);
    frame.header.flags |= FRAME_FLAG_COMPRESSED;
    frame.header.length = compressedSize;
    return true;
}
