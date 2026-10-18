import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { hilog } from '@kit.PerformanceAnalysisKit';

// The computer starts this ability with `aa start ... --ps source <screen|back|front>`;
// the page reads the choice from AppStorage and starts streaming by itself.
export default class EntryAbility extends UIAbility {
  onCreate(want: Want, _launchParam: AbilityConstant.LaunchParam): void {
    this.applySource(want);
  }

  onNewWant(want: Want, _launchParam: AbilityConstant.LaunchParam): void {
    this.applySource(want);
    AppStorage.setOrCreate<number>('restartToken', Date.now());
  }

  private applySource(want: Want): void {
    const value = want.parameters?.['source'];
    let source = 0;
    if (value === 'back') {
      source = 1;
    } else if (value === 'front') {
      source = 2;
    }
    AppStorage.setOrCreate<number>('source', source);
    hilog.info(0x3200, 'qtscrcpy', 'source=%{public}d', source);
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(0x3200, 'qtscrcpy', 'loadContent failed: %{public}s', JSON.stringify(err));
      }
    });
  }
}
