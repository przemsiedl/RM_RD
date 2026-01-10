#include "RemoteServer.h"
#include <stdio.h>
#include <conio.h>

int main() {
    printf("=== Remote Screen Server ===\n\n");

    RemoteServer server(8080);

    // Uruchom serwer
    if (! server.Start()) {
        printf("BLAD: Nie mozna uruchomic serwera!\n");
        printf("Sprawdz czy port 8080 jest wolny.\n");
        return 1;
    }
    
    printf("Serwer dziala na porcie %d\n", server.GetPort());
    printf("Czekam na polaczenia...\n");
    printf("\nNacisnij ENTER (w tym oknie) aby zatrzymac serwer.\n\n");

    // G³ówna pêtla - wyœwietlaj statystyki
    while (true) {

        static int lastCount = -1;
        int currentCount = server.GetClientCount();

        if (currentCount != lastCount) {
            printf("Polaczonych klientow: %d\n", currentCount);
            lastCount = currentCount;
        }

        Sleep(100);
    }
    
    printf("\nZatrzymywanie serwera.. .\n");
    server.Stop();
    printf("Serwer zatrzymany.\n");
    
    return 0;
}
