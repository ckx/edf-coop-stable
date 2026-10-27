// Calls the installed vtable hooks against a privately mapped EDF.dll. Native rendering, physics and packet
// handling are replaced by spies; the diagnostic reader, rate limiter and pass-through hooks are production code.
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "../../game/code.h"
#include "../../game/lasertrace.h"
#include "../../game/log.h"
#include "../../game/netplayer.h"

using namespace multislot;
namespace {
int failures = 0, updateCalls = 0, receiveCalls = 0, drawCalls = 0;
void* seenObject = nullptr;
void* seenContext = nullptr;
void* seenView = nullptr;
int seenPass = 0, sendCalls = 0;
bool seenRoute = false;
void Check(bool ok, const char* label) { if (!ok) { ++failures; std::printf("FAIL: %s\n", label); } }
void UpdateSpy(void* object, void* context) { ++updateCalls; seenObject = object; seenContext = context; }
void ReceiveSpy(void* object, void* reader) {
    ++receiveCalls; seenObject = object;
    // A real native receive consumes the message and may update the bullet. The diagnostic must retain
    // the original bytes for decoding while observing the resulting state, without rewinding this cursor.
    std::memcpy(static_cast<unsigned char*>(reader) + kSerializeReadPos,
                static_cast<unsigned char*>(reader) + kSerializeEnd, sizeof(std::uint64_t));
    const float after = 42.0f;
    std::memcpy(static_cast<unsigned char*>(object) - 0x120 + 0x90, &after, sizeof(after));
}
void SendSpy(void* object, void* writer, bool route) {
    ++sendCalls; seenObject = object; seenContext = writer; seenRoute = route;
}
void DrawSpy(void* object, void* context, int pass, void* view) {
    ++drawCalls; seenObject = object; seenContext = context; seenPass = pass; seenView = view;
}
template <typename T>
void Put(void* object, std::size_t offset, T value) {
    std::memcpy(static_cast<unsigned char*>(object) + offset, &value, sizeof(value));
}
template <typename T>
T Get(void* object, std::size_t offset) {
    T value{}; std::memcpy(&value, static_cast<unsigned char*>(object) + offset, sizeof(value)); return value;
}
bool Spy(unsigned char* at, std::uintptr_t target) {
    unsigned char code[] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
    std::memcpy(code + 2, &target, sizeof(target));
    return WriteCode(at, code, sizeof(code));
}
std::string ReadLog() {
    LogFlush();
    std::ifstream file("lasertrace-test.log", std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::size_t Count(const std::string& text, const std::string& word) {
    std::size_t count = 0, at = 0;
    while ((at = text.find(word, at)) != std::string::npos) { ++count; at += word.size(); }
    return count;
}
}
int main(int argc, char** argv) {
    if (argc < 2 || GetFileAttributesA(argv[1]) == INVALID_FILE_ATTRIBUTES) return 77;
    auto* base = reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES));
    if (!base) return 1;
    DeleteFileW(L"lasertrace-test.log");
    DeleteFileW(L"lasertrace-test.log.queue");
    LogOpen(L"lasertrace-test.log");
    const auto savedDraw = Get<std::uintptr_t>(base, 0x179FD60);
    const std::uintptr_t wrong = savedDraw + 1;
    Check(WriteCode(base + 0x179FD60, reinterpret_cast<const unsigned char*>(&wrong), 8), "alter last hook site");
    Check(!InstallLaserTrace(base), "foreign hook refuses the entire diagnostic installation");
    Check(Get<std::uintptr_t>(base, 0x179FCD8) == reinterpret_cast<std::uintptr_t>(base + 0x2535C0) &&
          Get<std::uintptr_t>(base, 0x179FD40) == reinterpret_cast<std::uintptr_t>(base + 0x22F660),
          "refusal leaves earlier update and receive slots untouched");
    Check(WriteCode(base + 0x179FD60, reinterpret_cast<const unsigned char*>(&savedDraw), 8), "restore draw slot");
    if (!InstallLaserTrace(base)) { std::puts("FAIL: install on supported EDF.dll"); return 1; }
    if (!Spy(base + 0x2535C0, reinterpret_cast<std::uintptr_t>(&UpdateSpy)) ||
        !Spy(base + 0x22F660, reinterpret_cast<std::uintptr_t>(&ReceiveSpy)) ||
        !Spy(base + 0x252E10, reinterpret_cast<std::uintptr_t>(&DrawSpy)) ||
        !Spy(base + 0x765570, reinterpret_cast<std::uintptr_t>(&SendSpy))) return 1;
    using UpdateFn = void (*)(void*, void*);
    using DrawFn = void (*)(void*, void*, int, void*);
    const auto update = Get<UpdateFn>(base, 0x179FCD8);
    const auto receive = Get<UpdateFn>(base, 0x179FD40);
    const auto draw = Get<DrawFn>(base, 0x179FD60);
    std::array<unsigned char, 0x1100> bullet{};
    std::array<unsigned char, 0x2000> owner{};
    std::array<unsigned char, 32> control{};
    Put(bullet.data(), 0, base + 0x179FCB0);
    Put(bullet.data(), 0x128, 0x123402);
    Put(bullet.data(), 0xB48, 120u);
    Put(bullet.data(), 0xAE8, owner.data());
    Put(bullet.data(), 0xAF0, control.data());
    Put(control.data(), 8, 1);
    Put(owner.data(), 0, base + 0x17D0FF8);
    Put(owner.data(), 0x128, static_cast<unsigned char>(1));
    int context = 123, view = 456;
    alignas(16) std::array<unsigned char, 0x600> stream{};
    update(bullet.data(), &context);
    Check(updateCalls == 1 && seenObject == bullet.data() && seenContext == &context,
          "native update receives the original object and context exactly once");
    draw(bullet.data() + 0xE90, &context, 1, &view);
    Check(drawCalls == 1 && seenObject == bullet.data() + 0xE90 && seenContext == &context &&
          seenPass == 1 && seenView == &view, "draw hook preserves every argument");
    Put(bullet.data(), 0xF38, 1u);
    update(bullet.data(), &context);
    const auto writeSmall = reinterpret_cast<bool (*)(void*, std::int8_t)>(base + 0x12B5790);
    const float first[3] = {1, 2, 3}, second[3] = {10, 20, 30};
    for (int type : {0, 3, 1, 2}) {
        stream.fill(0);
        writeSmall(stream.data(), static_cast<std::int8_t>(type));
        if (type == 1 || type == 2) {
            writeSmall(stream.data(), 7);  // reference, followed by part
            writeSmall(stream.data(), 2);
            if (type == 2) writeSmall(stream.data(), 9);
        }
        WriteBinElement(stream.data(), reinterpret_cast<const std::uint8_t*>(first), sizeof(first));
        if (type == 0 || type == 3)
            WriteBinElement(stream.data(), reinterpret_cast<const std::uint8_t*>(second), sizeof(second));
        else {
            const unsigned char half[6]{};
            WriteBinElement(stream.data(), half, sizeof(half));
        }
        const auto before = stream;
        receive(bullet.data() + 0x120, stream.data());
        Check(seenObject == bullet.data() + 0x120 &&
              Get<std::uint64_t>(stream.data(), kSerializeReadPos) == Get<std::uint64_t>(stream.data(), kSerializeEnd),
              "native receive retains its final cursor");
        Check(std::memcmp(before.data() + kSerializeData, stream.data() + kSerializeData, kSerializeCapacity) == 0,
              "diagnostic never changes live message bytes");
    }
    Check(receiveCalls == 4, "each native receive called exactly once");
    auto log = ReadLog();
    Check(log.find("event=receive-after") != std::string::npos && log.find("position=42.000,") != std::string::npos,
          "receive snapshot observes native post-receive state");
    for (int type : {0, 3, 1, 2})
        Check(log.find("decoded=1 type=" + std::to_string(type)) != std::string::npos,
              "native integer and bin messages decode through the installed hook");
    Check(log.find("reference=7 part=2 hitEvent=9") != std::string::npos,
          "target identity precedes geometry in native hit messages");
    // Execute the replacement call target, preserving NetworkBullet, writer and immediate-route arguments.
    const auto relative = Get<std::int32_t>(base, 0x22F309 + 1);
    const auto send = reinterpret_cast<void (*)(void*, void*, bool)>(base + 0x22F309 + 5 + relative);
    const auto sent = stream;
    send(bullet.data() + 0x120, stream.data(), true);
    Check(sendCalls == 1 && seenObject == bullet.data() + 0x120 && seenContext == stream.data() && seenRoute,
          "send hook preserves all original arguments");
    Check(stream == sent, "send diagnostic never changes the writer or cursor");
    log = ReadLog();
    Check(log.find("event=send-before") != std::string::npos && log.find("bulletId=1192962") != std::string::npos,
          "send and receive log the native bullet identity");
    // Wrong bin length is bounded, and never suppresses native handling.
    stream.fill(0);
    writeSmall(stream.data(), 0);
    stream[kSerializeData + 1] = 0xA0;
    stream[kSerializeData + 2] = 255;
    Put(stream.data(), kSerializeEnd, std::uint64_t{3});
    receive(bullet.data() + 0x120, stream.data());
    Check(receiveCalls == 5 && ReadLog().find("decoded=0 type=0") != std::string::npos,
          "malformed vector length is rejected without blocking native receive");
    auto* inaccessible = VirtualAlloc(nullptr, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    if (!inaccessible) return 1;
    update(inaccessible, &context);
    Check(updateCalls == 3 && seenObject == inaccessible, "unreadable diagnostic memory still calls native update");
    VirtualFree(inaccessible, 0, MEM_RELEASE);
    log = ReadLog();
    Check(log.find("ownerKnown=1 ownerFlags=1") != std::string::npos, "remote player owner is reported");
    Check(log.find("age=1 lifetime=120") != std::string::npos, "snapshot reads native lifetime and age fields");
    Check(log.find("drawCountKnown=1 drawCallbacks=1") != std::string::npos, "draw is associated with the beam");

    for (int i = 0; i < 100; ++i) {
        Put(bullet.data(), 0x90, static_cast<float>(i));  // keep the log's duplicate-line coalescing out of this test
        update(bullet.data(), &context);
    }
    Check(updateCalls == 103, "sampling limit never suppresses native updates");
    Check(Count(ReadLog(), "LASER sample") == 8, "detail logging is bounded during sustained firing");
    Sleep(30050);
    Put(bullet.data(), 0xF38, 0u);
    update(bullet.data(), &context);
    log = ReadLog();
    Check(Count(log, "LASER sample") == 9 && log.find("LASER summary") != std::string::npos,
          "logging resumes in the next window and reports aggregate activity");
    // 760C50 accepts a two-byte packed-vector header followed by eight raw bytes.
    stream.fill(0);
    writeSmall(stream.data(), 1);
    writeSmall(stream.data(), 7);
    writeSmall(stream.data(), 2);
    WriteBinElement(stream.data(), reinterpret_cast<const std::uint8_t*>(first), sizeof(first));
    const auto packedAt = Get<std::uint64_t>(stream.data(), kSerializeEnd);
    stream[kSerializeData + packedAt] = 0xE0;
    stream[kSerializeData + packedAt + 1] = 8;
    Put(stream.data(), kSerializeEnd, packedAt + 10);
    receive(bullet.data() + 0x120, stream.data());
    log = ReadLog();
    Check(Count(log, "decoded=1 type=1") == 2, "packed vector accepts its complete header and eight bytes");
    Put(stream.data(), kSerializeReadPos, std::uint64_t{0});
    Put(stream.data(), kSerializeEnd, packedAt + 9);
    receive(bullet.data() + 0x120, stream.data());
    Check(ReadLog().find("decoded=0 type=1") != std::string::npos,
          "packed vector missing its last byte is refused");
    stream.fill(0);
    stream[kSerializeData] = 0x80;  // integer requires eight more bytes
    Put(stream.data(), kSerializeEnd, std::uint64_t{1});
    receive(bullet.data() + 0x120, stream.data());
    Check(ReadLog().find("decoded=0 type=-1") != std::string::npos,
          "truncated integer never reaches the unchecked game integer reader");
    Put(bullet.data(), 0, base + 0x179FCB8);  // another projectile using the shared send implementation
    send(bullet.data() + 0x120, stream.data(), false);
    Check(sendCalls == 2 && !seenRoute, "foreign projectile forwards without diagnostic interpretation");
    LogClose();
    std::printf("laser diagnostics: %d failures\n", failures);
    return failures ? 1 : 0;
}
