# AviUtl2 FreeRenderFPS

[简体中文](README.zh-CN.md) | **日本語** | [English](README.en.md)

FreeRenderFPS は x264guiEx をベースにした AviUtl2 出力プラグインです。
プロジェクト FPS と独立したレートでシーンを評価します。30 FPS の
プロジェクトを 60 FPS で出力する場合、0.0、0.5、1.0、1.5…という
プロジェクトフレーム座標を実際に評価します。フレーム複製やメタデータ変更
だけではありません。

## Download

**v1.0.0 のリリースゲートに合格しました。** ソース対応とハッシュを監査した
x264、LGPL FFmpeg、ISC L-SMASH を同梱し、全 FPS マトリクスと
setup/portable 最終パッケージのみを使った実エンコードに合格しました。

## One-click installation

1. v1.0.0 setup ZIP を完全に展開します。
2. `payload` の隣にある setup EXE を実行します。
3. AviUtl2 を再起動します。
4. `x264guiEx FreeRenderFPS` を選びます。

インストーラーは既定で
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS` にだけ導入し、
更新前にタイムスタンプ付きバックアップを作成します。uninstall/rollback に対応し、
元の `x264guiEx` 配下への導入は拒否します。詳細は端末で `--help` を実行して
ください。展開した `payload` フォルダーは EXE の隣に必要です。

## Manual installation

v1.0.0 portable ZIP を AviUtl2 ルートへ直接展開します。ZIP には
`data\Plugin\x264guiEx-FreeRenderFPS` が含まれます。正式 AUO2 と 3 個の INI を保ち、
Debug、PDB、Probe、watcher、ブレークポイント用ツールをコピーしないでください。

## Usage

1. `x264guiEx FreeRenderFPS` の設定を開きます。
2. `Free Render FPS` タブを開きます。
3. `Enable Free Render FPS` を有効にします。
4. 目標 FPS プリセットを選びます。
5. 同梱監査済み runtime で出力します。

Free Render FPS を無効にすると元の整数フレーム経路を使用します。有効時は
プロジェクト FPS を変えず、同じ期間を目標 FPS で評価します。

## FPS presets

23.976、24、25、29.97、30、48、50、59.94、60、66、72、90、120、Custom。
内部は有理数 rate/scale を使い、59.94 は 60000/1001 です。

## Tested conversions

| Project → target | Frames | Video/audio duration | Adjacent duplicates | Status |
|---|---:|---:|---:|---|
| 30 → 60 | 300 | 5.000000 / 5.000000 s | 0、全 300 フレーム固有 | PASS |
| 66 → 60 | 300 | 5.000000 / 5.000000 s | 0、全 300 フレーム固有 | 保存済み統合成果物で PASS |
| 66 → 59.94 | 300 | 5.005000 / 5.000000 s | 0、全 300 固有 | PASS |
| 66 → 24 | 120 | 5.000000 / 5.000000 s | 0、全 120 固有 | PASS |
| 66 → 120 | 600 | 5.000000 / 5.000000 s | 0、全 600 固有 | PASS |
| 120 → 60 | 300 | 5.000000 / 5.000000 s | 0、全 300 固有 | PASS |

詳細は [docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md) を参照してください。

## Supported AviUtl2

現在は **AviUtl2 v2.1.4 x64 のみ**です。有効化前に image size、entry RVA、
timeline builder のバイト列と呼び出し関係を検査し、未知のビルドでは拒否します。

## Known limitations

- 内部 Hook は AviUtl2 のバージョンに依存します。
- 整数フレームキャッシュには neighbor-frame eviction を使用しています。
- FreeFPS 有効時は AFS、キーフレーム事前スキャン、timecode 出力を無効化します。
- FFmpeg/L-SMASH は由来を再現できますが、異なるパスでのバイト完全再現を
  誤って主張しません。
- テスト済み portable ZIP は `data\Plugin` への手動配置が必要です。
- インストーラーは最小のコンソール EXE です。

[docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md) も参照してください。

## Build

Visual Studio Desktop C++ ワークロードと .NET Framework 4.7.2 参照を用意し、
次を実行します。

```powershell
.\tools\package_release.ps1 -RunSelfTest
```

Release x64 ビルド、許可リスト方式のパッケージング、インストーラーの隔離自動テスト、
setup/portable ZIP と SHA-256 の生成を行います。詳細は
[docs/BUILD.md](docs/BUILD.md) にあります。

## Credits

[rigaya 氏の x264guiEx](https://github.com/rigaya/x264guiEx) のエンコード、
音声、mux、設定、ログ、エラー処理を再利用しています。

## License

[LICENSE](LICENSE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)、
[docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md) を参照してください。
由来またはライセンスが不完全な外部バイナリはパッケージに含めません。
