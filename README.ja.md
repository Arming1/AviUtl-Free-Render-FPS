# AviUtl2 / AviUtl 1.x 向け FreeRenderFPS

[简体中文](README.zh-CN.md) | **日本語** | [English](README.en.md)

> **AI 支援開発についての表明：** 本プロジェクトは AI の支援を受けて開発されています。
> コード、ドキュメント、テストツールは主に AI が作成しました（初期は OpenAI Codex、
> その後は Anthropic Claude / Claude Code を使用）。作者が企画・レビューを行い、
> 実際の AviUtl2 環境で検証しています。

FreeRenderFPS は x264guiEx をベースに、プロジェクト FPS とは独立した時刻でシーンを再評価します。
30 FPS のプロジェクトを 60 FPS で出力する場合は、0.0、0.5、1.0…というプロジェクトフレーム座標を
実際に評価します。フレーム複製やメタデータの変更ではありません。

## v1.3.0 の変更点

- 直線移動(時間制御) を任意のトラックで使用できるようにしました。補間移動(時間制御) も許可します。
- 時間制御の編集カーブ（行末の `|` パラメータ）と、「位置」にカーブが付いた場合をそのまま保持します。
- 大きなプロジェクトの変換を大幅に高速化しました（35 MB のプロジェクトで約 3 秒）。
- GitHub Actions が push ごとにビルドとテストを行い、AviUtl2 での実測結果を表示します。
- 目標 FPS が低すぎる場合（例: 66 → 10）、短いオブジェクトが重なるため拒否します。現時点では未対応です。

## 機能（1.2.5 から）

- 次合成のフレームレート：シーンごとに元のレートを維持できます。
- 時間制御と コマ落ち を秒単位で正確に換算します。
- Precise motion（正確な運動）は既定で有効です。ダイアログではチェックボックスを表示しません。
- `イージング（通常）@イージング` を許可します。

## ダウンロードとインストール

- **推奨：** `AviUtl2-FreeRenderFPS-v1.3.0-setup.zip` を完全に展開し、`payload` の隣にある `setup` EXE を実行してから AviUtl2 を再起動します。
- 手動インストール：`AviUtl2-FreeRenderFPS-v1.3.0-portable.zip` を AviUtl2 のルートへ展開します。

詳しい手順、更新、アンインストールは [docs/INSTALL.md](docs/INSTALL.md) を参照してください。

## 使い方

1. AviUtl2 の **編集（Edit）> Free FPS Export...** を開きます。書き出し前にプロジェクトをファイルとして保存してください。
2. 目標 FPS と次合成のフレームレート（既定は「自動（推奨）」）を設定します。
3. 「エンコード設定...」で x264guiEx のエンコード設定を選び、書き出します。

## 対応範囲

- **AviUtl2 v2.1.12：** Free FPS Export を使用します。
- **内蔵フック：** AviUtl2 v2.1.4 x64 のみ対応です。
- **AviUtl 1.x：** v1.0.1 パッケージを使用します（AviUtl 1.10 + ExEdit 0.92、Win32）。

## 検証

- 2026-10-17 に AviUtl2 v2.1.12 で Precise motion の 8 変換を実測し、すべて合格しました（重複フレームなし、運動残差 0.23 px 以下）。
- 30 分間の音声・映像同期（66・50・60000/1001 fps）も、すべて合格しました。
- 詳細は [docs/VALIDATION_MATRIX.md](docs/VALIDATION_MATRIX.md)、CI の結果は [GitHub Actions](https://github.com/Arming1/AviUtl-Free-Render-FPS/actions) を参照してください。

## 既知の制限

低フレームレートの階段の最終フレーム、2 点直線の最終フレーム、位置の小数 3 桁などの制限は、
[docs/KNOWN_LIMITATIONS.md](docs/KNOWN_LIMITATIONS.md) と [docs/RETIME_RULES.md](docs/RETIME_RULES.md) を参照してください。

## 履歴

バージョンごとの変更内容は [CHANGELOG.md](CHANGELOG.md) を参照してください。

## ライセンス

[LICENSE](LICENSE) と [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
