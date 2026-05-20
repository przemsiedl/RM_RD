@echo off
setlocal EnableExtensions
REM Domyslna instalacja MinGW (nadpisz przed uruchomieniem: set MINGW_ROOT=D:\Inny\MinGW )
if not defined MINGW_ROOT set "MINGW_ROOT=C:\MinGW"
set "CXX=%MINGW_ROOT%\bin\g++.exe"
set "RM_ROOT=%~dp0"
set "RM_ROOT=%RM_ROOT:~0,-1%"

if not exist "%CXX%" (
    echo [build_mingw] BLAD: brak kompilatora: "%CXX%"
    echo Ustaw zmienna MINGW_ROOT na katalog z podkatalogiem bin\g++.exe
    exit /b 1
)

set "PATH=%MINGW_ROOT%\bin;%PATH%"
cd /d "%RM_ROOT%"

set "CFLAGS=-O2 -Wall"
REM Bez tego na innym PC brakuje libgcc / libstdc++ z folderu MinGW:
set "LDFLAGS_RT=-static-libgcc -static-libstdc++"
set "LIBS_WIN=-lws2_32 -lgdi32 -luser32"

if /I "%~1"=="CLIENT" goto :build_client
if /I "%~1"=="SERVER" goto :build_server
if /I "%~1"=="TESTS" goto :build_tests

:build_all
call :do_client || exit /b 1
call :do_server || exit /b 1
call :do_tests  || exit /b 1
echo [build_mingw] Gotowe: Client\Client.exe Server\Server.exe Tests\tests.exe
exit /b 0

:build_client
call :do_client || exit /b 1
echo [build_mingw] Gotowe: Client\Client.exe
exit /b 0

:build_server
call :do_server || exit /b 1
echo [build_mingw] Gotowe: Server\Server.exe
exit /b 0

:build_tests
call :do_tests || exit /b 1
echo [build_mingw] Gotowe: Tests\tests.exe
exit /b 0

:do_client
echo [build_mingw] Kompilacja Client...
"%CXX%" %CFLAGS% %LDFLAGS_RT% -mwindows -o Client\Client.exe ^
    Client\Client.cpp Client\ClientArgs.cpp Client\ClientInputHandler.cpp Client\InputSender.cpp Client\remoteclient.cpp ^
    Shared\SocketIo.cpp Shared\Compression.cpp Shared\BmpStream.cpp ^
    %LIBS_WIN%
exit /b %ERRORLEVEL%

:do_server
echo [build_mingw] Kompilacja Server...
"%CXX%" %CFLAGS% %LDFLAGS_RT% -o Server\Server.exe ^
    Server\server.cpp Server\RemoteServer.cpp Server\FrameProvider.cpp Server\InputExecutor.cpp Server\Config.cpp ^
    Shared\SocketIo.cpp Shared\Compression.cpp Shared\BmpStream.cpp ^
    %LIBS_WIN%
exit /b %ERRORLEVEL%

:do_tests
echo [build_mingw] Kompilacja Tests...
"%CXX%" %CFLAGS% %LDFLAGS_RT% -o Tests\tests.exe ^
    Tests\tests.cpp Shared\Compression.cpp Shared\BmpStream.cpp ^
    -lgdi32 -luser32
exit /b %ERRORLEVEL%
