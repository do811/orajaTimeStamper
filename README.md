# oraja TimeStamper / Windows版

OBS Studioの配信開始・終了を監視し、**配信終了後にだけ**beatorajaのSQLite DBを一括読み込み、YouTube概要欄用のタイムスタンプを生成するC言語プログラムです。

## 実行ファイル

- `timestamp.exe` — 通常は黒いコマンドプロンプト画面で動くOBS監視・タイムスタンプ生成プログラム。
- `config.exe` — Win32 GUIの設定画面。最初に設定がない、または必須設定が不足している場合は`timestamp.exe`から自動起動します。後で設定を変えたい場合もこのEXEを直接実行します。
- `regenerate.exe` — 配信履歴または YouTube URL から、現在の設定でタイムスタンプを再生成する GUI。

ビルドすると、プロジェクトルートに3つの実行ファイルが生成されます。実行にPythonは不要です。

## 動作の流れ

1. `config.exe`で`song_db`、`score_db`（任意）、`scoredatalog_db`（推奨）、OBSの接続先・ポート・パスワードを設定。
2. OBSの**配信を開始する前**に`timestamp.exe`または`start_monitor.bat`を実行する。設定が足りなければ設定GUIを自動表示。
3. OBSの配信開始イベントで、開始時刻を`sessions/…json`に保存。
4. 配信中はDBを繰り返し読まず、OBS WebSocket v5の状態イベントだけを待つ。
5. OBSの配信終了イベントで終了時刻を保存。その後、`scoredatalog.db`と`songdata.db`から結果を読み取り`timestamps/`に`YYYYMMDD__HHMM_番号.txt`、`sessions/`に`_report.txt`を生成。
6. `Ctrl+C`でコンソール監視を終了。配信中に監視を中断した場合は未完了セッションを残します。

## ビルド（Windows / MinGW）

1. MinGW の GCC を用意します。既定のパスは `D:\gcc\mingw32\bin\gcc.exe` です。
2. 同梱の `external\sqlite\sqlite3.c` と `sqlite3.h` を使います。
3. PowerShell でプロジェクトルートを開き、ビルドします。

   ```powershell
   .\build.bat
   ```

GCC や SQLite を別の場所に置いている場合は、ビルド前にパスを指定できます。

```powershell
$env:BST_CC = 'C:\mingw64\bin\gcc.exe'
$env:BST_SQLITE_DIR = 'C:\deps\sqlite-amalgamation'
.\build.bat
```

`BST_CC` には `gcc.exe` のパス、`BST_SQLITE_DIR` には `sqlite3.c` と `sqlite3.h` があるフォルダーを指定します。SQLite は `timestamp.exe` に組み込まれるため、実行時に SQLite の DLL は不要です。

再生成・出力設定 GUI のビルドには Windows の .NET Framework 4.8 を使用します。`build.bat` は Windows 付属の C# コンパイラで `regenerate.exe` も作成します。

## 初期設定 (`config.exe`)

Win32 GUIから以下を編集できます。

| 設定 | 説明 |
|---|---|
| 楽曲DB | `songdata.db` または `song.db` の実ファイルパス（必須） |
| スコアDB | `score.db` のパス（任意・代替用） |
| プレー履歴DB | `scoredatalog.db`（最優先・推奨） |
| OBS IP/ホスト | 例 `localhost` / `192.168.1.20` |
| OBSポート | 通常 `4455` |
| OBS WebSocketパスワード | OBS > ツール > WebSocketサーバー設定で確認 |
| 曲開始時刻を推定 | 結果保存時刻－譜面長（デフォルトON） |
| 配信終了時に自動生成 | デフォルトON |
| アーティスト名を含める | デフォルトOFF |
| 補正秒数 | 正の数ならタイムスタンプをその秒数早める（例 `160` / `2:40`） |

### 特定の文字を置き換える

`config.exe` の「文字置換ルール・難易度表の設定…」で「＋ 行を追加」を押し、各行の「対象文字」に1文字、「置き換え後」に出力したい文字列を入力します。
この機能はyoutubeの概要欄にて`>`や`<`を使用出来ない仕様の為のものです。
たとえば対象を `<`、置き換え後を `?` にすると、難易度名等のの `<<yakumo～～` が `??yakumo～～` になります。対象文字が空欄なら無効、置き換え後が空欄なら対象文字を削除します。置換はタイムスタンプ出力の曲名欄（サブタイトルと、表示を有効にした場合のアーティスト名を含む）に適用されます。

置換は元の文字ごとに一度だけ適用します。「出力設定を保存」で確定し、旧版の置換設定も初回は引き継ぎます。

### 難易度表を同じ行に表示する

出力設定で表示を有効にして、beatoraja の `table` フォルダーを選びます。保存時にキャッシュ（`.bmt` または同形式の `.json`）から記号とレベルを取り込みます。表の更新後はもう一度保存してください。譜面の SHA-256 または MD5 が一致した行は `2:34 ★12 曲名` のように表示し、複数のレベルがあれば併記します。設定と索引は EXE 横の `output-format.json` に保存されます。
>>>>>>> 5034318 (再生成機能 難易度記号併記 置き換え対象文字をn個に)

`score.db`と`scoredatalog.db`は両方必須ではありません。**どちらか一方が存在すれば保存できます**。両方あれば`scoredatalog.db`を優先します。

`config.json`はEXEのあるフォルダに保存されます。Windows版のOBSパスワードは**Windows DPAPI（ユーザー単位）で暗号化して保存**し、JSON中に平文を保存しません。別PC・別Windowsアカウントへ設定をコピーした場合、パスワードは再設定してください。`config.json`や実データはGitHubにコミットしないでください。

**OBSの準備**：OBS Studio 28以降のWebSocket v5を有効にしてください。OBSと別PCで通信する場合は、認証パスワードを設定し、信頼できるネットワークでのみ使ってください。通常のobs-websocketの`ws://`通信はTLSで暗号化されません。

## 起動とコマンド

通常は次のコマンドだけで監視できます。

```bat
cd /d C:\orajaTimeStamper
timestamp.exe
```

または `start_monitor.bat` をダブルクリックしてください。

開始・終了を手動で記録したい場合：

```bat
timestamp.exe start
timestamp.exe stop
```

日時を明示して記録する場合：

```bat
timestamp.exe start --at "2026-11-16T19:00:00+09:00"
timestamp.exe stop  --at "2026-11-16T21:30:00+09:00"
```

終了済み配信のタイムスタンプを再生成する場合：

```bat
timestamp.exe generate
timestamp.exe generate --offset 160
timestamp.exe generate --basis result
timestamp.exe generate --basis estimated_start
timestamp.exe generate --session "sessions\20261116_190000_1794823200.json"
```

`--offset 160`（または`--offset 2:40`）はタイムスタンプを160秒前倒しします。`--basis result`は曲長を引かず結果保存時刻を使います。`generate`ではDBを読み直すので、曲DB更新後も再生成できます。タイムゾーン付きISO時刻を推奨します。

## 過去の配信を再生成する

1. `config.exe` で使いたい設定を保存し、`regenerate.exe` を起動します。
2. 履歴から開始・終了日時を確認して「選択した配信を再生成」を押します。OBS を起動していなくても生成できます。
3. `timestamps/` にローカル開始日時と連番を使った `20261128__1930_1.txt`、`20261128__1930_2.txt` などが作成されます。通常生成・CLI 再生成とも同じ命名で、旧版のファイルも残ります。

YouTube から生成する場合は、終了済みライブの URL（`/live/`、`watch?v=`、`youtu.be/`）を入力します。通常は公開ページの開始・終了時刻を使います。ページから取得できない場合は、YouTube Data API v3 を有効にした Google Cloud の API キーも入力してください。キーは保存せず、環境変数 `YOUTUBE_API_KEY` からも入力できます。実際の配信開始・終了時刻と長さを確認してから、該当期間のローカル DB 履歴でタイムスタンプを生成します。取得した時間帯は履歴にも追加されます。終了時刻が未確定の配信、非公開などで参照できない動画には利用できません。

## 出力構成

```text
orajaTimeStamper/
  timestamp.exe
  config.exe
  regenerate.exe
  config.json               # 初回GUI保存後
  active_session.json       # 配信中のみ
  last_session.json         # 最後に終了した配信
  sessions/
    YYYYMMDD_HHMMSS_....json
    YYYYMMDD_HHMMSS_...._report.txt
  timestamps/
    YYYYMMDD__HHMM_1.txt
  logs/
    timestamp.log                # 最大2MiB + 世代バックアップ3つ
  src/                      # Cソース
  build.bat
  start_monitor.bat
```

生成される`YYYYMMDD__HHMM_番号.txt`はUTF-8 BOM付きです。

```text
0:00 配信開始
3:12 Sample Song [HYPER]
6:45 Another Song
10:21 Sample Song [HYPER]
1:32:48 配信終了
```

同じ譜面を複数回プレーしていれば、それぞれ別の行に出力します（`scoredatalog.db`に残っている場合）。

## DBの仕様と時刻精度

- `scoredatalog.db` の `scoredatalog(sha256, date)` から配信開始～終了の結果保存時刻（Unix秒）を抽出し、すべてのプレーを並べます。
- `songdata.db` の `song(sha256, title, subtitle, artist, length)` で名称と譜面長を検索します。`length`はミリ秒です。
- 既定の曲開始推定は**結果保存時刻－譜面長**です。**実際の選曲時刻ではありません**。ロード時間・途中FAILED・結果画面・配信側の遅延でずれます。
- `score.db`は同一譜面の最終結果しか保存しないため、同じ曲を繰り返しプレーした履歴を復元できません。
- 未記録のプレー・曲名不明・長さ不明のケースはレポートに注意を書きます。
- OBSイベントが取れなかった場合、再接続時の`GetStreamStatus`から**配信継続時間を使った開始推定**を行います。終了のイベントが失われた場合は正確な停止時刻を復元できません。`stop --at`で実際の終了時刻を指定してください。
- OBSから来る`StreamStateChanged`イベントはOBSの配信ボタンの状態であり、YouTube側の配信公開画面で「ライブ開始」を押した瞬間とは必ずしも同じではありません。

**DBはSQLite読み取り専用で開きます。beatorajaのDBを変更しません。**

## 障害時・復旧

- `logs/timestamp.log`を確認してください。実行EXEの隣に生成します。
- OBS接続に失敗すると5秒間隔で再接続します。パスワード違いが続く場合は`config.exe`を開いて直してください。
- OBSが途中で終了したりネットワークが切れても、開始時刻は`active_session.json`と`sessions/*.json`に保存済みです。
- 監視できない間に配信が終了し、開始と終了の両方が取得できなくなった場合は、**OBS状態だけでは実際の終了時刻を復元できません**。`timestamp.exe stop --at "正しい日時"`で終了記録し、必要なら`generate`で再生成してください。
- `config.json`が破損・未設定の場合は`config.exe`が自動で開きます。設定変更時は直接`config.exe`を起動できます。

## 開発用テスト

Linuxでは、SQLite3開発用ライブラリがあれば**生成ロジックだけ**テストできます（Windows依存のOBS・設定GUIは含みません）。

```bash
mkdir -p build
gcc -D_POSIX_C_SOURCE=200809L -std=c11 -O2 -Isrc src/cli.c src/common.c src/json.c src/session.c src/generator.c src/format.c -o build/timestamp_test -lsqlite3 -lm
python -m unittest discover -s tests -v
```

Pythonは開発用テストスクリプトでのみ使用します。実際のCプログラムには不要です。

## ライセンス

[MIT License](LICENSE)

## 参照資料

- [OBS websocket 5.x protocol](https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md)
- [beatoraja SQLiteSongDatabaseAccessor.java](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/song/SQLiteSongDatabaseAccessor.java)
- [beatoraja ScoreDataLogDatabaseAccessor.java](https://github.com/exch-bms2/beatoraja/blob/master/src/bms/player/beatoraja/ScoreDataLogDatabaseAccessor.java)
