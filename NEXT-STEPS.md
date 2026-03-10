## NEXT STEPS

Plan architektury: `ARCHITECTURE-PLAN.md`

1. [done] **SendAll / RecvAll** – helpery TCP w Shared, uzyte wszedzie tam gdzie pelny transfer
2. [done] **Reuse bitmapy** – klient (SetDIBits)
3. [done] **Ciągły przechwyt + diff per-client** – jeden wątek przechwytu, lastSentFrame per klient, diff na żądanie
4. [active] Serwer w tray – okno ukryte, ikona w tray, menu kontekstowe „Zamknij"
5. Rozdzielenie RemoteServer – ConnectionHandler, FramePipeline itd.
6. Abstrakcje minimalne – interfejsy transportu pod testy
7. Rozszerzenie testow – Shared (NOCHANGE, diffy, bledne dane), Protokol (serializacja)
