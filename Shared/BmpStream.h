#ifndef BmpStreamH
#define BmpStreamH

#include <windows.h>

// Obraz z parametrami i danymi pikseli (do transmisji przez sieć)
struct ImageData
{
    int width;
    int height;
    int bitsPerPixel;
    int stride;
    long dataSize;
    char* pData;
    bool isFullFrame;  // true = pełny obraz, false = różnica XOR
    bool isEmptyDiff;  // true = różnica XOR jest pusta
    
    // Konstruktor - inicjalizacja
    ImageData() : width(0), height(0), bitsPerPixel(0),
                  stride(0), dataSize(0), pData(NULL),
                  isFullFrame(true), isEmptyDiff(false) {}
    
    // Destruktor - automatyczne zwalnianie
    ~ImageData() 
    { 
        if (pData) 
        {
            delete[] pData;
            pData = NULL;
        }
    }
    
    // Zabroń kopiowania (unikamy podwójnego delete)
private:
    ImageData(const ImageData&);
    ImageData& operator=(const ImageData&);
};

class BmpStream
{
private: 
    // Bufory obrazów
    ImageData* pPrevious;
    ImageData* pCurrent;
    ImageData* pDiff;  // obliczona różnica
    
    // Preferowane BPP
    int bitsPerPixel;
    
    // Zabroń kopiowania
    BmpStream(const BmpStream&);
    BmpStream& operator=(const BmpStream&);

public:
    BmpStream(int bpp = 24);
    ~BmpStream();
    
    void Reset();
    void Capture();
    bool CalcDiff();
    
    // Zwraca obliczoną różnicę (NIE zwalniać - zarządzane przez klasę)
    // Wymaga wcześniejszego wywołania CalcDiff()
    const ImageData* GetDiff() const { return pDiff; }
    
    // ===== SKŁADANIE OBRAZÓW (REKONSTRUKCJA Z RÓŻNIC) =====
    
    // Aplikuje różnicę XOR na istniejący bufor
    // target - bufor docelowy (zostanie zmodyfikowany)
    // diff - różnica do aplikacji
    static bool ApplyDiffXOR(char* target, const ImageData* diff);
};

#endif