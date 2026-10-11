# edf-coop-stable

[English](README.md) | **中文** | [日本語](README.ja.md)

**EDF6Coop** 让地球防卫军6（PC / Steam）的联机任务不再不同步，也不再因为断线而中断。它重发丢失的数据包，在 EOS 重连期间对游戏瞒住短时间的断线，并提供可选的直连房主 UDP 链路。它是一个 [EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) 插件 `EDF6Coop.dll`，支持 Windows，也支持 Linux 上的 Steam Proton。

> 非官方 MOD，与 D3 PUBLISHER / SANDLOT / Epic Games 无关。它只在内存里改动网络调用和少数游戏函数，从不修改游戏文件。设 `[DirectNet] Enabled=0` 或删掉 DLL 即恢复原版。
>
> **状态：实验性。** 可靠发送和断线宽限已在真实四人联机中验证（0.3.2：所有玩家都直连成功，7 次断线游戏完全没察觉）。出问题请提 [Issue](https://github.com/hajisensai/edf-coop-stable/issues) 并附上日志。

## 四人房间

这个版本使用游戏原本最多 4 人的房间。到 2.5.0 为止，EDF6Coop 还有大房间（Player MOD）、房间画面、武器页、房间武器文件、护甲复制和自动更新。这些都已移除，需要的人请继续用 2.5.0。

同一房间的人应使用同一版本。房主会拒绝联机协议版本不同的玩家（`[Netcode] RejectMismatched=1`），因为两边的规则不一样。可靠发送和断线宽限与任何版本都能配合，对没装插件的玩家也有效。

安装时旧的 `EDF6DirectNet.dll` / `EDF6MultiSlot.dll` 会改名为 `.disabled`。它们的设置文件保留，但不再读取。

## 解决的问题

| 症状 | 原因 | 插件做了什么 | 谁需要装 |
|---|---|---|---|
| 任务中途敌人 / 位置 / 血量对不上（不同步） | EDF6 所有联机数据都用 `UnreliableUnordered` 发送，丢一个包就永久丢一份状态 | 改为 `ReliableUnordered`（EOS 自动重发丢失的包；仍不保证顺序，所以游戏看到的情况与以前一样），并把 EOS 收发队列扩大到至少 64 MB | **发送方**。装了之后你发出的数据不再丢；最好所有人都装 |
| 网络抖一下就被踢出任务 | EOS 因超时 / 网络错误关闭连接时，游戏立刻把那个玩家移除 | 对游戏瞒住断线，让 EOS 在后台重连。30 秒内恢复的话游戏完全察觉不到，否则按原版处理 | **双方**（插件会自动识别对方，见下文） |
| 后进房的人一直完不成 P2P 握手，除房主外所有人都退出房间 | 原版遇到这个超时会让客人退出房间 | 客人留在房间里（`KeepRoomOnPeerTimeout=1`） | 每个客人 |
| 走 Epic 中继延迟高，NAT 穿透失败 | 所有流量都走 EOS P2P / 中继 | 房主可以开启直连：别人进房后直接连到房主（星形，房主转发） | 房主设置；加入者只要装了插件 |
| 打中了却没伤害，玩家位置乱跳 | 由被打的一方判定命中；其他玩家的位置发送得很粗 | 全房间都用这个版本时：由开枪的人判定命中，玩家位置带速度用于外推 | 房间里所有人 |
| 掉线了却不知道原因 | | 写日志：NAT 类型、直连还是中继、断线原因、大厅成员、每分钟流量、崩溃信息 | 自己 |

除直连（由房主开启）外，全部默认开启。

## 安装

### Windows

1. 从 [Releases](https://github.com/hajisensai/edf-coop-stable/releases/latest) 下载 `EDF6Coop-<版本>.zip`，**把整个压缩包解压**到任意文件夹。
2. 双击 **`INSTALL.bat`**。它会在 Steam 库里自动找到 EDF6 并安装。
   - 没有 EDFModLoader 时会装上自带的加载器：[BlueAmulet/EDFModLoader](https://github.com/BlueAmulet/EDFModLoader) v1.0.10（MIT）**加上对官方版多线程问题的修复**（它的调用转发共用一个目标变量，多个线程同时调用时可能跳到错误的函数；详见 [dist/LOADER_FIX.md](dist/LOADER_FIX.md)）。已有的 `winmm.dll` 一律不覆盖，唯一例外：与官方 v1.0.10 字节完全相同的会换成修复版，原文件保存为 `winmm.dll.bak-official`。
   - 找不到游戏时会请你输入游戏文件夹（在 Steam 库里右键 EDF6 → 管理 → 浏览本地文件）。
3. 照常从 Steam 启动游戏。第一次启动会生成 `Mods\Plugins\EDF6Coop.ini`（设置）和 `EDF6Coop.log`（日志）。

### Linux（Steam Proton）

1. 解压 zip，在解压出的文件夹里运行 `bash install.sh`。它会在 Steam 库里（原生和 Flatpak 版 Steam）找到 EDF6，并像 `INSTALL.bat` 一样安装；找不到时把游戏文件夹传给它（`bash install.sh "<文件夹>"`）。也可以手动安装：把 zip 里的内容复制进游戏文件夹（有 `EDF6.exe` 的那个；在 Steam 里右键 EDF6 → 管理 → 浏览本地文件），已有的 `winmm.dll` 保留不动。
2. 在 Steam 里右键 EDF6 → 属性，把启动选项设为

   ```
   WINEDLLOVERRIDES="winmm=n,b" %command%
   ```

   不设的话 Wine 会加载它自己的 `winmm.dll`，EDFModLoader 不会运行。
3. 启动游戏。设置文件和日志会像 Windows 上一样出现在 `Mods/Plugins/` 里。

zip 里有 README_EDF6Coop.txt（English）、README_EDF6Coop_zh.txt（中文）和 README_EDF6Coop_ja.txt（日本語）。设置文件的注释按 Windows 显示语言写成中文、日文或英文。

升级：用同样的方法安装新版本，设置会保留。没有自动更新。

卸载：双击 `UNINSTALL.bat`（Linux 上运行 `bash install.sh --uninstall`）。它会删除插件、设置、日志、旧版本的残留，以及插件在路由器上建立的 UPnP 映射（只删指向本机、名为 `EDF6DirectNet` 的那条）。EDFModLoader 和其他 MOD 不会动，包括安装程序升级过的加载器及其备份 `winmm.dll.bak-official`。如果添加过防火墙规则，用管理员身份运行 `UNINSTALL.bat` 会一并删除；没有管理员权限时它会显示删除命令。

## 房主：直连（可选）

只需房主设置。装了插件且 `AutoJoin=1`（默认）的加入者进房后会自动直连房主。

1. 打开 `Mods\Plugins\EDF6Coop.ini`，设 `Mode=host`。
2. Windows：右键游戏文件夹里的 `EDF6Coop_AllowFirewall.bat`，选**以管理员身份运行**（只需一次）。这条规则只放行 `EDF6.exe` 在你 `ListenPort`（未设时为 27015）上的入站 UDP；改了 `ListenPort` 后要再运行一次。Linux：如果有防火墙，放行这个 UDP 端口。
3. 让外网能连到你，任选其一：
   - **自动**：`PublicAddress` 留空。插件用路由器的 UPnP 映射 UDP 端口，同时公布本机的公网 IPv6。
   - **手动**：在路由器上把 UDP 端口转发到本机，再设 `PublicAddress=公网 IP:外部端口`（DDNS 域名也可以，例如 `myroom.ddns.net:40000`）。
   - **电脑直接拨号上网（PPPoE，没有路由器）**：插件识别不了这类网卡，请手动设 `PublicAddress=公网 IP:27015`。
4. 重启游戏，照常建房间。

查看日志 `Mods\Plugins\EDF6Coop.log`：

| 日志 | 含义 |
|---|---|
| `DIRECT players who join your room connect to ...` | 房主地址已确定并写进房间信息 |
| `UPNP router now forwards UDP ...` | UPnP 映射成功 |
| `UPNP no router with UPnP port mapping found` / `UPNP port mapping failed` | 路由器不支持 UPnP 或没开；请手动做端口转发 |
| `UPNP WARNING: the router WAN address ... is private (carrier-grade NAT)` | 运营商没给你公网 IPv4，IPv4 直连连不到你；只能靠 IPv6，或找运营商要公网 IP |
| `UPNP UDP 27015 is already forwarded to ...; left alone` | 这个端口已经转发给局域网里的其他设备，插件不会动它。换一个 `ListenPort` 或手动转发 |
| `DIRECT client ... connected from ...`（房主）/ `DIRECT connected to host ...`（加入者） | 直连已建立 |
| `DIRECT auto-connect stopped (the room host did not answer on any advertised address ...)` | 加入者连不到房主（防火墙、端口转发、`Key` 不一致、EDF6Coop 版本不同）。游戏继续走 Epic，60 秒后插件再试 |

加入者对每个地址尝试 10 秒，先 IPv4 后 IPv6。都连不上就留在 EOS 上，正常游戏不受影响。

**`Key=`** 是可选的暗号，不设也安全。每个装了插件的玩家都会在自己的大厅成员信息里公布本次游戏会话所生成密钥的指纹。房主只在对方证明持有玩家 X 的密钥后才让他以 X 的身份连接，加入者也只接受能证明持有房主密钥的房主。之后每个直连包都带有只有这两方才有的密钥算出的标签，所以其他任何人（房间里的其他玩家、网络路径上的人）都无法冒充别人连接、接管链路，或篡改、注入、重放数据包。`Key` 会作为额外的共享密钥混入这些密钥。它**不会**写进房间信息：房主设了的话，每个加入者都要在自己的 ini 里填一样的 `Key`，否则自动直连失败并回到 EOS。

**隐私**：房主的公网地址在其大厅成员属性里，能看到房间的人都能读到。直连的加入者（`AutoJoin=1`）从自己的公网地址连接房主，房主因此能看到这个地址（EOS 点对点通常也会让双方看到彼此的地址）。设 `AutoJoin=0` 则留在 EOS 上。

## 设置（`EDF6Coop.ini`，改完重启游戏）

| 键 | 默认 | 说明 |
|---|---|---|
| `[DirectNet] Enabled` | `1` | `0` = 插件什么都不装，就是原版 |
| `Mode` | `off` | `off` 普通玩家 / `host` 直连房主 / `join` 手动填房主地址（很少需要） |
| `ListenPort` | `27015` | host：监听的 UDP 端口（转发和放行防火墙的就是它）；join：本机端口，留空 = 自动 |
| `PublicAddress` | 空 | host：告诉别人连哪里。留空 = 公网 IPv6 + UPnP 映射的 IPv4 |
| `AutoJoin` | `1` | 进房后连上房主的直连（房主因此能看到你的公网地址；`0` 留在 EOS） |
| `HostAddress` | 空 | 仅 `Mode=join`：房主地址，例如 `123.45.67.89:27015` / `[2408:8207::5]:27015` |
| `Key` | 空 | 直连暗号，所有人要一样；只用英文字母和数字 |
| `UPnP` | `1` | 当房主时让路由器自动转发端口。只在 `Mode=host` 时使用 |
| `BindPhysicalInterface` | `1` | 直连流量固定走物理网卡，不被 Clash、VPN 或加速器的 TUN 网卡劫持 |
| `LinkTimeoutMs` | `60000` | 多久收不到对方数据就算直连断开（3000 到 300000） |
| `[EOS] FixedPort` | `0` | EOS 使用固定 UDP 端口 `FixedPort` 到 `FixedPort+7`；0 = 随机 |
| `Relay` | `default` | EOS 中继：`default` 不改 / `allow` / `norelay` / `force` |
| `[Sync] ReliableGameTraffic` | `1` | 可靠发送（防不同步）。`0` = 原版 |
| `[Resilience] HoldDisconnects` | `auto` | 断线宽限：`auto` 对装了插件的人 / `off` 原版 / `all` 对所有人、不检测（确定所有人都装了插件时才用） |
| `GraceSeconds` | `30` | 最多瞒住断线多少秒（1 到 600） |
| `KeepRoomOnPeerTimeout` | `1` | 与后进房的人的 P2P 握手一直完不成时留在房间。`0` = 原版 |
| `[Netcode] Fragments` | `1` | 把超过 EOS 1170 字节上限的数据拆开发送。全房间都开才生效 |
| `PlayerSync` | `1` | 玩家位置带速度，其他玩家移动更平滑。全房间都开才生效 |
| `HitAuthority` | `1` | 由开枪的人判定命中。全房间都开才生效 |
| `RejectMismatched` | `1` | 当房主时拒绝联机协议版本不同的玩家 |
| `[Log] CrashLog` | `1` | 崩溃时把地址和调用栈写进日志 |
| `CrashDump` | `0` | 第一次崩溃时另写 `EDF6Coop-crash.dmp`。里面有游戏内存（房间名、聊天），只在被要求时开 |

## 原理

### 可靠发送

`EDF.dll+0x12c8bc0` 里两处 `EOS_P2P_SendPacket` 调用都传 `Reliability = UnreliableUnordered`。插件在导入表里把它改成 `ReliableUnordered`：每个包都恰好送达一次，顺序可能乱。不可靠发送本来就可能乱序，所以游戏逻辑看不到任何新情况，只是包不再丢了。接收方不需要配合。

### 断线宽限与插件识别

对方没装插件的话，对方的游戏照常会把你移除。如果你这边还瞒着断线，两边的游戏就会分叉，所以只对装了插件的玩家给宽限。

EDF6 把收到的每个 EOS 包都当游戏数据读，插件没法通过 P2P 发探测包。它改用大厅的**成员属性**：在房间里时给自己写上 `EDF6DN=<协议>`（`EDF.dll` 没有导入成员属性相关函数，游戏看不到它），断线时检查对方有没有这个属性：

- 有属性且之前连接成功过：给宽限，每 2 秒请 EOS 重连。
- 没有属性：按原版处理。
- 直连成员：只要直连还有回应就给宽限（每秒 ping 四次，菜单里也是）；它的游戏数据本来就不走 EOS。
- Epic 大厅服务报告某成员断开（`DISCONNECTED`：它断开的是 Epic 大厅服务，不是游戏），而该成员的直连还有回应：对游戏瞒住。EOS 再次列出该成员（`JOINED`）时，两个事件都丢掉。直连沉默满 `GraceSeconds`，或者该成员真的离开或被踢，游戏才会知道。对你自己也一样。
- 对方离开了大厅：立刻告诉游戏。

### 直连

房主写入成员属性 `EDF6DN_ADDR`（空格分隔的地址列表）。其他成员每 2 秒读一次，对每个地址尝试 10 秒，先 IPv4 后 IPv6；全部失败的话 60 秒后再试。传输是自己的 UDP 协议：选择性确认、限速重发、每条链路独立的密钥。`IP_UNICAST_IF` 把它绑定到物理网卡。

直连只在对方有回应时承载游戏数据：每秒 ping 四次，大约 1.5 秒收不到回应（pong 或确认）就不再承载。它还没送达的数据改走 EOS（可靠发送），之后游戏的数据也走 EOS，直到直连重新回应满一秒。房主在加入者之间转发；送不到某个加入者的数据会退回给发送方，由发送方改走 EOS，其他加入者也不再经房主发给那个加入者。

每个装了插件的玩家还会写 `EDF6DN_ID`：本次游戏会话生成的 ECDSA P-256 密钥的 SHA-256 指纹。连接从一次 cookie 往返开始：房主对 hello 回一个绑定发送方地址的 cookie，在它回来之前不保存任何东西，所以伪造地址和 hello 洪泛都没有用。加入者带着 cookie 重发 hello，并用自己的密钥签名。只有当 X 在房间里并公布了该密钥的指纹时，房主才接受它作为 EOS ID X；之前会话的已签名 hello 会被当作重放拒绝。加入者只接受由房主公布的密钥签名的 welcome。hello 和 welcome 在这些签名下携带新的 ECDH 密钥，所以每条链路都有自己的密钥（设了 `Key` 时混入），每个包都带标签和计数器：被篡改、注入或重放的包会被丢弃。没有加密。直连协议不同的玩家会忽略彼此的包，互相之间留在 EOS 上。

### 联机协议版本门槛

每个玩家公布自己的联机协议和开启的功能（`EDF6NET_PROTO`、`EDF6NET_CAPS`）。房主公布整个房间运行的功能（`EDF6NET_ROOMCAPS`）。拆包发送、玩家同步和命中判定只在每个成员都运行相同协议并开启该功能时才生效；一个没开的玩家就会让整个房间关闭该功能。`RejectMismatched=1` 时房主会移除协议不同的玩家。

### 其他修复

- **武器保护**：其他玩家的武器在你的武器表里没有时（对方的 MOD 加了武器），你的游戏不会再在房间里或出击时崩溃。房间里显示为空栏；任务里变成你自己同类别同栏位的武器。
- **崩溃日志**：崩溃时日志里会写下地址、模块和调用栈；`CrashDump=1` 时另写 minidump。

## 排查问题

- **先看日志**：`Mods\Plugins\EDF6Coop.log`（大小保持在约 2 MB 以内；直连相关的行以 `[DN]` 开头）。有 `==== EDF6Coop x.y.z ====` 这一行说明插件已加载。没有的话就是 EDFModLoader 没装好；Linux 上请检查上面的启动选项。
- `REFUSED: EDF.dll is not the supported build`：游戏更新改变了 `EDF.dll`。插件什么都不改，游戏照常运行。请提 Issue。
- `EOS hooks FAILED`：`EDF.dll` 的导入表与预期不符，插件不碰 EOS。请提 Issue。
- `LOBBY ... other players treat us as vanilla`：插件没能公布大厅属性，其他玩家不会给你断线宽限。
- `EOS incoming packet queue FULL`：EOS 队列满了，正在丢包。请提 Issue 并附上日志。
- `RESILIENCE ... RECOVERED`：成功瞒住了一次断线。`did not come back within`：超过 `GraceSeconds` 后交给了游戏。
- `RESILIENCE ... lost Epic's lobby service but the direct link is up` / `back in Epic's lobby service`：Epic 大厅服务把某玩家断开又放了回来，游戏完全没看到。
- `NETCODE features in room ... off: ...`：房间里有人运行别的协议或关了某个功能，日志会写出是谁。`NETCODE REFUSED`：你作为房主移除了一个协议不同的玩家。
- `STATS last 60s: ...`：游戏有收发时每分钟一行：游戏发送量（kbps）、直连在线路上的实际用量（`wire up` / `down`，含重发，以及房主为别人转发的量）、直连和 EOS 的包数、以可靠方式发给 EOS 的包数、发送失败数、拆包数，以及每条链路的重发状态。`send-failures` 不为 0 时请附上日志。
- `DIRECT ... has not answered for ... ms: game data goes over EOS` / `answers again`：直连暂时不通，期间由 EOS 承载游戏。`... packets the link to ... had not delivered: ... go over EOS` 写出转交了多少。STATS 行里这样的链路显示 `NOT ANSWERING`。
- `DIRECT ... timed out: nothing received for ...`：对方不再发来任何数据。`DIRECT ... stalled: a packet the game sent reliably is unacknowledged ...`：对方还在回应，但游戏需要的某个包一直没送到。
- `DIRECT refused hello for ...`：有人想以某玩家身份直连却证明不了。玩家刚进房后一两秒内出现 `published no direct-link identity` 是正常的（他的房间信息还没传到房主）。`not signed by the identity that player published` 表示有人冒充该玩家，已被拒绝。
- `DIRECT the socket's receive buffer is N bytes, not the 1 MB asked for`（Linux）：房主超出这个大小的突发流量会被丢弃再重发。用 `sudo sysctl -w net.core.rmem_max=1048576 net.core.wmem_max=1048576` 调高上限。
- `UPNP WARNING: the router did not say its WAN address`：映射已建立，但插件无法告诉加入者你的 IPv4 地址。请设置 `PublicAddress`。
- `DIRECT ... speaks direct-link protocol N, we speak M`：该玩家运行的是别的版本。你们之间没有直连，游戏继续走 EOS。

## 构建

### Windows

需要 Windows x64、装了 *使用 C++ 的桌面开发* 的 Visual Studio 2022（用它自带的 CMake 和 Ninja）、Python 3.10+ 并 `pip install pefile==2024.8.26 capstone==5.0.9`，以及用于针对游戏代码测试的地球防卫军6（EDF.dll 版本 `678CCB46`）。

```powershell
$env:EDF6_GAME_DIR = 'C:\Program Files (x86)\Steam\steamapps\common\EARTH DEFENSE FORCE 6'
powershell -ExecutionPolicy Bypass -File build.ps1 -Test    # -> out\EDF6Coop.dll
powershell -ExecutionPolicy Bypass -File package.ps1        # -> release\EDF6Coop-<版本>.zip（+ .sha256）
```

- `-Test` 运行测试：单元测试、本机回环的直连测试，以及针对游戏代码的测试（用真实的 `EDF.dll` 和假的 EOS 跑多进程房间）。没有游戏时后者报告为跳过；`-RequireGame` 在没有游戏时拒绝配置。
- 游戏文件夹只读不写。
- `build.ps1` 会下载官方 EDFModLoader v1.0.10 的 `winmm.dll`（校验 SHA-256），并生成安装包附带的修复版加载器（`tools/fix_winmm_proxy.py`）。
- `package.ps1` 拒绝版本不对或比源码旧的 DLL。

### Linux

`build.sh` 用 clang 针对 MinGW-w64 交叉编译同一个 DLL（`cmake/mingw-w64.cmake`），并在 Wine 下、在专用的 Wine 前缀（`~/.local/share/wine-edf6coop-tests`）里运行测试。不需要 Windows SDK 或 Visual Studio。Fedora 上：

```sh
sudo dnf install clang lld mingw64-gcc-c++ mingw64-winpthreads-static wine cmake ninja-build python3
export EDF6_GAME_DIR="$HOME/.local/share/Steam/steamapps/common/EARTH DEFENSE FORCE 6"
./build.sh -t    # -> out/EDF6Coop.dll；-r 在没有 EDF.dll 时拒绝配置
```

Python 工具在 `build.sh` 于 `~/.local/share/edf6coop-venv`（可用 `EDF6COOP_VENV` 覆盖）中创建的虚拟环境里运行。发布包在 Windows 上用 `build.ps1` 和 `package.ps1` 构建。

## 发布

发布包在装有游戏的机器上构建，这样针对游戏代码的测试都会运行。GitHub Actions 运行其余测试（`.github/workflows/ci.yml`）。

1. 改版本号：`CMakeLists.txt` 里的 `project(EDF6Coop VERSION x.y.z)`，以及三份附带说明 `dist/README_EDF6Coop*.txt` 的第一行。
2. 写发布说明 `release-notes/<版本>.md`（Release 页面的正文，也是安装包里的 `RELEASE_NOTES_EDF6Coop.md`）。
3. 提交到 `main`，打标签并推送标签：`git tag vX.Y.Z && git push origin vX.Y.Z`。
4. 在装有游戏的机器上、在那个提交处运行：`powershell -ExecutionPolicy Bypass -File release.ps1 -Upload`。它运行 `build.ps1 -Test -RequireGame` 和 `package.ps1`，然后创建一个**草稿** Release，附上 `EDF6Coop-<版本>.zip` 及其 `.sha256`（不带 `-Upload` 时只把它们放到 `release\upload-<版本>\`）。
5. 在 GitHub 上检查草稿并发布。

## 目录结构

| 路径 | 内容 |
|---|---|
| `game/plugin.cpp` | EDFModLoader 入口：读取 `EDF6Coop.ini`，启动传输部分，修补 `EDF.dll` |
| `game/` | 游戏部分：补丁（武器保护、握手超时）、联机协议版本门槛、玩家同步、命中判定、崩溃日志、日志 |
| `src/config.*` | 设置及其注释（按 Windows 显示语言为中文 / 日文 / 英文） |
| `src/eos_min.h` | 用到的 EOS SDK 结构体（来自官方 1.15.5 头文件；游戏用的是 1.16.1） |
| `src/eos_hooks.cpp`、`src/iat.*` | 修改 `EDF.dll` 导入表，接管 EOS P2P 和大厅调用 |
| `src/hold.*` | 断线宽限 |
| `src/lobby_marker.*` | 大厅成员属性：谁装了插件、房主地址 |
| `src/direct_net.*`、`src/reliable.*`、`src/wire.*`、`src/auth.*`、`src/fragment.*` | 直连传输、拆包 |
| `src/netif.*`、`src/upnp.*` | 物理网卡识别、UPnP |
| `CMakeLists.txt` | 构建和唯一的版本号（`project(EDF6Coop VERSION x.y.z)`） |
| `dist/` | 安装脚本、附带说明（英文 / 中文 / 日文）和加载器修复说明 |
| `tests/` | 测试：传输部分；`tests/game/` 游戏部分；`tests/gamenet/` 用真实 `EDF.dll` 跑的多进程房间 |
| `tools/`、`third_party/` | 加载器修复工具及其输入、MSVC 环境脚本；EDFModLoader 的插件头文件和许可证 |

## 已知限制

- 不能在任务中途加入（游戏本身没有这个功能）。
- 只修复丢包导致的不同步；游戏逻辑本身的不同步需要具体症状才能逆向。
- 断线宽限期间其他玩家可能会在同步点等待，最多 `GraceSeconds` 秒。
- 房主在加入者之间转发：房主到接收方和到发送方的链路同时断开时，房主正在两者之间转发的数据会丢失。
- 直连流量有认证但没有加密：网络路径上的人能读到，也总能丢弃它（游戏随后回到 EOS）。
- 游戏退出时不会删除 UPnP 映射（那时没有安全的时机做网络调用）。插件会在下一次不当房主的启动时删除它（`Mode` 不是 `host`，或 `UPnP=0`），`UNINSTALL.bat` 也会删除。它从不碰其他设备的映射。也可以在路由器管理页面删除名为 `EDF6DirectNet` 的映射。
- 直连本身断了时什么都瞒不住：某个玩家自己的网络断开超过 `GraceSeconds`，游戏会照常处理。

## 许可

MIT，见 [LICENSE](LICENSE)。附带的 EDFModLoader 是 MIT 许可，其许可证以 `EDFModLoader_LICENSE.txt` 包含在安装包中。游戏部分（`game/`，最初由 momotori01 编写）属于公有领域（[Unlicense](game/LICENSE)）。

## 致谢

- 游戏部分源自 momotori01 的 EDF6MultiSlot：[EARTH-DEFENSE-FORCE-6-VR-MOD](https://github.com/momotori01/EARTH-DEFENSE-FORCE-6-VR-MOD)（公有领域）。
