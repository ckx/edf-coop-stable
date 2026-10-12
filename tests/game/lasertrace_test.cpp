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

using namespace multislot;
namespace {
int failures = 0, updateCalls = 0, receiveCalls = 0, drawCalls = 0;
void* seenObject = nullptr;
void* seenContext = nullptr;
void* seenView = nullptr;
int seenPass = 0;
void Check(bool ok, const char* label) { if (!ok) { ++failures; std::printf("FAIL: %s\n", label); } }
void UpdateSpy(void* object, void* context) { ++updateCalls; seenObject = object; seenContext = context; }
void ReceiveSpy(void* object, void* reader) { ++receiveCalls; seenObject = object; ++*static_cast<int*>(reader); }
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
        !Spy(base + 0x252E10, reinterpret_cast<std::uintptr_t>(&DrawSpy))) return 1;
    using UpdateFn = void (*)(void*, void*);
    using DrawFn = void (*)(void*, void*, int, void*);
    const auto update = Get<UpdateFn>(base, 0x179FCD8);
    const auto receive = Get<UpdateFn>(base, 0x179FD40);
    const auto draw = Get<DrawFn>(base, 0x179FD60);
    std::array<unsigned char, 0x1100> bullet{};
    std::array<unsigned char, 0x2000> owner{};
    std::array<unsigned char, 32> control{};
    Put(bullet.data(), 0xB48, 120u);
    Put(bullet.data(), 0xAE8, owner.data());
    Put(bullet.data(), 0xAF0, control.data());
    Put(control.data(), 8, 1);
    Put(owner.data(), 0, base + 0x17D0FF8);
    Put(owner.data(), 0x128, static_cast<unsigned char>(1));
    int context = 123, view = 456, cursor = 7;
    update(bullet.data(), &context);
    Check(updateCalls == 1 && seenObject == bullet.data() && seenContext == &context,
          "native update receives the original object and context exactly once");
    draw(bullet.data() + 0xE90, &context, 1, &view);
    Check(drawCalls == 1 && seenObject == bullet.data() + 0xE90 && seenContext == &context &&
          seenPass == 1 && seenView == &view, "draw hook preserves every argument");
    Put(bullet.data(), 0xF38, 1u);
    update(bullet.data(), &context);
    receive(bullet.data() + 0x120, &cursor);
    Check(receiveCalls == 1 && seenObject == bullet.data() + 0x120 && cursor == 8,
          "receive hook leaves packet reading to the original callback");
    auto* inaccessible = VirtualAlloc(nullptr, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    if (!inaccessible) return 1;
    update(inaccessible, &context);
    Check(updateCalls == 3 && seenObject == inaccessible, "unreadable diagnostic memory still calls native update");
    VirtualFree(inaccessible, 0, MEM_RELEASE);
    std::string log = ReadLog();
    Check(log.find("ownerKnown=1 ownerFlags=1") != std::string::npos, "remote player owner is reported");
    Check(log.find("age=1 lifetime=120") != std::string::npos, "snapshot reads native lifetime and age fields");
    Check(log.find("drawCountKnown=1 drawCallbacks=1") != std::string::npos, "draw is associated with the beam");
    Check(log.find("readable=0") != std::string::npos, "failed probe is explicit, not a zero-valued valid beam");
    for (int i = 0; i < 100; ++i) {
        Put(bullet.data(), 0x90, static_cast<float>(i));  // keep the log's duplicate-line coalescing out of this test
        update(bullet.data(), &context);
    }
    Check(updateCalls == 103, "sampling limit never suppresses native updates");
    Check(Count(ReadLog(), "LASER sample") == 8, "detail logging is bounded during sustained firing");
    Sleep(30050);
    update(bullet.data(), &context);
    log = ReadLog();
    Check(Count(log, "LASER sample") == 9 && log.find("LASER summary") != std::string::npos,
          "logging resumes in the next window and reports aggregate activity");
    LogClose();
    std::printf("laser diagnostics: %d failures\n", failures);
    return failures ? 1 : 0;
}
