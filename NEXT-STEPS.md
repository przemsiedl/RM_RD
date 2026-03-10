## NEXT STEPS

Plan architektury: `ARCHITECTURE-PLAN.md`

1. [active] **SendAll / RecvAll** – helpery TCP w Shared, uzyte wszedzie tam gdzie pelny transfer
2. Reuse bitmapy – klient (SetDIBits / double-buffer)
3. BmpStream globalny + broadcast – jeden przechwyt, ta sama klatka do wszystkich
4. Serwer w tray – okno ukryte, ikona w tray, menu kontekstowe „Zamknij"
5. Rozdzielenie RemoteServer – ConnectionHandler, FramePipeline itd.
6. Abstrakcje minimalne – interfejsy transportu pod testy
7. Rozszerzenie testow – Shared (NOCHANGE, diffy, bledne dane), Protokol (serializacja)
