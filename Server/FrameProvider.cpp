 #include "FrameProvider.h"
 #include "../Shared/Compression.h"
 
 FrameProvider::FrameProvider(int bpp) {
     frameCallback = NULL;
     callbackUserData = NULL;
     pBmpStream = new BmpStream(bpp);
 }
 
 FrameProvider::~FrameProvider() {
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
 
     return CaptureScreen(frame);
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
