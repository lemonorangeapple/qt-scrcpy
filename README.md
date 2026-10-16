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

需要 `adb` 和 `scrcpy` 在 `PATH` 里(或见下面的环境变量)。

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build          # 单元测试(不需要真机)
./build/examples/gui/qtscrcpy-gui
```

选项:`-DQTSCRCPY_BUILD_EXAMPLES=OFF`、`-DQTSCRCPY_BUILD_TESTS=OFF`、`-DBUILD_SHARED_LIBS=ON`。

作为子项目使用:

```cmake
add_subdirectory(qt-scrcpy)
target_link_libraries(myapp PRIVATE QtScrcpy::qtscrcpy)
```

## 查找可执行文件

| 程序   | 查找顺序 |
| ------ | -------- |
| adb    | 环境变量 `ADB` → 程序所在目录 → `ANDROID_SDK_ROOT`/`ANDROID_HOME` 及默认 SDK 目录下的 `platform-tools` → `PATH` |
| scrcpy | 环境变量 `SCRCPY_EXECUTABLE` → 程序所在目录 → `PATH` |

也可以随时用 `setExecutable()` 指定。`Scrcpy` 会把所用的 adb 通过 `$ADB` 传给 scrcpy(见 `setAdbExecutable()`),保证两者用同一个 adb。

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
src/qtscrcpy/    库:Adb、DeviceMonitor、Scrcpy、ScrcpyOptions、AdbDevice、CommandResult
examples/gui/    Qt Widgets 示例(中文界面):授权提示、屏幕/摄像头投屏选择、无线连接、截图、安装 APK、shell
tests/           QtTest 测试;tests/fake/ 里是假的 adb / scrcpy 脚本
```

## 已知限制

- scrcpy 的画面显示在 **scrcpy 自己的 SDL 窗口**里,本库管理的是进程与生命周期,并不把视频嵌入 Qt 控件。要内嵌需要自己实现 scrcpy 协议和视频解码,不在本库范围内。
- `Adb::inputText()` 受 Android `input text` 限制,只支持 ASCII。
- 进程类测试(`tst_adb`、`tst_scrcpy`、`tst_monitor`、`tst_compat`)依赖 shell 脚本,只在类 Unix 系统上构建;解析类测试在所有平台运行。
- **没有用真机验证过**。授权弹窗、摄像头采集都依赖真实手机;授权流程的测试是用假 adb 模拟 `unauthorized → device → 拔出` 的状态序列,摄像头参数则是用真实 scrcpy 3.3.3 验证"参数被接受",没有真正采集过画面。

### 对另一个 scrcpy 版本跑兼容性测试

```sh
SCRCPY_EXECUTABLE=/path/to/scrcpy ctest --test-dir build -R compat --output-on-failure
```
