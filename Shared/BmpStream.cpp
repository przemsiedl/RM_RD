#include "..\Shared\BmpStream.h"

BmpStream::BmpStream(int bpp) :
    pPrevious(NULL),
    pCurrent(NULL),
    pDiff(NULL),
    bitsPerPixel(bpp)
{
    if(bitsPerPixel != 24 && bitsPerPixel != 32)
    {
        bitsPerPixel = 24;
    }
}

BmpStream::~BmpStream()
{
    if(pPrevious)
    {
        delete pPrevious;
    }
    if(pCurrent)
    {
        delete pCurrent;
    }
    if(pDiff)
    {
        delete pDiff;
    }
}

void BmpStream::Reset()
{
    if(pPrevious)
    {
        delete pPrevious;
        pPrevious = NULL;
    }
    if(pCurrent)
    {
        delete pCurrent;
        pCurrent = NULL;
    }
    if(pDiff)
    {
        delete pDiff;
        pDiff = NULL;
    }
}

void BmpStream::Capture()
{
    if (pPrevious)
    {
        delete pPrevious;
    }

    pPrevious = pCurrent;
    pCurrent = NULL;

    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HDC hScreenDC = GetDC(NULL);
    if(!hScreenDC)
    {
        return;
    }

    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    if(!hMemDC)
    {
        ReleaseDC(NULL, hScreenDC);
        return;
    }

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = screenWidth;
    bmi.bmiHeader.biHeight = -screenHeight;  // ujemna = top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = bitsPerPixel;
    bmi.bmiHeader.biCompression = BI_RGB;

    char* pDibData = NULL;
    HBITMAP hDib = CreateDIBSection(hMemDC,
                                    &bmi,
                                    DIB_RGB_COLORS,
                                    (void**)&pDibData,
                                    NULL,
                                    0);
    if (!hDib || !pDibData)
    {
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        return;
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hDib);

    BitBlt( hMemDC,
            0,
            0,
            screenWidth,
            screenHeight,
            hScreenDC,
            0,
            0,
            SRCCOPY);
           
    int stride = ((screenWidth * bitsPerPixel + 31) / 32) * 4;  // wyrównanie do 4 bajtów
    long dataSize = stride * screenHeight;

    pCurrent = new ImageData();
    pCurrent->width = screenWidth;
    pCurrent->height = screenHeight;
    pCurrent->bitsPerPixel = bitsPerPixel;
    pCurrent->stride = stride;
    pCurrent->dataSize = dataSize;
    pCurrent->pData = new char[dataSize];
    pCurrent->isFullFrame = true;

    CopyMemory(pCurrent->pData, pDibData, dataSize);

    SelectObject(hMemDC, hOldBmp);
    DeleteObject(hDib);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
}

bool BmpStream::CalcDiff()
{
    if (pDiff)
    {
        delete pDiff;
        pDiff = NULL;
    }

    if (!pCurrent)
    {
        return false;
    }

    pDiff = new ImageData();
    pDiff->width = pCurrent->width;
    pDiff->height = pCurrent->height;
    pDiff->bitsPerPixel = pCurrent->bitsPerPixel;
    pDiff->stride = pCurrent->stride;
    pDiff->dataSize = pCurrent->dataSize;
    pDiff->pData = new char[pCurrent->dataSize];

    bool isFullFrame = (!pPrevious ||
                        pPrevious->width != pCurrent->width ||
                        pPrevious->height != pCurrent->height ||
                        pPrevious->bitsPerPixel != pCurrent->bitsPerPixel);

    pDiff->isFullFrame = isFullFrame;

    if (isFullFrame)
    {
        CopyMemory(pDiff->pData, pCurrent->pData, pCurrent->dataSize);
        pDiff->isEmptyDiff = false;
        return true;
    }
    int bytesPerPixel = bitsPerPixel / 8;
    bool anyDiff = false;

    for (int y = 0; y < pCurrent->height; y++)
    {
        char* pCurrRow = pCurrent->pData + (y * pCurrent->stride);
        char* pPrevRow = pPrevious->pData + (y * pPrevious->stride);
        char* pDiffRow = pDiff->pData + (y * pDiff->stride);

        for (int x = 0; x < pCurrent->width; x++)
        {
            int offset = x * bytesPerPixel;
            for (int i = 0; i < bytesPerPixel; i++)
            {
                char value = pCurrRow[offset + i] ^ pPrevRow[offset + i];
                pDiffRow[offset + i] = value;
                if (value != 0) {
                    anyDiff = true;
                }
            }
        }
    }

    pDiff->isEmptyDiff = !anyDiff;
    return anyDiff;
}

bool BmpStream::ApplyDiffXOR(char* target, const ImageData* diff)
{
    if (!target || ! diff || !diff->pData)
        return false;

    if (diff->isFullFrame)
    {
        CopyMemory(target, diff->pData, diff->dataSize);
        return true;
    }

    int bytesPerPixel = diff->bitsPerPixel / 8;
    for (int y = 0; y < diff->height; y++)
    {
        char* pDiffRow = diff->pData + (y * diff->stride);
        char* pTargetRow = target + (y * diff->stride);

        for (int x = 0; x < diff->width; x++)
        {
            int offset = x * bytesPerPixel;
            for (int i = 0; i < bytesPerPixel; i++)
            {
                pTargetRow[offset + i] ^= pDiffRow[offset + i];
            }
        }
    }

    return true;
}

bool BmpStream::ComputeDiff(const ImageData* current, const ImageData* previous,
                             ImageData* outDiff) {
    if (!current || !current->pData || !outDiff) return false;

    outDiff->width = current->width;
    outDiff->height = current->height;
    outDiff->bitsPerPixel = current->bitsPerPixel;
    outDiff->stride = current->stride;
    outDiff->dataSize = current->dataSize;

    if (outDiff->pData) delete[] outDiff->pData;
    outDiff->pData = new char[current->dataSize];
    if (!outDiff->pData) return false;

    bool isFullFrame = (!previous || !previous->pData ||
                        previous->width != current->width ||
                        previous->height != current->height ||
                        previous->bitsPerPixel != current->bitsPerPixel);

    outDiff->isFullFrame = isFullFrame;

    if (isFullFrame) {
        CopyMemory(outDiff->pData, current->pData, current->dataSize);
        outDiff->isEmptyDiff = false;
        return true;
    }

    int bytesPerPixel = current->bitsPerPixel / 8;
    bool anyDiff = false;

    for (int y = 0; y < current->height; y++) {
        char* pCurrRow = current->pData + (y * current->stride);
        char* pPrevRow = previous->pData + (y * previous->stride);
        char* pDiffRow = outDiff->pData + (y * outDiff->stride);

        for (int x = 0; x < current->width; x++) {
            int offset = x * bytesPerPixel;
            for (int i = 0; i < bytesPerPixel; i++) {
                char value = pCurrRow[offset + i] ^ pPrevRow[offset + i];
                pDiffRow[offset + i] = value;
                if (value != 0) anyDiff = true;
            }
        }
    }

    outDiff->isEmptyDiff = !anyDiff;
    return true;
}

bool BmpStream::CopyCurrentTo(ImageData* out) const {
    if (!pCurrent || !pCurrent->pData || !out) return false;
    if (out->width != pCurrent->width || out->height != pCurrent->height ||
        out->stride != pCurrent->stride || out->dataSize != pCurrent->dataSize ||
        !out->pData) {
        if (out->pData) delete[] out->pData;
        out->width = pCurrent->width;
        out->height = pCurrent->height;
        out->bitsPerPixel = pCurrent->bitsPerPixel;
        out->stride = pCurrent->stride;
        out->dataSize = pCurrent->dataSize;
        out->pData = new char[pCurrent->dataSize];
        if (!out->pData) return false;
    }
    CopyMemory(out->pData, pCurrent->pData, pCurrent->dataSize);
    return true;
}