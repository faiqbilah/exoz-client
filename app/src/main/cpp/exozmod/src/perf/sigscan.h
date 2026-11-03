#pragma once
#include <cstdint>

// Finds `pattern` ("48 8B ?? 10", ?? = wildcard) inside the executable mappings of the loaded
// library whose path contains `libName`. Returns the address, or 0 if not found.
// Use this instead of fixed offsets: offsets change on every game update.
uintptr_t sig_scan(const char* libName, const char* pattern);
