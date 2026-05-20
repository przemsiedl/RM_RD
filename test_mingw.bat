@echo off
setlocal EnableExtensions
set "RM_ROOT=%~dp0"
set "RM_ROOT=%RM_ROOT:~0,-1%"
cd /d "%RM_ROOT%"

if not exist "%RM_ROOT%\Tests\tests.exe" (
    echo [test_mingw] Brak Tests\tests.exe — uruchamiam build_mingw.bat TESTS...
    call "%RM_ROOT%\build_mingw.bat" TESTS
    if errorlevel 1 exit /b 1
)

echo [test_mingw] Uruchamianie Tests\tests.exe...
"%RM_ROOT%\Tests\tests.exe"
set "EC=%ERRORLEVEL%"
if not "%EC%"=="0" (
    echo [test_mingw] BLAD: kod wyjscia %EC%
    exit /b %EC%
)
echo [test_mingw] OK
exit /b 0
