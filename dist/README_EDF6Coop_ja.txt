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
ゲーム本来の最大4人の部屋で遊びます。2.5.0 までの大きな部屋（Player MOD）、部屋画面、武器ページ、部屋の
武器ファイル、アーマーコピー、自動更新はなくなりました。必要な人は 2.5.0 のままにしてください。部屋の全員が
同じ版を使ってください。ホストはネットコードのプロトコルが違うプレイヤーを入れません
（[Netcode] RejectMismatched=1）。

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
