#pragma once
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace multislot {

// Where a fault Guarded recovered from happened.
struct Fault {
    DWORD code = 0;
    std::uintptr_t instruction = 0;
};

// Guarded(body) runs `body`. A hardware fault inside it (an access violation reading game memory that is not there,
// at any call depth, also inside game code it calls) makes it return false instead of crashing the game; true when
// the body ran to its end. It replaces __try/__except, which only MSVC honours for every faulting instruction:
// MinGW-w64 compilers drop the handler, and clang keeps it only when the fault happens inside a call.
//
// A fault resumes in Guarded without unwinding: objects created inside the body are not destroyed and locks it took
// stay taken. Keep bodies to reads, writes and calls into the game. C++ and other software exceptions pass through.
namespace guard_detail {

struct Guard {
    CONTEXT resume;  // where Guarded continues after a fault, captured on its way in
    Guard* outer;
    volatile LONG64 faulted;  // 64 bits: no padding after the 16-byte aligned CONTEXT (MSVC warns of it)
    volatile Fault detail;
};
inline thread_local Guard* active = nullptr;

inline LONG CALLBACK OnFault(EXCEPTION_POINTERS* info) {
    Guard* guard = active;
    if (!guard) return EXCEPTION_CONTINUE_SEARCH;
    switch (info->ExceptionRecord->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    active = guard->outer;
    guard->detail.code = info->ExceptionRecord->ExceptionCode;
    guard->detail.instruction = reinterpret_cast<std::uintptr_t>(info->ExceptionRecord->ExceptionAddress);
    guard->faulted = 1;
    *info->ContextRecord = guard->resume;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// Not inlined: RtlCaptureContext returns a second time after a fault, with the registers it captured, so nothing of
// the caller may live in registers across it.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
inline bool Run(void (*body)(void*), void* context, Fault* detail) {
    static const PVOID handler = AddVectoredExceptionHandler(1, &OnFault);
    if (!handler) return false;
    if (detail) *detail = {};
    Guard guard{};
    guard.outer = active;
    RtlCaptureContext(&guard.resume);
    if (guard.faulted) {
        if (detail) {
            detail->code = guard.detail.code;
            detail->instruction = guard.detail.instruction;
        }
        return false;
    }
    active = &guard;
    body(context);
    active = guard.outer;
    return true;
}

}  // namespace guard_detail

// `detail`, when given, says where a fault happened.
template <typename Body>
bool Guarded(Body&& body, Fault* detail = nullptr) {
    using B = std::remove_reference_t<Body>;
    return guard_detail::Run([](void* p) { (*static_cast<B*>(p))(); },
                             const_cast<void*>(static_cast<const void*>(&body)), detail);
}

// True while this thread runs inside Guarded: the crash handler leaves those faults alone.
inline bool InGuarded() {
    return guard_detail::active != nullptr;
}

// Copies sizeof(T) bytes from `address`; false when they are not readable.
template <typename T>
bool ReadGuarded(std::uintptr_t address, T& out) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (!address) return false;
    T value{};
    if (!Guarded([&] { std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T)); })) return false;
    std::memcpy(&out, &value, sizeof(T));
    return true;
}

template <typename T>
bool ReadGuarded(const void* address, T& out) {
    return ReadGuarded(reinterpret_cast<std::uintptr_t>(address), out);
}

}  // namespace multislot
