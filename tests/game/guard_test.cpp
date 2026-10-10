// Guarded (src/guard.h): faults inside the body come back as false on every toolchain, and nothing else is caught.
#include "../../game/guard.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace multislot;

namespace {
int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

// A fault two calls deep, the way a game function faults under the plugin's guard.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
int ReadDeep(const volatile int* p, int depth) {
    if (depth > 0) return ReadDeep(p, depth - 1) + 1;
    return *p;
}
}  // namespace

int main() {
    // A plain load from an unmapped page, no call in between: the case clang and GCC drop from __try.
    int value = 7;
    CHECK(!Guarded([&] { value = *reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x10)); }));
    CHECK(value == 7);

    // A write to read-only memory.
    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READONLY);
    CHECK(page != nullptr);
    CHECK(!Guarded([&] { *static_cast<volatile int*>(page) = 1; }));
    VirtualFree(page, 0, MEM_RELEASE);

    // A fault inside called code; state written before it stays.
    int before = 0;
    CHECK(!Guarded([&] {
        before = 1;
        value = ReadDeep(nullptr, 3);
    }));
    CHECK(before == 1);

    // No fault: true, and the body's effects are kept.
    int good = 41;
    CHECK(Guarded([&] { value = ReadDeep(&good, 3); }));
    CHECK(value == 44);

    // ReadGuarded.
    std::uint64_t word = 0;
    CHECK(!ReadGuarded(std::uintptr_t{0x20}, word));
    CHECK(ReadGuarded(&good, value) && value == 41);

    // Nested guards: the inner fault is the inner guard's; the outer one runs on.
    bool innerFaulted = false;
    CHECK(Guarded([&] {
        innerFaulted = !Guarded([&] { value = ReadDeep(nullptr, 1); });
        CHECK(InGuarded());
    }));
    CHECK(innerFaulted);
    CHECK(!InGuarded());

    // A C++ exception thrown and caught inside the body is not a fault.
    bool caught = false;
    CHECK(Guarded([&] {
        try {
            throw std::runtime_error("inside");
        } catch (const std::exception&) {
            caught = true;
        }
    }));
    CHECK(caught);

    // Many faults in a row (nothing piles up on the stack) and on several threads at once.
    for (int i = 0; i < 10000; ++i)
        if (Guarded([&] { value = ReadDeep(nullptr, 2); })) {
            CHECK(false);
            break;
        }
    std::atomic<int> faults{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 1000; ++i) {
                int local = 0;
                if (!Guarded([&] { local = ReadDeep(nullptr, i % 4); })) ++faults;
            }
        });
    for (auto& t : threads) t.join();
    CHECK(faults.load() == 4000);

    if (failures) return 1;
    std::printf("guard: all passed\n");
    return 0;
}
