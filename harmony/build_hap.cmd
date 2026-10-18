@echo off
rem Builds the HarmonyOS companion application as an UNSIGNED package and puts it next to the program,
rem where qt-scrcpy-gui picks it up, signs it for the connected device and installs it.
rem Needs DevEco Studio (for hvigor and the SDK).
setlocal
set PROJECT=%~dp0.
set OUT=%PROJECT%\..\qtscrcpy-harmony-unsigned.hap

cd /d "%PROJECT%"
set HVIGOR=
if exist "%PROJECT%\hvigorw.bat" set HVIGOR=%PROJECT%\hvigorw.bat
if not defined HVIGOR if exist "%ProgramFiles%\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat" set HVIGOR=%ProgramFiles%\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat
if not defined HVIGOR (
    where hvigorw.bat >nul 2>&1 && set HVIGOR=hvigorw.bat
)
if not defined HVIGOR (
    echo Could not find hvigorw. Install DevEco Studio, or open this project in it and build
    echo "Build - Build Hap(s)" without a signing configuration.
    exit /b 1
)

set DEVECO=%ProgramFiles%\Huawei\DevEco Studio
if not defined DEVECO_SDK_HOME if exist "%DEVECO%\sdk" set DEVECO_SDK_HOME=%DEVECO%\sdk
if exist "%DEVECO%\tools\node\node.exe" set PATH=%DEVECO%\tools\node;%PATH%
where java >nul 2>&1 || if exist "%DEVECO%\jbr\bin\java.exe" set PATH=%DEVECO%\jbr\bin;%PATH%
if exist "%DEVECO%\tools\ohpm\bin\ohpm.bat" (
    call "%DEVECO%\tools\ohpm\bin\ohpm.bat" install --all
)

call "%HVIGOR%" --mode module -p product=default -p buildMode=debug assembleHap --no-daemon --no-parse-dependency
if errorlevel 1 (
    echo.
    echo hvigor failed. If it complains about signing, clear "Signing Configs" in DevEco Studio
    echo Project Structure so that it builds an unsigned package.
    exit /b 1
)

set BUILT=
for /r "%PROJECT%\entry\build" %%f in (*-unsigned.hap) do set BUILT=%%f
if not defined BUILT for /r "%PROJECT%\entry\build" %%f in (*.hap) do set BUILT=%%f
if not defined BUILT (
    echo No .hap was produced under entry\build.
    exit /b 1
)

copy /y "%BUILT%" "%OUT%" >nul
echo.
echo Copied to: %OUT%
echo Now start qt-scrcpy-gui, connect the phone and press "start mirroring": the package is
echo signed for that phone and installed automatically.
endlocal
