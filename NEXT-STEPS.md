## NEXT STEPS

Plan architektury: `ARCHITECTURE-PLAN.md`

1. [done] **SendAll / RecvAll** – helpery TCP w Shared
2. [done] **Reuse bitmapy** – sekcja DIB po stronie klienta, bez `SetDIBits` co klatke
3. [done] **Ciagly przechwyt + diff per klient** – jeden watek zrzutu, `lastSentFrame` per klient
4. [done] **Tania sciezka klatki** – reuse GDI i buforow, `NOCHANGE` przez `memcmp`, bloczki XOR bez pelnego bufora, bramka translacji, stala tablica hasza LZ, surowe bajty gdy LZ nie pomaga, `Bpp` 16/24/32, brudny prostokat na kliencie, watek sieci i kolejka wejscia, adaptacyjny odstep klatek, sen zrzutu bez klientow
5. [active] Serwer w tray – okno ukryte, ikona w tray, menu kontekstowe „Zamknij"
6. Rozdzielenie RemoteServer – ConnectionHandler, FramePipeline itd.
7. Abstrakcje minimalne – interfejsy transportu pod testy
8. Rozszerzenie testow – wiecej przypadkow protokolu (poza obecnym harnessem kompresji, bloczkow i 16 bpp)

### Co dalej przy wydajnosci (jesli bedzie potrzeba)

- Osobny socket albo szczelina w protokole na wejscie w trakcie dlugiego `recv`, zeby klik nie czekal na koniec duzej klatki. Teraz UI nie zamraza sie, ale serwer dostaje mysz dopiero miedzy klatkami.
- Translacja przy prawie pelnym przewinieciu (dzis próg ~70% idzie w jeden XOR, bo szukanie przesuniecia na slabym CPU jest drogie).
- Jesli `Bpp=16` ma byc per lacze, a nie per serwer, trzeba osobnego zrzutu albo konwersji — jeden `BmpStream` jest wspolny dla wszystkich klientow.
