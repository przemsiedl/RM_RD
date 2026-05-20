## RM_RD

Prosty projekt zdalnego pulpitu dla Windows, zbudowany w C++ na WinAPI i WinSock.

Priorytety projektu:

- prostota implementacji i latwosc utrzymania,
- wydajnosc przesylu obrazu przy malym narzucie,
- kompatybilnosc ze starszymi systemami Windows, w tym z ograniczeniami zblizonymi do Windows 98,
- brak zbednych zaleznosci zewnetrznych.

## Co jest w repo

- `Client/` - aplikacja GUI pobierajaca klatki ekranu i przekazujaca wejscie myszy/klawiatury,
- `Server/` - aplikacja konsolowa nasluchujaca polaczen i wysylajaca obraz ekranu,
- `Shared/` - wspolny protokol binarny, przechwytywanie obrazu, diff XOR i kompresja,
- `Tests/` - prosty harness testowy dla kompresji i rekonstrukcji obrazu,
- `clean.bat` - czyszczenie artefaktow kompilacji.

## Jak to dziala

1. Klient laczy sie z serwerem TCP.
2. Klient wysyla `CMD_GET_FRAME`.
3. Serwer przechwytuje ekran przez `BmpStream`.
4. Serwer wylicza pelna klatke albo roznice XOR do poprzedniej klatki.
5. Dane sa opcjonalnie kompresowane i wysylane z naglowkiem `HeaderBmp`.
6. Klient odbiera dane, dekompresuje je, sklada obraz i wyswietla bitmape.
7. Zdarzenia myszy i klawiatury sa wysylane do serwera jako osobne komendy.

## Zalozenia techniczne

- Transport: TCP.
- Domyslny port serwera: `8080`.
- Protokol: wlasny prosty format binarny z naglowkami `FrameCmd` i `HeaderBmp`.
- Format obrazu: glownie `24 bpp`, z obsluga buforow DIB.
- Redukcja transferu: diff XOR + flaga `NOCHANGE` + kompresja; przy niewielkim ruchu — **bloczki** (`FRAME_FLAG_BLOCKS`, siatka 32x32), przy duzej zmianie obrazu — jeden skompresowany caly XOR (patrz `Shared/Frame.h`).

## Uruchamianie

Serwer:

- plik konfiguracyjny: `Server/config.ini`,
- uruchom aplikacje serwera; nasluchuje na porcie z konfiguracji albo `8080`.

Klient:

- uruchom z argumentami: `Client.exe <host> [port]`,
- przyklad: `Client.exe 192.168.1.10 8080`,
- pomocniczy skrypt: `Client/client.bat`.

## Uwagi o buildzie

Pliki projektu IDE: `Client/Client.ide`, `Server/Server.ide`, `Tests/Tests.ide`; czyszczenie: `clean.bat`. Przy budowaniu Client i Server dolacz zrodla z `Shared/`, w tym `Shared/SocketIo.cpp`.

**MinGW (gcc):** domyslnie `C:\MinGW` — `build_mingw.bat` (pelny build lub argumenty `CLIENT`, `SERVER`, `TESTS`), testy: `test_mingw.bat`. Inna instalacja: `set MINGW_ROOT=...` przed uruchomieniem skryptu. Skrypt linkuje statycznie `libgcc`/`libstdc++`, zeby `.exe` dzialaly na komputerze bez instalacji MinGW. Szczegoly: `.cursor/steering/MAP_PROJECT.md`.

**Siatka diff (klient):** `RemoteClient::SetDiffGrid(blockW, blockH)` (8..128); przed `GET_FRAME` wysylane jest `CMD_SET_DIFF_GRID` (`DataDiffGrid` w `Frame.h`). Domyslnie 32x32.

## Dla agentow AI (Cursor)

Na poczatku sesji kodowej: `README.md`, `.cursor/steering/DECISIONS.md`, `.cursor/steering/INTERFACES.md`; jesli istnieje – `.cursor/steering/NEXT-STEPS.md`. Kontrakty binarne i naglowki C++: rejestr w `INTERFACES.md`. Pelniejsza roadmapa: `NEXT-STEPS.md` w korzeniu repo.

## Najwazniejsze cechy projektu

- Malo warstw i malo posrednikow: logika jest bezposrednia i szybka do sledzenia.
- Dane obrazu sa przetwarzane na surowych buforach, co sprzyja wydajnosci.
- Projekt trzyma sie starszego API Windows i unika nowoczesnych zaleznosci.

## Najwazniejsze ograniczenia

- Brak uwierzytelnienia i szyfrowania transportu.
- Brak rozbudowanej diagnostyki i logowania.
- Brak automatycznego runnera testow.
- Duza czesc kodu opiera sie na recznym zarzadzaniu pamiecia i uchwytami.
