#pragma once
#include <cstddef>

namespace multislot {

// The EOS ProductUserId behind `id` as its 32 characters, written into `out` (33 bytes is enough). Returns `out`,
// which holds "" when the id is null, invalid, or the SDK is not loaded. Asked through the SDK's own export, inside
// Guarded (guard.h).
const char* ProductUserIdText(const void* id, char* out, std::size_t size);

}  // namespace multislot
