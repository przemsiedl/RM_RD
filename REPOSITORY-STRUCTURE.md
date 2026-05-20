## Struktura repo

Repo jest zorganizowane procesowo i technicznie. To pasuje do malego projektu systemowego, w ktorym prostota i koszt wykonania sa wazniejsze niz rozbudowana architektura warstw biznesowych.

## Katalogi glowne

### `Client/`

Aplikacja GUI odbierajaca obraz i wysylajaca wejscie.

Najwazniejsze pliki:

- `Client/Client.cpp` - punkt startowy okna, odswiezanie i rysowanie,
- `Client/RemoteClient.h`
- `Client/remoteclient.cpp` - logika sieciowa klienta i skladanie obrazu,
- `Client/InputSender.h`
- `Client/InputSender.cpp` - wysylka komend myszy i klawiatury,
- `Client/ClientInputHandler.h`
- `Client/ClientInputHandler.cpp` - translacja zdarzen WinAPI na komendy protokolu,
- `Client/ClientArgs.h`
- `Client/ClientArgs.cpp` - parsowanie hosta i portu,
- `Client/client.bat` - prosty start klienta.

### `Server/`

Aplikacja konsolowa odpowiedzialna za przechwytywanie ekranu, siec i wykonanie wejscia.

Najwazniejsze pliki:

- `Server/server.cpp` - punkt startowy procesu,
- `Server/RemoteServer.h`
- `Server/RemoteServer.cpp` - sockety, akceptacja klientow, obsluga komend,
- `Server/FrameProvider.h`
- `Server/FrameProvider.cpp` - generowanie i pakowanie klatek,
- `Server/InputExecutor.h`
- `Server/InputExecutor.cpp` - wykonanie wejscia na lokalnym systemie,
- `Server/Config.h`
- `Server/Config.cpp` - odczyt portu z `config.ini`,
- `Server/config.ini` - konfiguracja nasluchu.

### `Shared/`

Kod wspoldzielony przez klienta i serwer.

Najwazniejsze pliki:

- `Shared/Frame.h` - kontrakt protokolu binarnego,
- `Shared/BmpStream.h`
- `Shared/BmpStream.cpp` - zrzut ekranu i roznice XOR,
- `Shared/compression.h` (nazwa pliku na dysku; include `Compression.h` dziala na Windows)
- `Shared/Compression.cpp` - kompresja i dekompresja,
- `Shared/SocketIo.h`
- `Shared/SocketIo.cpp` - SendAll/RecvAll dla pelnych transferow TCP.

### `Tests/`

Lekki projekt testowy dla algorytmow wspoldzielonych.

Najwazniejsze pliki:

- `Tests/tests.cpp` - round-trip kompresji i testy `ApplyDiffXOR`.

## Pliki repo w katalogu glownym

- `README.md` - szybkie wejscie do projektu,
- `ARCHITECTURE.md` - opis granic odpowiedzialnosci i ryzyk,
- `REPOSITORY-STRUCTURE.md` - mapa katalogow,
- `NEXT-STEPS.md` - roadmapa funkcji i wieksze kroki,
- `AGENTS.md` - wskazowki dla agenta AI,
- `clean.bat` - czyszczenie artefaktow kompilacji,
- `build_mingw.bat`, `test_mingw.bat` - kompilacja i testy pod MinGW (gcc), domyslnie `C:\MinGW`.

### Cursor / reguly sesji

- `.cursor/steering/DECISIONS.md` - decyzje architektoniczne dla tego repo (C++ vs szablon .NET w regolach),
- `.cursor/steering/INTERFACES.md` - rejestr kontraktow (protokol + naglowki `Shared/`),
- `.cursor/steering/NEXT-STEPS.md` - krotka kolejka pod workflow Cursor + odnosnik do `NEXT-STEPS.md`,
- `.cursor/steering/MAP_PROJECT.md` - build, sciezki, testy.

## Zasady poruszania sie po repo

- Zmiany protokolu zaczynaj od `Shared/Frame.h`.
- Zmiany wydajnosci przesylu obrazu zaczynaj od `Shared/BmpStream.cpp` i `Shared/Compression.cpp`.
- Zmiany renderowania lub inputu klienta zaczynaj od `Client/Client.cpp` i `Client/remoteclient.cpp`.
- Zmiany sieci lub wieloklientowosci zaczynaj od `Server/RemoteServer.cpp`.
- Zmiany uruchamiania i konfiguracji serwera zaczynaj od `Server/server.cpp` i `Server/Config.cpp`.

## Obecne braki strukturalne

- Brak jednego, zautomatyzowanego opisu kompilacji dla potwierdzonego toolchaina (pliki `.ide` sa w repo).
- Testy obejmuja tylko czesc warstwy `Shared`.
