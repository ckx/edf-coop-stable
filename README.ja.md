# edf-coop-stable

[English](README.md) | [中文](README.zh-CN.md) | **日本語**

**EDF6Coop** は、地球防衛軍6（PC / Steam）のオンライン協力ミッションが同期ズレを起こしたり、回線切れで終わったりしないようにします。失われたパケットを再送し、EOS が再接続する間は短い回線切れをゲームに知らせず、任意でホストへの直接 UDP 接続を用意します。[EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) のプラグイン1つ（`EDF6Coop.dll`）で、Windows と Linux の Steam Proton で動きます。

> 非公式 MOD です。D3 PUBLISHER / SANDLOT / Epic Games とは関係ありません。メモリ上で通信の呼び出しといくつかのゲーム関数を変えるだけで、ゲームのファイルは変更しません。`[DirectNet] Enabled=0` にするか DLL を消せば元のゲームに戻ります。
>
> **状態：実験段階。** 信頼性のある送信と回線切れの猶予は、実際の4人オンラインプレイで確認済みです（0.3.2：全員が直接接続し、7回の回線切れをゲームはまったく検知しませんでした）。問題があれば [Issue](https://github.com/hajisensai/edf-coop-stable/issues) にログを添付してください。

## 4人の部屋

この版はゲーム本来の最大4人の部屋で遊びます。2.5.0 までの EDF6Coop には大きな部屋（Player MOD）、部屋画面、武器ページ、部屋の武器ファイル、アーマーコピー、自動更新もありました。これらはなくなりました。必要な人は 2.5.0 のままにしてください。

部屋の全員が同じ版を使ってください。ホストはネットコードのプロトコルが違うプレイヤーを入れません（`[Netcode] RejectMismatched=1`）。両者のルールが違ってしまうためです。信頼性のある送信と回線切れの猶予はどの版とも、プラグインを入れていない人とも一緒に働きます。

インストール時に古い `EDF6DirectNet.dll` / `EDF6MultiSlot.dll` は `.disabled` に改名されます。それらの設定ファイルは残りますが読み込まれません。

## 直すもの

| 症状 | 原因 | プラグインがすること | 必要な人 |
|---|---|---|---|
| ミッション中に敵・位置・体力がずれる（同期ズレ） | EDF6 はオンラインのデータをすべて `UnreliableUnordered` で送るため、パケットが1つ失われるとその状態は永久に失われる | `ReliableUnordered` で送る（失われたパケットは EOS が再送。順序は今まで通り保証しないので、ゲームから見える状況は変わらない）。EOS の送受信キューを 64 MB 以上に広げる | **送る側**。入れれば自分が送るデータは失われない。全員が入れるのが理想 |
| 一瞬の回線不調でミッションから落とされる | EOS がタイムアウトや通信エラーで接続を閉じると、ゲームはそのプレイヤーをすぐに外す | 回線切れをゲームに知らせず、裏で EOS に再接続させる。30 秒以内に戻ればゲームは気づかない。戻らなければ元のゲームどおり | **双方**（相手のプラグインは自動で判別、後述） |
| 後から入った人の P2P ハンドシェイクが終わらず、ホスト以外が全員部屋から出される | 元のゲームはこのタイムアウトでゲストを退出させる | ゲストは部屋に残る（`KeepRoomOnPeerTimeout=1`） | ゲストそれぞれ |
| Epic のリレー経由で遅延が大きい、NAT 越えに失敗する | 通信がすべて EOS P2P / リレーを通る | ホストが直接接続を有効にできる：参加者は入室後ホストへ直接つなぐ（スター型、ホストが中継） | ホストが設定。参加者はプラグインを入れるだけ |
| 当たったのにダメージがない、プレイヤーが飛び回る | 撃たれた側が命中を判定する。ほかのプレイヤーの位置が粗く送られる | 部屋の全員がこの版のとき：撃った側が命中を判定し、プレイヤーの位置に先読み用の速度が付く | 部屋の全員 |
| 切断されたのに理由がわからない | | ログを書く：NAT タイプ、直接かリレーか、切断理由、ロビーのメンバー、毎分の通信量、クラッシュの詳細 | 自分 |

直接接続（ホストが有効にする）以外はすべて初期状態でオンです。

## 導入

### Windows

1. [Releases](https://github.com/hajisensai/edf-coop-stable/releases/latest) から `EDF6Coop-<バージョン>.zip` をダウンロードし、**ZIP 全体を**好きなフォルダに展開します。
2. **`INSTALL.bat`** をダブルクリックします。Steam ライブラリから EDF6 を見つけてインストールします。
   - EDFModLoader がなければ同梱のローダーを入れます：[BlueAmulet/EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) v1.0.10（MIT）に**公式版のマルチスレッド不具合の修正**を加えたものです（呼び出しの転送先を1つの変数で共有しており、複数スレッドから同時に呼ぶと別の関数へ飛ぶことがありました。詳しくは [dist/LOADER_FIX.md](dist/LOADER_FIX.md)）。既存の `winmm.dll` は上書きしません。例外は公式 v1.0.10 とバイト単位で同じものだけで、修正版に置き換え、元のファイルを `winmm.dll.bak-official` に保存します。
   - ゲームが見つからないときはゲームフォルダを尋ねます（Steam ライブラリで EDF6 を右クリック → 管理 → ローカルファイルを閲覧）。
3. いつも通り Steam からゲームを起動します。初回起動で `Mods\Plugins\EDF6Coop.ini`（設定）と `EDF6Coop.log`（ログ）ができます。

### Linux（Steam Proton）

1. ZIP を展開し、展開したフォルダで `bash install.sh` を実行します。Steam ライブラリ（通常版と Flatpak 版の Steam）から EDF6 を見つけ、`INSTALL.bat` と同じようにインストールします。見つからないときはゲームフォルダを渡してください（`bash install.sh "<フォルダ>"`）。手で入れる場合は ZIP の中身をゲームフォルダ（`EDF6.exe` のあるフォルダ。Steam で EDF6 を右クリック → 管理 → ローカルファイルを閲覧）にコピーし、すでにある `winmm.dll` は残してください。
2. Steam で EDF6 を右クリック → プロパティで、起動オプションを次のようにします。

   ```
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```

   これがないと Wine が自前の `winmm.dll` を読み込み、EDFModLoader が動きません。
3. ゲームを起動します。設定ファイルとログは Windows と同じく `Mods/Plugins/` にできます。

ZIP には README_EDF6Coop.txt（English）、README_EDF6Coop_zh.txt（中文）、README_EDF6Coop_ja.txt（日本語）が入っています。設定ファイルのコメントは Windows の表示言語に合わせて中国語・日本語・英語のいずれかです。

更新：新しい版を同じ方法で入れてください。設定は残ります。自動更新はありません。

削除：`UNINSTALL.bat` をダブルクリックします（Linux では `bash install.sh --uninstall`）。プラグイン、設定、ログ、古い版の残り、プラグインがルーターに作った UPnP の設定（この PC 向けで名前が `EDF6DirectNet` のものだけ）を消します。EDFModLoader とほかの MOD はそのままで、インストーラーが更新したローダーとそのバックアップ `winmm.dll.bak-official` も残ります。Linux では UPnP の設定が残ります。ホストしない次のゲーム起動で消えるか、ルーターで `EDF6DirectNet` という項目を消してください。ファイアウォールの規則を追加していた場合は、`UNINSTALL.bat` を管理者として実行すると一緒に消します。管理者権限がなければ消すためのコマンドを表示します。

## ホスト：直接接続（任意）

設定するのはホストだけです。プラグインを入れていて `AutoJoin=1`（初期値）の参加者は、入室後に自動でホストへ直接つなぎます。

1. `Mods\Plugins\EDF6Coop.ini` を開き、`Mode=host` にします。
2. Windows：ゲームフォルダの `EDF6Coop_AllowFirewall.bat` を右クリックし、**管理者として実行**します（一度だけ）。この規則は `EDF6.exe` の、あなたの `ListenPort`（未設定なら 27015）への受信 UDP だけを許可します。`ListenPort` を変えたらもう一度実行してください。Linux：ファイアウォールがあればその UDP ポートを許可してください。
3. インターネットから届くようにします。次のどれか：
   - **自動**：`PublicAddress` を空のままにします。プラグインがルーターの UPnP で UDP ポートを開け、この PC のグローバル IPv6 も知らせます。
   - **手動**：ルーターで UDP ポートをこの PC へ転送し、`PublicAddress=グローバル IP:外部ポート` にします（DDNS のホスト名でも構いません。例：`myroom.ddns.net:40000`）。
   - **PC が直接インターネットにつながっている（PPPoE、ルーターなし）**：この種のアダプターはプラグインが判別できません。`PublicAddress=グローバル IP:27015` を手で設定してください。
4. ゲームを再起動し、いつも通り部屋を作ります。

ログ `Mods\Plugins\EDF6Coop.log` で確認します：

| ログ | 意味 |
|---|---|
| `DIRECT players who join your room connect to ...` | ホストのアドレスが決まり、部屋の情報に入った |
| `UPNP router now forwards UDP ...` | UPnP の設定に成功した |
| `UPNP no router with UPnP port mapping found` / `UPNP port mapping failed` | ルーターに UPnP がないか無効。手動でポートを開けてください |
| `UPNP WARNING: the router WAN address ... is private (carrier-grade NAT)` | プロバイダーからグローバル IPv4 をもらえていないため、IPv4 の直接接続は届きません。IPv6 に頼るか、プロバイダーにグローバル IP を頼んでください |
| `UPNP UDP 27015 is already forwarded to ...; left alone` | そのポートは LAN のほかの機器へ転送済み。プラグインは触りません。別の `ListenPort` にするか手動で開けてください |
| `DIRECT client ... connected from ...`（ホスト）/ `DIRECT connected to host ...`（参加者） | 直接接続できた |
| `DIRECT auto-connect stopped (the room host did not answer on any advertised address ...)` | 参加者がホストに届かない（ファイアウォール、ポート転送、`Key` の不一致、EDF6Coop の版違い）。ゲームは Epic 経由で続き、60 秒後にプラグインが再試行します |

参加者は各アドレスを 10 秒ずつ、IPv4、IPv6 の順に試します。どれもだめなら EOS のままで、普段のプレイに影響はありません。

**`Key=`** は任意の合言葉で、なくても安全です。プラグインを入れた各プレイヤーは、このゲームセッション用に作った鍵の指紋を自分のロビーメンバー情報に公開します。ホストは、相手がプレイヤー X の鍵を持っていると証明したときだけ X として接続させ、参加者もルームオーナーの鍵を持っていると証明したホストだけを受け入れます。その後の直接接続のパケットには、その2人だけが持つ鍵で作ったタグが付くので、ほかの誰も（部屋のほかのプレイヤーも、通信経路上の誰かも）他人として接続したり、接続を乗っ取ったり、パケットを改ざん・注入・再送したりできません。`Key` は追加の共有秘密としてその鍵に混ぜ込まれます。部屋の情報には**書かれません**。ホストが設定したら参加者全員が自分の ini に同じ `Key` を入れる必要があり、違えば自動接続に失敗して EOS に戻ります。

**プライバシー**：ホストのグローバルアドレスはロビーのメンバー属性にあり、部屋が見える人は誰でも読めます。直接つなぐ参加者（`AutoJoin=1`）は自分のグローバルアドレスからホストへ接続するので、ホストにはそのアドレスが見えます（EOS の P2P でも通常は互いのアドレスが見えます）。`AutoJoin=0` にすると EOS のままです。

## 設定（`EDF6Coop.ini`、変更後はゲームを再起動）

| キー | 初期値 | 説明 |
|---|---|---|
| `[DirectNet] Enabled` | `1` | `0` = プラグインは何も入れない。元のゲーム |
| `Mode` | `off` | `off` 通常のプレイヤー / `host` 直接接続のホスト / `join` ホストのアドレスを手で入れる（ほぼ不要） |
| `ListenPort` | `27015` | host：待ち受ける UDP ポート（開放・ファイアウォール許可の対象）。join：自分側のポート、空 = 自動 |
| `PublicAddress` | 空 | host：参加者の接続先。空 = グローバル IPv6 + UPnP で開けた IPv4 |
| `AutoJoin` | `1` | 入室後にホストの直接接続へつなぐ（ホストにグローバルアドレスが見える。`0` は EOS のまま） |
| `HostAddress` | 空 | `Mode=join` のみ：ホストのアドレス。例：`123.45.67.89:27015` / `[2408:8207::5]:27015` |
| `Key` | 空 | 直接接続の合言葉。全員同じにする。英数字のみ |
| `UPnP` | `1` | ホストのときルーターにポートを開けてもらう。`Mode=host` のときだけ使う |
| `BindPhysicalInterface` | `1` | 直接接続の通信を物理 NIC に固定し、Clash・VPN・ゲーム加速器の TUN アダプターに横取りされないようにする |
| `LinkTimeoutMs` | `60000` | 相手から何も届かない状態がこれだけ続いたら直接接続が切れたとみなす（3000～300000） |
| `[EOS] FixedPort` | `0` | EOS が固定 UDP ポート `FixedPort`～`FixedPort+7` を使う。0 = ランダム |
| `Relay` | `norelay` | EOS リレー：`norelay` Epic のリレーを使わない / `allow` / `force` / `default` ゲームの設定のまま。つながらないときは `allow` に。リレーなしでは、NAT が EOS の直接通信を通さないプレイヤーは直接接続でしか入れません |
| `[Sync] ReliableGameTraffic` | `1` | 信頼性のある送信（同期ズレ対策）。`0` = 元の動作 |
| `[Resilience] HoldDisconnects` | `auto` | 回線切れの猶予：`auto` プラグインを入れている人に / `off` 元の動作 / `all` 判別せず全員に（全員が入れていると確かなときだけ） |
| `GraceSeconds` | `30` | 回線切れを隠す最大秒数（1～600） |
| `KeepRoomOnPeerTimeout` | `1` | 後から入った人との P2P ハンドシェイクが終わらないとき部屋に残る。`0` = 元の動作 |
| `[Netcode] Fragments` | `1` | EOS の 1170 バイト上限を超えるデータを分割して送る。部屋の全員がオンのときだけ有効 |
| `PlayerSync` | `1` | プレイヤーの位置に速度を付け、ほかのプレイヤーの動きをなめらかにする。全員がオンのときだけ有効 |
| `HitAuthority` | `1` | 撃った側が命中を判定する。全員がオンのときだけ有効 |
| `RejectMismatched` | `1` | ホストのとき、ネットコードのプロトコルが違うプレイヤーを入れない |
| `[Display] RadarItemColors` | `1` | レーダー上のアイテムを種類で色分け：アーマー赤、回復白、武器緑。自分の画面だけ。`0` = 全部緑（元の動作） |
| `[Diagnostics] Laser` | `1` | Closed Laser M2 の回数制限付き診断。`0` で無効。同じ `LaserBullet01` を使う武器も対象。発射やダメージは変更しません |
| `[Log] CrashLog` | `1` | クラッシュ時にアドレスと呼び出し履歴をログに書く |
| `CrashDump` | `0` | 最初のクラッシュで `EDF6Coop-crash.dmp` も書く。ゲームのメモリ（部屋名、チャット）を含むので、頼まれたときだけオンに |

`[NetPlayer] MaxExtrapolateMs` の既定値は診断版5と同じ350 msです。INI の指定値は保持します。短い更新間隔の空白を補いますが、停止や方向転換では先読みが行き過ぎる場合があります。古いデータへのフォールバックは400 msのままです。

レーザー診断は30秒に最大8行の `LASER sample` と、その後のレーザー処理時に集計を記録します。所有者、位置、方向、経過フレームと寿命、両端、描画寸法、描画コールバック数を記録します。`receive-before` はレーザー固有の受信処理であり、全発射通知の数ではありません。描画コールバックだけでは画面上に見えたとは断定できず、初回描画前の0回も異常の証拠ではありません。ダメージ同期の成否もこの診断だけでは判断できません。表示が食い違った時刻を控え、終了後に射手と観察者のログを集めてください。

プレイヤーの位置補正では、移動量を実際の経過時間で割って速度を求めます。フレーム間隔が変わっても、正しく動いている他のプレイヤーを前後に押し戻さないためです。

## しくみ

### 信頼性のある送信

`EDF.dll+0x12c8bc0` にある2か所の `EOS_P2P_SendPacket` 呼び出しは、どちらも `Reliability = UnreliableUnordered` を渡します。プラグインはインポートテーブルでこれを `ReliableUnordered` に変えます。どのパケットもちょうど1回届き、順序は入れ替わることがあります。信頼性のない送信でももともと順序は入れ替わりうるので、ゲームのロジックから見て新しいことは何も起きず、パケットが失われなくなるだけです。受け取る側の協力はいりません。

### 回線切れの猶予とプラグインの判別

相手がプラグインを入れていなければ、相手のゲームはいつも通りあなたを外します。そのときこちらが回線切れを隠し続けると2つのゲームが食い違うので、猶予はプラグインを入れている人にだけ与えます。

EDF6 は受け取った EOS パケットをすべてゲームのデータとして読むため、プラグインは P2P で探りのパケットを送れません。代わりにロビーの**メンバー属性**を使います。部屋にいる間、自分に `EDF6DN=<プロトコル>` を書き（`EDF.dll` はメンバー属性の関数をインポートしていないので、ゲームからは見えません）、回線切れのとき相手にその属性があるかを見ます：

- 属性があり、以前に接続できていた：猶予を与え、2 秒ごとに EOS へ再接続を頼みます。
- 属性がない：元のゲームどおりに扱います。
- 直接接続のメンバー：直接接続が応答している間は猶予を与えます（メニューでも毎秒 4 回 ping します）。そのゲームデータはもともと EOS を通りません。
- Epic のロビーサービスがメンバーの切断（`DISCONNECTED`：切れたのは Epic のロビーサービスで、ゲームではない）を伝えたが、そのメンバーの直接接続は応答している：ゲームには隠します。EOS がそのメンバーを再び載せたら（`JOINED`）、両方のイベントを捨てます。直接接続が `GraceSeconds` の間黙ったとき、またはそのメンバーが本当に出たか蹴られたときにだけゲームに知らせます。自分自身についても同じです。
- 相手がロビーを出た：すぐにゲームに知らせます。

### 直接接続

ホストはメンバー属性 `EDF6DN_ADDR`（空白区切りのアドレス一覧）を書きます。ほかのメンバーは 2 秒ごとにそれを読み、各アドレスを IPv4、IPv6 の順に 10 秒ずつ試します。すべて失敗したら 60 秒後にまた試します。通信は独自の UDP プロトコルで、選択的確認応答、速度制限付きの再送、接続ごとの鍵を持ちます。`IP_UNICAST_IF` で物理アダプターに固定します。

直接接続は相手が応答している間だけゲームデータを運びます。毎秒 4 回 ping し、約 1.5 秒応答（pong か確認応答）がなければ運ぶのをやめます。届けられなかったデータは EOS で（信頼性ありで）送り、その後のゲームのデータも、直接接続が 1 秒続けて応答するまで EOS で送ります。ホストは参加者同士を中継し、ある参加者に届けられないデータは送り主に戻して EOS で送らせ、ほかの参加者もその参加者へホスト経由では送らなくなります。

プラグインを入れた各プレイヤーは `EDF6DN_ID` も書きます。このゲームセッション用に作った ECDSA P-256 鍵の SHA-256 指紋です。接続はクッキーの往復から始まります。ホストは hello に送信元アドレスに結び付けたクッキーで答え、それが戻るまで何も保持しないので、偽の送信元アドレスや hello の大量送信は効果がありません。参加者はクッキー付きで hello を送り直し、自分の鍵で署名します。ホストは、X が部屋にいてその鍵の指紋を公開しているときだけ、それを EOS ID X として受け入れます。以前のセッションの署名付き hello は再送として拒否します。参加者は、ルームオーナーが公開した鍵で署名された welcome だけを受け入れます。hello と welcome はその署名の下で新しい ECDH 鍵を運ぶので、接続ごとに固有の鍵ができ（`Key` があれば混ぜ込みます）、各パケットにはタグとカウンターが付きます。改ざん・注入・再送されたパケットは捨てられます。暗号化はしません。直接接続のプロトコルが違うプレイヤー同士は互いのパケットを無視し、EOS のままです。

### ネットコードの版の確認

敵や物体への命中は、装甲や弱点を含むゲーム本来の衝突データを保ったまま、撃った側のマシンで処理します。他のプレイヤーや車両への命中は、その所有者のマシンへ送ります。`NetHit` ログでは、再生したイベント数と実際に HP が減った数を区別します。HP が変わらない場合は、装甲、無敵状態、味方への攻撃のルールで防がれた可能性もあります。3.0.2 はネットコードのプロトコル 4 を使うため、この修正を使うには部屋の全員が更新してください。

各プレイヤーは自分のネットコードのプロトコルとオンにしている機能を公開します（`EDF6NET_PROTO`、`EDF6NET_CAPS`）。ホストは部屋全体で動かす機能を公開します（`EDF6NET_ROOMCAPS`）。分割送信、プレイヤー同期、命中判定は、メンバー全員が同じプロトコルでその機能をオンにしているときだけ動きます。1人でもオフなら部屋全体でオフです。`RejectMismatched=1` のとき、ホストはプロトコルの違うプレイヤーを外します。

### そのほかの修正

- **武器ガード**：ほかのプレイヤーの武器があなたの武器テーブルにないとき（相手の MOD が武器を追加している）、部屋の中や出撃時にゲームが落ちなくなりました。部屋では空の枠として表示し、ミッションではあなた自身の同じ兵科・同じ枠の武器になります。
- **クラッシュログ**：クラッシュ時にアドレス、モジュール、呼び出し履歴をログに書きます。`CrashDump=1` ならミニダンプも書きます。

## 困ったとき

- **まずログを見てください**：`Mods\Plugins\EDF6Coop.log`（約 2 MB 以下に保たれます。直接接続の行は `[DN]` で始まります）。`==== EDF6Coop x.y.z ====` の行があればプラグインは読み込まれています。なければ EDFModLoader が正しく入っていません。Linux では上の起動オプションを確認してください。
- `REFUSED: EDF.dll is not the supported build`：ゲームの更新で `EDF.dll` が変わりました。プラグインは何も変えず、ゲームはプラグインなしと同じに動きます。Issue を作ってください。
- `EOS hooks FAILED`：`EDF.dll` のインポートテーブルが想定と違います。プラグインは EOS に触りません。Issue を作ってください。
- `LOBBY ... other players treat us as vanilla`：プラグインがロビー属性を公開できなかったので、ほかのプレイヤーはあなたに回線切れの猶予を与えません。
- `EOS incoming packet queue FULL`：EOS のキューがいっぱいでパケットが捨てられています。ログを添えて Issue を作ってください。
- `RESILIENCE ... RECOVERED`：回線切れを隠せました。`did not come back within`：`GraceSeconds` を過ぎたのでゲームに渡しました。
- `RESILIENCE ... lost Epic's lobby service but the direct link is up` / `back in Epic's lobby service`：Epic のロビーサービスがプレイヤーを外してまた戻しました。ゲームは気づいていません。
- `NETCODE features in room ... off: ...`：部屋の誰かが別のプロトコルか、機能をオフにしています。ログに誰かが出ます。`NETCODE REFUSED`：ホストとしてプロトコルの違うプレイヤーを外しました。
- `STATS last 60s: ...`：ゲームが送受信している間、1 分に 1 行：ゲームの送信量（kbps）、直接接続が回線で実際に使った量（`wire up` / `down`、再送とホストがほかの人のために中継した分を含む）、直接接続と EOS のパケット数、EOS に信頼性ありで送った数、送信失敗、分割数、接続ごとの再送の状態。`send-failures` が 0 でなければログを添付してください。
- `DIRECT ... has not answered for ... ms: game data goes over EOS` / `answers again`：直接接続が一時的に通じず、その間は EOS がゲームを運びました。`... packets the link to ... had not delivered: ... go over EOS` は引き渡した数です。STATS の行ではそのような接続に `NOT ANSWERING` と出ます。
- `DIRECT ... timed out: nothing received for ...`：相手から何も届かなくなりました。`DIRECT ... stalled: a packet the game sent reliably is unacknowledged ...`：相手は応答していますが、ゲームが必要とするパケットが1つ届いていません。
- `DIRECT refused hello for ...`：誰かがあるプレイヤーとして接続しようとして証明できませんでした。入室直後の1～2 秒に `published no direct-link identity` が出るのは普通です（その人の部屋の情報がまだホストに届いていない）。`not signed by the identity that player published` は誰かがそのプレイヤーになりすまそうとして拒否されたことを示します。
- `DIRECT the socket's receive buffer is N bytes, not the 1 MB asked for`（Linux）：ホストでそれを超える一度の通信は捨てられて再送されます。`sudo sysctl -w net.core.rmem_max=1048576 net.core.wmem_max=1048576` で上限を上げてください。
- `UPNP WARNING: the router did not say its WAN address`：ポート開放はできていますが、参加者に IPv4 アドレスを伝えられません。`PublicAddress` を設定してください。
- `DIRECT ... speaks direct-link protocol N, we speak M`：そのプレイヤーは別の版です。2人の間に直接接続はなく、ゲームは EOS で続きます。

## ビルド

### Windows

Windows x64、*C++ によるデスクトップ開発* を入れた Visual Studio 2022（付属の CMake と Ninja を使います）、Python 3.10 以上と `pip install pefile==2024.8.26 capstone==5.0.9`、ゲームのコードに対するテスト用に地球防衛軍6（EDF.dll ビルド `678CCB46`）が必要です。

```powershell
$env:EDF6_GAME_DIR = 'C:\Program Files (x86)\Steam\steamapps\common\EARTH DEFENSE FORCE 6'
powershell -ExecutionPolicy Bypass -File build.ps1 -Test    # -> out\EDF6Coop.dll
powershell -ExecutionPolicy Bypass -File package.ps1        # -> release\EDF6Coop-<バージョン>.zip（+ .sha256）
```

- `-Test` はテストを実行します：単体テスト、ループバックでの直接接続テスト、ゲームのコードに対するテスト（本物の `EDF.dll` と偽の EOS で複数プロセスの部屋を動かす）。ゲームがなければ後者はスキップと報告されます。`-RequireGame` はゲームがないと構成を拒否します。
- ゲームフォルダは読むだけです。
- `build.ps1` は公式 EDFModLoader v1.0.10 の `winmm.dll` を取得し（SHA-256 で確認）、パッケージに入れる修正版ローダーを作ります（`tools/fix_winmm_proxy.py`）。
- `package.ps1` は版の違う DLL やソースより古い DLL を拒否します。

### Linux

`build.sh` は clang で MinGW-w64 向けに同じ DLL をクロスビルドし（`cmake/mingw-w64.cmake`）、専用の Wine プレフィックス（`~/.local/share/wine-edf6coop-tests`）でテストを Wine 上で動かします。Windows SDK や Visual Studio はいりません。Fedora では：

```sh
sudo dnf install clang lld mingw64-gcc-c++ mingw64-winpthreads-static wine cmake ninja-build python3
export EDF6_GAME_DIR="$HOME/.local/share/Steam/steamapps/common/EARTH DEFENSE FORCE 6"
./build.sh -t    # -> out/EDF6Coop.dll。-r は EDF.dll がないと構成を拒否する
```

Python のツールは `build.sh` が `~/.local/share/edf6coop-venv`（`EDF6COOP_VENV` で変更可）に作る仮想環境で動きます。リリース用パッケージは Windows で `build.ps1` と `package.ps1` で作ります。

## リリース

リリース用パッケージは、ゲームのコードに対するテストがすべて動くよう、ゲームのあるマシンで作ります。そのほかのテストは GitHub Actions が動かします（`.github/workflows/ci.yml`）。

1. 版を上げます：`CMakeLists.txt` の `project(EDF6Coop VERSION x.y.z)`。
2. リリースノート `release-notes/<バージョン>.md` を書きます（Release ページの本文、パッケージの `RELEASE_NOTES_EDF6Coop.md`）。
3. `main` にコミットし、タグを付けて push します：`git tag vX.Y.Z && git push origin vX.Y.Z`。
4. ゲームのあるマシンでそのコミットにして：`powershell -ExecutionPolicy Bypass -File release.ps1 -Upload`。`build.ps1 -Test -RequireGame` と `package.ps1` を実行し、`EDF6Coop-<バージョン>.zip` とその `.sha256` を付けた**下書き**の Release を作ります（`-Upload` なしなら `release\upload-<バージョン>\` に置くだけ）。
5. GitHub で下書きを確認して公開します。

## 構成

| パス | 内容 |
|---|---|
| `game/plugin.cpp` | EDFModLoader の入口：`EDF6Coop.ini` を読み、通信部分を起動し、`EDF.dll` にパッチを当てる |
| `game/` | ゲーム部分：パッチ（武器ガード、ハンドシェイクのタイムアウト）、ネットコードの版の確認、プレイヤー同期、命中判定、クラッシュログ、ログ |
| `src/config.*` | 設定とそのコメント（Windows の表示言語に合わせて中国語 / 日本語 / 英語） |
| `src/eos_min.h` | 使っている EOS SDK の構造体（公式 1.15.5 のヘッダーから。ゲームは 1.16.1） |
| `src/eos_hooks.cpp`、`src/iat.*` | `EDF.dll` のインポートテーブルを書き換え、EOS の P2P とロビーの呼び出しを引き受ける |
| `src/hold.*` | 回線切れの猶予 |
| `src/lobby_marker.*` | ロビーのメンバー属性：誰がプラグインを入れているか、ホストのアドレス |
| `src/direct_net.*`、`src/reliable.*`、`src/wire.*`、`src/auth.*`、`src/fragment.*` | 直接接続の通信、分割送信 |
| `src/netif.*`、`src/upnp.*` | 物理アダプターの判別、UPnP |
| `CMakeLists.txt` | ビルドと唯一のバージョン番号（`project(EDF6Coop VERSION x.y.z)`） |
| `dist/` | インストールスクリプト、同梱の説明書（英語 / 中国語 / 日本語）、ローダー修正の説明 |
| `tests/` | テスト：通信部分、`tests/game/` はゲーム部分、`tests/gamenet/` は本物の `EDF.dll` で動かす複数プロセスの部屋 |
| `tools/`、`third_party/` | ローダー修正ツールとその入力、MSVC 環境スクリプト、EDFModLoader のプラグインヘッダーとライセンス |

## 既知の制限

- ミッションの途中からは参加できません（ゲーム自体にその機能がありません）。
- 直すのはパケット損失による同期ズレだけです。ゲームのロジック自体の同期ズレは、具体的な症状がないと解析できません。
- 回線切れの猶予の間、ほかのプレイヤーは同期点で最大 `GraceSeconds` 秒待つことがあります。
- ホストは参加者同士を中継します：ホストから受け手への接続と送り手への接続が同時に切れると、その 2 人の間で中継中のデータは失われます。
- 直接接続の通信は認証されますが暗号化はされません：通信経路上の人は読めますし、いつでも捨てられます（ゲームはそのあと EOS に戻ります）。
- ゲーム終了時に UPnP の設定は消しません（そのときネットワークを呼ぶ安全な時点がありません）。プラグインは次にホストしない起動（`Mode` が `host` 以外、または `UPnP=0`）で消し、`UNINSTALL.bat` も消します。ほかの機器の設定には触りません。ルーターの管理画面で `EDF6DirectNet` という名前の設定を消すこともできます。
- 直接接続そのものが切れているときは何も隠せません：あるプレイヤーの回線が `GraceSeconds` より長く切れたら、ゲームはいつも通り扱います。

## ライセンス

MIT、[LICENSE](LICENSE) を参照。同梱の EDFModLoader は MIT で、そのライセンスはパッケージに `EDFModLoader_LICENSE.txt` として入っています。ゲーム部分（`game/`、最初は momotori01 さんが作成）はパブリックドメインです（[Unlicense](game/LICENSE)）。

## 謝辞

- ゲーム部分は momotori01 さんの EDF6MultiSlot から始まりました：[EARTH-DEFENSE-FORCE-6-VR-MOD](https://github.com/momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD)（パブリックドメイン）。
