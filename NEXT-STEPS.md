## NEXT STEPS

Plan architektury: `ARCHITECTURE-PLAN.md`

1. [done] **SendAll / RecvAll** – helpery TCP w Shared, uzyte wszedzie tam gdzie pelny transfer
2. [done] **Reuse bitmapy** – klient (CreateDIBSection + memcpy)
3. [done] **BmpStream globalny + broadcast** – jeden przechwyt, ta sama klatka do wszystkich
4. [active] Serwer w tray – okno ukryte, ikona w tray, menu kontekstowe „Zamknij"
5. Rozdzielenie RemoteServer – ConnectionHandler, FramePipeline itd.
6. Abstrakcje minimalne – interfejsy transportu pod testy
7. Rozszerzenie testow – Shared (NOCHANGE, diffy, bledne dane), Protokol (serializacja)
