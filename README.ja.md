# AviUtl2 / AviUtl 1.x 向け FreeRenderFPS

[简体中文](README.zh-CN.md) | **日本語** | [English](README.en.md)

FreeRenderFPS は x264guiEx をベースに、AviUtl2 と旧 AviUtl 1.x の
ホストアダプターを提供します。プロジェクト FPS と独立したレートでシーンを
評価し、30 FPS のプロジェクトを 60 FPS で出力する場合は 0.0、0.5、1.0、
1.5…というプロジェクトフレーム座標を実際に評価します。フレーム複製や
メタデータ変更だけではありません。

## v1.1.0 の変更点

v1.1.0 は、自由フレームレート出力を単一の AviUtl2 プラグインファイルに統合したリリースです。詳細は [CHANGELOG.md](CHANGELOG.md) を参照してください。

- `x264guiEx-FreeRenderFPS.aux2` に出力プラグインと「編集 > **Free FPS Export...**」メニュー項目が含まれます。公開 AviUtl2 SDK でシーンを再評価し、AviUtl2 v2.1.12 では任意の有理数フレームレートで出力できます。
- 内蔵の Free Render FPS フックは引き続き AviUtl2 v2.1.4 x64 のみ対応です。それ以外のホストでは Free FPS Export への案内が表示されます。
- インストーラーは旧 `.auo2` を `.aux2` へ移行し、バックアップを `Plugin` の外に保存します。`--cleanup-legacy-backups` は古いバックアップフォルダーを `Plugin` の外へ移動します（明示的な指定が必要で、削除はしません）。
- `.aup2` のフレームレート変換ツール、ホスト非依存のテスト、検証ツールを追加しました。

以下のリリースゲートと実測値は v1.0.0 の結果です。Free FPS Export は 2026-10-17 に
AviUtl2 v2.1.12 で実機検証しました。Precise motion を有効にした 8 変換（フレーム数は
期待値どおり、重複・逆行フレームなし、運動残差 0.12–0.23 px、上限 1.0 px）と、
66・50・60000/1001 fps の 30 分間の音声・映像同期（すべて PASS。最大ずれは 66・50 fps で
0.00 ms、60000/1001 で ±8.33 ms＝半フレーム）です。測定に使用したバイナリは `x264guiEx-FreeRenderFPS.aux2`（バージョンリソース 1.1.0、SHA256 114C3070…6076B、2026-10-17 17:52 に再パッケージ、Precise motion を含む）です。最初の 1.1.0 ビルドには精密モードは含まれていません。内蔵フックは AviUtl2 v2.1.4 での
実機検証が未実施です。AviUtl 1.x パッケージは引き続き v1.0.1 で、AviUtl 1.10 + ExEdit 0.92
での再検証が未実施です。

## Download

**AviUtl2 v1.0.0 のリリースゲートに合格しました。** ソース対応とハッシュを監査した
x264、LGPL FFmpeg、ISC L-SMASH を同梱し、全 FPS マトリクスと
setup/portable 最終パッケージのみを使った実エンコードに合格しました。

旧 AviUtl 1.x 版は AviUtl 1.10 + ExEdit 0.92 用の独立した Win32 ビルドです。
`dist\AviUtl1-FreeRenderFPS-v1.0.1` にあり、手動インストール用 portable ZIP
として配布しています。AviUtl ホスト本体は含みません。

## One-click installation

1. v1.1.0 setup ZIP を完全に展開します。
2. `payload` の隣にある setup EXE を実行します。
3. AviUtl2 を再起動します。
4. `x264guiEx FreeRenderFPS` を選びます。

インストーラーは既定で
`%ProgramData%\aviutl2\Plugin\x264guiEx-FreeRenderFPS` にだけ導入し、
更新前にタイムスタンプ付きバックアップを作成します。uninstall/rollback に対応し、
元の `x264guiEx` 配下への導入は拒否します。詳細は端末で `--help` を実行して
ください。展開した `payload` フォルダーは EXE の隣に必要です。

## Manual installation

v1.1.0 portable ZIP を AviUtl2 ルートへ直接展開します。ZIP には
`data\Plugin\x264guiEx-FreeRenderFPS` が含まれます。正式な
`x264guiEx-FreeRenderFPS.aux2` と 3 個の INI を保ち、
Debug、PDB、Probe、watcher、ブレークポイント用ツールをコピーしないでください。

### 旧 AviUtl 1.x（Win32）

`AviUtl1-FreeRenderFPS-v1.0.1-portable.zip` を AviUtl 1.10 のルートへ展開し、
`Plugins` の構成を保ちます。`Plugins\x264guiEx-FreeRenderFPS.auo` と同名の
3 個の INI を同じ場所に置き、runtime は `Plugins\exe_files`、プリセットは
`Plugins\x264guiEx-FreeRenderFPS_stg` に置かれます。AUO を古い
`x264guiEx-FreeFPS.auo` に変更したり、古いコピーを同時に読み込ませたりしないで
ください。詳細は [docs/LEGACY_AVIUTL1_SETUP.md](docs/LEGACY_AVIUTL1_SETUP.md) を
参照してください。

## v1.0.x からの更新

- インストーラーは導入/更新時に、プラグインフォルダー内の旧 `x264guiEx-FreeRenderFPS.auo2`
  をバックアップした上で削除します。手動で導入した場合は自分で削除してください。
  AviUtl2 に `.auo2` と `.aux2` を同時に読み込ませないでください。
- 実験版 `free_fps_export.aux2` をコピーしていた場合は削除してください。上記の
  Free FPS Export に置き換えられており、インストーラーは触れません。
- インストーラーのバックアップは `C:\ProgramData\aviutl2\FreeRenderFPS-backups` に作成されるようになり、`Plugin` の外に置かれます。
  `C:\ProgramData\aviutl2\Plugin` 内の古い `x264guiEx-FreeRenderFPS.backup-*` フォルダーには、それぞれプラグインファイルのコピーが入っています（古いものには `.auo2` があります）。AviUtl2 は `Plugin` の 1 階層下のフォルダーのプラグインも読み込むため、これらの複製が重複として読み込まれる可能性があります。導入・更新・アンインストールの既定動作ではこれらには触れません。`FreeRenderFPS-backups` へ移動する場合（移動のみで削除はしません）は、インストーラーに `--cleanup-legacy-backups` を指定してください。先に一覧を確認するには `--dry-run` を付けます。

## Usage

1. `x264guiEx FreeRenderFPS` の設定を開きます。
2. `Free Render FPS` タブを開きます。
3. `Enable Free Render FPS` を有効にします。
4. 目標 FPS プリセットを選びます。
5. 同梱監査済み runtime で出力します。

Free Render FPS を無効にすると元の整数フレーム経路を使用します。有効時は
プロジェクト FPS を変えず、同じ期間を目標 FPS で評価します。

## Free FPS Export（AviUtl2 v2.1.12）

同じ `x264guiEx-FreeRenderFPS.aux2` が AviUtl2 の **編集（Edit）** メニューに
**Free FPS Export...** を追加します。AviUtl2 v2.1.12 でフリー描画 FPS を使う場合は
こちらを使用してください。公開 SDK の API でプロジェクトのタイミングを組み直し、
任意の有理数 FPS で出力します。出力前にプロジェクトをファイルとして保存してください。

## FPS presets

23.976、24、25、29.97、30、48、50、59.94、60、66、72、90、120、Custom。
内部は有理数 rate/scale を使い、59.94 は 60000/1001 です。

## AviUtl2 Tested conversions

| Project → target | Frames | Video/audio duration | Adjacent duplicates | Status |
|---|---:|---:|---:|---|
| 30 → 60 | 300 | 5.000000 / 5.000000 s | 0、全 300 フレーム固有 | PASS |
| 66 → 60 | 300 | 5.000000 / 5.000000 s | 0、全 300 フレーム固有 | 保存済み統合成果物で PASS |
| 66 → 59.94 | 300 | 5.005000 / 5.000000 s | 0、全 300 固有 | PASS |
| 66 → 24 | 120 | 5.000000 / 5.000000 s | 0、全 120 固有 | PASS |
| 66 → 120 | 600 | 5.000000 / 5.000000 s | 0、全 600 固有 | PASS |
| 120 → 60 | 300 | 5.000000 / 5.000000 s | 0、全 300 固有 | PASS |

詳細は [docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md) を参照してください。

### 旧 AviUtl 1.x の実出力検証

| ホスト/プロジェクト → 目標 | フレーム | 映像/音声期間 | デコード済みハッシュ | 結果 |
|---|---:|---:|---|---|
| AviUtl 1.10 + ExEdit 0.92、30 → 60 | 300 | 5.000000 / 5.000000 s | 299 固有、末端に 1 組の重複 | 連続動作部分 PASS |

末端の重複は 5 秒のテストプロジェクトで最後のプロジェクトフレームに
クランプされた境界です。連続動作部分は 30 FPS のフレーム対として出力されて
いません。51.2 → 60、51.2 → 60000/1001、66 → 60 は旧ホストで新しい実出力を
生成してから、AviUtl2 と同じ全マトリクスを主張します。詳細は
[docs/LEGACY_AVIUTL1_VALIDATION.md](docs/LEGACY_AVIUTL1_VALIDATION.md) を参照してください。

## Supported AviUtl2

内蔵フックは **AviUtl2 v2.1.4 x64 のみ**対応です。AviUtl2 v2.1.12 では Free FPS Export を使用してください。
有効化前に image size、entry RVA、
timeline builder のバイト列と呼び出し関係を検査し、未知のビルドでは拒否します。

## 対応する旧 AviUtl 1.x

旧アダプターは **AviUtl 1.10 + ExEdit 0.92** のみをサポートします。バージョンを
固定した保護付き ExEdit detour を使用する x86 `.auo` 出力プラグインです。
x264/FFmpeg/L-SMASH は子プロセスとして動作するため、64 bit Windows では同梱の
x64 runtime を使用できます。その他の AviUtl / ExEdit バイナリは拒否されます。

## Known limitations

- 内部 Hook は AviUtl2 のバージョンに依存します。
- 整数フレームキャッシュには neighbor-frame eviction を使用しています。
- FreeFPS 有効時は AFS、キーフレーム事前スキャン、timecode 出力を無効化します。
- FFmpeg/L-SMASH は由来を再現できますが、異なるパスでのバイト完全再現を
  誤って主張しません。
- テスト済み portable ZIP は `data\Plugin` への手動配置が必要です。
- インストーラーは最小のコンソール EXE です。
- 旧アダプターの ExEdit サブフレーム余りは 0.01 プロジェクトフレーム単位に量子化
  され、整数フレームキャッシュの eviction workaround を使用します。最後の出力
  サンプルが最終プロジェクトフレームにクランプされる場合があります。
- 旧版パッケージは現在 30 → 60 の実出力を直接検証済みで、拡張マトリクスは未検証です。

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

旧 Win32 プラグインは同じプロジェクトを `Release|Win32` でビルドします。

```powershell
msbuild .\src\x264guiEx-FreeFPS\x264guiEx\x264guiEx.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

パッケージ済みの旧版は `dist\AviUtl1-FreeRenderFPS-v1.0.1` にあります。

## Credits

[rigaya 氏の x264guiEx](https://github.com/rigaya/x264guiEx) のエンコード、
音声、mux、設定、ログ、エラー処理を再利用しています。

## License

[LICENSE](LICENSE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)、
[docs/RUNTIME_DEPENDENCIES.md](docs/RUNTIME_DEPENDENCIES.md) を参照してください。
由来またはライセンスが不完全な外部バイナリはパッケージに含めません。
