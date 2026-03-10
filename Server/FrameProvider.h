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

    void CaptureLoop();
    static DWORD WINAPI CaptureThreadProc(LPVOID param);
    bool ImageDataToFrameBmp(const ImageData* img, FrameBmp& frame);

public:
    explicit FrameProvider(int bpp = 24);
    ~FrameProvider();

    void SetFrameGenerator(FrameGeneratorCallback callback, void* userData = NULL);
    bool GetFrame(FrameBmp& frame, ImageData* lastSentFrame);

    void StartCaptureThread();
    void StopCaptureThread();
};

#endif
