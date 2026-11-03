@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion
title 安卓设备硬盘健康检测工具
cd /d "%~dp0"

echo ================================================
echo     安卓设备硬盘健康检测工具  v1.2
echo     无需安装任何软件, 电脑与设备同一网段即可
echo ================================================
echo.

:loop
set IP=
set /p IP=请输入设备IP(如 192.168.150.171, 直接回车退出): 
for /f "tokens=1" %%a in ("!IP!") do set IP=%%a
if "!IP!"=="" goto end

echo.
echo [1/4] 正在连接 !IP! ...
adb connect !IP! <nul >nul 2>&1
ping -n 2 127.0.0.1 >nul
set STATE=
for /f "tokens=1" %%d in ('adb -s !IP! get-state 2^>nul') do set STATE=%%d
if not "!STATE!"=="device" (
    echo.
    echo *** 连接失败! 请检查:
    echo     1. 设备是否已开机
    echo     2. IP 是否输错
    echo     3. 电脑与设备是否在同一网段
    echo     4. 设备 adb 调试端口是否已打开  默认5555, 可输 IP:端口 指定
    echo.
    goto loop
)
echo 连接成功!

echo [2/4] 推送检测工具到设备 ...
adb -s !IP! push smart_arm /data/local/tmp/smart_arm <nul >nul 2>&1
adb -s !IP! push smart.sh /data/local/tmp/smart.sh <nul >nul 2>&1
adb -s !IP! shell chmod 755 /data/local/tmp/smart_arm /data/local/tmp/smart.sh <nul >nul 2>&1

echo [3/4] 读取硬盘 SMART 健康信息 ...
set OUTFILE=硬盘检测报告_!IP!.txt
set OUTFILE=!OUTFILE::=_!
set TMPOUT=!IP:.=_!
set TMPOUT=!TMPOUT::=_!
set TMPOUT=%temp%\smart_tmp_!TMPOUT!.txt
adb -s !IP! shell sh /data/local/tmp/smart.sh <nul > "!TMPOUT!" 2>&1
findstr /C:"Permission denied" "!TMPOUT!" >nul && (
    echo 权限不足, 尝试 adb root 提权...
    adb -s !IP! root >nul 2>&1
    ping -n 4 127.0.0.1 >nul
    adb connect !IP! <nul >nul 2>&1
    adb -s !IP! shell /data/local/tmp/smart_arm <nul > "!TMPOUT!" 2>&1
)
(
    echo ================================================
    echo   安卓设备硬盘健康检测报告
    echo   设备IP: !IP!
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

echo [4/4] 检测完成!
echo.
echo ---------------- 检测结果 ----------------
type "!OUTFILE!"
echo --------------------------------------------
echo 报告已保存到: %~dp0!OUTFILE!
echo.
goto loop

:end
echo.
echo 检测结束, 按任意键退出...
pause >nul
