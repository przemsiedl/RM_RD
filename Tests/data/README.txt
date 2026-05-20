Opcjonalne wejscia binarne dla tests.exe (katalog roboczy = korzen repo przy test_mingw.bat).

W repo:
- ramp4k.bin — 4096 bajtow (wartosci i mod 251), test round-trip kompresji.
- gradient_32x32.raw — obraz 32x32, 24 bpp, stride DIB; test pelnej sciezki wire (jak serwer -> klient).

Pliki (dowolna nazwa, ponizej przyklady):
- *.bin — surowe bajty do round-trip kompresji (caly plik = jeden bufor).
- *_w*h*bpp.raw — obraz: width x height, bpp bitow na piksel (8 lub 24), wiersze dopelnione do wielokrotnosci 4 bajtow (stride DIB), little-endian BGR dla 24 bpp.

Jesli plik nie istnieje, testy i tak przechodza na generatorach wbudowanych w tests.cpp.
