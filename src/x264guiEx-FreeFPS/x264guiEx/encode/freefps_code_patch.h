#pragma once

#include <windows.h>

#include <cstddef>
#include <vector>

// A code range that is about to be rewritten.
struct FreeFpsPatchRange {
    const void* address;
    size_t size;
};

// Suspends every other thread of the current process while code is patched.
//
// Thread IDs are collected before the first thread is suspended, and both
// lists are members that are only resized while nothing is suspended, so no
// heap memory is allocated or freed while another thread may be frozen
// holding the process heap lock.  Callers must not allocate between freeze()
// and thaw() either.  A thread whose
// instruction pointer lies strictly inside a patched range would resume in
// the middle of the new bytes; such an attempt is released and retried, and
// freeze() fails closed if it never becomes safe.
class FreeFpsThreadFreeze {
public:
    FreeFpsThreadFreeze() = default;
    ~FreeFpsThreadFreeze();

    FreeFpsThreadFreeze(const FreeFpsThreadFreeze&) = delete;
    FreeFpsThreadFreeze& operator=(const FreeFpsThreadFreeze&) = delete;

    bool freeze(const FreeFpsPatchRange* ranges, size_t range_count);
    void thaw();

private:
    std::vector<DWORD> thread_ids_;
    std::vector<HANDLE> threads_;
};

bool freefps_write_code(void* destination, const void* source, size_t size);
