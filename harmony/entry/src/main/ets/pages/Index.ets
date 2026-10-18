import streamer from 'libstreamer.so';
import { abilityAccessCtrl, common, Permissions } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { display } from '@kit.ArkUI';

const PORT = 27183;
const SOURCE_NAMES: string[] = ['屏幕', '后置摄像头', '前置摄像头'];

@Entry
@Component
struct Index {
  @StorageLink('source') source: number = 0;
  @StorageProp('restartToken') @Watch('onRestart') restartToken: number = 0;
  @State message: string = '正在准备…';
  @State running: boolean = false;

  aboutToAppear(): void {
    this.begin();
  }

  onRestart(): void {
    this.begin(); // the computer started the ability again, possibly with another source
  }

  aboutToDisappear(): void {
    streamer.stop();
  }

  private async begin(): Promise<void> {
    streamer.stop();
    this.running = false;

    if (this.source !== 0) {
      const granted = await this.requestCamera();
      if (!granted) {
        this.message = '没有相机权限，无法进行摄像头投屏。请在系统设置里允许后重新启动。';
        return;
      }
    }

    const size = this.captureSize();
    this.message = this.source === 0 ? '请在弹出的窗口里点“允许”开始录屏…' : '正在打开摄像头…';
    // Starting is synchronous in the native code (it shows the system prompt for the screen).
    const error = streamer.start(this.source, size[0], size[1], 30, 6000000, PORT);
    if (error.length > 0) {
      this.message = '启动失败：' + error;
      return;
    }
    this.running = true;
    this.message = '正在推送' + SOURCE_NAMES[this.source] + '，请在电脑上点“开始投屏”。';
  }

  private captureSize(): number[] {
    if (this.source !== 0) {
      return [1280, 720]; // the camera adjusts this to a size it supports
    }
    // Half the physical resolution keeps the stream light; both sides stay even for the encoder.
    try {
      const d = display.getDefaultDisplaySync();
      const w = Math.max(2, Math.floor(d.width / 2 / 2) * 2);
      const h = Math.max(2, Math.floor(d.height / 2 / 2) * 2);
      return [w, h];
    } catch (e) {
      return [720, 1280];
    }
  }

  private async requestCamera(): Promise<boolean> {
    const permissions: Permissions[] = ['ohos.permission.CAMERA'];
    try {
      const manager = abilityAccessCtrl.createAtManager();
      const context = getContext(this) as common.UIAbilityContext;
      const result = await manager.requestPermissionsFromUser(context, permissions);
      return result.authResults.every((value) => value === 0);
    } catch (e) {
      const err = e as BusinessError;
      this.message = '请求权限失败：' + err.message;
      return false;
    }
  }

  build() {
    Column({ space: 20 }) {
      Text('qt-scrcpy 推流')
        .fontSize(26)
        .fontWeight(FontWeight.Bold)
      Text(this.message)
        .fontSize(18)
        .textAlign(TextAlign.Center)
      Text('来源：' + SOURCE_NAMES[this.source])
        .fontSize(16)
        .fontColor('#666666')
      Button(this.running ? '停止' : '重新开始')
        .onClick(() => {
          if (this.running) {
            streamer.stop();
            this.running = false;
            this.message = '已停止。';
          } else {
            this.begin();
          }
        })
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .padding(24)
  }
}
