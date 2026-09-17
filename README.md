# zip64j / unzip64 - 64bit 統合アーカイバAPI仕様 ZIP DLL

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

- zip32j.dll / unzip32.dll の 64bit 版が無かったので作りました
- 統合アーカイバAPI仕様に準拠した 64bit ZIP DLL ペアです
  - **zip64j.dll**: ZIP 書庫の作成（Zip* API）
  - **unzip64.dll**: ZIP 書庫の展開・閲覧（UnZip* API / アーカイブハンドル API）
- あふｗで動作確認しています

## 機能

- **zip64j.dll** — ZIP 圧縮
  - `Zip` / `ZipW` / `ZipGetVersion` など統合アーカイバAPI仕様の Zip* API
  - Info-ZIP zip 3.0 をライブラリ化して静的リンク
  - `-P` によるパスワード付き書庫の作成、`-t` / `-tt` の日付フィルタ、`-b` の一時ディレクトリ指定に対応
  - 既存書庫の更新（`-u` / `-f` / `-g`）とエントリ削除（`-d`）に対応
  - UnZip* は公開しません（展開・閲覧は unzip64.dll を使ってください）
- **unzip64.dll** — ZIP 展開・閲覧
  - `UnZip` / `UnZipW` / `UnZipOpenArchive` / `UnZipFindFirst` / `UnZipFindNext` など
  - `*_Ex` / `*64` の 64bit サイズ・FILETIME 時刻アクセサに対応
  - `UnZipSetOwnerWindow` / `UnZipSetOwnerWindowEx` / `UnZipSetOwnerWindowEx64` による展開の進捗通知
  - Info-ZIP unzip 6.0 をライブラリ化して静的リンク
- Zip64 拡張フィールド対応（4GB 超のファイル・書庫）
- unzip64.dll は `ZipUnZip*` エイリアス経由でも呼び出し可能

## 開発環境

1. **Visual Studio 2022**
   - C++ (C) 開発ツールをインストール
   - x64 ビルドが前提（32bit ビルドは不可）

2. **CMake 3.21 以上**
   - Visual Studio 2022 に同梱のもので可

## ビルド手順

1. **リポジトリのクローン**
   ```powershell
   git clone https://github.com/mtkhs/zip64j
   cd zip64j
   ```

2. **ビルドの実行**
   ```powershell
   .\build.ps1
   ```

   または手動でビルド:
   ```powershell
   cmake -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

ビルドされた DLL は `build/dist/` に出力されます
- `zip64j.dll`
- `unzip64.dll`

## 参考資料

- **[Info-ZIP Zip 3.0 / UnZip 6.0](https://infozip.sourceforge.net/)**: 圧縮・展開の中核実装。BSD ライクのライセンスで同梱（詳細は `third_party/` 配下参照）
- **統合アーカイバAPI仕様**: 日本の Windows 向けアーカイバ DLL 共通 API
- **[zip32j.dll](http://www.madobe.net/archiver/lib/zip32j.html)** (吉岡恒夫 氏 / フリーウェア、改変・再配布自由): 32bit 統合アーカイバAPI仕様版 ZIP DLL。コマンドライントークナイザ (`cmdline.c`) を 64bit 向けに移植

---

**注意**: ルートの `LICENSE`（MIT）は本プロジェクト自身のコードに適用されます。本 DLL は Info-ZIP zip 3.0 / unzip 6.0 を静的リンクしており、それらのライセンス条件（`third_party/zip30/LICENSE` および `third_party/unzip60/LICENSE`）に従います。再配布する場合は、改変版であることを明記してください。
