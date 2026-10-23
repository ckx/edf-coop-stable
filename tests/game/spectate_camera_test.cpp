// ShowCamera against the real EDF.dll, mapped without running its initialisation: the game's weak_from_this
// (1198950) and view switch (1193FA0) move a view from one camera to another. The switch takes its weak_ptr by
// value and releases it itself, so a wrong hand-over leaves a reference count off by one: a camera that is never
// freed, or one freed while the view still points at it. The counts must come out exactly as the view's own
// references explain them.
//   SpectateCameraTests EDF.dll
// Exits 77 (skipped) without EDF.dll.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "../../game/spectate.h"

using namespace multislot::spectate;

namespace {

int failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

// An MSVC shared_ptr control block: vtable (destroy the object, delete the block), uses +8, weaks +0xC.
struct Control {
    void* const* vtable;
    LONG uses;
    LONG weaks;
};
int freed = 0;
void Destroy(Control*) { ++freed; }
void DeleteBlock(Control*) { ++freed; }
void* const kControlVtable[] = {reinterpret_cast<void*>(&Destroy), reinterpret_cast<void*>(&DeleteBlock)};

// A SceneCamera as far as these two functions read it: vtable, enable_shared_from_this's weak_ptr at +0x10, and the
// view's calls to slot 6 (+0x30, now current) and slot 7 (+0x38, no longer current).
struct Camera {
    void* const* vtable;
    void* unused;
    Camera* self;
    Control* control;
    int shown;
    int hidden;
};
void Shown(Camera* camera) { ++camera->shown; }
void Hidden(Camera* camera) { ++camera->hidden; }
void Unexpected(Camera*) {
    ++failures;
    std::printf("FAIL: an unexpected camera call\n");
}
void* const kCameraVtable[] = {
    reinterpret_cast<void*>(&Unexpected), reinterpret_cast<void*>(&Unexpected), reinterpret_cast<void*>(&Unexpected),
    reinterpret_cast<void*>(&Unexpected), reinterpret_cast<void*>(&Unexpected), reinterpret_cast<void*>(&Unexpected),
    reinterpret_cast<void*>(&Shown),      reinterpret_cast<void*>(&Hidden)};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || !std::filesystem::exists(argv[1])) {
        std::printf("SKIPPED: no EDF.dll (set EDF6_GAME_DIR)\n");
        return 77;
    }
    const HMODULE module = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!module) {
        std::printf("FAIL: could not map %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    auto* const game = reinterpret_cast<unsigned char*>(module);

    // Each camera owned by one shared_ptr (the view's camera list holds it in the game). Weak counts: 1 for the
    // owners together, 1 for the camera's weak_ptr to itself, and for `a` 1 for the view showing it.
    Control ownA{kControlVtable, 1, 3}, ownB{kControlVtable, 1, 2};
    Camera a{kCameraVtable, nullptr, nullptr, &ownA, 0, 0};
    Camera b{kCameraVtable, nullptr, nullptr, &ownB, 0, 0};
    a.self = &a;
    b.self = &b;
    // A view showing `a`: its current camera is a weak_ptr at +0x8.
    alignas(16) unsigned char view[0x188]{};
    Camera* const shownA = &a;
    Control* const shownControl = &ownA;
    std::memcpy(view + 0x8, &shownA, sizeof(shownA));
    std::memcpy(view + 0x10, &shownControl, sizeof(shownControl));

    ShowCamera(game, reinterpret_cast<std::uintptr_t>(view), reinterpret_cast<std::uintptr_t>(&b));

    void* current[2]{};
    std::memcpy(current, view + 0x8, sizeof(current));
    Check(current[0] == &b && current[1] == &ownB, "the view now shows the new camera");
    Check(a.hidden == 1 && a.shown == 0, "the old camera was told it is no longer shown");
    Check(b.shown == 1 && b.hidden == 0, "the new camera was told it is shown");
    Check(ownA.uses == 1 && ownA.weaks == 2, "the old camera lost only the view's weak reference");
    Check(ownB.uses == 1 && ownB.weaks == 3, "the new camera gained only the view's weak reference");
    Check(freed == 0, "nothing was freed");

    std::printf(failures ? "%d check(s) failed\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
