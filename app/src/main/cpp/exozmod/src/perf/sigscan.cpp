#include "perf/sigscan.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;   // true = must match
};

bool parse(const char* s, Pattern& p) {
    while (*s) {
        while (*s == ' ') ++s;
        if (!*s) break;
        if (*s == '?') {
            p.bytes.push_back(0);
            p.mask.push_back(false);
            s += (s[1] == '?') ? 2 : 1;
        } else {
            unsigned v = 0;
            if (sscanf(s, "%2x", &v) != 1) return false;
            p.bytes.push_back((uint8_t)v);
            p.mask.push_back(true);
            s += 2;
        }
    }
    return !p.bytes.empty();
}
}  // namespace

uintptr_t sig_scan(const char* libName, const char* pattern) {
    Pattern pat;
    if (!parse(pattern, pat)) return 0;

    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;

    char line[512];
    uintptr_t result = 0;
    while (!result && fgets(line, sizeof line, f)) {
        unsigned long lo = 0, hi = 0;
        char perms[8] = {};
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) != 3) continue;
        if (perms[0] != 'r' || perms[2] != 'x') continue;     // readable code only
        if (!strstr(line, libName)) continue;

        const uint8_t* base = reinterpret_cast<const uint8_t*>(lo);
        const size_t len = hi - lo, n = pat.bytes.size();
        for (size_t i = 0; i + n <= len; ++i) {
            size_t j = 0;
            while (j < n && (!pat.mask[j] || base[i + j] == pat.bytes[j])) ++j;
            if (j == n) { result = lo + i; break; }
        }
    }
    fclose(f);
    return result;
}
