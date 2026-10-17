#include "freefps_code_patch.h"

#include <tlhelp32.h>

#include <cstdint>
#include <cstring>

#pragma managed(push, off)

namespace {

constexpr int kFreezeAttempts = 50;

std::vector<DWORD> other_thread_ids() {
    std::vector<DWORD> ids;
    const DWORD process_id = GetCurrentProcessId();
    const DWORD current_thread = GetCurrentThreadId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return ids;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == process_id &&
                entry.th32ThreadID != current_thread) {
                ids.push_back(entry.th32ThreadID);
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return ids;
}

// GetThreadContext also waits until an asynchronous SuspendThread has taken
// effect.  A context that cannot be read is treated as unsafe.
bool inside_patch(HANDLE thread, const FreeFpsPatchRange* ranges,
                  size_t range_count) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_CONTROL;
    if (!GetThreadContext(thread, &context)) {
        return true;
    }
#if defined(_M_X64)
    const std::uintptr_t ip = context.Rip;
#else
    const std::uintptr_t ip = context.Eip;
#endif
    for (size_t i = 0; i < range_count; ++i) {
        const auto start = reinterpret_cast<std::uintptr_t>(ranges[i].address);
        if (ip > start && ip < start + ranges[i].size) {
            return true;
        }
    }
    return false;
}

} // namespace

FreeFpsThreadFreeze::~FreeFpsThreadFreeze() {
    thaw();
}

bool FreeFpsThreadFreeze::freeze(const FreeFpsPatchRange* ranges,
                                 size_t range_count) {
    thaw();
    for (int attempt = 0; attempt < kFreezeAttempts; ++attempt) {
        // Nothing is suspended here, so replacing the old list is safe.
        thread_ids_ = other_thread_ids();
        threads_.reserve(thread_ids_.size());
        bool safe = true;
        for (DWORD id : thread_ids_) {
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT,
                                       FALSE, id);
            if (thread == nullptr) {
                continue;
            }
            if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
                CloseHandle(thread);
                continue;
            }
            // Capacity is reserved above; this never reallocates.
            threads_.push_back(thread);
            if (inside_patch(thread, ranges, range_count)) {
                safe = false;
                break;
            }
        }
        if (safe) {
            return true;
        }
        thaw();
        Sleep(1);
    }
    return false;
}

void FreeFpsThreadFreeze::thaw() {
    for (HANDLE thread : threads_) {
        ResumeThread(thread);
        CloseHandle(thread);
    }
    threads_.clear();
}

bool freefps_write_code(void* destination, const void* source, size_t size) {
    DWORD old_protect = 0;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE,
                        &old_protect)) {
        return false;
    }
    std::memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    DWORD ignored = 0;
    VirtualProtect(destination, size, old_protect, &ignored);
    return true;
}

#pragma managed(pop)
