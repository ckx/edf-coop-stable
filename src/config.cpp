#include "config.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace dn {
namespace {

// The default settings file. Keys and defaults are written once; only the comments are translated
// (Chinese, Japanese, English), so the three files can never disagree on a setting. Written as UTF-8
// with BOM so Notepad shows the comments (Ini skips the BOM).
struct IniEntry {
    const char* setting;     // "Key=default", or "[Section]"
    const char* comment[3];  // zh, ja, en; lines separated by '\n'
};

const char* const kIniTitle[3] = {
    "EDF6Coop 设置（改完重启游戏生效）",
    "EDF6Coop 設定（変更後はゲームを再起動すると反映されます）",
    "EDF6Coop settings (restart the game after editing)",
};

const IniEntry kIniEntries[] = {
    {"[DirectNet]", {}},
    {"Enabled=1",
     {"1 启用 / 0 关闭插件（关闭后与原版完全一样）",
      "1 有効 / 0 無効（無効にすると元のゲームと完全に同じ）",
      "1 on / 0 off (off = exactly the original game)"}},
    {"Mode=off",
     {"公网直连：off 平时 / host 我当房主（别人进我房间会自动直连我）/ join 手动指定房主地址",
      "直接接続：off 通常 / host 自分がホスト（部屋に入った人が自動で直接つないでくる）/ join ホストのアドレスを手動指定",
      "Direct link: off normal / host I host (players who join my room connect to me directly) / join enter the host address by hand"}},
    {"ListenPort=27015",
     {"host：监听的 UDP 端口（端口转发、防火墙放行的就是它）；join：本机端口，0 = 自动",
      "host：待ち受ける UDP ポート（ポート開放・ファイアウォール許可の対象）。join：自分側のポート、0 = 自動",
      "host: UDP port to listen on (the one to forward and allow in the firewall); join: local port, 0 = automatic"}},
    {"PublicAddress=",
     {"host：告诉别人连哪里。留空 = 自动（公网 IPv6 + UPnP 映射的 IPv4）\n"
      "  自己做了端口映射就手动填公网地址:外部端口，例：123.45.67.89:40000 / myroom.ddns.net:40000",
      "host：参加者の接続先。空欄 = 自動（グローバル IPv6 + UPnP で開けた IPv4）\n"
      "  自分でポートを開けた場合はグローバルアドレス:外部ポートを記入。例：123.45.67.89:40000 / myroom.ddns.net:40000",
      "host: where other players connect. Empty = automatic (public IPv6 + IPv4 mapped via UPnP)\n"
      "  Forwarded a port yourself? Enter public address:external port, e.g. 123.45.67.89:40000 / myroom.ddns.net:40000"}},
    {"AutoJoin=1",
     {"进房后自动直连房主（房主设了 Mode=host 才会生效）。1 开 / 0 关",
      "部屋に入ったらホストへ自動で直接接続（ホストが Mode=host のときのみ）。1 オン / 0 オフ",
      "Connect directly to the room host after joining (only when the host uses Mode=host). 1 on / 0 off"}},
    {"HostAddress=",
     {"手动模式 join 才用：房主地址，例：123.45.67.89:27015 / [2408:8207::5]:27015",
      "手動モード join のみ：ホストのアドレス。例：123.45.67.89:27015 / [2408:8207::5]:27015",
      "Manual join mode only: the host address, e.g. 123.45.67.89:27015 / [2408:8207::5]:27015"}},
    {"Key=",
     {"可选暗号，只用英文字母和数字。不会自动发给别人：房主设了，每个加入者都要填一样的，否则自动直连失败",
      "任意の合言葉（英数字のみ）。自動では伝わりません：ホストが設定したら参加者全員が同じものを記入しないと自動直接接続は失敗します",
      "Optional shared key, letters and digits only. It is not sent to anyone: if the host sets one, every player who joins must enter the same key or auto-connect fails"}},
    {"UPnP=1",
     {"host 时自动让路由器做端口映射（UPnP）",
      "host のときルーターに自動でポートを開けてもらう（UPnP）",
      "host: ask the router to forward the port automatically (UPnP)"}},
    {"BindPhysicalInterface=1",
     {"直连流量固定走物理网卡，不被 Clash / 加速器的 TUN 网卡劫持",
      "直接接続の通信を物理 NIC に固定し、Clash や VPN の TUN アダプターに横取りされないようにする",
      "Keep direct-link traffic on the physical network adapter so Clash / VPN TUN adapters cannot capture it"}},
    {"LinkTimeoutMs=60000",
     {"直连多久收不到对方任何数据才算断开（毫秒）",
      "相手から何も届かない状態が何ミリ秒続いたら直接接続が切れたとみなすか",
      "Milliseconds without any data from the other side before the direct link counts as lost"}},
    {"[EOS]", {}},
    {"FixedPort=0",
     {"游戏原本联机的固定 UDP 端口（占用 FixedPort ~ FixedPort+7），0 = 随机。一般不用改",
      "ゲーム本来のオンライン通信の固定 UDP ポート（FixedPort ～ FixedPort+7 を使用）、0 = ランダム。通常は変更不要",
      "Fixed UDP ports for the game's own online traffic (uses FixedPort to FixedPort+7), 0 = random. Usually leave it"}},
    {"Relay=norelay",
     {"EOS 中继：norelay 不用 Epic 中继 / allow 允许 / force 强制 / default 游戏原设置。\n"
      "  NAT 打洞失败、直连也连不上时请改成 allow",
      "EOS リレー：norelay Epic のリレーを使わない / allow 許可 / force 強制 / default ゲームの設定のまま。\n"
      "  NAT 越えに失敗して直接接続もできないときは allow に",
      "EOS relay: norelay never use Epic's relay servers / allow / force always / default the game's own setting.\n"
      "  Set allow if you cannot connect: NAT traversal fails and there is no direct link"}},
    {"[Sync]", {}},
    {"ReliableGameTraffic=1",
     {"防不同步：联机数据丢包自动重发。1 开 / 0 原版",
      "同期ズレ対策：取りこぼした通信を自動で再送。1 オン / 0 元の動作",
      "Desync fix: lost online packets are resent automatically. 1 on / 0 original"}},
    {"[Resilience]", {}},
    {"HoldDisconnects=auto",
     {"掉线不重来：网络抖动断开时先不让游戏踢人，后台自动重连\n"
      "  auto 对装了本插件的人生效（自动识别）/ off 原版行为",
      "回線落ちでやり直さない：回線が一瞬切れてもすぐには切断扱いにせず、裏で自動再接続\n"
      "  auto このプラグインを入れている人に有効（自動判別）/ off 元の動作",
      "Survive drops: when a connection hiccups, keep the player in the mission while reconnecting in the background\n"
      "  auto for players who run this plugin (detected automatically) / off original behaviour"}},
    {"GraceSeconds=30",
     {"最多等多少秒，等不回来就按原版处理",
      "最大何秒待つか。戻らなければ元のゲームどおりに処理",
      "Wait at most this many seconds, then handle it like the original game"}},
    {"KeepRoomOnPeerTimeout=1",
     {"有人进房后与你的 P2P 握手一直没连上时，你留在房间里（原版会让房主以外的人全部退出）。1 开 / 0 原版",
      "後から入った人との P2P ハンドシェイクが完了しないとき、部屋に残る（元のゲームはホスト以外を全員退出させる）。1 オン / 0 元の動作",
      "Stay in the room when the P2P handshake with someone who joined after you never completes (the original game\n"
      "  makes everyone but the host leave). 1 on / 0 original"}},
    {"[Netcode]", {}},
    {"Fragments=1",
     {"超过 EOS 单包上限（1170 字节）的数据拆成小块发送。全房间都开才生效。1 开 / 0 关",
      "EOS の 1 パケット上限（1170 バイト）を超えるデータを分割して送る。部屋の全員がオンのときだけ有効。1 オン / 0 オフ",
      "Split data above EOS's 1170-byte packet limit into fragments. Only used while everyone in the room has it on. 1 on / 0 off"}},
    {"PlayerSync=1",
     {"其他玩家的位置按速度外推，移动更平滑、更准。全房间都开才生效。1 开 / 0 原版",
      "他のプレイヤーの位置を速度から先読みして、動きをなめらかで正確にする。部屋の全員がオンのときだけ有効。1 オン / 0 元の動作",
      "Extrapolate other players' positions from their velocity, so they move smoother and truer. Only used while\n"
      "  everyone in the room has it on. 1 on / 0 original"}},
    {"HitAuthority=1",
     {"开枪的人判定命中（原版由被打的一方判定，常出现「打中了对方却没掉血」）。全房间都开才生效。1 开 / 0 原版",
      "撃った側が命中を判定する（元のゲームは撃たれた側が判定し「当たったのにダメージがない」が起きる）。部屋の全員がオンのときだけ有効。1 オン / 0 元の動作",
      "The shooter decides hits (the original game lets the target decide, so a hit you saw can do nothing). Only\n"
      "  used while everyone in the room has it on. 1 on / 0 original"}},
    {"RejectMismatched=1",
     {"房主拒绝联机协议版本不同的玩家（避免规则不一致导致不同步）。1 开 / 0 关",
      "ホストはネットコードのバージョンが違うプレイヤーを入れない（ルールの食い違いによる同期ズレを防ぐ）。1 オン / 0 オフ",
      "As host, refuse players whose netcode protocol differs (different rules would desync). 1 on / 0 off"}},
    {"[Display]", {}},
    {"RadarItemColors=1",
     {"雷达上的道具按种类着色：护甲红、回复白、武器绿（原版全是绿色）。只影响自己的画面。1 开 / 0 原版",
      "レーダー上のアイテムを種類で色分け：アーマー赤、回復白、武器緑（元のゲームは全部緑）。自分の画面だけ。1 オン / 0 元の動作",
      "Color pickups on the radar by type: armor red, health white, weapons green (the original game shows all of them\n"
      "  green). Only your own screen. 1 on / 0 original"}},
    {"[Diagnostics]", {}},
    {"Laser=1",
     {"记录 Closed Laser M2 所用激光类型的限量诊断，不改变开火或伤害。0 关闭",
      "Closed Laser M2 が使うレーザー型の診断を回数制限付きで記録。発射やダメージは変更しません。0 で無効",
      "Record bounded diagnostics for the laser class used by Closed Laser M2; firing and damage are unchanged. 0 off"}},
    {"[Log]", {}},
    {"CrashLog=1",
     {"崩溃时把地址和调用栈写进 EDF6Coop.log",
      "クラッシュ時にアドレスと呼び出し履歴を EDF6Coop.log に書く",
      "Write crash details (address and call stack) to EDF6Coop.log"}},
    {"CrashDump=0",
     {"游戏或本插件的代码第一次访问违规时另写 EDF6Coop-crash.dmp（每次启动一个）。里面有游戏内存（房间名、聊天），只在被要求时开",
      "ゲームかこのプラグインのコードで最初のアクセス違反が起きたとき EDF6Coop-crash.dmp も書く（起動ごとに 1 つ）。ゲームのメモリ（部屋名、チャット）を含むので頼まれたときだけオンに",
      "Also write EDF6Coop-crash.dmp for the first access violation in the game's or this plugin's code (one per\n"
      "  launch). It holds game memory (room name, chat), so only turn it on when asked for one"}},
};

int iniLanguage(unsigned short langId) {
    switch (PRIMARYLANGID(langId)) {
    case LANG_CHINESE: return 0;
    case LANG_JAPANESE: return 1;
    default: return 2;
    }
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}

// The settings file as the plugin reads it. GetPrivateProfileString is not used: it reads UTF-8 as the
// ANSI code page (garbling non-ASCII values), treats a UTF-8 BOM as part of the first line (a file that
// starts with [DirectNet] loses that whole section) and keeps comments after a value
// ("Mode=host ; me" is not host).
class Ini {
public:
    explicit Ini(const std::wstring& path) {
        std::wstring text = decode(readAll(path));
        std::wstring section;
        for (size_t at = 0; at < text.size();) {
            size_t end = text.find(L'\n', at);
            if (end == std::wstring::npos) end = text.size();
            std::wstring line = trim(text.substr(at, end - at));
            at = end + 1;
            if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
            if (line[0] == L'[') {
                size_t close = line.find(L']');
                section = lower(trim(line.substr(1, close == std::wstring::npos ? std::wstring::npos : close - 1)));
                continue;
            }
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = trim(line.substr(0, eq));
            entries_.push_back({section, lower(key), value(line.substr(eq + 1))});
        }
    }

    static std::wstring decodeText(const std::string& bytes) { return decode(bytes); }

    // The first value of `key` in `section` (both case-insensitive, like Windows); nullptr when absent.
    const std::wstring* find(const wchar_t* section, const wchar_t* key) const {
        std::wstring s = lower(section), k = lower(key);
        for (const Entry& e : entries_)
            if (e.section == s && e.key == k) return &e.value;
        return nullptr;
    }

private:
    struct Entry {
        std::wstring section, key, value;  // section and key in lower case
    };
    std::vector<Entry> entries_;

    static std::string readAll(const std::wstring& path) {
        std::string bytes;
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) return bytes;
        char buf[4096];
        for (size_t n; bytes.size() < 1024 * 1024 && (n = fread(buf, 1, sizeof(buf), f)) > 0;) bytes.append(buf, n);
        fclose(f);
        return bytes;
    }

    // UTF-16LE with BOM; else UTF-8 (BOM optional); else the ANSI code page (saved by an old editor).
    static std::wstring decode(const std::string& bytes) {
        if (bytes.size() >= 2 && bytes[0] == '\xFF' && bytes[1] == '\xFE')
            return std::wstring(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
        size_t skip = bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
        const char* p = bytes.data() + skip;
        int size = static_cast<int>(bytes.size() - skip);
        UINT codePage = CP_UTF8;
        int n = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS, p, size, nullptr, 0);
        if (n <= 0 && size > 0) n = MultiByteToWideChar(codePage = CP_ACP, 0, p, size, nullptr, 0);
        std::wstring w(n > 0 ? n : 0, L'\0');
        if (n > 0) MultiByteToWideChar(codePage, 0, p, size, w.data(), n);
        return w;
    }

    static std::wstring trim(const std::wstring& s) {
        size_t begin = s.find_first_not_of(L" \t\r");
        if (begin == std::wstring::npos) return {};
        return s.substr(begin, s.find_last_not_of(L" \t\r") - begin + 1);
    }

    // A value without its comment (a ';' or '#' at the start or after a space) and, like Windows, without
    // surrounding double quotes.
    static std::wstring value(const std::wstring& raw) {
        std::wstring v = raw;
        for (size_t i = 0; i < v.size(); ++i) {
            if ((v[i] == L';' || v[i] == L'#') && (i == 0 || v[i - 1] == L' ' || v[i - 1] == L'\t')) {
                v.resize(i);
                break;
            }
        }
        v = trim(v);
        if (v.size() >= 2 && v.front() == L'"' && v.back() == L'"') v = v.substr(1, v.size() - 2);
        return v;
    }
};

std::wstring readString(const Ini& ini, const wchar_t* section, const wchar_t* key, const wchar_t* def) {
    const std::wstring* v = ini.find(section, key);
    return v ? *v : def;
}

std::string toUtf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

// An integer from `min` to `max` written as plain decimal digits. Absent or empty: `def`. Anything else
// (letters, "0x10", "27015abc", out of range) also keeps `def` and says so in `warnings`: a typo must
// not silently become 0, e.g. a random port or a setting turned off.
int readInt(const Ini& ini, const wchar_t* section, const wchar_t* key, int def, int min, int max,
            std::vector<std::string>& warnings) {
    const std::wstring* v = ini.find(section, key);
    if (!v || v->empty()) return def;
    bool digits = v->size() <= 9 && v->find_first_not_of(L"0123456789") == std::wstring::npos;
    long long n = digits ? std::wcstoll(v->c_str(), nullptr, 10) : 0;
    if (digits && n >= min && n <= max) return static_cast<int>(n);
    char line[512];
    snprintf(line, sizeof(line), "CONFIG [%s] %s=%s is not a number from %d to %d; using %d", toUtf8(section).c_str(),
             toUtf8(key).c_str(), toUtf8(*v).c_str(), min, max, def);
    warnings.push_back(line);
    return def;
}

bool readBool(const Ini& ini, const wchar_t* section, const wchar_t* key, bool def, std::vector<std::string>& warnings) {
    return readInt(ini, section, key, def ? 1 : 0, 0, 1, warnings) != 0;
}

}  // namespace

std::string defaultIni(unsigned short langId) {
    int lang = iniLanguage(langId);
    std::string s = "\xEF\xBB\xBF; ";
    s += kIniTitle[lang];
    s += "\r\n";
    for (const IniEntry& e : kIniEntries) {
        s += "\r\n";
        for (const char* p = e.comment[lang]; p && *p;) {
            const char* end = strchr(p, '\n');
            size_t n = end ? static_cast<size_t>(end - p) : strlen(p);
            s += "; ";
            s.append(p, n);
            s += "\r\n";
            p += n + (end ? 1 : 0);
        }
        s += e.setting;
        s += "\r\n";
    }
    return s;
}

bool writeNewIniUtf16(const std::wstring& path, const std::string& utf8) {
    std::wstring text = Ini::decodeText(utf8);  // drops a UTF-8 BOM
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::string bytes = "\xFF\xFE";
    bytes.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    DWORD written = 0;
    bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
              written == bytes.size();
    CloseHandle(file);
    if (!ok) DeleteFileW(path.c_str());
    return ok;
}

Config loadConfig(const std::wstring& iniPath) {
    Config c;
    std::vector<std::string>& w = c.warnings;
    if (GetFileAttributesW(iniPath.c_str()) == INVALID_FILE_ATTRIBUTES &&
        !writeNewIniUtf16(iniPath, defaultIni(GetUserDefaultUILanguage())))
        w.push_back("CONFIG could not write the settings file with the defaults; running on them");
    Ini ini(iniPath);
    const wchar_t* s = L"DirectNet";
    c.enabled = readBool(ini, s, L"Enabled", true, w);
    // A value none of these names keeps the default, with a warning: `Mode=hots` must not quietly mean off.
    auto pick = [&](const wchar_t* section, const wchar_t* key, const wchar_t* def,
                    std::initializer_list<const wchar_t*> names) {
        std::wstring v = lower(readString(ini, section, key, def));
        if (std::find_if(names.begin(), names.end(), [&](const wchar_t* n) { return v == n; }) != names.end()) return v;
        std::wstring all;
        for (const wchar_t* n : names) all += (all.empty() ? L"" : L"/") + std::wstring(n);
        w.push_back("CONFIG [" + toUtf8(section) + "] " + toUtf8(key) + "=" + toUtf8(v) + " is not one of " + toUtf8(all) +
                    "; using " + toUtf8(def));
        return std::wstring(def);
    };
    std::wstring mode = pick(s, L"Mode", L"off", {L"off", L"host", L"join"});
    c.direct.mode = mode == L"host" ? Mode::Host : mode == L"join" ? Mode::Join : Mode::Off;
    c.direct.listenPort = static_cast<uint16_t>(readInt(ini, s, L"ListenPort", 27015, 0, 65535, w));
    if (c.direct.mode == Mode::Join && readString(ini, s, L"ListenPort", L"").empty()) c.direct.listenPort = 0;
    c.direct.hostAddress = toUtf8(readString(ini, s, L"HostAddress", L""));
    c.direct.key = toUtf8(readString(ini, s, L"Key", L""));
    c.direct.linkTimeoutMs =
        static_cast<uint32_t>(std::clamp(readInt(ini, s, L"LinkTimeoutMs", 60000, 0, 999999999, w), 3000, 300000));
    c.direct.stallTimeoutMs = c.direct.linkTimeoutMs;  // one setting for both ways a link can die
    c.publicAddress = toUtf8(readString(ini, s, L"PublicAddress", L""));
    c.autoJoin = readBool(ini, s, L"AutoJoin", true, w);
    c.upnp = readBool(ini, s, L"UPnP", true, w);
    c.bindPhysicalInterface = readBool(ini, s, L"BindPhysicalInterface", true, w);

    c.eosFixedPort = static_cast<uint16_t>(readInt(ini, L"EOS", L"FixedPort", 0, 0, 65535, w));
    std::wstring relay = pick(L"EOS", L"Relay", L"norelay", {L"default", L"norelay", L"allow", L"force"});
    c.eosRelay = relay == L"norelay" ? 0 : relay == L"allow" ? 1 : relay == L"force" ? 2 : -1;

    std::wstring hold = pick(L"Resilience", L"HoldDisconnects", L"auto", {L"auto", L"off", L"0", L"all"});
    c.hold = hold == L"all" ? Config::Hold::All : (hold == L"off" || hold == L"0") ? Config::Hold::Off : Config::Hold::Auto;
    c.reliableGameTraffic = readBool(ini, L"Sync", L"ReliableGameTraffic", true, w);
    c.direct.upgradeUnreliable = c.reliableGameTraffic;
    c.graceMs =
        static_cast<uint32_t>(std::clamp(readInt(ini, L"Resilience", L"GraceSeconds", 30, 0, 999999999, w), 1, 600)) * 1000u;
    c.direct.testOutageAfterMs = static_cast<uint64_t>(readInt(ini, L"Test", L"DirectOutageAfterMs", 0, 0, 999999999, w));
    c.direct.testOutageForMs = static_cast<uint64_t>(readInt(ini, L"Test", L"DirectOutageForMs", 0, 0, 999999999, w));
    return c;
}

const char* modeName(Mode m) {
    switch (m) {
        case Mode::Host: return "host";
        case Mode::Join: return "join";
        default: return "off";
    }
}

const char* relayName(int relay) {
    switch (relay) {
        case 0: return "norelay";
        case 1: return "allow";
        case 2: return "force";
        default: return "default";
    }
}

}  // namespace dn
