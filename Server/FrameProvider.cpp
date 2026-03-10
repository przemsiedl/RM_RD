#include "FrameProvider.h"
#include "../Shared/Compression.h"

FrameProvider::FrameProvider(int bpp) {
    frameCallback = NULL;
    callbackUserData = NULL;
    pBmpStream = new BmpStream(bpp);
    hasCachedFrame = false;
    captureThread = NULL;
    captureRunning = false;
    InitializeCriticalSection(&frameSection);
}

FrameProvider::~FrameProvider() {
    StopCaptureThread();
    DeleteCriticalSection(&frameSection);
    if (pBmpStream) {
        delete pBmpStream;
        pBmpStream = NULL;
    }
}
 
 void FrameProvider::Reset() {
     if (pBmpStream) {
         pBmpStream->Reset();
     }
 }
 
 void FrameProvider::SetFrameGenerator(FrameGeneratorCallback callback, void* userData) {
     frameCallback = callback;
     callbackUserData = userData;
 }
 
bool FrameProvider::GetFrame(FrameBmp& frame) {
    if (frameCallback) {
        return frameCallback(frame, callbackUserData);
    }

    EnterCriticalSection(&frameSection);
    if (!hasCachedFrame) {
        LeaveCriticalSection(&frameSection);
        return false;
    }
    frame.header = cachedFrame.header;
    if (cachedFrame.header.length > 0 && cachedFrame.data.data) {
        frame.data.CopyData(cachedFrame.data.data, cachedFrame.header.length);
    } else {
        frame.data.Reset();
    }
    LeaveCriticalSection(&frameSection);
    return true;
}

void FrameProvider::CaptureAndStore() {
    FrameBmp frame;
    if (!CaptureScreen(frame)) {
        return;
    }
    EnterCriticalSection(&frameSection);
    cachedFrame.data.Reset();
    cachedFrame.header = frame.header;
    if (frame.header.length > 0 && frame.data.data) {
        cachedFrame.data.CopyData(frame.data.data, frame.header.length);
    }
    hasCachedFrame = true;
    LeaveCriticalSection(&frameSection);
}

DWORD WINAPI FrameProvider::CaptureThreadProc(LPVOID param) {
    FrameProvider* self = (FrameProvider*)param;
    self->CaptureAndStore();
    while (self->captureRunning) {
        self->CaptureAndStore();
        for (int i = 0; i < 5 && self->captureRunning; i++) {
            Sleep(10);
        }
    }
    return 0;
}

void FrameProvider::StartCaptureThread() {
    if (captureThread) {
        return;
    }
    captureRunning = true;
    DWORD threadId = 0;
    captureThread = CreateThread(NULL, 0, CaptureThreadProc, this, 0, &threadId);
    if (!captureThread) {
        captureRunning = false;
    }
}

void FrameProvider::StopCaptureThread() {
    captureRunning = false;
    if (captureThread) {
        WaitForSingleObject(captureThread, 5000);
        CloseHandle(captureThread);
        captureThread = NULL;
    }
}
 
 bool FrameProvider::CaptureScreen(FrameBmp& frame) {
     if (!pBmpStream) {
         return false;
     }
 
     pBmpStream->Capture();
     pBmpStream->CalcDiff();
 
     const ImageData* diff = pBmpStream->GetDiff();
     if (!diff || !diff->pData) {
         return false;
     }
 
     ImageDataToFrameBmp(diff, frame);
     return true;
 }
 
 void FrameProvider::ImageDataToFrameBmp(const ImageData* img, FrameBmp& frame) {
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
         return;
     }
 
     int maxCompressedSize = img->dataSize + 1024;
     char* compressedData = new char[maxCompressedSize];
     int compressedSize = 0;
 
     Compression::encrypt(img->pData, img->dataSize, compressedData, compressedSize);
 
     frame.data.SetReferenceWithOwn(compressedData);
     frame.header.flags |= FRAME_FLAG_COMPRESSED;
     frame.header.length = compressedSize;
 }
