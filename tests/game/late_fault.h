#pragma once
// A write fault that every vectored handler installed before it sees (the plugin's crash log among them), after
// which the test resumes where LateFault was called. A vectored handler added last, at first use, does the resuming.
#include <windows.h>

#include <cstdint>

namespace late_fault {

inline CONTEXT resume;
inline volatile bool armed = false;
// Set by Recover, so RtlCaptureContext's second return can tell itself from the first. Not a local of LateFault:
// MSVC 19.50 took a local whose address never escapes to be 0 after the call and faulted again forever.
inline volatile bool resumed = false;

inline LONG CALLBACK Recover(EXCEPTION_POINTERS* info) {
    if (!armed || info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    armed = false;
    resumed = true;
    *info->ContextRecord = resume;
    return EXCEPTION_CONTINUE_EXECUTION;
}

}  // namespace late_fault

// Writes to `address` (an unmapped one) and returns once the fault has gone through the other handlers.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
inline void LateFault(std::uintptr_t address) {
    static const PVOID handler = AddVectoredExceptionHandler(0, &late_fault::Recover);
    late_fault::resumed = false;
    RtlCaptureContext(&late_fault::resume);
    if (late_fault::resumed || !handler) return;
    late_fault::armed = true;
    *reinterpret_cast<volatile int*>(address) = 1;
}
