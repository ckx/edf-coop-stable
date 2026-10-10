# The bundled EDFModLoader (winmm.dll)

**English** | [中文](#中文) | [日本語](#日本語)

The `winmm.dll` in this package is EDFModLoader v1.0.10 by BlueAmulet (MIT, `EDFModLoader_LICENSE.txt`) with one fix. The official build forwards 180 Windows functions through one shared variable that holds the call target, so two threads calling at once can land in the wrong function. In the fixed build each forwarder jumps to its own function directly. Nothing else in the loader changes: the differences are 2,160 bytes in the 180 forwarders, all listed in `loader-fix.json`.

- Fixed `winmm.dll` SHA-256: `BE94E1FAC0CA12C41B6924E2EB168851641C999CE951D2A5A9FAEA5161B0F9A3`
- Official v1.0.10 SHA-256: `B80E4DA6AE7264F0E9774C992DE3C5F9B6E9BC9422146ADEEB5BB2BD681E112E`

The installer puts it in place only when there is no `winmm.dll`, or when the one there is the official v1.0.10 file (kept as `winmm.dll.bak-official`). Any other loader stays. Packs that bundle EDF6Coop should ship this `winmm.dll` with it, not the official one. The fix is made at build time by `tools/fix_winmm_proxy.py` from the pinned official file.

## 中文

本安装包里的 `winmm.dll` 是 BlueAmulet 的 EDFModLoader v1.0.10（MIT，见 `EDFModLoader_LICENSE.txt`），只修了一处。官方版通过一个共用的变量保存调用目标，转发 180 个 Windows 函数，两个线程同时调用时可能跳到错误的函数。修复版的每个转发入口直接跳到自己的函数。加载器的其他部分没有变化：差异只有 180 个转发入口里的 2,160 字节，全部列在 `loader-fix.json` 里。

- 修复版 `winmm.dll` SHA-256：`BE94E1FAC0CA12C41B6924E2EB168851641C999CE951D2A5A9FAEA5161B0F9A3`
- 官方 v1.0.10 SHA-256：`B80E4DA6AE7264F0E9774C992DE3C5F9B6E9BC9422146ADEEB5BB2BD681E112E`

安装程序只在没有 `winmm.dll`，或者已有的是官方 v1.0.10 文件时放入它（原文件保存为 `winmm.dll.bak-official`），其他加载器不动。附带 EDF6Coop 的整合包应同时附带这个 `winmm.dll`，而不是官方版。修复在构建时由 `tools/fix_winmm_proxy.py` 从固定的官方文件生成。

## 日本語

このパッケージの `winmm.dll` は BlueAmulet さんの EDFModLoader v1.0.10（MIT、`EDFModLoader_LICENSE.txt`）に修正を 1 つ加えたものです。公式版は 180 個の Windows 関数を、呼び出し先を入れる 1 つの共有変数を通して転送しており、2 つのスレッドが同時に呼ぶと別の関数へ飛ぶことがあります。修正版ではそれぞれの転送口が自分の関数へ直接飛びます。ローダーのほかの部分は同じで、違いは 180 個の転送口の 2,160 バイトだけです（すべて `loader-fix.json` にあります）。

- 修正版 `winmm.dll` SHA-256：`BE94E1FAC0CA12C41B6924E2EB168851641C999CE951D2A5A9FAEA5161B0F9A3`
- 公式 v1.0.10 SHA-256：`B80E4DA6AE7264F0E9774C992DE3C5F9B6E9BC9422146ADEEB5BB2BD681E112E`

インストーラーは `winmm.dll` がないとき、またはあるものが公式 v1.0.10 そのものであるときだけこれを置きます（元のファイルは `winmm.dll.bak-official`）。ほかのローダーはそのままです。EDF6Coop を同梱するパックは、公式版ではなくこの `winmm.dll` を一緒に入れてください。修正はビルド時に `tools/fix_winmm_proxy.py` が固定の公式ファイルから作ります。
