# WhateverXe

[English](README.md)

WhateverXeは、一部のIntel Xe-LP内蔵GPU向けにAppleのグラフィックスドライバを適応させるLiluプラグインです。WhateverGreenを基に開発しています。

本プロジェクトは実験段階であり、[OpenCore Legacy Patcherのfork](https://github.com/b00t0x/OpenCore-Legacy-Patcher)が提供するXe root patchを必要とします。

## 対応範囲

現在の対象は **macOS Big Sur build 20G1443** です。Tiger Lakeは **macOS Tahoe 26.7.1 build 25G241**（このbuildのみ）でも試験的に動作します。25G241でMetalを使うには、Big SurのMetalコンパイラ（3902）が別途必要で、root patchはまだこれを提供しません。

| GPUファミリー | バックエンド実装があるdevice ID |
| --- | --- |
| Tiger Lake | `9A40`、`9A49`、`9A78` |
| Alder Lake-N / Twin Lake | `46D0`–`46D4` |

対応状況は機種やコネクタ構成によって異なります。この表はバックエンドの実装範囲を示すもので、すべての機種での動作を保証するものではありません。詳細な選択条件と上書き設定は[デバイス選択](docs/device-selection.md)を参照してください。

Tiger LakeとAlder Lake-Nで、外部2画面の1920×1080・60Hz出力、Metal/OpenGL、ハードウェアHEVC、画面の消灯／復帰を確認しています。Tiger LakeではハードウェアH.264のエンコード／デコードと、バックライト制御を含む内蔵eDPパネルも確認しています。3画面同時出力、システムスリープ、より高い解像度や他のコネクタ構成は追加検証が必要です。USB-Cへの対応は下記ロードマップに含まれます。

[Releases](https://github.com/b00t0x/WhateverXe/releases)から **RELEASE** ZIPをダウンロードしてください。DEBUG ZIPは問題調査用です。

## 必須構成

| コンポーネント | 役割 |
| --- | --- |
| Lilu | WhateverXeの動作に必要なランタイム |
| WhateverXe.kext | OpenCoreでLiluより後に読み込む |
| [OpenCore Legacy Patcherのfork](https://github.com/b00t0x/OpenCore-Legacy-Patcher) | Xe root patchを適用してグラフィックス構成を導入する |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/OpenCore-Legacy-Patcher/blob/mod/payloads/Kexts/Acidanthera/AMFIPass-v1.4.1-Mod-RELEASE.zip) | 改変されたroot patchのグラフィックス構成に必要。OpenCoreで導入する |

OCLP forkは[Releasesページ](https://github.com/b00t0x/OpenCore-Legacy-Patcher/releases)からダウンロードしてください。rootの構成要素の導入とkernel collectionの更新はOCLPが行います。

リンク先の **Mod版** アーカイブから`AMFIPass.kext`を展開し、`EFI/OC/Kexts`へ配置して、`Kernel > Add`で有効にしてください。既存のAMFIPassがある場合はこのMod版へ置き換え、両方を同時に読み込まないでください。

複数GPUの構成を含め、WhateverGreenとWhateverXeは同時に有効にしないでください。

## 設定

GPUはPCI device IDから自動選択します。対応範囲内ではファームウェアのVBTから配線を読み取るため、固定HDMI／固定DisplayPort構成は通常、配線propertyの手動指定が不要です。高度な設定としての`device-id`上書きは[デバイス選択](docs/device-selection.md)で説明しています。

### DVMT

BIOS/UEFIで変更できる場合は、**DVMT Pre-Allocatedを96MBに設定することを推奨します**。

**60MBから変更できない場合**は、`boot-args`に`-igfxdvmt`を追加し、OpenCoreの`DeviceProperties > Add`にあるiGPUの項目（通常は`PciRoot(0x0)/Pci(0x2,0x0)`）へ以下の2つを設定してください。

| Property | 型 | 値（16進数） |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

Data値はリトルエンディアンです。`-igfxdvmt`は容量計算を補正し、DevicePropertiesは表示用メモリ領域を60MB内に収めます。ファームウェアが確保したメモリを増やす設定ではありません。96MBの場合、これらのDVMT用設定は不要です。

### 必須ブート引数

OpenCoreの`boot-args`に以下を追加してください。

```text
-allow3d -disablegfxfirmware
```

| 引数 | 用途 |
| --- | --- |
| `-allow3d` | Appleドライバの3D処理を有効にする。 |
| `-disablegfxfirmware` | 現在のバックエンドで、Appleドライバのグラフィックスファームウェア読み込みを無効にする。 |
| `-wex-nopanel` | 内蔵画面のないTiger Lake機で追加する。現在のAlder Lake-N / Twin Lakeは外部画面用の構成を自動生成するため、この引数を使用しない。 |

Alder Lake-N / Twin Lakeで世代選択や個別機能の有効化引数を追加する必要はありません。

### 任意のブート引数

| 引数 | 用途 |
| --- | --- |
| `gfxrst=` | 起動時framebufferの引継ぎ方式。0：自動（既定）、1：reset、2：copy、3：clear、4：無効。 |
| `-wexdbg` | debug buildでログを有効にする。 |
| `-wexoff` | WhateverXeを無効にする。 |
| `-wexbeta` | プラグインのOSバージョンチェックを回避する。kernel／driverの一致条件は維持する。 |
| `-wex-tgl-off` | Tiger Lakeの処理を無効にする。 |
| `-wex-tahoe-off` | macOS 26.7.1（25G241）向けの処理を無効にする。 |
| `-wex-accel-off` | Tiger Lakeのframebufferは維持し、acceleratorを割り当てない。 |
| `-wex-edp-trans-off` | Tiger LakeのeDP transcoder補正を無効にする。 |
| `-wex-edphpd-off` | Tiger LakeでeDP（DDI A）のhotplugを有効にしない。 |
| `-wex-blfreq-off` | ファームウェアの値ではなく、ドライバ内蔵のバックライトPWM周期を使う。 |
| `-wex-bl64-off` | ドライバ本来の32-bitバックライトduty計算を使う。 |
| `-wex-lt-off` | 内蔵パネルのmode set失敗時にディスプレイレジスタを記録しない。 |

## ロードマップ

### Planned

- OCLPの機能を利用したmacOS Monterey〜Tahoe対応
- Alder Lake／Raptor LakeのS/H/P/U対応
- USB-C画面出力対応
- Alder Lake-N / Twin Lakeの内蔵ディスプレイ対応

### Not Planned

- Xe-LPG対応
- Xe2以降のアーキテクチャへの対応

## ビルドとテスト

macOS、Xcode command-line tools、Python 3.9以降、MacKernelSDK、Liluの開発用ヘッダ／ライブラリを使用します。`MacKernelSDK/`と`Lilu.kext/`をリポジトリ直下に配置するか、`--sdk`と`--lilu`でパスを指定してください。

```sh
python3 tools/build.py --release
python3 tools/test.py
```

kextとソースのmanifestは`build/`に出力されます。任意のdonor再生試験には`--donor /path/to/AppleIntelTGLGraphicsFramebuffer`と`--accelerator /path/to/AppleIntelTGLGraphics`を指定します。各試験が受け付ける正確なバイナリが必要です。CPUテストだけで実GPUの互換性が確認できるわけではありません。ビルドにWhateverGreenのソースは不要です。

## ライセンスとクレジット

WhateverXeはWhateverGreenの著作権表示とBSD-3-Clauseライセンスを継承しています。[LICENSE.txt](LICENSE.txt)を参照してください。LiluとMacKernelSDKは別の依存コンポーネントです。
