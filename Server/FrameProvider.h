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
    FrameBmp cachedFrame;
    bool hasCachedFrame;

    HANDLE captureThread;
    volatile bool captureRunning;

    bool CaptureScreen(FrameBmp& frame);
    void ImageDataToFrameBmp(const ImageData* img, FrameBmp& frame);
    void CaptureAndStore();
    static DWORD WINAPI CaptureThreadProc(LPVOID param);

public:
    explicit FrameProvider(int bpp = 24);
    ~FrameProvider();

    void Reset();
    void SetFrameGenerator(FrameGeneratorCallback callback, void* userData = NULL);
    bool GetFrame(FrameBmp& frame);

    void StartCaptureThread();
    void StopCaptureThread();
};

#endif
