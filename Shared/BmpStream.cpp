#include "..\Shared\BmpStream.h"
#include "..\Shared\compression.h"
#include <stdlib.h>

/* VC6 / starsze Win32: brak typu long long (C99) — SSD na __int64 */
typedef __int64 BmpSsdAcc;
#define BMP_SSD_HUGE ((BmpSsdAcc)1 << 62)

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

bool BmpStream::CalcDiff(int x, int y, int w, int h)
{
    if (pDiff) { delete pDiff; pDiff = NULL; }
    if (!pCurrent || w <= 0 || h <= 0) return false;
    if (x < 0 || y < 0 || x + w > pCurrent->width || y + h > pCurrent->height) return false;

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

    ZeroMemory(pDiff->pData, pDiff->dataSize);
    int bytesPerPixel = bitsPerPixel / 8;
    bool anyDiff = false;

    for (int py = y; py < y + h; py++)
    {
        char* pCurrRow = pCurrent->pData + (py * pCurrent->stride);
        char* pPrevRow = pPrevious->pData + (py * pPrevious->stride);
        char* pDiffRow = pDiff->pData + (py * pDiff->stride);

        for (int px = x; px < x + w; px++)
        {
            int offset = px * bytesPerPixel;
            for (int i = 0; i < bytesPerPixel; i++)
            {
                char value = pCurrRow[offset + i] ^ pPrevRow[offset + i];
                pDiffRow[offset + i] = value;
                if (value != 0) anyDiff = true;
            }
        }
    }

    pDiff->isEmptyDiff = !anyDiff;
    return anyDiff;
}

bool BmpStream::ApplyDiff(char* target, int targetStride, int targetBpp,
    const char* diffData, int diffStride, int diffWidth, int diffHeight,
    int targetX, int targetY, bool isFullFrame)
{
    if (!target || !diffData || diffWidth <= 0 || diffHeight <= 0) return false;

    int bytesPerPixel = targetBpp / 8;

    if (isFullFrame)
    {
        int rowBytes = (diffStride < targetStride) ? diffStride : targetStride;
        for (int y = 0; y < diffHeight; y++)
        {
            char* pTargetRow = target + (y * targetStride);
            const char* pDiffRow = diffData + (y * diffStride);
            CopyMemory(pTargetRow, pDiffRow, rowBytes);
        }
        return true;
    }

    for (int y = 0; y < diffHeight; y++)
    {
        char* pTargetRow = target + ((targetY + y) * targetStride) + (targetX * bytesPerPixel);
        const char* pDiffRow = diffData + (y * diffStride);

        for (int x = 0; x < diffWidth; x++)
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
    ImageData* outDiff, int rx, int ry, int rw, int rh)
{
    if (!current || !current->pData || !outDiff || rw <= 0 || rh <= 0) return false;
    if (rx < 0 || ry < 0 || rx + rw > current->width || ry + rh > current->height) return false;

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

    ZeroMemory(outDiff->pData, outDiff->dataSize);
    int bytesPerPixel = current->bitsPerPixel / 8;
    bool anyDiff = false;

    for (int py = ry; py < ry + rh; py++) {
        char* pCurrRow = current->pData + (py * current->stride);
        char* pPrevRow = previous->pData + (py * previous->stride);
        char* pDiffRow = outDiff->pData + (py * outDiff->stride);

        for (int px = rx; px < rx + rw; px++) {
            int offset = px * bytesPerPixel;
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

bool BmpStream::ComputeDiffBoundingRect(const ImageData* diff,
    int& outMinX, int& outMinY, int& outMaxX, int& outMaxY,
    int sx, int sy, int sw, int sh)
{
    if (!diff || !diff->pData || diff->width <= 0 || diff->height <= 0) return false;
    if (sw <= 0 || sh <= 0 || sx < 0 || sy < 0 ||
        sx + sw > diff->width || sy + sh > diff->height) return false;

    int bytesPerPixel = diff->bitsPerPixel / 8;
    outMinX = diff->width;
    outMinY = diff->height;
    outMaxX = -1;
    outMaxY = -1;

    for (int y = sy; y < sy + sh; y++) {
        const char* pRow = diff->pData + (y * diff->stride);
        for (int x = sx; x < sx + sw; x++) {
            int offset = x * bytesPerPixel;
            bool nonZero = false;
            for (int i = 0; i < bytesPerPixel; i++) {
                if (pRow[offset + i] != 0) { nonZero = true; break; }
            }
            if (nonZero) {
                if (x < outMinX) outMinX = x;
                if (x > outMaxX) outMaxX = x;
                if (y < outMinY) outMinY = y;
                if (y > outMaxY) outMaxY = y;
            }
        }
    }

    return (outMaxX >= outMinX && outMaxY >= outMinY);
}

bool BmpStream::CopyCurrentTo(ImageData* out, int x, int y, int w, int h) const {
    if (!pCurrent || !pCurrent->pData || !out || w <= 0 || h <= 0) return false;
    if (x < 0 || y < 0 || x + w > pCurrent->width || y + h > pCurrent->height) return false;

    int bytesPerPixel = pCurrent->bitsPerPixel / 8;
    int regionStride = ((w * bytesPerPixel + 3) / 4) * 4;
    long dataSize = regionStride * h;

    if (out->width != w || out->height != h || out->stride != regionStride ||
        out->dataSize != dataSize || !out->pData) {
        if (out->pData) delete[] out->pData;
        out->width = w;
        out->height = h;
        out->bitsPerPixel = pCurrent->bitsPerPixel;
        out->stride = regionStride;
        out->dataSize = dataSize;
        out->pData = new char[dataSize];
        if (!out->pData) return false;
    }

    for (int py = 0; py < h; py++) {
        const char* pSrcRow = pCurrent->pData + ((y + py) * pCurrent->stride) + (x * bytesPerPixel);
        char* pOutRow = out->pData + (py * regionStride);
        CopyMemory(pOutRow, pSrcRow, w * bytesPerPixel);
        if (regionStride > w * bytesPerPixel) {
            ZeroMemory(pOutRow + w * bytesPerPixel, regionStride - w * bytesPerPixel);
        }
    }
    return true;
}

static bool DiffBlockHasMotion(const ImageData* diff, int bx, int by, int bw, int bh) {
    int bpp = diff->bitsPerPixel / 8;
    for (int y = by; y < by + bh; y++) {
        const char* row = diff->pData + y * diff->stride;
        for (int x = bx; x < bx + bw; x++) {
            const char* pix = row + x * bpp;
            for (int k = 0; k < bpp; k++) {
                if (pix[k] != 0)
                    return true;
            }
        }
    }
    return false;
}

static int MinInt(int a, int b) {
    return (a < b) ? a : b;
}

bool BmpStream::PackDiffAsBlocks(const ImageData* diff, int blockW, int blockH,
    float maxDirtyAreaRatio, char** outBuf, int* outLen) {
    *outBuf = NULL;
    *outLen = 0;
    if (!diff || !diff->pData || diff->isFullFrame || diff->isEmptyDiff)
        return false;
    if (blockW <= 0 || blockH <= 0 || maxDirtyAreaRatio <= 0.0f || maxDirtyAreaRatio > 1.0f)
        return false;

    int bpp = diff->bitsPerPixel / 8;
    long totalPixels = (long)diff->width * diff->height;
    if (totalPixels <= 0)
        return false;

    int cols = (diff->width + blockW - 1) / blockW;
    int rows = (diff->height + blockH - 1) / blockH;
    int maxBlocks = cols * rows;

    typedef struct {
        int x, y, w, h;
    } Cell;
    Cell* cells = (Cell*)malloc((size_t)maxBlocks * sizeof(Cell));
    if (!cells)
        return false;

    long dirtyPixels = 0;
    int nCells = 0;

    for (int ri = 0; ri < rows; ri++) {
        for (int ci = 0; ci < cols; ci++) {
            int bx = ci * blockW;
            int by = ri * blockH;
            int bw = MinInt(blockW, diff->width - bx);
            int bh = MinInt(blockH, diff->height - by);
            if (bw <= 0 || bh <= 0)
                continue;
            if (!DiffBlockHasMotion(diff, bx, by, bw, bh))
                continue;
            cells[nCells].x = bx;
            cells[nCells].y = by;
            cells[nCells].w = bw;
            cells[nCells].h = bh;
            dirtyPixels += (long)bw * bh;
            nCells++;
        }
    }

    if (nCells == 0) {
        free(cells);
        return false;
    }

    if ((float)dirtyPixels >= (float)totalPixels * maxDirtyAreaRatio) {
        free(cells);
        return false;
    }

    int estimated = 4;
    for (int i = 0; i < nCells; i++) {
        int rs = ((cells[i].w * bpp + 3) / 4) * 4;
        int plainBlock = rs * cells[i].h;
        int cap = plainBlock * 2 + 4096;
        estimated += 20 + cap;
    }

    char* out = (char*)malloc((size_t)estimated);
    if (!out) {
        free(cells);
        return false;
    }

    char* p = out;
    memcpy(p, &nCells, 4);
    p += 4;

    for (int i = 0; i < nCells; i++) {
        int x = cells[i].x;
        int y = cells[i].y;
        int w = cells[i].w;
        int h = cells[i].h;
        int rs = ((w * bpp + 3) / 4) * 4;
        int plainBlock = rs * h;
        int maxComp = plainBlock * 2 + 4096;
        char* comp = (char*)malloc((size_t)maxComp);
        if (!comp) {
            free(out);
            free(cells);
            return false;
        }
        int clen = 0;
        Compression::encryptRegion(diff->pData, diff->stride, x, y, w, h, bpp, comp, clen, maxComp);
        memcpy(p, &x, 4);
        p += 4;
        memcpy(p, &y, 4);
        p += 4;
        memcpy(p, &w, 4);
        p += 4;
        memcpy(p, &h, 4);
        p += 4;
        memcpy(p, &clen, 4);
        p += 4;
        memcpy(p, comp, (size_t)clen);
        p += clen;
        free(comp);
    }

    free(cells);
    *outLen = (int)(p - out);
    *outBuf = out;
    return true;
}

bool BmpStream::ApplyPackedBlockDiffs(ImageData* img, const char* payload, int payloadLen) {
    if (!img || !img->pData || !payload || payloadLen < 4)
        return false;

    const char* p = payload;
    const char* end = payload + payloadLen;
    int n;
    memcpy(&n, p, 4);
    p += 4;
    if (n <= 0 || n > 65536)
        return false;

    for (int i = 0; i < n; i++) {
        if (p + 20 > end)
            return false;
        int x, y, w, h, clen;
        memcpy(&x, p, 4);
        p += 4;
        memcpy(&y, p, 4);
        p += 4;
        memcpy(&w, p, 4);
        p += 4;
        memcpy(&h, p, 4);
        p += 4;
        memcpy(&clen, p, 4);
        p += 4;
        if (clen < 0 || p + clen > end)
            return false;
        if (w <= 0 || h <= 0)
            return false;
        if (x < 0 || y < 0 || x + w > img->width || y + h > img->height)
            return false;

        int bpp = img->bitsPerPixel / 8;
        int regionStride = ((w * bpp + 3) / 4) * 4;
        int expSize = regionStride * h;
        int maxDec = expSize + 4096;
        char* dec = (char*)malloc((size_t)maxDec);
        if (!dec)
            return false;
        int dlen = 0;
        Compression::decrypt(p, clen, dec, dlen);
        if (dlen != expSize) {
            free(dec);
            return false;
        }
        if (!ApplyDiff(img->pData, img->stride, img->bitsPerPixel,
                dec, regionStride, w, h, x, y, false)) {
            free(dec);
            return false;
        }
        free(dec);
        p += clen;
    }

    return (p == end);
}

/* --- Global translation (motion) --- */

static int CountNonzeroDiffPixels(const ImageData* d) {
    if (!d || !d->pData || d->width <= 0 || d->height <= 0)
        return 0;
    int bpp = d->bitsPerPixel / 8;
    int c = 0;
    for (int y = 0; y < d->height; y++) {
        const char* row = d->pData + y * d->stride;
        for (int x = 0; x < d->width; x++) {
            const char* pix = row + x * bpp;
            bool nz = false;
            for (int k = 0; k < bpp; k++) {
                if (pix[k] != 0) {
                    nz = true;
                    break;
                }
            }
            if (nz)
                c++;
        }
    }
    return c;
}

static unsigned char* DownsampleGray(const ImageData* img, int scale, int* outW, int* outH) {
    int w = img->width;
    int h = img->height;
    int wS = (w + scale - 1) / scale;
    int hS = (h + scale - 1) / scale;
    int bpp = img->bitsPerPixel / 8;
    unsigned char* buf = (unsigned char*)malloc((size_t)wS * (size_t)hS);
    if (!buf)
        return NULL;
    for (int ys = 0; ys < hS; ys++) {
        for (int xs = 0; xs < wS; xs++) {
            long sum = 0;
            int cnt = 0;
            int y0 = ys * scale;
            int x0 = xs * scale;
            for (int tdy = 0; tdy < scale && y0 + tdy < h; tdy++) {
                for (int tdx = 0; tdx < scale && x0 + tdx < w; tdx++) {
                    const char* p = img->pData + (y0 + tdy) * img->stride + (x0 + tdx) * bpp;
                    sum += (unsigned char)p[0] + (unsigned char)p[1] + (unsigned char)p[2];
                    cnt += 3;
                }
            }
            buf[ys * wS + xs] = cnt ? (unsigned char)(sum / cnt) : 0;
        }
    }
    *outW = wS;
    *outH = hS;
    return buf;
}

static BmpSsdAcc SsdGrayOffset(const unsigned char* cur, int w, int h,
    const unsigned char* prev, int pw, int ph, int dx, int dy) {
    BmpSsdAcc s = 0;
    for (int y = 0; y < h; y++) {
        int sy = y - dy;
        if (sy < 0 || sy >= ph) {
            s += (BmpSsdAcc)255 * 255 * w;
            continue;
        }
        for (int x = 0; x < w; x++) {
            int sx = x - dx;
            if (sx < 0 || sx >= pw)
                s += (BmpSsdAcc)255 * 255;
            else {
                int d = (int)cur[y * w + x] - (int)prev[sy * pw + sx];
                s += (BmpSsdAcc)d * d;
            }
        }
    }
    return s;
}

static BmpSsdAcc SparseSsdBgr(const ImageData* c, const ImageData* p, int dx, int dy, int step) {
    int w = c->width;
    int h = c->height;
    int bpp = c->bitsPerPixel / 8;
    BmpSsdAcc s = 0;
    if (step < 1)
        step = 1;
    for (int y = 0; y < h; y += step) {
        int sy = y - dy;
        if (sy < 0 || sy >= h) {
            s += (BmpSsdAcc)255 * 255 * bpp * ((w + step - 1) / step);
            continue;
        }
        const char* rowC = c->pData + y * c->stride;
        const char* rowP = p->pData + sy * p->stride;
        for (int x = 0; x < w; x += step) {
            int sx = x - dx;
            if (sx < 0 || sx >= w) {
                s += (BmpSsdAcc)255 * 255 * bpp;
                continue;
            }
            int oc = x * bpp;
            int os = sx * bpp;
            for (int k = 0; k < bpp; k++) {
                int d = (unsigned char)rowC[oc + k] - (unsigned char)rowP[os + k];
                s += (BmpSsdAcc)d * d;
            }
        }
    }
    return s;
}

bool BmpStream::TryGlobalTranslationDiff(const ImageData* current, const ImageData* previous,
    ImageData* outDiff, int* outDx, int* outDy) {
    if (!current || !previous || !outDiff || !outDx || !outDy)
        return false;
    if (!current->pData || !previous->pData)
        return false;
    if (current->width != previous->width || current->height != previous->height ||
        current->stride != previous->stride || current->bitsPerPixel != previous->bitsPerPixel)
        return false;

    int w = current->width;
    int h = current->height;
    int bpp = current->bitsPerPixel / 8;

    ImageData naive;
    if (!ComputeDiff(current, previous, &naive, 0, 0, w, h))
        return false;
    if (naive.isEmptyDiff)
        return false;

    int nzNaive = CountNonzeroDiffPixels(&naive);

    int scale = 1;
    int maxDim = w > h ? w : h;
    while (maxDim / scale > 112 && scale < 48)
        scale++;

    int wS, hS;
    unsigned char* gCur = DownsampleGray(current, scale, &wS, &hS);
    unsigned char* gPrev = DownsampleGray(previous, scale, &wS, &hS);
    if (!gCur || !gPrev) {
        free(gCur);
        free(gPrev);
        return false;
    }

    int rangeS = wS / 4;
    if (rangeS > 32)
        rangeS = 32;
    if (rangeS < 2)
        rangeS = MinInt(wS / 2, hS / 2);
    if (rangeS < 1)
        rangeS = 1;

    BmpSsdAcc bestSsd = BMP_SSD_HUGE;
    int bestDxS = 0, bestDyS = 0;

    for (int dys = -rangeS; dys <= rangeS; dys += 2) {
        for (int dxs = -rangeS; dxs <= rangeS; dxs += 2) {
            BmpSsdAcc s = SsdGrayOffset(gCur, wS, hS, gPrev, wS, hS, dxs, dys);
            if (s < bestSsd) {
                bestSsd = s;
                bestDxS = dxs;
                bestDyS = dys;
            }
        }
    }

    for (int dys = bestDyS - 4; dys <= bestDyS + 4; dys++) {
        for (int dxs = bestDxS - 4; dxs <= bestDxS + 4; dxs++) {
            if (dxs < -rangeS || dxs > rangeS || dys < -rangeS || dys > rangeS)
                continue;
            BmpSsdAcc s2 = SsdGrayOffset(gCur, wS, hS, gPrev, wS, hS, dxs, dys);
            if (s2 < bestSsd) {
                bestSsd = s2;
                bestDxS = dxs;
                bestDyS = dys;
            }
        }
    }

    free(gCur);
    free(gPrev);

    int bestDx = bestDxS * scale;
    int bestDy = bestDyS * scale;

    BmpSsdAcc bestSparse = BMP_SSD_HUGE;
    int pickDx = bestDx, pickDy = bestDy;
    for (int ady = -1; ady <= 1; ady++) {
        for (int adx = -1; adx <= 1; adx++) {
            int tdx = bestDx + adx;
            int tdy = bestDy + ady;
            if (tdx < -w || tdx > w || tdy < -h || tdy > h)
                continue;
            BmpSsdAcc s = SparseSsdBgr(current, previous, tdx, tdy, 8);
            if (s < bestSparse) {
                bestSparse = s;
                pickDx = tdx;
                pickDy = tdy;
            }
        }
    }
    bestDx = pickDx;
    bestDy = pickDy;

    if (outDiff->pData) {
        delete[] outDiff->pData;
        outDiff->pData = NULL;
    }
    outDiff->width = w;
    outDiff->height = h;
    outDiff->bitsPerPixel = current->bitsPerPixel;
    outDiff->stride = current->stride;
    outDiff->dataSize = current->dataSize;
    outDiff->isFullFrame = false;
    outDiff->pData = new char[outDiff->dataSize];
    if (!outDiff->pData)
        return false;
    ZeroMemory(outDiff->pData, outDiff->dataSize);
    bool any = false;
    for (int y = 0; y < h; y++) {
        char* dRow = outDiff->pData + y * outDiff->stride;
        const char* cRow = current->pData + y * current->stride;
        for (int x = 0; x < w; x++) {
            int sx = x - bestDx;
            int sy = y - bestDy;
            int off = x * bpp;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                const char* pPix = previous->pData + sy * previous->stride + sx * bpp;
                for (int k = 0; k < bpp; k++) {
                    char v = (char)((unsigned char)cRow[off + k] ^ (unsigned char)pPix[k]);
                    dRow[off + k] = v;
                    if (v != 0)
                        any = true;
                }
            } else {
                for (int k = 0; k < bpp; k++) {
                    char v = cRow[off + k];
                    dRow[off + k] = v;
                    if (v != 0)
                        any = true;
                }
            }
        }
    }
    outDiff->isEmptyDiff = !any;

    int nzT = CountNonzeroDiffPixels(outDiff);
    if (nzT >= nzNaive) {
        delete[] outDiff->pData;
        outDiff->pData = NULL;
        return false;
    }
    if (nzT * 100 > nzNaive * 92) {
        delete[] outDiff->pData;
        outDiff->pData = NULL;
        return false;
    }

    *outDx = bestDx;
    *outDy = bestDy;
    return true;
}

bool BmpStream::ApplyTranslationResidual(ImageData* img, int dx, int dy,
    const char* xorPlane, int xorStride, int xorW, int xorH) {
    if (!img || !img->pData || img->width <= 0 || img->height <= 0)
        return false;
    int bpp = img->bitsPerPixel / 8;
    int w = img->width;
    int h = img->height;
    (void)xorW;
    (void)xorH;
    if (xorPlane) {
        if (xorStride < w * bpp || xorW != w || xorH != h)
            return false;
    }
    long sz = (long)img->stride * h;
    char* tmp = (char*)malloc((size_t)sz);
    if (!tmp)
        return false;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sx = x - dx;
            int sy = y - dy;
            char* dst = tmp + y * img->stride + x * bpp;
            const char* xorPtr = NULL;
            if (xorPlane)
                xorPtr = xorPlane + y * xorStride + x * bpp;
            if (sx >= 0 && sx < w && sy >= 0 && sy < h) {
                const char* src = img->pData + sy * img->stride + sx * bpp;
                for (int k = 0; k < bpp; k++) {
                    unsigned char xv = xorPtr ? (unsigned char)xorPtr[k] : 0;
                    dst[k] = (char)((unsigned char)src[k] ^ xv);
                }
            } else {
                for (int k = 0; k < bpp; k++) {
                    unsigned char xv = xorPtr ? (unsigned char)xorPtr[k] : 0;
                    dst[k] = (char)(xv);
                }
            }
        }
    }
    CopyMemory(img->pData, tmp, (size_t)sz);
    free(tmp);
    return true;
}