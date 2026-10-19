#ifndef ConfigH
#define ConfigH

// Wczytuje port z config.ini (sekcja [Server], klucz Port).
// Jesli plik nie istnieje lub wartosc jest nieprawidlowa, zwraca 8080.
int LoadPortFromConfig();

// Bpp z config.ini (klucz Bpp): 16, 24 albo 32. Domyslnie 24.
int LoadBitsPerPixelFromConfig();

#endif
