 #ifndef FrameProviderH
 #define FrameProviderH
 
 #include "../Shared/Frame.h"
 #include "../Shared/BmpStream.h"
 
 typedef bool (*FrameGeneratorCallback)(FrameBmp& frame, void* userData);
 
 class FrameProvider {
 private:
     BmpStream* pBmpStream;
     FrameGeneratorCallback frameCallback;
     void* callbackUserData;
 
     bool CaptureScreen(FrameBmp& frame);
     void ImageDataToFrameBmp(const ImageData* img, FrameBmp& frame);
 
 public:
     explicit FrameProvider(int bpp = 24);
     ~FrameProvider();
 
     void Reset();
     void SetFrameGenerator(FrameGeneratorCallback callback, void* userData = NULL);
     bool GetFrame(FrameBmp& frame);
 };
 
 #endif
