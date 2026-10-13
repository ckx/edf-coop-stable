EDF6Coop - 地球防衛軍6 のオンライン協力プレイを安定させるプラグイン
プロジェクトページ：https://github.com/hajisensai/edf-coop-stable
English: README_EDF6Coop.txt / 中文: README_EDF6Coop_zh.txt
問題があれば Issue を作り、Mods\Plugins\EDF6Coop.log を添付してください。

EDF6Coop は EDFModLoader のプラグイン1つ（Mods\Plugins\EDF6Coop.dll）で、オンラインのミッションが同期ズレを
起こしたり、回線切れで終わったりしないようにします：
- 失われたゲームのパケットを再送します（同期ズレ対策）。
- 短い回線切れはゲームに知らせず、その間に EOS が裏で再接続します（最大 30 秒）。
- 後から入った人との P2P ハンドシェイクが完了しなくても、部屋に残ります。
- 任意で、Epic のリレーが遅いときや NAT 越えに失敗するときにホストへ直接接続します。
- 部屋の全員がこの版のとき：撃った側が命中を判定し、プレイヤーの位置に速度が付きます。
敵や物体への命中は、装甲や弱点を含むゲーム本来の衝突データを保ち、撃った側で処理します。
他のプレイヤーや車両への命中は所有者へ送ります。NetHit ログではイベントの再生数と HP が減った数を
区別します。装甲、無敵状態、味方への攻撃のルールで HP が減らない場合もあります。
3.0.3 はネットコードのプロトコル 4 を使うため、この修正を使うには部屋の全員が更新してください。
自分のレーダーでは、アイテムが種類の色になります：回復白、アーマーと武器は緑
（[Display] RadarItemColors=0 で全部緑のまま）。
ゲーム本来の最大4人の部屋で遊びます。2.5.0 までの大きな部屋（Player MOD）、部屋画面、武器ページ、部屋の
武器ファイル、アーマーコピー、自動更新はなくなりました。必要な人は 2.5.0 のままにしてください。部屋の全員が
同じ版を使ってください。ホストはネットコードのプロトコルが違うプレイヤーを入れません
（[Netcode] RejectMismatched=1）。
プレイヤーの位置補正では、移動量を実際の経過時間で割って速度を求めます。フレーム間隔が変わっても、正しく動いている他のプレイヤーを前後に押し戻さないためです。

[NetPlayer] MaxExtrapolateMs の既定値は診断版5と同じ350 msです。INI の指定値は保持します。短い更新間隔の空白を補いますが、停止や方向転換では先読みが行き過ぎる場合があります。古いデータへのフォールバックは400 msのままです。

レーザー診断は30秒に最大8行の LASER sample と、その後のレーザー処理時に集計を記録します。所有者、位置、方向、経過フレームと寿命、両端、描画寸法、描画コールバック数を記録します。receive-before はレーザー固有の受信処理であり、全発射通知の数ではありません。描画コールバックだけでは画面上に見えたとは断定できず、初回描画前の0回も異常の証拠ではありません。ダメージ同期の成否もこの診断だけでは判断できません。表示が食い違った時刻を控え、終了後に射手と観察者のログを集めてください。
[Diagnostics] Laser=0: レーザー診断を無効にします。

■ 導入・削除
Windows：ZIP を全部展開して INSTALL.bat をダブルクリックし、いつも通り Steam から起動します。（ZIP の中身を
EDF6.exe のあるゲームフォルダにそのままドロップしても同じです。中身の配置はゲームフォルダと同じです。）
Linux（Steam Proton）：ZIP を全部展開してそこで "bash install.sh" を実行し（Steam ライブラリからゲームを
見つけます。"bash install.sh <ゲームフォルダ>" でも可）、Steam でゲームの起動オプション（プロパティ）を
    WINEDLLOVERRIDES="winmm=n,b" %command%
にします。これがないと Wine が自前の winmm.dll を読み込み、EDFModLoader が動きません。
EDFModLoader（winmm.dll）がなければ同梱のものを入れます：公式 v1.0.10 にマルチスレッドの不具合修正を
加えたものです（LOADER_FIX.md）。既存の winmm.dll は上書きしません。ただし公式 v1.0.10 とバイト単位で
同じものだけは修正版に置き換え、元のファイルを winmm.dll.bak-official に保存します。
EDF6DirectNet / EDF6MultiSlot からの更新：Mods\Plugins にあるそれらの DLL は .disabled に改名されます。古い設定
ファイルは残りますが読み込まれません。2.5.0 までが Mods に書いたメニューレイアウトと HUD のファイル
（UI\LYT_MAINFRAME.SGO、HUD\ONLINEHUDTEXTURE.RAB）は初回起動時に消えます。ほかの MOD の同名ファイルはそのままです。
更新：新しい版を同じ方法で入れてください。設定は残ります。
削除：UNINSTALL.bat をダブルクリックします（管理者として実行するとファイアウォールの規則も消します）。
EDF6Coop のファイル・設定・ログ・UPnP の設定を消します。EDFModLoader とほかの MOD はそのままです。Linux では
"bash install.sh --uninstall" を実行してください。UPnP の設定は残ります。ホストしない次のゲーム起動で
消えるか、ルーターで EDF6DirectNet という項目を消してください。

■ 直接接続（任意、ホストだけ）
あなたの部屋に入った人があなたへ直接つなぎます。INI のコメント（Windows の表示言語に合わせて中国語・日本語・
英語）がすべての設定を説明しています。
1. EDF6Coop.ini の [DirectNet] セクションで Mode=host にします。
2. Windows：ゲームフォルダの EDF6Coop_AllowFirewall.bat を右クリック → 管理者として実行（一度だけ。ListenPort を
   変えたらもう一度）。
3. 外から届くようにします：PublicAddress を空のままにする（プラグインが UPnP で UDP ポートを開け、グローバル
   IPv6 も知らせます）か、自分で UDP ポートを開けて PublicAddress=グローバル IP:ポート にします。
4. いつも通り部屋を作ります。ログに "DIRECT client ... connected" と出ればプレイヤーが直接つながっています。
参加する側は何もしなくて構いません。部屋の情報にホストのアドレスがあります。つながらないときはいつも通り
Epic 経由で遊べます。Key= は任意の合言葉です。設定したら参加者全員が同じ Key を入れてください。

■ ログ
Mods\Plugins\EDF6Coop.log：すべてを1つのログに書きます（直接接続の行は [DN] で始まります）。約 2 MB 以下に
保たれます。不具合の報告に添付してください。

■ 無効にする
EDF6Coop.ini の [DirectNet] Enabled=0：プラグインは自分で外れ、ゲームは元のままです。

■ 同梱物
- EDF6Coop：Mods\Plugins\EDF6Coop.dll（MIT、LICENSE.txt。ゲーム部分は momotori01 さんが EDF6MultiSlot として
  書いたもので、パブリックドメインです）。
- BlueAmulet さんの EDFModLoader（MIT、EDFModLoader_LICENSE.txt）：winmm.dll。LOADER_FIX.md の修正入り。
