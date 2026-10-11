@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "HOLDWAS=0"
cd /d "%~dp0"
title BroClock-PRO firmware installer
echo ===================================================
echo      FIRMWARE INSTALLER BroClock-PRO
echo ===================================================
echo.
echo  Step 1. Connect the device to the computer with a USB cable.
echo          Data cable required - charge-only will not work.
echo.
echo  Step 2. Choose what to install.
echo.

set "TRY=0"
:MENU
set "BIN="
set "CH="
echo   [1] Full flash  BroClock_Pro_factory.bin
echo       - fully rewrites the device
echo       - RESETS settings: Wi-Fi, city, themes, photos.
echo.
echo   [2] Firmware only  BroClock_Pro_firmware.bin
echo       - updates the software only
echo       - device settings are kept.
echo.
echo   [0] Exit
echo.
set /p CH="Your choice - type 1, 2 or 0: "
if "%CH%"=="0" goto END
if "%CH%"=="1" set "BIN=BroClock_Pro_factory.bin"
if "%CH%"=="2" set "BIN=BroClock_Pro_firmware.bin"
if defined BIN goto CH_OK
set /a TRY+=1
if %TRY% GTR 5 (
  echo.
  echo  Too many invalid attempts, exiting.
  goto END
)
echo.
echo  Unrecognized input, please try again.
echo.
goto MENU

:CH_OK
if exist "%~dp0%BIN%" goto CH_FILE_OK
echo.
echo  ERROR: file "%BIN%" was not found next to this bat file.
echo  Put both in one folder: flash_broclock.bat and "%BIN%".
echo.
set /a TRY+=1
if %TRY% GTR 5 goto END
goto MENU

:CH_FILE_OK
echo.
echo  Selected: %BIN%
echo.

set "TRY=0"
:SHOW_PORTS
echo  Step 3. Looking for the device - USB port...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=@(Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPClass -eq 'Ports' -and $_.Name -match '\((COM\d+)\)' } | ForEach-Object { $matches[1] + '|' + $_.Name }); $u=@($p | Where-Object { $_ -match '(?i)usb' }); if ($u.Count -gt 0) { $u } else { $p | Where-Object { $_ -notmatch 'Bluetooth' } }" > "%TEMP%\brock_ports.txt" 2>nul
set "N=0"
if exist "%TEMP%\brock_ports.txt" (
  for /f "usebackq tokens=1,* delims=|" %%a in ("%TEMP%\brock_ports.txt") do (
    set /a N+=1
    set "P_NUM!N!=%%a"
    set "P_NAME!N!=%%b"
  )
)
if "%N%"=="0" goto NO_PORTS
if "%N%"=="1" (
  set "PORT=!P_NUM1!"
  set "PNAME=!P_NAME1!"
  goto PORT_READY
)
echo.
echo  Several ports found:
set "I=0"
for /l %%i in (1,1,%N%) do (
  set /a I+=1
  echo    [!I!]  !P_NUM%%i!  !P_NAME%%i!
)
echo.
set "SEL="
set /p SEL="Device port number? Type the number: "
set "PORT="
set "PNAME="
if defined SEL (
  set "PORT=!P_NUM%SEL%!"
  set "PNAME=!P_NAME%SEL%!"
)
if defined PORT goto PORT_READY
set /a TRY+=1
if %TRY% GTR 5 (
  echo.
  echo  Too many invalid attempts, exiting.
  goto END
)
echo.
echo  Unrecognized choice, please try again.
goto SHOW_PORTS

:PORT_READY
echo  Device: %PORT%  (%PNAME%)
echo.

echo  Step 4. Checking esptool...
set "EPT="
where esptool >nul 2>nul
if not errorlevel 1 (
  esptool write-flash --help >nul 2>nul
  if not errorlevel 1 set "EPT=esptool"
)
set "TOOLS=%LOCALAPPDATA%\BroClock_flash"
if not defined EPT if exist "%TOOLS%\esptool-windows-amd64\esptool.exe" (
  "%TOOLS%\esptool-windows-amd64\esptool.exe" write-flash --help >nul 2>nul
  if not errorlevel 1 set "EPT=%TOOLS%\esptool-windows-amd64\esptool.exe"
)
if defined EPT (
  echo  esptool found.
  goto ESPTOOL_OK
)

echo  esptool not installed, downloading 66 MB - one time...
mkdir "%TOOLS%" >nul 2>nul
set "EURL=https://github.com/espressif/esptool/releases/download/v5.5.0/esptool-v5.5.0-windows-amd64.zip"
where curl >nul 2>nul
if not errorlevel 1 (
  curl -L --fail --silent --show-error -o "%TOOLS%\esptool.zip" "%EURL%"
)
if not exist "%TOOLS%\esptool.zip" (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest -Uri '%EURL%' -OutFile '%TOOLS%\esptool.zip' -UseBasicParsing"
)
if not exist "%TOOLS%\esptool.zip" goto NO_NET
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -Path '%TOOLS%\esptool.zip' -DestinationPath '%TOOLS%' -Force" >nul 2>nul
if not exist "%TOOLS%\esptool-windows-amd64\esptool.exe" goto NO_NET
"%TOOLS%\esptool-windows-amd64\esptool.exe" write-flash --help >nul 2>nul
if errorlevel 1 goto NO_NET
set "EPT=%TOOLS%\esptool-windows-amd64\esptool.exe"
echo  esptool downloaded and ready.
:ESPTOOL_OK
echo.

echo  Step 5. Confirmation.
echo.
echo  Will flash:  %BIN%
echo  Port:        %PORT%
if "%CH%"=="1" echo  WARNING: all device settings will be RESET.
echo.
set "GO="
set /p GO="Flash now? Enter = yes, n = no: "
if /i "%GO%"=="n" goto CANCEL
if "%GO%"=="0" goto CANCEL

powershell -NoProfile -ExecutionPolicy Bypass -Command "$k=0; Get-CimInstance Win32_Process | Where-Object { ($_.Name -eq 'pythonw.exe' -or $_.Name -eq 'python.exe') -and $_.CommandLine -like '*comhold*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue; $k=1 }; if ($k) { '1' } else { '0' }" > "%TEMP%\brock_hold.txt" 2>nul
if exist "%TEMP%\brock_hold.txt" set /p HOLDWAS=<"%TEMP%\brock_hold.txt"
if "%HOLDWAS%"=="1" echo  Port-holding service comhold stopped.
echo.
echo  Flashing, please wait...
echo.
"%EPT%" --port %PORT% --baud 460800 write-flash 0x0 "%~dp0%BIN%"
set "RC=%errorlevel%"
if "%RC%"=="0" goto FLASH_OK

echo.
echo  First flash attempt failed.
set "R1="
set /p R1="Retry at a lower speed? Enter = yes, n = no: "
if /i "%R1%"=="n" goto FLASH_FAIL
echo.
echo  Retrying at 115200...
echo.
"%EPT%" --port %PORT% --baud 115200 write-flash 0x0 "%~dp0%BIN%"
set "RC=%errorlevel%"
if "%RC%"=="0" goto FLASH_OK
goto FLASH_FAIL

:FLASH_OK
echo.
echo ===================================================
echo   INSTALL COMPLETED SUCCESSFULLY!
echo ===================================================
echo   Firmware written and verified.
echo   The device is rebooting now, about 15 seconds.
echo   You can unplug the cable.
echo.
goto TAIL

:FLASH_FAIL
echo.
echo ===================================================
echo   FLASH ERROR
echo ===================================================
echo   Possible causes:
echo    - wrong cable - charge-only, no data
echo    - the device disconnected during flashing
echo    - the port is busy with another program
echo   Try running this file again.
echo.
goto TAIL

:CANCEL
echo.
echo  Flashing cancelled.
echo.
goto TAIL

:NO_PORTS
echo.
echo  Device not found - no suitable USB ports.
echo   - Check that the cable is plugged into the device and the computer
echo   - Try another USB port - better directly in the computer, not a hub
echo   - If the device is connected for the first time, install the driver
echo     USB-UART: CH340 by WCH or CP210x by Silicon Labs
echo   - After installing the driver, run this file again
echo.
goto TAIL

:NO_NET
echo.
echo  Could not download esptool - no internet or the site is unreachable.
echo  Options:
echo   - Connect to the internet and run this file again
echo   - Or install Python 3 and run one command:
echo       pip install esptool
echo.
goto TAIL

:TAIL
if "%HOLDWAS%"=="1" (
  where pythonw >nul 2>nul
  if not errorlevel 1 (
    start "" pythonw "%APPDATA%\SDPRO\comhold.py"
    echo  Port-holding service comhold started again.
  )
)
echo.
pause
:END
endlocal
