# 第三方声明

本仓库自己的代码使用 MIT 许可证(见 [LICENSE](LICENSE));本文件只涉及随构建产物分发的第三方组件。

本仓库**不包含** scrcpy 或 adb 的任何二进制文件。

启用 `QTSCRCPY_BUNDLE_SCRCPY`(顶层构建默认 `AUTO`)后,CMake 会在**构建这台机器上**从 scrcpy 官方发布页下载发布包,用固定的 SHA-256 校验,再复制到程序旁边(见 `cmake/QtScrcpyBundle.cmake`)。这样构建出来的程序、以及 `cmake --install` 的安装目录里**含有**下面这些第三方组件。把它们分发给别人时,下面的许可证条款就适用于分发者。

> 本文件整理的是调查时读到的事实,**不是法律意见**。标了「未确认」的地方是没能查证的,不要当成结论。不想分发 adb 的话,用 `-DQTSCRCPY_BUNDLE_ADB=OFF` 构建(只带 scrcpy,使用者自己安装 platform-tools),或用 `-DQTSCRCPY_BUNDLE_SCRCPY=OFF`(都不带)。用 `QTSCRCPY_BUNDLE_ADB=OFF` 构建时,下面「adb」一节以及 Windows 上的 `AdbWinApi.dll`、`AdbWinUsbApi.dll` 不适用,其余部分仍然适用。

**这份声明随 `cmake --install` 一起安装(`share/doc/…/`),但只有安装才会带上它。** 直接拷贝构建目录、或用 `NO_INSTALL` 的目标,拿到的只有二进制,没有任何声明或许可证文本;分发前需要自己补上。

> **重要(与旧版本的区别):** 现在的构建只从 scrcpy 官方包里取 `scrcpy-server` 和 `adb`(Windows 还有 `AdbWinApi.dll`、`AdbWinUsbApi.dll`),**不再分发** `scrcpy.exe`、SDL2、libusb、dav1d 以及 scrcpy 自带的那套 FFmpeg。下文「随安装附带的许可证文本」「编译进 scrcpy 客户端的库」里关于这些库的内容,只在你直接分发 scrcpy 官方包时才适用,对本项目的发布包不适用。本项目自己分发的视频解码库见文末「FFmpeg」一节(自编译精简版)。

调查对象是 scrcpy v3.3.3 的官方发布包(读取的是 scrcpy 仓库该标签下的构建脚本和文档):

| 文件 | SHA-256 |
| ---- | ------- |
| scrcpy-linux-x86_64-v3.3.3.tar.gz | `9b30e813e8191329ba8025dc80cb0f198fb0a318960a3b5c15395cf675c9c638` |
| scrcpy-win64-v3.3.3.zip | `4b458d33d0436688c69875cd267cae6fa8be08aa3c17772edf3a940a3dc4b17e` |
| scrcpy-win32-v3.3.3.zip | `e3d43e21c0bd6e070381c390c1e4cccd48a1e71ae73a8c217e6e6b8506598c79` |
| scrcpy-macos-aarch64-v3.3.3.tar.gz | `b93299468f19ae89ac70f7c1453914c41f1f2bcd31f6ab530038da885c19581f` |
| scrcpy-macos-x86_64-v3.3.3.tar.gz | `c767fc1d41e4ae26e40558656570962f474739924fd22ee023d8754889ee4366` |

这些值与 scrcpy 在 `doc/linux.md`、`doc/windows.md`、`doc/macos.md` 里公布的一致。发布包里**没有**任何 LICENSE / NOTICE 文件。构建只取其中的程序文件(`scrcpy`、`adb`、`scrcpy-server`、`icon.png`,Windows 另有 `.exe` 和 `.dll`),不含手册页和 `.bat`/`.vbs` 辅助脚本。

如果你用 `QTSCRCPY_SCRCPY_VERSION` / `QTSCRCPY_SCRCPY_SHA256` / `QTSCRCPY_SCRCPY_ARCHIVE` 换成了别的包,本文件不再描述你实际打包的内容(构建时会给出警告)。

## 随安装附带的许可证文本

在 `licenses/` 下,均为上游文件的原样副本:

| 文件 | 来源 | 适用于 |
| ---- | ---- | ------ |
| `scrcpy-LICENSE.txt` | scrcpy `v3.3.3` 的 `LICENSE`(Apache-2.0) | scrcpy、scrcpy-server |
| `ffmpeg-LICENSE.md` | FFmpeg `n7.1.1` 的 `LICENSE.md` | FFmpeg |
| `libusb-COPYING-LGPL-2.1.txt` | libusb `v1.0.29` 的 `COPYING`(GNU LGPL 2.1 全文) | libusb;也是 FFmpeg 所指的 LGPL 2.1 的文本 |
| `sdl2-LICENSE.txt` | SDL `release-2.32.8` 的 `LICENSE.txt`(zlib 许可证) | SDL2 |
| `dav1d-COPYING.txt` | dav1d `1.5.0` 的 `COPYING`(BSD 2-Clause) | dav1d |

**没有附带**:adb 本身的许可证和它内嵌组件的声明(见下)、zlib 的许可证文本、各库的源码或获取源码的书面承诺。FFmpeg 和 libusb 的源码可以从下面「编译进 scrcpy 客户端的库」里的链接获得。

## scrcpy 与 scrcpy-server —— Apache License 2.0

- 版权:Copyright (C) 2018 Genymobile,Copyright (C) 2018-2025 Romain Vimont。
- 源码:<https://github.com/Genymobile/scrcpy>(标签 `v3.3.3`)。
- 许可证全文:[licenses/scrcpy-LICENSE.txt](licenses/scrcpy-LICENSE.txt)(取自该标签的 `LICENSE`,SHA-256 `5e6597827749877cca06765747bf29ed9205ed5021c6aec1ad42373711bcda05`)。Apache-2.0 第 4(a) 条要求把许可证副本交给接收者。
- scrcpy-server 是推送到手机上运行的 Android APK,同样属于该仓库。它没有运行时依赖(`server/build.gradle` 只有测试用的 junit);其源码里是否包含他处复制的代码,**未确认**。
- 该标签下没有 NOTICE 文件(请求 `NOTICE` 返回 404;仓库根目录的列表因访问受限没能读到)。

## adb —— Android SDK Platform-Tools 36.0.0(Google)

- 来源:scrcpy 的构建脚本(`app/deps/adb_linux.sh`、`adb_macos.sh`、`adb_windows.sh`)下载 Google 的 `platform-tools_r36.0.0-{linux,darwin,win}.zip`,并只取出 `adb`(Windows 是 `adb.exe`、`AdbWinApi.dll`、`AdbWinUsbApi.dll`)放进 scrcpy 发布包。包里实测的 `adb version` 是 `Version 36.0.0-13206524`。
- **adb 自身的许可证:未确认。**
  - Android Developer 的 platform-tools 页面(<https://developer.android.com/tools/releases/platform-tools>)上展示的是「Android Software Development Kit License Agreement」,页面没有提到 Apache、GPL 或 BSD,也没有给出 adb 的逐组件许可证。
  - adb 二进制里带有 AOSP 源码路径(例如 `packages/modules/adb/client/usb_libusb_device.cpp`),说明它由开源代码构建;这不等于已经确认它以哪种许可证分发。
  - platform-tools 压缩包自带的 `NOTICE.txt` 应该写明这些,但调查时 `dl.google.com` 无法访问,**没有读到**。
- 与再分发有关的条款(摘自读取时该页面显示的协议文本,可能和 r36.0.0 发布时的文本不同):
  - 3.1:授予「a limited, worldwide, royalty-free, non-assignable, non-exclusive, and non-sublicensable license to use the SDK solely to develop applications for compatible implementations of Android.」
  - 3.4:「You may not use the SDK for any purpose not expressly permitted by the License Agreement. Except to the extent required by applicable third party licenses, you may not copy (except for backup purposes), modify, adapt, redistribute, decompile, reverse engineer, disassemble, or create derivative works of the SDK or any part of the SDK.」
  - 3.5:「Use, reproduction and distribution of components of the SDK licensed under an open source software license are governed solely by the terms of that open source software license and not the License Agreement.」
  - 3.7、3.8:不得使用 Google 的商标;不得移除或改动 SDK 里的专有权利声明。
- **未确认:在你的应用里捆绑并分发 adb 是否被允许。** 这需要你自己或法律顾问判断,本项目不替你下结论。
- adb 二进制里静态包含的第三方组件(从符号和字符串看出,各平台的证据不同):libusb(LGPL;Linux、macOS、Windows 都有 `libusb_init_context`,所用版本**未确认**,和下表 scrcpy 客户端用的 1.0.29 不一定是同一份)、BoringSSL、protobuf、zlib(Linux 上字符串为 1.3.0.1)、zstd、lz4、brotli、abseil、mDNS 实现、libc++。它们各自的许可证声明本应在 platform-tools 的 `NOTICE.txt` 里,**未读到,因此未确认**,本文件也没有为它们附带声明。

## 编译进 scrcpy 客户端的库

下列库的许可证 scrcpy 自己没有写明,以下是从各库上游对应版本的文件读到的:

| 组件 | 版本 | 许可证 | 在发布包中的形式 |
| ---- | ---- | ------ | ---------------- |
| FFmpeg(libavcodec / libavformat / libavutil / libswresample,Linux 另有 libavdevice) | 7.1.1 | LGPL v2.1 或更新(构建参数里没有 `--enable-gpl`、`--enable-nonfree`、`--enable-version3`) | Linux、macOS:静态链接;Windows:独立 DLL |
| libusb | 1.0.29 | LGPL 2.1 | Linux、macOS:静态链接;Windows:`libusb-1.0.dll` |
| SDL2 | 2.32.8 | zlib 许可证(Copyright (C) 1997-2025 Sam Lantinga) | Linux、macOS:静态链接;Windows:`SDL2.dll` |
| dav1d | 1.5.0 | BSD 2-Clause(Copyright © 2018-2019, VideoLAN and dav1d authors) | 静态链接进 FFmpeg / scrcpy |
| zlib | **未确认** | **未确认**(zlib 许可证) | Linux 的 scrcpy 里字符串为 1.2.11;Windows 的两个 avcodec DLL 里为 1.3.1;macOS arm64 的 scrcpy 静态包含,macOS x86_64 的 scrcpy 链接系统的 `libz.1.dylib` |

- 各库源码:FFmpeg <https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz>,libusb <https://github.com/libusb/libusb/archive/refs/tags/v1.0.29.tar.gz>,SDL <https://github.com/libsdl-org/SDL/releases/tag/release-2.32.8>,dav1d <https://code.videolan.org/videolan/dav1d/-/archive/1.5.0/dav1d-1.5.0.tar.gz>。完整构建方法在 scrcpy 仓库该标签下的 `app/deps/*.sh` 和 `release/build_*.sh`。
- LGPL 部分(FFmpeg、libusb)在 Linux、macOS 上是静态链接、在 Windows 上是独立 DLL,这会影响使用者替换它们的方式。分发时需要满足的具体义务(例如提供源码或书面承诺)没有在这里替你梳理。
- Linux 版 scrcpy 运行时还需要使用者系统自带的 libc(发布包里最高要求 glibc 2.35)、libm 和 `libudev.so.1`;随包的 adb 另需 libgcc_s.so.1 等;这些都不在发布包里。


## FFmpeg（电脑端 H.264 解码：安卓和鸿蒙设备都用）

发布包里的 `avcodec-61.dll`、`avutil-59.dll`、`swscale-8.dll`。本程序只使用其中的 **H.264 解码器**和像素格式转换，通过动态链接调用，用户可以自行替换这些 DLL（保持同一主版本）。

- **这是本项目自己编译的精简版 FFmpeg 7.1.1**，不是第三方预编译包：只启用了 `h264`、`hevc`、`mjpeg`（系统截图是 JPEG，降级投屏要用）三个解码器和 h264/hevc 解析器，没有任何编码器、封装/解复用、滤镜、网络协议和外部库。
- 配置命令（32 位把 `--arch=x86_64` 换成 `--arch=x86`，并用 32 位 MinGW）：

  ```
  ./configure --arch=x86_64 --target-os=mingw32 --enable-shared --disable-static --disable-programs --disable-doc
    --disable-avdevice --disable-avformat --disable-avfilter --disable-swresample --disable-network
    --disable-everything --enable-decoder=h264,hevc,mjpeg --enable-parser=h264,hevc --disable-debug --enable-small
    --disable-autodetect --disable-iconv --disable-d3d11va --disable-dxva2 --disable-hwaccels
    --enable-swscale --disable-postproc
  ```
- 许可证：configure 输出 **LGPL 2.1 或更高**（没有 `--enable-gpl`、`--enable-nonfree`、`--enable-version3`，也没有链接任何第三方库）。许可证文本：`licenses/ffmpeg-COPYING.LGPLv2.1`。
- 源码：<https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz>（SHA-256 请自行核对官方发布页）。分发这些 DLL 的人需要能向接收者提供对应源码；上面的配置命令加官方源码即可重建。
- 本项目**没有修改** FFmpeg 源码。
- 本文件整理的是事实，**不是法律意见**。

## scrcpy 客户端协议（内置在 qtscrcpy.dll）

库里的 `ScrcpyStream` 是按 scrcpy v3.3.3 的公开客户端协议（`app/src/server.c`、`demuxer.c` 与服务端 `Options.java` / `Streamer.java`）**重新实现**的最小客户端，没有复制 scrcpy 客户端的源码；只用来和随包的 `scrcpy-server` 通信。`scrcpy-server` 本身仍是 scrcpy 项目的 Apache-2.0 文件，见上文。发布包**不再包含** `scrcpy.exe`、SDL2、scrcpy 自带的 FFmpeg（`avcodec-61` 那一套）和 libusb，上文里关于它们的条目对本发布包不适用（仍然适用于直接使用 scrcpy 官方包的情况）。

## HarmonyOS 签名材料（signing/）

为了让程序能按连接到的设备自动签名安装配套应用，发布包里带了 OpenHarmony SDK 的调试签名材料：

| 文件 | 来源 | 说明 |
| ---- | ---- | ---- |
| `hap-sign-tool.jar` | OpenHarmony `developtools_hapsigner`（Apache-2.0） | 签名工具，运行时由 Java 执行 |
| `UnsgnedDebugProfileTemplate.json` | OpenHarmony SDK `toolchains/lib` | 调试 profile 模板（含 OpenHarmony 调试应用证书） |
| `OpenHarmony.p12` | 同上 | 口令 `123456`，公开的 OpenHarmony 调试密钥库 |
| `OpenHarmonyProfileDebug.pem` | 同上 | profile 签名证书链 |
| `app-cert-chain.pem` | 由上面的模板和证书拼成 | 应用证书链（应用证书 + CA + 根） |

这些是 OpenHarmony 公开发布的调试凭据，**不是**任何个人或华为账号的凭证；用它们签出来的应用只有信任 OpenHarmony 调试根证书的设备会安装。
本项目不修改这些文件；重新分发时请保留其原始许可证（Apache-2.0，见 `licenses/`）。

## 配套应用里的软件编码器（harmony/）

手机没有可用的硬件视频编码器时，配套应用改用内置的软件编码，依次尝试 H.264 和逐帧 JPEG：

| 组件 | 版本 | 许可证 | 用途 |
| ---- | ---- | ------ | ---- |
| OpenH264（Cisco） | v2.6.0 | BSD-2-Clause，见 `licenses/openh264-LICENSE.txt` | 软件 H.264 编码，只编译可移植的 C++ 代码，不含汇编 |
| stb_image_write（Sean Barrett） | master | MIT 或公有领域二选一，见 `licenses/stb-LICENSE.txt` | 逐帧 JPEG 编码 |

- OpenH264 是**从源码自行编译**进配套应用的，没有使用 Cisco 发布的二进制。Cisco 为其官方二进制承担 H.264 专利许可费，**自行编译的版本不享受这项专利授权**；分发含 H.264 编码的构建前，请自行评估所在地区的专利义务。本项目不修改 OpenH264 源码。
- OpenH264 源码不随仓库分发：构建时用环境变量 `QTSCRCPY_OPENH264_DIR` 指向源码目录，没有时只编译 JPEG 兜底。stb 的头文件随仓库放在 `harmony/entry/src/main/cpp/third_party/stb/`。
- 本文件整理的是事实，**不是法律意见**。

## hdc（HarmonyOS Device Connector）

发布包里带了 `hdc.exe` 和 `libusb_shared.dll`，用来连接鸿蒙设备。它们取自 OpenHarmony SDK 的 `toolchains/`（本仓库构建时放在 `build/deps/hdc/`），与本机 DevEco Studio 里的文件逐字节相同。

| 文件 | 来源 | 许可证 |
| ---- | ---- | ------ |
| `hdc.exe` | OpenHarmony `developtools_hdc` | Apache-2.0，SDK 的 NOTICE 摘录见 `licenses/hdc-NOTICE.txt` |
| `libusb_shared.dll` | libusb 1.0.28（OpenHarmony 的构建） | LGPL-2.1，文本见 `licenses/libusb-COPYING-LGPL-2.1.txt` |

- `libusb_shared.dll` 是 LGPL 的动态库，随包分发时与程序分开，用户可以自行替换；分发的人需要能提供对应源码（libusb 官方源码即可，但 OpenHarmony 的构建可能带有自己的补丁，本项目没有核对）。
- 本项目不修改这两个文件。hdc 的许可证归属我是按 SDK 自带的 NOTICE 文件核对的，**不是法律意见**。

