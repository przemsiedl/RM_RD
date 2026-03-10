#include "FrameProvider.h"
#include "../Shared/Compression.h"

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
    pBmpStream->CopyCurrentTo(pCurrentFrame);
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
    pBmpStream->CopyCurrentTo(pCurrentFrame);
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

bool FrameProvider::GetFrame(FrameBmp& frame, ImageData* lastSentFrame) {
    if (frameCallback) {
        return frameCallback(frame, callbackUserData);
    }

    EnterCriticalSection(&frameSection);
    if (!pCurrentFrame || !pCurrentFrame->pData) {
        LeaveCriticalSection(&frameSection);
        return false;
    }

    ImageData diff;
    if (!BmpStream::ComputeDiff(pCurrentFrame, lastSentFrame, &diff)) {
        LeaveCriticalSection(&frameSection);
        return false;
    }

    bool ok = ImageDataToFrameBmp(&diff, frame);

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

bool FrameProvider::ImageDataToFrameBmp(const ImageData* img, FrameBmp& frame) {
    frame.header.x = 0;
    frame.header.y = 0;
    frame.header.width = img->width;
    frame.header.height = img->height;
    frame.header.bitsPerPixel = img->bitsPerPixel;
    frame.header.stride = img->stride;

    if (img->isFullFrame) {
        frame.header.flags = FRAME_FLAG_FULL;
    } else {
        frame.header.flags = FRAME_FLAG_DIFF;
    }

    if (!img->isFullFrame && img->isEmptyDiff) {
        frame.header.flags |= FRAME_FLAG_NOCHANGE;
        frame.header.length = 0;
        frame.data.Reset();
        return true;
    }

    int maxCompressedSize = img->dataSize + 1024;
    char* compressedData = new char[maxCompressedSize];
    if (!compressedData) return false;
    int compressedSize = 0;

    Compression::encrypt(img->pData, img->dataSize, compressedData, compressedSize);

    frame.data.SetReferenceWithOwn(compressedData);
    frame.header.flags |= FRAME_FLAG_COMPRESSED;
    frame.header.length = compressedSize;
    return true;
}
