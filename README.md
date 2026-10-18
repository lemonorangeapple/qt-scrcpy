# qt-scrcpy

用 C++/Qt5 封装 [scrcpy](https://github.com/Genymobile/scrcpy) 与 `adb`，只做一件事：**把 Android 手机的屏幕或摄像头画面镜像到电脑**。

- 默认 **仅查看**：自动带上 `--no-control`，电脑上不能操作手机。
- 默认 **不传声音**：自动带上 `--no-audio`（scrcpy 1.x 本来就不传声音）。
- 只保留两种来源：**屏幕**和**摄像头**（后置 / 前置 / 外接）。无线连接、截图、安装 APK、adb shell、参数面板等都已移除。
- 摄像头投屏需要 scrcpy ≥ 2.2 和 Android 12+，版本不够时会提示并拒绝启动。
- scrcpy 客户端**已内置进库**（`qtscrcpy.dll`）：通过 adb 服务端协议推送 `scrcpy-server`、启动、接收 H.264 并用 FFmpeg 解码，所以发布包里**没有 `scrcpy.exe`**，也没有 SDL2、scrcpy 自带的那套 FFmpeg 和 libusb；剩下的 `adb.exe` 只作为 adb 服务端使用。
- 同时支持 **adb**（安卓、带安卓兼容层的鸿蒙）和 **hdc**（HarmonyOS NEXT 等 OpenHarmony 设备）。hdc 设备由手机上的配套应用（`harmony/`）把屏幕或摄像头画面编码成 H.264，经 `hdc fport` 转发到电脑，程序用 FFmpeg 解码后在 Qt 窗口里显示，帧率是实时视频级别，屏幕和前/后置摄像头都能看。
- 同时提供 32 位（win32）和 64 位（win64）构建。
- 图形界面只有一个设备列表、一个来源下拉框和一个「开始投屏 / 停止投屏」按钮。
- Windows 下程序以 GUI 子系统构建，启动时不会出现命令行黑窗口；由它启动的 `adb` / `scrcpy` 子进程也不会弹出控制台。

## 构建

解码鸿蒙画面需要 FFmpeg 开发包：`-DQTSCRCPY_FFMPEG_ROOT=<目录>`（含 `include/`、`lib/`、Windows 还要 `bin/` 里的 DLL；LGPL 动态库即可，构建会把需要的 DLL 复制到程序旁边）。不给这个选项也能编译，只是鸿蒙设备不能投屏。32 位用 32 位的 FFmpeg，64 位用 64 位的。

需要 CMake ≥ 3.16、Qt 5.14+ 和 C++17。默认会在配置阶段下载官方 scrcpy 发布包，**只取其中的 `adb` 和 `scrcpy-server`**（校验 SHA-256 后复制到程序旁边），因此目标机器不需要预装 scrcpy / adb。

```sh
cmake -S . -B build -G Ninja -DQTSCRCPY_FFMPEG_ROOT=<FFmpeg 目录> -DBUILD_SHARED_LIBS=ON
cmake --build build
ctest --test-dir build --output-on-failure   # 需要 CMake ≥ 3.20
./build/examples/gui/qtscrcpy-gui
```

构建时会把 Qt 的运行库和平台插件、`qtscrcpy.dll`、FFmpeg 的 DLL 一起复制到程序目录，所以**构建目录里也能直接双击运行**，不需要设 PATH。

常用选项：

| 选项 | 作用 |
| ---- | ---- |
| `-DQTSCRCPY_BUNDLE_SCRCPY=OFF` | 不下载/不打包 scrcpy，使用系统里已安装的 |
| `-DQTSCRCPY_BUNDLE_ADB=OFF` | 只打包 `scrcpy-server`，不带 adb |
| `-DQTSCRCPY_BUILD_EXAMPLES=OFF` | 不构建 GUI |
| `-DQTSCRCPY_BUILD_TESTS=OFF` | 不构建测试 |
| `-DBUILD_SHARED_LIBS=ON` | 把库构建成 `qtscrcpy.dll`（Windows 下会和 GUI 放在同一目录） |

32 位构建按编译器实际的指针宽度选择 scrcpy 的 `win32` 包；用 32 位 Qt/MinGW 配置即可。

作为子项目使用：

```cmake
add_subdirectory(qt-scrcpy)
target_link_libraries(myapp PRIVATE QtScrcpy::qtscrcpy)
```

## 使用

1. 用数据线连接手机，在「开发者选项」中打开 USB 调试。
2. 手机弹出授权提示时点「允许」。
3. 在列表中选择设备（hdc 设备带 `hdc` 标记），在下拉框选「屏幕」或某个摄像头，点「开始投屏」。
4. 鸿蒙设备第一次使用前，先安装配套应用，见下一节；之后每次点「开始投屏」程序会自动在手机上启动它，手机上要点一次「允许录屏」（摄像头则是相机权限）。

设备状态（等待授权、离线、投屏中等）直接显示在列表项里。拔掉数据线时投屏窗口会关闭；重新插上后可以再次开始。

hdc 的查找顺序：环境变量 `HDC` → `HOS_SDK_HOME` / `OHOS_BASE_SDK_HOME` / `DEVECO_SDK_HOME` 下的 `toolchains` → 程序所在目录 → `PATH`。**构建不会打包 hdc**，需要自己安装（DevEco Studio 的 SDK 里带有）。

adb 的查找顺序：环境变量 `ADB` → 程序所在目录（随包分发的副本）→ Android SDK 的 `platform-tools` → `PATH`。`scrcpy-server` 的查找顺序：环境变量 `SCRCPY_SERVER_PATH` → 程序所在目录 → `PATH` 里 `scrcpy` 所在目录。

## 鸿蒙设备（hdc）：插上就用

选中鸿蒙设备点「开始投屏」时，程序会自己完成下面几步，不需要手敲命令：

1. `hdc shell bm dump -n com.qtscrcpy.streamer` 看配套应用在不在。
2. 不在，就读设备 UDID（`hdc shell bm get -u`）。
3. 用这个 UDID 生成一份调试 profile，签好名，再把 HAP 签名（`signing/` 里的 OpenHarmony 调试证书和 `hap-sign-tool.jar`）。
4. `hdc install -r` 安装，`aa start` 启动，然后开始投屏。

### 签不上就自动降级（零售版鸿蒙也能看屏幕）

如果配套应用装不上（华为零售版 HarmonyOS 只认华为签发的证书），程序**不会就此失败**，而是自动改用系统自带的截图命令
（`snapshot_display` + `file recv`）轮询取画面：

- 能看**屏幕**，帧率低（每秒几帧，受截图和传输速度限制），窗口标题会标上「低帧率」，状态栏和弹窗里会说明降级原因。
- **摄像头不能降级**（截图命令取不到相机画面），这种情况会直接报告错误原因。
- 想关掉这个降级：调用 `HdcStream::setFallbackToScreenshots(false)`（C++），或给 `MirrorOptions::HapIsUnsigned` 之外的场景自行处理。

**你要做的只有一件事**：把一份**未签名**的 HAP 放到程序目录下，命名成 `qtscrcpy-harmony-unsigned.hap`
（或 `qtscrcpy-harmony.hap`，也可以是已签名的；也可以用环境变量 `QTSCRCPY_HAP` 指定）。之后插线、点按钮即可。

- 手机端仍然要点一次「允许 USB 调试」，以及每次投屏的录屏/相机授权，这些是系统弹窗，程序没法代点。
- **重要限制**：上面这套自动签名是 OpenHarmony 的调试签名，**开发板**（如 dayu200）和允许调试安装的 OpenHarmony 设备能用；
  华为零售版 HarmonyOS 只认华为签发的证书，会拒绝安装，程序会把手机返回的原文弹出来。
  这种设备要自己用 DevEco Studio 的「自动签名」签一次，然后把**签好的** HAP 放进程序目录（那时不再需要签名材料）。
- 签名需要 Java：程序依次找 `$JAVA_HOME`、`PATH`、DevEco Studio 自带的 `jbr`。
- 签名材料放在 `signing/`（`hap-sign-tool.jar`、`UnsgnedDebugProfileTemplate.json`、`OpenHarmony.p12`、
  `OpenHarmonyProfileDebug.pem`、`app-cert-chain.pem`），都是 OpenHarmony SDK 里的**公开**调试材料，不含任何私密凭证。
  缺哪份会在弹窗里说明；也可以用 `QTSCRCPY_SIGNING_DIR` 指向 SDK 的 `toolchains/lib`。

### 构建配套应用（一次性）

用 DevEco Studio 打开 `harmony/`，**清空 Signing Configs**（让它构建未签名包），然后运行 `harmony/build_hap.cmd`
（或 DevEco 菜单 `Build → Build Hap(s)`）；脚本会把产物复制到程序目录并命名成 `qtscrcpy-harmony-unsigned.hap`。

之后插线、点「开始投屏」，签名和安装由程序完成。

`harmony/` 是手机端应用的工程（ArkTS 界面 + C++ 原生推流：屏幕录制/相机 → 硬件 H.264 编码器 → TCP）。协议见 [harmony/PROTOCOL.md](harmony/PROTOCOL.md)。用 DevEco Studio 打开这个目录，选择「构建未签名 HAP」，把产物放到程序目录即可（不要用 DevEco 的自动签名，让本程序按设备签名）。



`harmony/` 是手机端应用的完整工程（ArkTS 界面 + C++ 原生推流：屏幕录制/相机 → 硬件 H.264 编码器 → TCP）。协议见 [harmony/PROTOCOL.md](harmony/PROTOCOL.md)。**它需要你用 DevEco Studio 自己构建、签名并安装**（签名证书是你个人的，仓库里不可能带）：

1. 用 DevEco Studio 打开 `harmony/` 目录，在 `File → Project Structure → Signing Configs` 里勾选自动签名（需要登录华为开发者账号），连接手机。
2. 点运行，或 `hvigorw assembleHap` 后用 `hdc install` 安装。包名 `com.qtscrcpy.streamer`。
3. 手机上打开「开发者选项 → USB 调试」。之后在电脑端选中设备点「开始投屏」即可。

设备端原生代码已用 OpenHarmony 6.0 的 NDK 编译并链接通过（aarch64），ArkTS 部分和整个应用**没有在真机上运行过**，也没有用 hvigor 构建过。第一次在真机上跑，如果有问题，手机端应用界面会显示原因；电脑端会弹窗显示错误。
- 屏幕画面用的是 `OH_AVScreenCapture_StartScreenCaptureWithSurface`，画面直接送入硬件编码器，不经过 CPU 拷贝；每秒强制一个关键帧，丢包后最多 1 秒恢复。
- 摄像头来源需要 `ohos.permission.CAMERA`。HarmonyOS 摄像头不能在后台运行，所以摄像头投屏时手机应用要留在前台。
- 电脑端 hdc 的查找顺序：环境变量 `HDC` → `HOS_SDK_HOME` / `OHOS_BASE_SDK_HOME` / `DEVECO_SDK_HOME` 下的 `toolchains` → 程序所在目录 → `PATH`。**发布包不带 hdc**（它是华为 SDK 的一部分），需要自己安装。

## 给其他程序用：qtscrcpy.dll

库可以编译成 `qtscrcpy.dll`（`-DBUILD_SHARED_LIBS=ON`），有两层接口，**都同时覆盖 adb 和 hdc、屏幕和摄像头**：

### 纯 C 接口（任何语言都能调）

头文件 [`src/qtscrcpy/qtscrcpy_c.h`](src/qtscrcpy/qtscrcpy_c.h)，不出现任何 Qt 类型；C、C#、Python ctypes、Delphi 等都可以直接加载 DLL 调用。`qsc_init` 会在 DLL 自己的目录里找 `adb`、`hdc`、`scrcpy-server`（环境变量 `ADB` / `HDC` / `SCRCPY_SERVER_PATH` 优先）。

```c
#include <qtscrcpy/qtscrcpy_c.h>

static void QSC_CALL on_frame(const qsc_frame *f, void *user) { /* f->bgra: width*height*4 字节 */ }
static void QSC_CALL on_done(const char *error, void *user)  { /* error 为 NULL 表示正常停止 */ }

qsc_init(NULL);
qsc_device devices[16];
int n = qsc_list_devices(devices, 16);           /* adb 和 hdc 设备都在里面，约 1 秒后才找得到 */
qsc_session *s = qsc_session_create(devices[0].backend, devices[0].serial, QSC_SOURCE_SCREEN);
qsc_session_start(s, on_frame, on_done, NULL);   /* 回调在库的线程里，每个画面一次 */
/* ... */
qsc_session_stop(s);
qsc_session_destroy(s);
qsc_shutdown();
```

`QSC_SOURCE_SCREEN` / `QSC_SOURCE_CAMERA_BACK` / `QSC_SOURCE_CAMERA_FRONT` / `QSC_SOURCE_CAMERA_EXTERNAL`（后者只有 adb 支持，`qsc_source_unsupported()` 可以事先问）。回调里要尽快返回，来不及处理的画面会被丢掉而不是堆积。

### C++/Qt 接口

`QtScrcpy::MirrorSession`（[`mirror.h`](src/qtscrcpy/mirror.h)）：同样的功能，信号槽形式，`MirrorOptions::backend` 选 `Backend::Adb` 或 `Backend::Hdc`。

```cpp
QtScrcpy::MirrorOptions o;
o.backend = QtScrcpy::Backend::Hdc;              // 或 Adb
o.serial = "…";
o.source = QtScrcpy::MirrorSource::FrontCamera;  // 或 Screen / BackCamera / ExternalCamera
auto *session = new QtScrcpy::MirrorSession(o, &hdc);
connect(session, &QtScrcpy::MirrorSession::frameReady, …);   // VideoFrame: BGRA
session->start();
```

## 库 API

库只保留设备发现与 scrcpy 进程管理：

- `QtScrcpy::Adb`：查找/运行 adb、读取设备列表、跟踪设备变化。
- `QtScrcpy::MirrorSession`：统一入口（adb 或 hdc，屏幕或摄像头），见上一节。
- `QtScrcpy::AdbClient`：直接说 adb 服务端协议（查询、端口转发、推送文件、`shell:`），不需要为每一步启动 `adb` 进程。
- `QtScrcpy::ScrcpyStream`：库内的 scrcpy 客户端（安卓）。
- `QtScrcpy::Hdc`：查找/运行 hdc、轮询设备列表。
- `QtScrcpy::HdcStream`：启动手机端应用、端口转发、接收并解码 H.264 视频流（`frameReady` 信号给出 BGRA 画面）。
- `QtScrcpy::H264Decoder`、`QtScrcpy::StreamProtocol`：解码和协议解析，可单独使用。
- `QtScrcpy::DeviceMonitor`：把设备列表变化转换成插入、授权、就绪、拔出事件。

```cpp
#include <qtscrcpy/devicemonitor.h>
#include <qtscrcpy/scrcpy.h>

QtScrcpy::Adb adb;
QtScrcpy::DeviceMonitor monitor(&adb);
QtScrcpy::Scrcpy scrcpy;
scrcpy.setAdbExecutable(adb.executable());

QObject::connect(&monitor, &QtScrcpy::DeviceMonitor::deviceReady,
                 [&](const QtScrcpy::AdbDevice &device) {
    if (!scrcpy.start(QtScrcpy::ScrcpyOptions::forScreen(device.serial)))
        return;
});
monitor.start(); // 程序结束前记得 monitor.stop()
```

`ScrcpyOptions::toArguments()` 会按检测到的 scrcpy 版本生成参数（例如 1.x 不传 `--no-audio`）。

## 打包 scrcpy 和 adb

`cmake/QtScrcpyBundle.cmake` 负责下载、校验、解压和复制，默认版本 3.3.3（也是库内置 scrcpy 客户端所对应的协议版本，不要随意换）。相关变量：

| 变量 | 作用 |
| ---- | ---- |
| `QTSCRCPY_BUNDLE_SCRCPY=AUTO\|ON\|OFF` | 顶层构建默认 `AUTO`；`ON` 必须成功；`OFF` 不打包 |
| `QTSCRCPY_BUNDLE_ADB=ON\|OFF` | 是否把 adb 一起打包 |
| `QTSCRCPY_SCRCPY_MIRROR=<url>` | 从镜像下载，仍会校验 SHA-256 |
| `QTSCRCPY_SCRCPY_ARCHIVE=<file>` | 使用已下载的离线包 |
| `QTSCRCPY_DOWNLOAD_DIR=<dir>` | 多个构建目录共用下载缓存 |
| `QTSCRCPY_SCRCPY_VERSION` / `QTSCRCPY_SCRCPY_SHA256` | 换版本时必须同时提供校验值 |

`cmake --install build --prefix <dir>` 会把 GUI、随包程序、第三方声明和许可证一起安装。

## 目录结构

```
src/qtscrcpy/     库（adb 设备跟踪 + scrcpy 进程管理）
examples/gui/     极简 GUI
tests/            单元测试与兼容性测试
cmake/            下载/打包 scrcpy 与 adb
licenses/         第三方许可证原文
```

## 界面

底部只有一个来源下拉框、一个「开始投屏 / 停止投屏」按钮和一个「刷新」按钮；状态栏显示工具与设备概况
（`adb xxx · hdc：已找到 · scrcpy-server：已找到 · 设备 1 台`）。

- **刷新**：立刻重新读取 adb 设备列表和 hdc 目标列表。如果刷新后列表还是空的，会弹出诊断信息，
  包括用的是哪个 adb、最近一次 adb 报错、以及常见原因（USB 调试未开、数据线只充电、多个 adb 版本冲突、缺 scrcpy-server / hdc）。
- **开始投屏**：只要列表里有选中项就能点；缺少条件时（解码库、adb/hdc、scrcpy-server、设备未授权）
  会弹出具体原因，而不是毫无反应。

## adb 响应超时怎么办

程序不会因为一次 adb 超时就打断你：

- 设备列表的读取超时按 60 秒算（不是普通命令的 15 秒），因为首次启动 adb 服务端可能很慢；
  `adb start-server` 按 120 秒算。
- 超时会**安静地重试**（2 秒、4 秒…最多 10 秒一次），连续失败 3 次才提示一次。
- 连续失败到阈值时会自动执行一次 `adb kill-server` 再重启（最多两次），这正是手动排障会做的事。
- 想给更慢的机器留余量：设环境变量 `QTSCRCPY_ADB_TIMEOUT_MS=120000`（毫秒）。
- 提示仍然反复出现时，通常是 adb 被安全软件拦截、或系统里同时存在多个版本的 adb（例如 DevEco 自带的）；
  让程序用随包的那个 adb（不要设 `ADB` 环境变量），或手动跑一次 `adb kill-server`。

## 已知限制

- 鸿蒙配套应用没有在真机上验证过（见上一节）；电脑端的协议解析、H.264 解码、hdc 调用流程用真实 H.264 样本和模拟手机做过端到端测试。
- 刻意不传声音、不允许操作手机；摄像头不能选具体 id，只能按朝向选。
- 视频显示在 scrcpy 自己的原生窗口里，不嵌入 Qt 窗口。
- 安卓设备需要 adb 和 `scrcpy-server`：要么让构建过程打包，要么在系统里安装好；`scrcpy-server` 的版本必须和库内置的客户端一致（3.3.3），否则服务端会拒绝连接。

## 许可证

本仓库代码使用 MIT 许可证，见 [LICENSE](LICENSE)。随构建产物分发的 scrcpy、adb 等第三方组件见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
