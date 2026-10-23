EDF6Coop - 地球防卫军6 联机稳定插件
项目主页：https://github.com/hajisensai/edf-coop-stable
English: README_EDF6Coop.txt / 日本語: README_EDF6Coop_ja.txt
遇到问题请提 Issue，并附上 Mods\Plugins\EDF6Coop.log。

EDF6Coop 是一个 EDFModLoader 插件（Mods\Plugins\EDF6Coop.dll），让联机任务不再不同步、不再因为断线而中断：
- 丢失的游戏数据包自动重发（防不同步）；
- 短时间断线时先瞒住游戏，等 EOS 在后台重连（最多 30 秒）；
- 后进房的人与你的 P2P 握手一直连不上时，你留在房间里；
- 可选：Epic 中继延迟高或 NAT 穿透失败时，直连房主；
- 全房间都用这个版本时：由开枪的人判定命中，玩家位置带速度信息。
对敌人和物体的命中在开枪者的机器上结算，保留完整碰撞数据，包括装甲和弱点。
对其他玩家和载具的命中发送给其所属机器。NetHit 日志区分事件重放数与实际扣血次数；
装甲、无敌或友伤规则也可能阻止扣血。3.0.4 使用联机协议 4，房间内所有人都需更新才能使用这些修复。
自己的雷达上，道具按种类着色：回复白、护甲和武器绿（[Display] RadarItemColors=0 保持全绿）。
它使用游戏原本最多 4 人的房间。2.5.0 及更早版本的大房间（Player MOD）、房间画面、武器页、房间武器文件、
护甲复制和自动更新都已移除；需要这些功能的人请继续用 2.5.0。同一房间的人应使用同一版本：房主会拒绝联机
协议版本不同的玩家（[Netcode] RejectMismatched=1）。
玩家位置校正按实际经过的时间计算每秒速度，避免帧间隔变化让本来移动正确的远端玩家受到前后交替的校正。物理没有执行的校正（着陆、硬直、向下推时的地面）不再被当作反方向的移动，因此不会累积成猛推。发送的速度按其他人收到位置的 100 ms 区间测量，而不是最近的 33 ms：只测 33 ms 时，冲刺的开始会被当作停止发出。

[NetPlayer] MaxExtrapolateMs 默认350 ms，与诊断版5一致，保留 INI 中的显式设置。较长预测可跨过短暂更新间隔，但停止或转向时可能预测过头。数据过期回退仍为400 ms。

激光诊断每30秒最多记录8条 LASER sample，并在随后的激光回调中输出活动汇总。记录可用的持有者身份、位置、朝向、帧龄、寿命、两端、绘制尺寸及绘制回调数。receive-before 表示激光自身的网络回调，并非全部开火通知。绘制回调不证明屏幕上确实可见，首次绘制之前的0次回调也不能证明光束缺失。这些诊断不能确认伤害是否同步。发现光束不一致时请记下时间，退出游戏后收集射手和观察者的日志。
[Diagnostics] Laser=0: 关闭激光诊断。

默认开启玩家移动诊断。检测到至少0.25 m的校正、瞬移或至少200 ms的样本年龄时，记录此前最多8帧，以及当前和后续共16帧，仅记录实际执行校正的帧。每个对象每30秒最多触发一次，所有对象合计每30秒最多输出96行 PLAYER motion。内容包括收到的位置和速度、物理代理与显示对象的位置、实测位移、请求的校正量和估算的本地速度。t 是执行钩子时的毫秒时间，历史帧会延后输出。这些记录不能证明物理系统实际应用了请求的校正。设置 [Diagnostics] Player=0 可关闭。报告抖动时，请记下发生时间，并在退出游戏后收集房主和出现症状的加入者日志。

[安装 / 卸载]
Windows：解压整个 zip，双击 INSTALL.bat，然后照常从 Steam 启动游戏。（也可以把 zip 里的内容直接拖进游戏
文件夹，和 EDF6.exe 放在一起：压缩包里的目录结构就是游戏文件夹的结构。）
Linux（Steam Proton）：解压整个 zip，在那里运行 "bash install.sh"（它会在 Steam 库里找到游戏；也可以
"bash install.sh <游戏文件夹>"），然后在 Steam 里把游戏的启动选项（属性）设为
    WINEDLLOVERRIDES="winmm=n,b" %command%
不设的话 Wine 会加载它自己的 winmm.dll，EDFModLoader 不会运行。
没有 EDFModLoader（winmm.dll）时会装上自带的：官方 v1.0.10 加多线程问题的修复（见 LOADER_FIX.md）。
已有的 winmm.dll 一律不覆盖，唯一例外是与官方 v1.0.10 字节完全相同的那个：会换成修复版，原文件保存为
winmm.dll.bak-official。
从 EDF6DirectNet / EDF6MultiSlot 升级：Mods\Plugins 里它们的 DLL 会改名为 .disabled。旧设置文件保留，但不再
读取。2.5.0 及更早版本在 Mods 下写出的菜单布局和 HUD 文件（UI\LYT_MAINFRAME.SGO、HUD\ONLINEHUDTEXTURE.RAB）
会在第一次启动时删除；其他 MOD 的同名文件不动。
升级：用同样的方法安装新版本，设置会保留。
卸载：双击 UNINSTALL.bat（用管理员身份运行可同时删除防火墙规则）。它会删除 EDF6Coop 的文件、设置、日志和
它建立的 UPnP 映射；EDFModLoader 和其他 MOD 不会动。Linux 上运行 "bash install.sh --uninstall"；
UPnP 映射会保留，下次不当房主启动游戏时删除，也可以在路由器上删掉名为 EDF6DirectNet 的条目。

[直连（可选，只需房主设置）]
进你房间的玩家会直接连到你。INI 里的注释（按 Windows 显示语言写成中文 / 日文 / 英文）解释了每一项设置。
1. 在 EDF6Coop.ini 的 [DirectNet] 段设 Mode=host。
2. Windows：右键游戏文件夹里的 EDF6Coop_AllowFirewall.bat → 以管理员身份运行（只需一次；改了 ListenPort
   要再运行一次）。
3. 让外网能连到你：PublicAddress 留空（插件会用 UPnP 映射 UDP 端口，并同时公布你的公网 IPv6），或者自己做
   UDP 端口转发，再设 PublicAddress=公网 IP:端口。
4. 照常建房间。日志里出现 "DIRECT client ... connected" 就说明有玩家直连上了。
加入的人什么都不用做：房间信息里就有房主地址。连不上时照常走 Epic，不影响游戏。Key= 是可选的暗号：
设了的话每个加入者都要填一样的 Key。

[日志]
Mods\Plugins\EDF6Coop.log：所有内容都在这一份日志里（直连相关的行以 [DN] 开头），大小保持在约 2 MB 以内。
报告问题时请附上它。

[关闭]
EDF6Coop.ini 里设 [DirectNet] Enabled=0：插件自行卸载，游戏就是原版。

[包含的内容]
- EDF6Coop：Mods\Plugins\EDF6Coop.dll（MIT，见 LICENSE.txt；游戏部分最初由 momotori01 作为 EDF6MultiSlot
  编写，属于公有领域）。
- BlueAmulet 的 EDFModLoader（MIT，见 EDFModLoader_LICENSE.txt）：winmm.dll，含 LOADER_FIX.md 所述的修复。
