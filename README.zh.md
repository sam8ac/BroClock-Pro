![BroClock Pro — ESP8266 固件，适配 SmallTV / SD PRO 桌面时钟](assets/banner.svg)

[![Release](https://img.shields.io/github/v/release/sam8ac/BroClock-Pro?label=Release&color=c56a90&style=for-the-badge&logo=star)](https://github.com/sam8ac/BroClock-Pro/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)
[![CI](https://img.shields.io/github/actions/workflow/status/sam8ac/BroClock-Pro/ci.yml?branch=main&label=Build&style=for-the-badge&logo=github)](https://github.com/sam8ac/BroClock-Pro/actions/workflows/ci.yml)
![ESP8266](https://img.shields.io/badge/platform-ESP8266-2b6?style=for-the-badge)
![PlatformIO](https://img.shields.io/badge/PlatformIO-ready-orange?style=for-the-badge&logo=platformio)
[![Downloads](https://img.shields.io/github/downloads/sam8ac/BroClock-Pro/total?label=Downloads&style=for-the-badge)](https://github.com/sam8ac/BroClock-Pro/releases)
[![Issues](https://img.shields.io/github/issues/sam8ac/BroClock-Pro?style=for-the-badge)](https://github.com/sam8ac/BroClock-Pro/issues)
![i18n: EN · RU · ES · ZH](https://img.shields.io/badge/i18n-EN%20%C2%B7%20RU%20%C2%B7%20ES%20%C2%B7%20ZH-2b6?style=for-the-badge)

**页面语言：** [Русский](README.md) · [English](README.en.md) · [Español](README.es.md) · **中文**

**RetroTV Pro**（BroClock Pro）是运行在 **GeekMagic SmallTV / SmallTV-Ultra** 系列（含 SD PRO 板卡）桌面上的复古电视造型时钟的开源 ESP8266 固件：加密货币行情 + 天气、8 套主题、K 线图、轮播、节假日日历、OTA 升级，以及 **4 种语言**的网页管理界面。

> 🌐 **核心亮点——界面完整本地化为 4 种语言：英语 · 俄语 · 西班牙语 · 中文。**
> 不只是菜单：状态、错误、警告、WiFi 扫描提示、照片上传与固件升级消息——全部翻译。在网页中一键切换，即时生效、无需刷新；选择保存在设备中，与屏幕语言同步，100% 离线可用。

[安装](docs/INSTALL.md) · [硬件](docs/HARDWARE.md) [HTTP API](docs/HTTP-API.md) · [发行版](https://github.com/sam8ac/BroClock-Pro/releases) · [Issues](https://github.com/sam8ac/BroClock-Pro/issues)

---

把 $20 的电视造型桌面时钟从摆件变成真正的信息终端：**比特币与山寨币实时价格（48 周期 K 线图）、任意语言天气、8 套可换界面主题、多国节假日日历、夜间背光调节，以及手机浏览器上的完整控制**——全部运行在原厂 ESP8266（ESP-12E）与 ST7789 240×240 屏幕上。无云服务、无账号，MIT 许可证。

![BroClock Pro — 屏幕上显示 BTC 图表的时钟](assets/device.jpg)

*实机演示：屏幕上运行 BTC 图表。*

## 💻 项目语言

| 部分 | 语言 | 位置 |
|---|---|---|
| 固件（全部逻辑、屏幕、网络） | **C++**（Arduino, ESP8266） | [`src/main.cpp`](src/main.cpp) + [`src/*.h`](src) |
| 网页界面（嵌入固件） | **HTML + CSS + JavaScript** | `main.cpp` 中的 raw 字符串 `PAGE[]`、`SETUP_PAGE[]` |
| 资源/字体构建工具 | **Python + Node.js** | [`tools/`](tools) |
| 构建系统 | **PlatformIO**，版本锁定——可复现 | [`platformio.ini`](platformio.ini) |

网页界面不从互联网加载任何东西——没有外部库也没有字体：由时钟本身提供，完全离线可用。

## 🌐 多语言——首发即支持 4 种语言

**EN · RU · ES · ZH** —— 全部界面，包括错误信息、警告、弹窗提示和状态。网页切换即时生效（无需刷新），选择保存在设备中并与屏幕语言同步。浏览器标签页标题、设置分区、时钟格式（12/24 小时）、WiFi 扫描警告、照片上传错误、地理定位与固件升级——全部翻译。

本 README 也提供：[Русский](README.md) · [English](README.en.md) · [Español](README.es.md)。
固件本地化只是 [`src/main.cpp`](src/main.cpp) 开头的一张字典表——新增语言是最简单的贡献方式。

## ✨ 功能特性

### 💰 加密货币
- **CoinGecko 前 250 名**币种目录，支持实时搜索与手动添加
- 主屏最多 **4 种货币** + 次级行情条最多 **3 种**
- **48 根 K 线**或折线图，约 60 秒自动刷新
- 行情缓存在 Flash 中——重启后立即显示数据，无需等待网络
- **「立即刷新数据」**——强制更新价格/K 线（忽略缓存与退避）
- 抓取任务分散在 4 秒空隙中：启动批次（K 线+价格+行情）不再一次性耗尽堆内存——空闲块稳定约 18 KB，无「heap low」也不重启
- 时区支持 POSIX 字符串（**设置 → 时钟与语言**），NTP 同步

### 🌤 天气——两个数据源，均可使用
- **Open-Meteo** —— 无需密钥、无需注册；**OpenWeatherMap** —— 需要 API 密钥；可热切换，无需重启
- **城市自动补全**（最多 5 条结果）：支持**西里尔字母与任意拉丁字母** —— `Vitebsk` 与 `Витебск` 都能找到 `Vitebsk,BY`
- **「我的位置」**——浏览器定位或按 IP 定位；全新安装时会按设备 IP 自动填入城市（可保存，表单仍可编辑）
- 5 天预报 + 当前天气，每 10 分钟自动更新
- 城市以 `名称,CC` 格式保存，**两个数据源**都接受
- **城市默认为空**并有「门控」：未设城市时设备不会猛刷 API 限额，显示「no city」（按所选语言）

### 🎨 主题与外观
- **8 套主题**，横向选择条并高亮当前主题
- 轮播：任意主题组合，间隔 5–900 秒
- 夜间模式：亮度渐变 + 独立夜间亮度
- 背景色（预设色调 + 调色板），Classic 主题角落动画
- **照片主题**：从浏览器上传图片，自动裁剪为 240×240，上限 256 KB，GIF 取第一帧

![项目 8 套主题 — GIF 循环演示](assets/screen.gif)

*8 套主题一览（轮播）。*

### 🧭 网页界面
- 独立标签页：**主题 · 设置 · 加密 · 天气 · WiFi · 固件**；「设置」按功能分块（轮播 / 时钟、时区与语言 / 背光 / 动画 / 照片 / 日历）
- 响应式设计：手机上导航变为按钮行
- 所有更改即时生效，无需刷新

![网页界面菜单 — GIF 循环演示](<assets/Web interface.gif>)

*网页界面：标签页切换。*

### 📡 WiFi 与连接
- **首次启动强制门户**：接入热点 `BroClock-PRO` → `192.168.4.1` → 选择你的网络
- 网络扫描显示信号强度；已保存网络自动连接
- 当已连接到所选网络时——SSID 只读、密码隐藏、按钮禁用（「已连接」）：不会误破坏正常连接
- 单击按钮「忘记已保存的网络」

### 🔒 安全与稳定性
- **OTA 指纹校验**：只接受本系列镜像（芯片、Flash、屏幕引脚）——陌生固件**不会被刷入**，旧固件继续运行。损坏/改名的文件无法变砖
- **TLS 堆内存门控**（7550/12000 字节）+ 抓取任务间隔 4 秒：即使 4 种货币 + K 线也不会 OOM 重启；RAM 占用 — 69 %
- **快速启动**：连接 WiFi 后 12 秒「静默」窗口（DNS/堆稳定，设置页立即可开），随后每 4 秒抓取 K 线/价格/行情，城市解析成功后立即抓天气；连续失败 3 分钟 → 自动重启
- 天气/加密缓存重启后仍然有效；调度器兼容时区/夏令时与看门狗
- 密码永不在界面显示；WiFi 凭据保存在 ESP8266 存储中

### 📅 日历与更多
- **40 多个国家**的节假日，按 IP 自动识别国家，年度缓存（date.nager.at）

### 🛠 开发与运维
- [完整安装指南](docs/INSTALL.md)：3 种操作系统、浏览器刷机、备份、回滚、故障排查表、常见问题
- **Windows 一键安装器**（`tools\flash_broclock.bat`）：自动查找 CH340、刷机并校验哈希
- **HTTP API**（`/api/status`、`/api/set`、`/api/refresh`、`/api/city`、`/api/wifi`……）——可集成 Home Assistant 与脚本
- **可复现构建**：平台/库版本锁定，自动修补框架（`tools/apply_framework_patches.py`），每次 push 时 CI 编译两个硬件版本
- **MIT 许可证**——时钟随便你怎么用

## 🖼 截图

设备照片与 GIF 演示已嵌入上方；完整图库在 [`assets/`](assets/PHOTOS.md)。

## 🧩 兼容性

已在 **SD PRO**（两个引脚版本）与 NodeMCU + ST7789 上验证；
电气兼容 [GeekMagic SmallTV / SmallTV-Ultra](https://github.com/GeekMagicClock/smalltv-ultra) 系列。

> ⚠️ **SmallTV-Pro（ESP32）不兼容。** 首次刷机需要 USB-UART，之后支持 OTA。完整设备列表与步骤见
> [docs/INSTALL.md](docs/INSTALL.md)。

## 🚀 快速开始

```bash
# 1. 从 Releases 下载 SDP_firmware.bin 并刷入
python -m esptool --port COM5 --baud 460800 write-flash 0x0 SDP_firmware.bin

# 2. 连接 BroClock-PRO 热点，打开 http://192.168.4.1
# 3. 选择你的 WiFi 网络 —— 时钟加入局域网，IP 显示在屏幕上
# 4. 之后的更新 —— 在「固件」标签页完成
```

Windows 用户？把 `tools\flash_broclock.bat` 放到 `.bin` 旁边运行——自动查找
CH340、刷机并校验哈希：

![flash_broclock.bat — Windows 固件安装器](<assets/Installation using bat.JPG>)

## 🔨 从源码编译

```bash
git clone https://github.com/sam8ac/BroClock-Pro.git
cd BroClock-Pro
pio run -e sdpro        # 或 sdpro_alt 用于另一硬件版本
```

## 🗺 路线图

- [x] 8 套主题 + 轮播，4 语言网页界面，主题/设置分块标签页
- [x] 双天气数据源，城市自动补全（俄/英），节假日日历
- [x] OTA 指纹校验、缓存、堆门控、Windows 安装器
- [ ] MQTT / Home Assistant 自动发现
- [x] 城市名西里尔化（Open-Meteo，`language=ru`）
- [ ] 通过 URL 导入主题

## 🤝 贡献

欢迎 Issue 与 PR——清单见
[.github/PULL_REQUEST_TEMPLATE.md](.github/PULL_REQUEST_TEMPLATE.md)。
本地化是最好的入门贡献：整个界面字典就是 `src/main.cpp` 开头的一张表。

## ⭐ 相关项目

- [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware) —— 面向 GeekMagic / HelloCubic 的 GPLv3 开源固件
- [aydarik/geekmagic-tv-esp8266](https://github.com/aydarik/geekmagic-tv-esp8266) —— GeekMagic SmallTV 的 MIT 许可 ESP8266 固件
- [GeekMagicClock/smalltv-ultra](https://github.com/GeekMagicClock/smalltv-ultra) —— 官方 Ultra 固件与手册

## 📜 许可证

[MIT](LICENSE) © 2026 sam8ac

---

*关键词：smalltv · geekmagic · GeekMagicClock · SmallTV-Ultra · ESP8266 · ST7789 · 复古时钟 · 加密货币行情 · 天气站 · PlatformIO · SD PRO · Arduino*
