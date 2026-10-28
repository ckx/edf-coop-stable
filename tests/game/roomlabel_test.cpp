// Verify refusal and native render ABI using the installed room hook. Only the font renderer and shader
// submission are spies; the hook and the game's orthographic projection run unchanged.
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "../../game/code.h"
#include "../../game/spectate.h"

using namespace multislot;
namespace {
int failures = 0, draws = 0, flushes = 0, ends = 0;
void* expectedContext = nullptr;
void Check(bool ok, const char* message) { if (!ok) { ++failures; std::printf("FAIL: %s\n", message); } }
template <typename T> void Put(void* p, std::size_t at, T value) {
    std::memcpy(static_cast<unsigned char*>(p) + at, &value, sizeof(value));
}
template <typename T> T Get(const void* p, std::size_t at) {
    T value{}; std::memcpy(&value, static_cast<const unsigned char*>(p) + at, sizeof(value)); return value;
}
void Begin(void*, void*) {}
void Style(void*, void* context, const void*) { Check(context == expectedContext, "style receives draw context"); }
void Measure(void*, float* size, const wchar_t*, std::int64_t, bool) { size[0] = 200; size[1] = 24; }
void Draw(void*, void* context, const float* matrix, const float*, const wchar_t* text, std::int64_t length) {
    ++draws;
    Check(context == expectedContext && length == -1, "draw receives the context and terminated text");
    wchar_t expected[48];
    std::swprintf(expected, std::size(expected), L"EDF6Coop %d.%d.%d",
                  EDF6COOP_VERSION_MAJOR, EDF6COOP_VERSION_MINOR, EDF6COOP_VERSION_PATCH);
    Check(std::wcscmp(text, expected) == 0, "label carries this build's version");
    Check(matrix[12] + 200 == 930 && matrix[13] == 498, "measured text fits the bottom right margin");
}
void Flush(void*, void* context) { ++flushes; Check(context == expectedContext, "flush uses the draw context"); }
void End(void*) { ++ends; }
void Transform(void*, void* context, const float* view, const float* projection) {
    Check(context == expectedContext && view[0] == 1 && view[15] == 1, "independent identity view");
    const float right = 960 * projection[0] + projection[12];
    const float bottom = 540 * projection[5] + projection[13];
    Check(std::fabs(right - 1) < 0.00001f && std::fabs(bottom + 1) < 0.00001f,
          "native projection maps the room's bottom right to the viewport edge");
}
bool Spy(unsigned char* at, std::uintptr_t target) {
    unsigned char code[] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
    std::memcpy(code + 2, &target, sizeof(target));
    return WriteCode(at, code, sizeof(code));
}
}
int main(int argc, char** argv) {
    if (argc < 2 || GetFileAttributesA(argv[1]) == INVALID_FILE_ATTRIBUTES) return 77;
    auto* game = reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES));
    if (!game) return 1;
    const auto original = Get<std::uintptr_t>(game, 0x180ACC0);
    const auto foreign = original + 1;
    WriteCode(game + 0x180ACC0, reinterpret_cast<const unsigned char*>(&foreign), sizeof(foreign));
    Check(!spectate::InstallLobbyVersion(game) && Get<std::uintptr_t>(game, 0x180ACC0) == foreign,
          "another mod's draw slot is refused without overwriting it");
    WriteCode(game + 0x180ACC0, reinterpret_cast<const unsigned char*>(&original), sizeof(original));
    Check(spectate::InstallLobbyVersion(game), "verified room hook installs independently of the spectator");
    struct Site { unsigned rva; std::uintptr_t target; };
    for (const Site& site : {Site{0x11517A0, reinterpret_cast<std::uintptr_t>(&Begin)},
                            Site{0x113B4A0, reinterpret_cast<std::uintptr_t>(&Style)},
                            Site{0x1139790, reinterpret_cast<std::uintptr_t>(&Measure)},
                            Site{0x113C520, reinterpret_cast<std::uintptr_t>(&Draw)},
                            Site{0x113C6F0, reinterpret_cast<std::uintptr_t>(&Flush)},
                            Site{0x1138630, reinterpret_cast<std::uintptr_t>(&End)},
                            Site{0x112F590, reinterpret_cast<std::uintptr_t>(&Transform)}})
        Check(Spy(game + site.rva, site.target), "renderer spy installs");
    using DrawRoom = void (*)(void*, void*);
    const auto draw = Get<DrawRoom>(game, 0x180ACC0);
    int context = 123, room = 456;
    expectedContext = &context;
    Put(game, 0x20B29C0, std::uintptr_t{0});
    draw(&room, &context);
    Check(draws == 0, "missing font is harmless");
    std::array<unsigned char, 0xC0> font{};
    Put(game, 0x20B29C0, font.data());
    Put(game, 0x2139A98, &context);
    draw(&room, &context);
    Check(draws == 1 && flushes == 1 && ends == 1, "one label draw flushes and releases its text object");
    std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
