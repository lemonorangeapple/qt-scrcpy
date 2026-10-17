# qt-scrcpy

用 C++/Qt 封装 [scrcpy](https://github.com/Genymobile/scrcpy) 和 `adb` 的小型库,另附一个 Qt Widgets 示例程序。

- 只依赖 **Qt6 Core** 和 C++17,不链接 Qt GUI,可以直接用在命令行/后台程序里(CMake 要求 Qt ≥ 6.2,实际在 6.4 上开发和测试)。
- 通过调用本机已安装的 `adb` / `scrcpy` 可执行文件工作(不重新实现协议),所以行为和你在终端里用的完全一致。
- 兼容 scrcpy 1.x 与 2.x/3.x:启动前会检测版本,自动处理参数写法的差异(如 `--no-audio` 仅 2.0+ 有、`--bit-rate` 在 3.0 被删除)。兼容性测试 `tst_compat` 会用**真实的 scrcpy** 检查本库生成的参数是否被接受,已在 scrcpy 1.25 和 3.3.3 上跑过;2.x 没有真实二进制验证,选项名只对照了各版本源码。

## 插线 → 授权 → 选择投屏方式

示例程序(以及 `DeviceMonitor`)实现的完整流程:

1. **插上数据线**:程序自动跟踪 adb 设备,界面提示"请在手机上点击允许 USB 调试",同时闪烁任务栏。此时设备是 `unauthorized` 状态,"开始投屏"不可用。
2. **在手机上点"允许"**:设备变为可用,电脑上弹出选择窗口。
3. **在电脑上选择**:**屏幕投屏**(镜像并可操作手机),或 **摄像头投屏**(把后置/前置/外接摄像头的画面显示到电脑)。

摄像头投屏的限制(来自 scrcpy 本身):需要 **scrcpy ≥ 2.2** 和 **Android 12+**,不满足时选择窗口里该按钮会置灰并说明原因;摄像头模式下不能操作手机;默认传输的是手机**麦克风**的声音(可勾选"不传输声音")。

## 构建

需要 CMake ≥ 3.16 和 Qt 6。默认**不需要**预先安装 scrcpy 和 adb:构建时会自动下载并带上它们(见下一节)。第一次配置需要能访问 GitHub(或自己提供镜像/离线包);用 `-DQTSCRCPY_BUNDLE_SCRCPY=OFF` 可以关闭,这时需要 `adb` 和 `scrcpy` 在 `PATH` 里(或见"查找可执行文件")。

```sh
cmake -S . -B build -G Ninja
cmake --build build
(cd build && ctest)             # 测试(不需要真机);`ctest --test-dir build` 需要 CMake ≥ 3.20
./build/examples/gui/qtscrcpy-gui
```

选项:`-DQTSCRCPY_BUILD_EXAMPLES=OFF`、`-DQTSCRCPY_BUILD_TESTS=OFF`、`-DBUILD_SHARED_LIBS=ON`。

作为子项目使用:

```cmake
add_subdirectory(qt-scrcpy)
target_link_libraries(myapp PRIVATE QtScrcpy::qtscrcpy)
```

## 构建时自动包含 scrcpy 和 adb

配置阶段,CMake 会下载 **scrcpy 官方发布包**(固定为 3.3.3;包里自带 `adb` 和 `scrcpy-server`),用写死在 `cmake/QtScrcpyBundle.cmake` 里的 SHA-256 校验,只留下程序需要的文件(`scrcpy`、`adb`、`scrcpy-server`、`icon.png`,Windows 另有 `.exe` 和 `.dll`;不含手册页和 `.bat`/`.vbs`)并缓存。目标程序每次**链接之后**会把这些文件复制到它旁边(只复制有变化的文件,写入是原子的,所以几个目标共用一个输出目录并行构建也没问题;只改了 CMake 选项而没有重新链接时不会重新复制),`cmake --install` 也会一并安装。库会在程序所在目录里找 `adb` / `scrcpy`(环境变量 `ADB` / `SCRCPY_EXECUTABLE` 优先),所以这样构建出来的程序在没装 scrcpy 和 adb 的机器上也能用。

```sh
cmake -S . -B build && cmake --build build
ls build/examples/gui        # qtscrcpy-gui  adb  scrcpy  scrcpy-server  icon.png
cmake --install build --prefix /opt/qt-scrcpy
```

**哪些构建会带上:** 示例程序 `qtscrcpy-gui`,以及你自己用 `qtscrcpy_bundle_runtime()` 指定的目标。`-DQTSCRCPY_BUILD_EXAMPLES=OFF` 且没有别的目标时不会下载任何东西。`cmake --install` 只安装示例程序、随它的 scrcpy/adb、声明文件(动态库构建时还有 `libqtscrcpy.so`),**不**安装头文件或 CMake 配置包:把库用在自己项目里请用 `add_subdirectory`。

**第一次配置会下载什么:** Linux x86_64 的包约 12.9 MB(解压后约 33 MB),Windows 64 位约 7.3 MB。缓存默认在各自的构建目录里(`<build>/qtscrcpy-downloads`),所以每个全新的构建目录都会重新下载;用 `QTSCRCPY_DOWNLOAD_DIR` 让多个构建目录共用同一份缓存(并发配置时会互相等待,不会冲突)。连接卡住超过 60 秒无数据会放弃并报错。缓存已经完整时配置只读不写(只读的预填充缓存目录也能用)。**没有网络时配置会失败**,并提示三种办法:镜像、离线包、`OFF`。

| 选项 | 作用 |
| ---- | ---- |
| `-DQTSCRCPY_BUNDLE_SCRCPY=AUTO\|ON\|OFF` | `ON`:一定要带上,做不到就报错。`AUTO`(qt-scrcpy 作为顶层项目时的默认值):scrcpy 发布了对应平台的包就带上,没有(如 Linux aarch64)就给出警告并继续、不带。`OFF`:不下载,使用系统里安装的。作为子项目时默认 `OFF` |
| `-DQTSCRCPY_BUNDLE_ADB=ON\|OFF` | 默认 `ON`。`OFF`:只带上 scrcpy 和 scrcpy-server,**不带 adb**(Windows 上也不带 `AdbWinApi.dll`、`AdbWinUsbApi.dll`),使用者的机器上要自己装 adb(见"查找可执行文件",库会通过 `ADB` 环境变量把找到的 adb 交给 scrcpy)。想分发而不想分发 adb 时用这个。切换后会使用单独的解压缓存(目录名带 `-noadb`),不需要清缓存 |
| `-DQTSCRCPY_SCRCPY_MIRROR=<url>` | 从镜像下载(GitHub 访问慢时用),`http`、`https` 或 `file` 地址;镜像上的文件名要和官方一致:`scrcpy-<平台>-v<版本>.tar.gz`(Windows 为 `.zip`)。校验值仍然生效,镜像无法换成别的内容(除非你同时改了下面的 SHA256) |
| `-DQTSCRCPY_SCRCPY_ARCHIVE=<file>` | 用已经下载好的包(离线构建),同样要通过校验。请给绝对路径:这是 `FILEPATH` 类型的缓存变量,CMake 会把命令行里的相对路径当作相对于执行 `cmake` 时的当前目录 |
| `-DQTSCRCPY_DOWNLOAD_DIR=<dir>` | 下载与解压缓存目录 |
| `-DQTSCRCPY_SCRCPY_VERSION=<v>` | 换用别的 scrcpy 版本;没有内置校验值的版本必须同时给出 `QTSCRCPY_SCRCPY_SHA256` |
| `-DQTSCRCPY_SCRCPY_SHA256=<hash>` | 包的 SHA-256(64 位十六进制)。**设置后会替换内置校验值,即使是 3.3.3**;这些选项都是缓存变量,会一直保留到你清除它们 |
| `-DQTSCRCPY_SCRCPY_PLATFORM=<p>` | 手动指定平台,默认按目标系统检测。取值:`linux-x86_64`、`win64`、`win32`、`macos-aarch64`、`macos-x86_64`;同时给了 `ARCHIVE` 和 `SHA256` 时可以用别的名字(只能含字母、数字、`_`、`.`、`-`);可执行文件的 `.exe` 后缀按目标系统是否为 Windows 或名字是否以 `win` 开头判断 |

平台检测:Windows 按编译器指针宽度区分 32/64 位;Windows on ARM 没有原生包,可以试试 `-DQTSCRCPY_SCRCPY_PLATFORM=win64`(在提供 x64 仿真的 Windows 版本上也许能运行,没有验证);macOS 的 `CMAKE_OSX_ARCHITECTURES` 写了多个架构(通用二进制)时只会带上一种架构的包,并给出警告。下载失败、校验不符、压缩包里有可疑条目,都会让配置直接失败并说明原因,不会悄悄退回成"没带上"。

在你自己的 CMake 项目里:

```cmake
set(QTSCRCPY_BUNDLE_SCRCPY ON)          # 作为子项目时默认是 OFF
add_subdirectory(qt-scrcpy)
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE QtScrcpy::qtscrcpy)
qtscrcpy_bundle_runtime(myapp INSTALL_DESTINATION libexec/myapp)  # 链接后复制到 myapp 旁边,并加入 install
install(TARGETS myapp RUNTIME DESTINATION libexec/myapp)           # 程序和 scrcpy/adb 装进同一个目录
```

`INSTALL_DESTINATION` 必须是**程序自己的安装目录**(默认 `bin`),因为 scrcpy/adb 要和可执行文件放在一起;`NO_INSTALL` 表示只复制、不安装。不要把 `adb`、`scrcpy` 这类通用名字装进共享的 `bin`(默认前缀 `/usr/local` 的 `bin` 里可能有用户自己的),所以上面的配方用的是私有目录。示例程序也是这样:Unix 上(Linux 和 macOS)装进 `libexec/qt-scrcpy/`;Linux 上再在 `bin/` 里放一个指向它的启动器链接(Qt 会解析链接,仍然能找到旁边的文件;macOS 上没有验证这一点,所以没有链接,直接运行 `libexec/qt-scrcpy/qtscrcpy-gui`)。Windows 的默认安装目录是 `<Program Files>/<项目名>`,本来就是应用私有的。

注意:

- **平台验证情况:** 只有 Linux x86_64 上真正构建、运行、测试过。Windows 和 macOS 的包只验证了下载、校验、解压和复制这几步(在 Linux 主机上用对应的包做的),没有在 Windows/macOS 上配置、构建或运行过;它们的 `$ADB`、DLL、`windeployqt` 等相关说法来自读源码和压缩包清单。
- 包里的 adb 是 platform-tools **36.0.0**。实测它和系统里的 34.0.4 共用同一个 adb server 时互不干扰(两者的 adb 协议版本都是 1.0.41;这依赖协议版本相同,没有测试过更旧的 adb)。
- **Linux 运行要求:** scrcpy 已经把 SDL、FFmpeg、libusb 静态链接进去,但需要系统有 **glibc ≥ 2.35**(实测符号版本)和 `libudev.so.1`,显示窗口还需要 X11/Wayland 等图形库;随包的 adb 另需 `libgcc_s.so.1`。比 Ubuntu 22.04 更老的系统(glibc 2.31 等)上构建没问题,但包里的 scrcpy 起不来,而且程序旁边的 scrcpy 会优先于 `PATH` 里能用的那个,`tst_bundled` 和 `compat_bundled` 也会失败:这样的系统请用 `-DQTSCRCPY_BUNDLE_SCRCPY=OFF`(或设置 `SCRCPY_EXECUTABLE`)。
- Windows 包会把 `adb.exe`、`scrcpy.exe` 和所需 DLL 一起复制过来;Qt 自己的运行库不在其中,请照常用 `windeployqt` / `macdeployqt` 等处理。
- 库在 `QCoreApplication` 创建之前找不到程序所在目录里的文件(它要靠 `applicationDirPath()`),所以不要在静态/全局对象里构造 `Adb` / `Scrcpy`。
- **分发前请先看 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。** 仓库本身不含任何二进制,是每个构建者自己下载;但把构建出来的程序分发给别人,就等于分发了它们:scrcpy 是 Apache-2.0,FFmpeg、libusb 是 LGPL,而 **adb 来自 Android SDK Platform-Tools:它的下载页展示的是 Google 的 Android SDK 许可协议(其中有对再分发的限制),但 adb 本身适用哪个许可证、是否允许这样捆绑分发,没有查证清楚**。不想分发 adb 就用 `-DQTSCRCPY_BUNDLE_ADB=OFF`(只带 scrcpy,使用者自己装 adb;整个不带则用 `-DQTSCRCPY_BUNDLE_SCRCPY=OFF`)。配置时会打印一行提示说明包里是否含 adb。是否可以分发 adb 由你自己判断,这里没有替你做结论。`cmake --install` 会把声明文件和部分许可证文本一起装到 `share/doc/`;直接拷贝构建目录得到的只有二进制,不带任何声明。

## 查找可执行文件

| 程序   | 查找顺序 |
| ------ | -------- |
| adb    | 环境变量 `ADB`(要能解析到一个程序,否则被忽略)→ 程序所在目录(内置的就在这里)→ `ANDROID_SDK_ROOT`/`ANDROID_HOME` 及默认 SDK 目录下的 `platform-tools` → `PATH` |
| scrcpy | 环境变量 `SCRCPY_EXECUTABLE` → 程序所在目录(内置的就在这里)→ `PATH` |

也可以随时用 `setExecutable()` 指定。`Scrcpy` 启动 scrcpy 时,通过 `$ADB` 告诉它用哪个 adb,优先级是:显式的 `setAdbExecutable()` > 环境里已有且能解析的 `$ADB`(不动它)> 与 scrcpy 真实文件同目录的 adb(符号链接会被解析;发布包里两者放在一起。Windows 上由 scrcpy 自己找,不导出 `$ADB`,因为 scrcpy 拼命令行时不给路径加引号,装在 `Program Files` 这种带空格的目录里会出错)> `Adb::findExecutable()` 会用的 adb。最后一项是因为发布包里的 scrcpy 在 Linux/macOS 上**不会**到 `PATH` 里找 adb。

## 用法

### Adb

```cpp
#include <qtscrcpy/adb.h>
using namespace QtScrcpy;

Adb adb;

// 阻塞式调用:返回 CommandResult,ok() 为 false 时用 failureReason() 取原因
for (const AdbDevice &d : adb.devices())
    qInfo() << d.displayName() << d.serial << d.stateText;

adb.connectDevice("192.168.1.5:5555");        // 无线连接
adb.install(serial, "app.apk");
adb.shell(serial, "getprop ro.product.model");
QByteArray png = adb.screenshot(serial);      // PNG 数据
adb.push(serial, "local.txt", "/sdcard/local.txt");

// 非阻塞(GUI 线程里用这些):回调在事件循环中执行
adb.installAsync(serial, "app.apk", [](const CommandResult &r) {
    qInfo() << (r.ok() ? "installed" : r.failureReason());
});

// 设备热插拔:基于 `adb track-devices`,adb server 退出后会自动重启跟踪
QObject::connect(&adb, &Adb::devicesChanged, [](const QList<AdbDevice> &devices) { /* ... */ });
adb.startTracking();
```

已封装:设备列表/跟踪、Android 版本、`connect`/`disconnect`/`pair`/`tcpip`/`usb`、设备 IP、`shell`、`getprop`、`push`/`pull`、`install`/`uninstall`、包列表、截图、按键/点击/滑动/输入文字、`forward`/`reverse`。其它命令用 `run()` / `runAsync()` 直接传参数即可。

`connect`、`pair`、`install`、`uninstall` 这几个命令在失败时 adb 也可能返回退出码 0,封装已经根据输出内容修正,所以 `ok()` 可以直接信赖。

### DeviceMonitor:插拔与授权事件

```cpp
#include <qtscrcpy/devicemonitor.h>

DeviceMonitor monitor(&adb);
QObject::connect(&monitor, &DeviceMonitor::authorizationRequired, [](const AdbDevice &d) {
    // 手机正在显示"允许 USB 调试?":提示用户去点"允许"
});
QObject::connect(&monitor, &DeviceMonitor::deviceReady, [](const AdbDevice &d) {
    // 设备可用了(新插入且已授权,或刚刚点了"允许"):让用户选屏幕/摄像头
});
QObject::connect(&monitor, &DeviceMonitor::deviceDetached, [](const AdbDevice &d) { /* 拔出 */ });
monitor.start();
```

事件顺序:`deviceAttached → authorizationRequired → deviceStateChanged → deviceReady`;之前已授权过的手机直接是 `deviceAttached → deviceReady`。`DeviceMonitor::diff()` 是纯函数,可以单独使用和测试。

### Scrcpy


```cpp
#include <qtscrcpy/scrcpy.h>
using namespace QtScrcpy;

Scrcpy scrcpy;                                  // 每台设备用一个实例
scrcpy.setAdbExecutable(adb.executable());

ScrcpyOptions options;
options.serial = serial;
options.maxSize = 1280;
options.bitRate = 8'000'000;
options.showTouches = true;
options.windowTitle = "My phone";

QObject::connect(&scrcpy, &Scrcpy::outputReceived, [](const QString &line, bool isError) { /* 日志 */ });
QObject::connect(&scrcpy, &Scrcpy::finished, [](int exitCode, bool crashed) { /* 窗口被关闭/设备断开 */ });
scrcpy.start(options);
// ...
scrcpy.stop();                                  // 先请求退出,超时后强制结束
```

```cpp
// 摄像头投屏
auto camera = ScrcpyOptions::forCamera(serial, CameraFacing::Front);   // Back / Front / External / Any
camera.cameraFps = 30;
camera.noAudio = true;                  // 不传输手机麦克风
if (scrcpy.start(camera)) { /* ... */ } // scrcpy 版本过低等情况返回 false 并发出 errorOccurred
```

`ScrcpyOptions::validate()` 会拒绝 scrcpy 本身不接受的组合(摄像头模式但 scrcpy < 2.2、`cameraId` 与 `cameraFacing` 同时给出、`cameraSize` 与 `maxSize`/`cameraAspectRatio` 同时给出),`start()` 会先调用它。需要设备控制的选项(显示触摸点、保持唤醒、熄屏、关机)在摄像头模式和"仅查看"(`noControl`)下不会传给 scrcpy,否则 scrcpy 会直接报错退出。可以用 `Adb::androidSdkVersion()` 判断设备是否 ≥ Android 12(API 31)。

不在 `ScrcpyOptions` 里的参数可以放进 `extraArguments`,原样传给 scrcpy。

## 目录结构

```
cmake/           QtScrcpyBundle.cmake:构建时下载并带上 scrcpy 和 adb
licenses/        随安装附带的许可证文本(THIRD_PARTY_NOTICES.md 在仓库根目录)
src/qtscrcpy/    库:Adb、DeviceMonitor、Scrcpy、ScrcpyOptions、AdbDevice、CommandResult
examples/gui/    Qt Widgets 示例(中文界面):授权提示、屏幕/摄像头投屏选择、无线连接、截图、安装 APK、shell
tests/           QtTest 测试;tests/fake/ 里是假的 adb / scrcpy 脚本
```

## 已知限制

- scrcpy 的画面显示在 **scrcpy 自己的 SDL 窗口**里,本库管理的是进程与生命周期,并不把视频嵌入 Qt 控件。要内嵌需要自己实现 scrcpy 协议和视频解码,不在本库范围内。
- `Adb::inputText()` 受 Android `input text` 限制,只支持 ASCII。
- 进程类测试(`tst_adb`、`tst_scrcpy`、`tst_monitor`、`tst_compat`、`tst_locate`)依赖 shell 脚本或可执行文件,只在类 Unix 系统上构建;解析类测试在所有平台运行。需要可执行临时文件的用例在 `TMPDIR` 挂在 `noexec` 上时会跳过(并说明原因)。
- 与"内置 scrcpy/adb"有关的测试:`tst_bundled`(旁边的 scrcpy/adb 能被找到并运行)和 `compat_bundled`(内置的 scrcpy 接受本库生成的参数)只在同时构建示例程序、并且确实带上了包时才有;`bundle_module` 用假的小压缩包测试 `cmake/QtScrcpyBundle.cmake` 本身,不需要网络:校验与镜像、各种选项和错误提示、缓存的完整性与并发配置、含空格/中文/方括号/`$` 的目录(包括 C 区域设置)、多个目标并行复制,以及 12 种恶意压缩包(需要 python3)。
- **没有用真机验证过**。授权弹窗、摄像头采集都依赖真实手机;授权流程的测试是用假 adb 模拟 `unauthorized → device → 拔出` 的状态序列,摄像头参数则是用真实 scrcpy 3.3.3 验证"参数被接受",没有真正采集过画面。

### 对另一个 scrcpy 版本跑兼容性测试

```sh
(cd build && SCRCPY_EXECUTABLE=/path/to/scrcpy ctest -R '^tst_compat$' --output-on-failure)
```
