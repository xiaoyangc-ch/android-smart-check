@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
title 安卓设备网段扫描硬盘检测
cd /d "%~dp0"

echo ================================================
echo    安卓设备网段扫描硬盘检测工具  v1.0
echo    自动扫描网段内所有 adb 设备并检测硬盘
echo ================================================
echo.

:getnet
set INPUT=
set /p INPUT=请输入网段前3段(如 192.168.150, 直接回车退出): 
for /f "tokens=1" %%a in ("!INPUT!") do set INPUT=%%a
if "!INPUT!"=="" goto end
set NA=
set NB=
set NC=
set ND=
for /f "tokens=1-4 delims=." %%a in ("!INPUT!") do (
    set NA=%%a
    set NB=%%b
    set NC=%%c
    set ND=%%d
)
if "!NB!"=="" goto getnet
if "!NC!"=="" goto getnet
if not "!ND!"=="" (
    echo 只需输入前3段, 例如 192.168.150
    echo.
    goto getnet
)

echo.
echo 正在扫描 !NA!.!NB!.!NC!.1 到 .254 ...
echo 大约需要1分钟, 请稍候。
adb disconnect >nul 2>&1
set ONLINEFILE=%temp%\scan_online_!RANDOM!.txt
if exist "!ONLINEFILE!" del "!ONLINEFILE!"
set COUNT=0
for /L %%i in (1,1,254) do (
    set /a COUNT+=1
    set /a MOD=COUNT %% 25
    if "!MOD!"=="0" echo   已扫描 !COUNT! 个地址 ...
    ping -n 1 -w 100 !NA!.!NB!.!NC!.%%i >nul 2>&1
    if not errorlevel 1 echo !NA!.!NB!.!NC!.%%i>>"!ONLINEFILE!"
)
echo 扫描完成, 正在并发连接发现的在线主机 ...
for /f "usebackq" %%i in ("!ONLINEFILE!") do start /b adb connect %%i:5555 >nul 2>&1
ping -n 13 127.0.0.1 >nul
del "!ONLINEFILE!" 2>nul

echo.
echo 扫描完成, 正在汇总 adb 设备列表 ...
echo.
set FOUND=0
for /f "skip=1 tokens=1,2" %%a in ('adb devices') do (
    if "%%b"=="device" set /a FOUND+=1
)
if "!FOUND!"=="0" (
    echo 未发现 adb 设备。请检查:
    echo   1. 设备是否已开机
    echo   2. 电脑与设备是否在同一网段
    echo   3. 设备 adb 端口是否为 5555
    echo.
    goto getnet
)

echo 发现 !FOUND! 台 adb 设备, 开始逐台检测硬盘:
echo.
for /f "skip=1 tokens=1,2" %%a in ('adb devices') do (
    if "%%b"=="device" call :checkone %%a
)
echo.
echo ================================================
echo 全部检测完成, 报告已保存到本目录。
echo ================================================
echo.
goto getnet

:checkone
set DEV=%~1
echo ---- 正在检测 !DEV! ----
adb -s !DEV! push smart_arm /data/local/tmp/smart_arm <nul >nul 2>&1
adb -s !DEV! push smart.sh /data/local/tmp/smart.sh <nul >nul 2>&1
adb -s !DEV! shell chmod 755 /data/local/tmp/smart_arm /data/local/tmp/smart.sh <nul >nul 2>&1
set OUTFILE=硬盘检测报告_!DEV!.txt
set OUTFILE=!OUTFILE::=_!
set TMPOUT=%temp%\scan_tmp_!RANDOM!.txt
adb -s !DEV! shell sh /data/local/tmp/smart.sh <nul > "!TMPOUT!" 2>&1
findstr /C:"Permission denied" "!TMPOUT!" >nul && (
    adb -s !DEV! root >nul 2>&1
    ping -n 4 127.0.0.1 >nul
    adb connect !DEV! <nul >nul 2>&1
    adb -s !DEV! shell /data/local/tmp/smart_arm <nul > "!TMPOUT!" 2>&1
)
(
    echo ================================================
    echo   安卓设备硬盘健康检测报告
    echo   设备IP: !DEV!
    echo   检测时间: !date! !time!
    echo ================================================
    echo.
    type "!TMPOUT!"
    echo.
) > "!OUTFILE!" 2>&1
del "!TMPOUT!" 2>nul
findstr /C:"Permission denied" "!OUTFILE!" >nul && (
    echo.
    echo 提示: 该设备权限不足, 无法读取硬盘信息。需要设备开启root调试权限。
) >> "!OUTFILE!" 2>&1
goto :eof

:end
echo.
echo 按任意键退出...
pause >nul
