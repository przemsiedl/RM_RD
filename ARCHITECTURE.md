## Cel architektury

Architektura projektu ma pozostac:

- prosta do zrozumienia i debugowania,
- szybka w krytycznej sciezce przechwycenie -> diff -> kompresja -> wysylka -> rekonstrukcja,
- zgodna z mozliwie starym API Windows,
- lokalna, bez nadmiarowych abstrakcji i bez nowych zaleznosci.

## Aktualny podzial odpowiedzialnosci

### `Client`

Warstwa prezentacji i transportu po stronie odbiorcy:

- tworzy okno WinAPI,
- pobiera klatki z serwera,
- rekonstruuje obraz lokalnie,
- mapuje lokalne zdarzenia wejscia na komendy protokolu.

Kluczowe pliki:

- `Client/Client.cpp` - glowne okno, timer odswiezania, rysowanie,
- `Client/remoteclient.cpp` - komunikacja z serwerem i skladanie obrazu,
- `Client/InputSender.cpp` - wysylka komend wejscia,
- `Client/ClientInputHandler.cpp` - mapowanie wejscia GUI na wspolrzedne zdalne.

### `Server`

Warstwa hosta i wykonania po stronie nadawcy:

- nasluchuje klientow TCP,
- dla zadania `GET_FRAME` generuje kolejna klatke,
- wykonuje zdalne komendy myszy i klawiatury,
- utrzymuje prosty model wielowatkowy: jeden watek nasluchu i jeden watek na klienta.

Kluczowe pliki:

- `Server/server.cpp` - start procesu i petla zycia serwera,
- `Server/RemoteServer.cpp` - sockety, watki i obsluga protokolu,
- `Server/FrameProvider.cpp` - przechwycenie i serializacja klatki,
- `Server/InputExecutor.cpp` - wykonanie wejscia na lokalnej maszynie.

### `Shared`

Wspolna warstwa kontraktu i algorytmow:

- `Shared/Frame.h` - kontrakt binarny i flagi ramek,
- `Shared/BmpStream.cpp` - przechwytywanie ekranu i diff XOR,
- `Shared/Compression.cpp` - lekka wlasna kompresja dla surowych buforow.

To jest najwazniejsza warstwa dla wydajnosci i kompatybilnosci.

### `Tests`

Minimalna warstwa weryfikacyjna:

- sprawdza round-trip kompresji,
- sprawdza skladanie obrazu przez XOR diff.

## Przeplyw danych

1. `Client` wysyla `CMD_GET_FRAME`.
2. `Server` wywoluje `FrameProvider`.
3. `FrameProvider` korzysta z `BmpStream`, aby pobrac aktualny ekran.
4. `BmpStream` tworzy pelna klatke albo roznice XOR do poprzedniej klatki.
5. `FrameProvider` kompresuje payload i uzupelnia `HeaderBmp`.
6. `RemoteServer` wysyla naglowek i payload przez TCP.
7. `RemoteClient` odbiera naglowek, odtwarza bufor obrazu i tworzy `HBITMAP`.
8. `Client` wyswietla gotowa bitmape w oknie.

## Decyzje zgodne z priorytetami projektu

### Prostota

- Wlasny binarny protokol jest maly i czytelny.
- Brak warstw frameworkowych i brak zaleznosci zewnetrznych.
- Kod jest zorganizowany wedlug roli procesu: klient, serwer, wspoldzielone.

### Wydajnosc

- `24 bpp` jest domyslne; `16 bpp` (config `Bpp`) obcina bufor i lacze o okolo jedna trzecia.
- XOR diff zmniejsza payload przy niewielkich zmianach ekranu. Przy malym ruchu serwer nie buduje pelnej plaszczyzny XOR — porownuje komorki siatki i kompresuje tylko brudne.
- Flaga `FRAME_FLAG_NOCHANGE` po `memcmp` pomija XOR, kompresje i przerysowanie.
- Globalne przesuniecie (`TryGlobalTranslationDiff`) liczy blad tylko na wspolnym obszarze i startuje, gdy zmienione jest co najmniej ok. 12% ekranu, a mniej niz ok. 70%. Kursor i pelna zmiana sceny go omijaja.
- Kompresja LZ uzywa stalej tablicy hasza. Gdy wynik nie jest mniejszy od surowych danych, na kabel ida bajty bez flagi `COMPRESSED` (klient obsluguje oba warianty).
- Klient rysuje z sekcji DIB i uniewaznia tylko brudny prostokat. Odswiezanie i wejscie sa rozdzielone: watek sieci bierze klatki, UI tylko kolejkuje mysz i klawiature.
- Zrzut ekranu spi, gdy od ostatniego `GET_FRAME` minelo duzo czasu. Klient sam wydluza odstep, gdy runda trwa dluzej albo ekran stoi.

### Kompatybilnosc ze starszym Windows

- WinAPI i WinSock 1.1 zamiast nowszych bibliotek.
- Unikanie nowszych funkcji CRT typu `_s`.
- Proste prymitywy systemowe: `CreateThread`, `CRITICAL_SECTION`, `mouse_event`, `keybd_event`, `GetPrivateProfileIntA`.

## Ograniczenia i ryzyka

### high

- `Server/RemoteServer.cpp`: jeden zrzut jest wspolny, a diff jest liczony per klient (`pLastSentFrame`). Przy wielu klientach kazdy i tak kopiuje pelna klatke do swojego bufora roboczego. To jest spojne, ale przy kilku odbiorcach koszt pamieci rosnie liniowo (dwa pelne bufory na klienta).

- Sekcja krytyczna zrzutu obejmuje tylko publikacje i kopie migawki. Diff i kompresja ida poza nia, wiec wolny klient nie blokuje `BitBlt`. Tablica hasza kompresji ma wlasny zamek.

### medium

- `Server/server.cpp`: glowna petla nigdy nie obsluguje faktycznie ESC, mimo komunikatu w konsoli. Zachowanie procesu nie odpowiada interfejsowi tekstowemu.

- `Server/RemoteServer.cpp` i `Client/remoteclient.cpp`: czesc operacji `send` i `recv` dla naglowkow zaklada pojedynczy pelny transfer. Dla TCP to nie jest gwarantowane.

- `Shared/Compression.cpp`: `reinterpret_cast<const unsigned int*>` w `getHash` zaklada wygodny odczyt 4 bajtow. Na docelowej platformie Windows/x86 zwykle dziala, ale to delikatny punkt implementacyjny.

### low

- Brak centralnego dokumentu build/run byl przeszkoda operacyjna i zostal uzupelniony.

- Projekt ma niewiele testow regresyjnych dla protokolu i wielokrotnych ramek.

## Kierunek dalszego rozwoju

Priorytetem powinny byc tylko takie zmiany, ktore:

- nie psuja kompatybilnosci starego API Windows,
- nie komplikuja przeplywu danych,
- poprawiaja stabilnosc lub koszt goracej sciezki,
- daja sie zweryfikowac malymi testami.

Nastepne sensowne kroki sa w `NEXT-STEPS.md`.
