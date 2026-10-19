#ifndef FrameProviderH
#define FrameProviderH

#include <windows.h>
#include "../Shared/Frame.h"
#include "../Shared/BmpStream.h"

typedef bool (*FrameGeneratorCallback)(FrameBmp& frame, void* userData);

class FrameProvider {
private:
    BmpStream* pBmpStream;
    FrameGeneratorCallback frameCallback;
    void* callbackUserData;

    CRITICAL_SECTION frameSection;
    ImageData* pCurrentFrame;
    HANDLE captureThread;
    volatile long captureRunning;
    volatile DWORD lastConsumerTick;

    void CaptureLoop();
    static DWORD WINAPI CaptureThreadProc(LPVOID param);
    bool ImageDataToFrameBmp(const ImageData* img, FrameBmp& frame, int diffBlockW, int diffBlockH);
    bool BuildFromSnapshot(ImageData* current, ImageData* lastSent, FrameBmp& frame,
        int diffBlockW, int diffBlockH);
    DWORD SuggestedCaptureSleep() const;

public:
    explicit FrameProvider(int bpp = 24);
    ~FrameProvider();

    void SetFrameGenerator(FrameGeneratorCallback callback, void* userData = NULL);
    /* scratch: bufor roboczy klienta (pelna klatka), zeby nie alokowac jej przy kazdym zadaniu. */
    bool GetFrame(FrameBmp& frame, ImageData* lastSentFrame, ImageData* scratch,
        int diffBlockW, int diffBlockH);

    void StartCaptureThread();
    void StopCaptureThread();
};

#endif
