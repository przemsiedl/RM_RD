#include "BmpStream. h"

// Konstruktor
BmpStream::BmpStream(int bpp) : 
    pPrevious(NULL), 
    pCurrent(NULL), 
    pDiff(NULL),
    bitsPerPixel(bpp)
{
    // Upewnij się że BPP jest poprawne (24 lub 32)
    if (bitsPerPixel != 24 && bitsPerPixel != 32)
        bitsPerPixel = 24;
}

// Destruktor
BmpStream::~BmpStream()
{
    if (pPrevious) delete pPrevious;
    if (pCurrent) delete pCurrent;
    if (pDiff) delete pDiff;
}

// Przechwytuje nowy obraz ekranu
void BmpStream:: Capture()
{
    // Przesuń current -> previous
    if (pPrevious) delete pPrevious;
    pPrevious = pCurrent;
    pCurrent = NULL;
    
    // Pobierz rozmiar ekranu
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    // Utwórz DC
    HDC hScreenDC = GetDC(NULL);
    if (!hScreenDC) return;
    
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    if (!hMemDC)
    {
        ReleaseDC(NULL, hScreenDC);
        return;
    }
    
    // Przygotuj BITMAPINFO dla DIB Section
    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(BITMAPINFO));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = screenWidth;
    bmi.bmiHeader.biHeight = -screenHeight;  // ujemna = top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = bitsPerPixel;
    bmi.bmiHeader. biCompression = BI_RGB;
    
    // Utwórz DIB Section (bezpośredni dostęp do pamięci)
    BYTE* pDibData = NULL;
    HBITMAP hDib = CreateDIBSection(hMemDC, &bmi, DIB_RGB_COLORS, 
                                     (void**)&pDibData, NULL, 0);
    if (!hDib || !pDibData)
    {
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        return;
    }
    
    // Wybierz DIB do Memory DC
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hMemDC, hDib);
    
    // BitBlt - kopiuj z ekranu do DIB
    BitBlt(hMemDC, 0, 0, screenWidth, screenHeight, 
           hScreenDC, 0, 0, SRCCOPY);
    
    // Oblicz rozmiar danych
    int stride = ((screenWidth * bitsPerPixel + 31) / 32) * 4;  // wyrównanie do 4 bajtów
    DWORD dataSize = stride * screenHeight;
    
    // Utwórz nową strukturę ImageData
    pCurrent = new ImageData();
    pCurrent->width = screenWidth;
    pCurrent->height = screenHeight;
    pCurrent->bitsPerPixel = bitsPerPixel;
    pCurrent->stride = stride;
    pCurrent->dataSize = dataSize;
    pCurrent->pData = new BYTE[dataSize];
    
    // Skopiuj dane z DIB Section
    CopyMemory(pCurrent->pData, pDibData, dataSize);
    
    // Zwolnij zasoby GDI
    SelectObject(hMemDC, hOldBmp);
    DeleteObject(hDib);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
}

// Oblicza różnicę między obrazami
void BmpStream::CalcDiff()
{
    // Zwolnij starą różnicę
    if (pDiff) 
    {
        delete pDiff;
        pDiff = NULL;
    }
    
    // Jeśli nie ma aktualnego obrazu - wyjdź
    if (!pCurrent) return;
    
    // Utwórz nową strukturę różnicy
    pDiff = new ImageData();
    pDiff->width = pCurrent->width;
    pDiff->height = pCurrent->height;
    pDiff->bitsPerPixel = pCurrent->bitsPerPixel;
    pDiff->stride = pCurrent->stride;
    pDiff->dataSize = pCurrent->dataSize;
    pDiff->pData = new BYTE[pCurrent->dataSize];
    
    // Jeśli nie ma poprzedniego obrazu - skopiuj pełny obraz
    if (!pPrevious || 
        pPrevious->width != pCurrent->width ||
        pPrevious->height != pCurrent->height ||
        pPrevious->bitsPerPixel != pCurrent->bitsPerPixel)
    {
        CopyMemory(pDiff->pData, pCurrent->pData, pCurrent->dataSize);
        return;
    }
    
    // Oblicz różnicę pixel po pixelu
    int bytesPerPixel = bitsPerPixel / 8;
    
    for (int y = 0; y < pCurrent->height; y++)
    {
        BYTE* pCurrRow = pCurrent->pData + (y * pCurrent->stride);
        BYTE* pPrevRow = pPrevious->pData + (y * pPrevious->stride);
        BYTE* pDiffRow = pDiff->pData + (y * pDiff->stride);
        
        for (int x = 0; x < pCurrent->width; x++)
        {
            int offset = x * bytesPerPixel;
            bool changed = false;
            
            // Porównaj wszystkie komponenty piksela (BGR lub BGRA)
            for (int i = 0; i < bytesPerPixel; i++)
            {
                if (pCurrRow[offset + i] != pPrevRow[offset + i])
                {
                    changed = true;
                    break;
                }
            }
            
            // Jeśli zmieniony - skopiuj nowy kolor, jeśli nie - ustaw czarny
            if (changed)
            {
                for (int i = 0; i < bytesPerPixel; i++)
                    pDiffRow[offset + i] = pCurrRow[offset + i];
            }
            else
            {
                for (int i = 0; i < bytesPerPixel; i++)
                    pDiffRow[offset + i] = 0;  // czarny = brak zmiany
            }
        }
    }
}