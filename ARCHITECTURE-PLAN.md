# Plan poprawy architektury RM_RD

Plan jest realizowany przez serie pytan. Kazde pytanie wymaga Twojej decyzji, zanim przejdziemy do nastepnego kroku.

---

## Faza 1: Zakres i priorytety

### Pytanie 1.1 – Cel poprawy
**Co jest glownym celem poprawy architektury?**

Priorytety (od najwazniejszego):
1. [x] **Wydajnosc** – mniejszy koszt CPU/GDI po stronie klienta i serwera
2. [x] **Czytelnosc i utrzymanie** – lepszy podzial odpowiedzialnosci, mniej couplingu
3. [x] **Stabilnosc wieloklientowa** – serwer obsluguje wielu klientow bez niespojnosci ramek
4. [x] **Niezawodnosc sieci** – pelne odczyty/zapisy TCP, obsluga rozlaczen

---

### Pytanie 1.2 – Ograniczenia
**Czy trzymamy sie scisle ograniczen projektu?**

- [x] **Tak** – bez nowych zaleznosci, API zgodne z Windows 98, bez zmian protokolu binarnego

---

## Faza 2: Serwer – wieloklientowosc

### Pytanie 2.1 – Model stanu ramek
**Jak ma wygladac stan ramek przy wielu klientach?**

- [x] **Jeden BmpStream globalny + kolejka pelnych klatek** – przechwycenie raz, rozeslanie tej samej klatki do wszystkich

---

### Pytanie 2.2 – Zycie serwera
**Czy serwer ma obslugiwac graceful shutdown?**

- [x] **Serwer jako aplikacja w tray** – minimalizowana do tray, wyjscie przez menu kontekstowe (np. „Zamknij”). Petla glowna to message loop WinAPI; zatrzymanie nasluchu i czekanie na klientow przy wyborze zamkniecia.

---

## Faza 3: Klient – renderowanie

### Pytanie 3.1 – Reuse bitmapy
**Czy klient ma reuzywac bufora/bitmapy zamiast tworzyc nowa HBITMAP dla kazdej klatki?**

- [x] **Tak** – update w miejscu (np. SetDIBits) lub double-buffer z dwoma bitmapami

---

## Faza 4: Siec i protokol

### Pytanie 4.1 – Pelne send/recv
**Czy wprowadzamy helpery do pelnych odczytow i zapisow TCP?**

- [x] **Tak** – funkcje typu `SendAll` / `RecvAll` uzywane wszędzie tam, gdzie oczekujemy pelnego transferu

---

## Faza 5: Struktura kodu

### Pytanie 5.1 – Abstrakcje
**Czy wprowadzamy abstrakcje pod testowanie / podmiane implementacji?**

- [ ] **Nie** – zachowujemy bezposrednie wywolania WinAPI/WinSock
- [x] **Minimalnie** – np. interfejs dla „wysylania ramki” i „odbioru ramki”, zeby w testach podstawic fake
- [ ] **Bardziej** – wiecej interfejsow (transport, capture, compression) – ryzyko over-engineeringu

---

### Pytanie 5.2 – Rozdzielenie odpowiedzialnosci
**Czy rozdzielamy RemoteServer na mniejsze klasy?**

- [ ] **Nie** – zostawiamy jedna duza klase
- [ ] **Tak, lekko** – np. wyciagniecie „CommandProcessor” lub „FrameSender” do osobnych plikow
- [x] **Tak, bardziej** – pelniejszy podzial (ConnectionHandler, FramePipeline itd.)

---

## Faza 6: Testy

### Pytanie 6.1 – Zakres testow
**Co rozszerzamy w testach?**

- [x] **Shared** – NOCHANGE, wiele diffow, bledne dane
- [x] **Protokol** – serializacja/deserializacja FrameCmd, HeaderBmp
- [ ] **Integracyjne** – mock serwera, klient odbiera ramki (jesli beda abstrakcje)
- [ ] **Nic** – zostawiamy obecny zakres

---

## Kolejnosc realizacji

Wszystkie pytania zebrane. Proponowana kolejność (wg priorytetów i zależności):

| # | Krok | Priorytet | Uwagi |
|---|------|-----------|-------|
| 1 | **SendAll / RecvAll** – helpery TCP w Shared | niezawodność | Mały, niezależny. Fundacja pod resztę. |
| 2 | **Reuse bitmapy** – klient (SetDIBits / double-buffer) | wydajność | Niezależny. Szybki zysk. |
| 3 | **BmpStream globalny + broadcast** – jeden przechwyt, ta sama klatka do wszystkich | stabilność | Wymaga zmiany FrameProvider i przepływu na serwerze. |
| 4 | **Serwer w tray** – okno ukryte, ikona w tray, menu kontekstowe „Zamknij” | UX | Message loop, Shell_NotifyIcon, TrackPopupMenu. |
| 5 | **Rozdzielenie RemoteServer** – ConnectionHandler, FramePipeline itd. | czytelność | Po ustabilizowaniu modelu ramek. |
| 6 | **Abstrakcje minimalne** – interfejsy transportu pod testy | testowalność | Jeśli potrzebne do testów integracyjnych. |
| 7 | **Rozszerzenie testów** – Shared (NOCHANGE, diffy, błędne dane), Protokół (serializacja) | weryfikacja | Na końcu, gdy kontrakt jest ustalony. |

Realizujemy krok po kroku, z potwierdzeniem po każdym większym kroku.
