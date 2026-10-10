#include "identity.h"

#include <windows.h>

#include <cstdint>

#include "guard.h"

namespace multislot {
namespace {

// EOS renders its own ids nowhere in full, so the SDK has to do it for us.
constexpr const char* kEosDll = "EOSSDK-Win64-Shipping.dll";
constexpr std::size_t kProductUserIdChars = 32;

using ProductUserIdIsValidFn = int(__cdecl*)(const void*);
using ProductUserIdToStringFn = int(__cdecl*)(const void*, char*, std::int32_t*);

}  // namespace

const char* ProductUserIdText(const void* id, char* out, std::size_t size) {
    if (!out || !size) return "";
    out[0] = 0;
    if (!id || size <= kProductUserIdChars) return out;
    const bool read = Guarded([&] {
        const HMODULE eos = GetModuleHandleA(kEosDll);
        if (!eos) return;
        const auto isValid = reinterpret_cast<ProductUserIdIsValidFn>(
            reinterpret_cast<void*>(GetProcAddress(eos, "EOS_ProductUserId_IsValid")));
        const auto toString = reinterpret_cast<ProductUserIdToStringFn>(
            reinterpret_cast<void*>(GetProcAddress(eos, "EOS_ProductUserId_ToString")));
        if (!isValid || !toString || !isValid(id)) return;
        auto length = static_cast<std::int32_t>(size);
        if (toString(id, out, &length) != 0) out[0] = 0;  // anything but EOS_Success
        out[size - 1] = 0;
    });
    if (!read) out[0] = 0;
    return out;
}

}  // namespace multislot
