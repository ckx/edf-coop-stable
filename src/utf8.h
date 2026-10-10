#pragma once
#include <windows.h>

#include <cwchar>

namespace dn {

// Wide text as UTF-8 for a %s in a log line. The CRT's own %ls depends on a locale, and the MinGW-w64 runtime
// (msvcrt.dll) cannot be set to UTF-8. No heap: safe in the crash handler. Text past 341 UTF-16 units is cut,
// and a unit with no UTF-8 form (a lone surrogate) becomes U+FFFD.
class Utf8Text {
public:
    explicit Utf8Text(const wchar_t* text) {
        if (!text) return;
        // At most 3 bytes per UTF-16 unit, so this many always fit (paths are shorter still).
        const int units = static_cast<int>(wcsnlen(text, (sizeof(text_) - 1) / 3));
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, units, text_, sizeof(text_) - 1, nullptr, nullptr);
        text_[bytes > 0 ? bytes : 0] = 0;
    }
    const char* c_str() const { return text_; }

private:
    char text_[1024]{};
};

}  // namespace dn
