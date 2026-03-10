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
- Redukcja transferu: diff XOR + flaga `NOCHANGE` + kompresja.

## Uruchamianie

Serwer:

- plik konfiguracyjny: `Server/config.ini`,
- uruchom aplikacje serwera; nasluchuje na porcie z konfiguracji albo `8080`.

Klient:

- uruchom z argumentami: `Client.exe <host> [port]`,
- przyklad: `Client.exe 192.168.1.10 8080`,
- pomocniczy skrypt: `Client/client.bat`.

## Uwagi o buildzie

W aktualnym snapshotcie repo widoczne sa pliki zrodlowe i skrypty `.bat`. Dedykowane pliki projektu IDE nie sa obecne w workspace, wiec instrukcje kompilacji trzeba doprecyzowac po odtworzeniu lub wskazaniu uzywanego toolchaina.

## Najwazniejsze cechy projektu

- Malo warstw i malo posrednikow: logika jest bezposrednia i szybka do sledzenia.
- Dane obrazu sa przetwarzane na surowych buforach, co sprzyja wydajnosci.
- Projekt trzyma sie starszego API Windows i unika nowoczesnych zaleznosci.

## Najwazniejsze ograniczenia

- Brak uwierzytelnienia i szyfrowania transportu.
- Brak rozbudowanej diagnostyki i logowania.
- Brak automatycznego runnera testow.
- Duza czesc kodu opiera sie na recznym zarzadzaniu pamiecia i uchwytami.
