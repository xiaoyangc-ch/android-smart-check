# Android SMART 硬盘检测工具

免安装的安卓设备硬盘健康检测工具：电脑通过 ADB 连接安卓设备（机顶盒 / 一体机等），
在设备端直接向硬盘发送 ATA 标准 SMART 命令，读取 512 字节健康记录并输出中文检测报告。
**只读不改，对硬盘零风险。**

## 功能特性

- SATA 直连盘：`HDIO_DRIVE_CMD` ioctl 直读（libata 自动补全寄存器）
- USB 桥接盘：SG_IO + ATA PASS-THROUGH(16)/(12)，自动尝试 4 种 CDB 布局
- 多硬盘自动扫描（`/dev/block/sd[a-z]`），统计 SATA 端口数与未识别盘位
- ATA IDENTIFY 解析：型号 / 序列号 / 固件版本 / LBA48 容量
- 判定规则：厂商阈值（current ≤ threshold）+ 原始值敏感规则
  （坏道类属性 >0 即判故障；CRC 错误 >1000 / 命令超时 >100 计异常）
- 全链路 UTF-8：源码 / 脚本 / 报告统一编码，报告带 BOM 兼容旧版记事本

## 使用方法

把以下文件放在同一目录：`smart_arm`（编译产物）、`smart.sh`、两个 `.bat`、`adb.exe`

| 脚本 | 用途 |
|---|---|
| 硬盘检测.bat | 单台设备检测：输入设备 IP（默认 adb 端口 5555，可输 `IP:端口`） |
| 网段扫描检测.bat | 网段批量检测：输入网段前 3 段，自动 ping 扫描 + 批量 adb 连接 + 逐台检测 |

- 电脑与设备需在同一网段，设备已开启 adb 网络调试（5555）
- 检测完成后在本目录生成 `硬盘检测报告_<IP>.txt`
- 权限不足时脚本自动尝试 `adb root` / 设备内 `su` 提权后重读

## 编译 smart_arm

下载 [zig 编译器](https://ziglang.org/download/)（免安装），执行：

```sh
zig cc -target arm-linux-musleabihf -O2 -static smart.c -o smart_arm
```

生成安卓 32 位 ARM 静态二进制。

## 目录结构

```
smart.c              核心检测程序源码（C，安卓端运行，含中文注释）
smart.sh             设备端提权包装脚本（非 root → su 回退链）
硬盘检测.bat          单台设备检测脚本（UTF-8，chcp 65001）
网段扫描检测.bat      网段扫描批量检测脚本（UTF-8，chcp 65001）
```

## 技术要点

1. 原理：通过 ADB 进入安卓设备，用 Linux ioctl 向硬盘发送 ATA 标准命令
   SMART READ DATA（操作码 `0xB0`，特征 `0xD0`），硬盘固件返回 512 字节健康记录。
2. 读 SMART 的三条路径（按优先级自动回退）：
   - `HDIO_DRIVE_CMD` ioctl（`0x031F`）对 `/dev/block/sda` —— SATA 直连首选
   - SG_IO + ATA PASS-THROUGH(16)（`0x85`）—— libata 拒绝 T_DIR 位，`cdb[1]` 必须 `0x08`
   - SG_IO + ATA PASS-THROUGH(12)（`0xA1`）—— libata 下按 SFF-8070i 布局；USB 桥接另有常用布局变体
3. 判定规则：厂商阈值（current ≤ threshold）+ 原始值敏感规则（生产环境标准）
4. 编码：全链路 UTF-8，bat 开头 `chcp 65001` 保证任何语言 Windows 显示正常

## 已知说明

- `chcp 65001` 下 `set /p` 读文件重定向会失效（cmd 老 bug），真实键盘输入不受影响
- 设备需开启 root 调试权限（`adb root` 或 `su`）才能读取 SMART
- 自动扫描允许 sdX 缺号（连续 3 个不存在即结束）

## 更新记录

- v1.1：修复阈值表未初始化参与健康判定；修复 SATA 盘型号字节序乱序；SG 设备改用 sysfs 关联（失败自动回退扫描），避免多 USB 盘数据串扰；su 回退链成功即停；修复 IP:端口 输入临时文件名冒号导致报告丢失
- v1.0：初始版本

## 许可

[MIT](LICENSE)
