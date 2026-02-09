#include "RemoteServer.h"
#include "Config.h"
#include <stdio.h>
#include <conio.h>

int main() {
    printf("=== Remote Screen Server ===\n\n");

    int port = LoadPortFromConfig();
    RemoteServer server(port);

    if (!server.Start()) {
        printf("BLAD: Nie mozna uruchomic serwera!\n");
        printf("Sprawdz czy port %d jest wolny.\n", port);
        return 1;
    }
    
    printf("Serwer dziala na porcie %d\n", server.GetPort());
    printf("Czekam na polaczenia...\n");
    printf("\nNacisnij ESC aby zatrzymac serwer.\n\n");

    int lastCount = -1;
    
    while (true) {
        int currentCount = server.GetClientCount();
        
        if (currentCount != lastCount) {
            printf("Polaczonych klientow: %d\n", currentCount);
            lastCount = currentCount;
        }
        
        Sleep(100);
    }
    
    printf("\nZatrzymywanie serwera...\n");
    server.Stop();
    printf("Serwer zatrzymany.\n");
    
    return 0;
}