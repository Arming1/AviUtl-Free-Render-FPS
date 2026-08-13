#include "freefps_aviutl_hook.h"

#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cstring>
#include <vector>

#pragma managed(push, off)

namespace {

constexpr DWORD kExpectedImageSize = 0x527000;
constexpr DWORD kExpectedEntryRva = 0x2b6ebc;
constexpr std::uint64_t kExpectedFileSize = 5228544;
constexpr std::uintptr_t kTimelineBuilderRva = 0x2662d0;
constexpr std::uintptr_t kOrdinaryCallerRva = 0x2657e3;
constexpr size_t kPatchSize = 14;

constexpr std::array<std::uint8_t, kPatchSize> kExpectedPrologue{
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18,
    0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
};
constexpr std::array<std::uint8_t, 5> kExpectedCaller{
    0xe8, 0xe8, 0x0a, 0x00, 0x00,
};

using TimelineBuilder = std::uintptr_t (__fastcall *)(
    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t, double);

SRWLOCK g_request_lock = SRWLOCK_INIT;
FreeFpsRequestContext g_request{};
int g_builder_hits = 0;

std::uint8_t* g_target = nullptr;
void* g_trampoline_memory = nullptr;
TimelineBuilder g_original = nullptr;
std::array<std::uint8_t, kPatchSize> g_detour_bytes{};
bool g_installed = false;

std::array<std::uint8_t, kPatchSize> make_absolute_jump(const void* destination) {
    std::array<std::uint8_t, kPatchSize> bytes{
        0xff, 0x25, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(destination);
    std::memcpy(bytes.data() + 6, &address, sizeof(address));
    return bytes;
}

std::vector<HANDLE> suspend_other_threads() {
    std::vector<HANDLE> threads;
    const DWORD process_id = GetCurrentProcessId();
    const DWORD current_thread = GetCurrentThreadId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return threads;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != process_id ||
                entry.th32ThreadID == current_thread) {
                continue;
            }
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE,
                                       entry.th32ThreadID);
            if (thread != nullptr) {
                if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
                    threads.push_back(thread);
                } else {
                    CloseHandle(thread);
                }
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return threads;
}

void resume_threads(std::vector<HANDLE>& threads) {
    for (HANDLE thread : threads) {
        ResumeThread(thread);
        CloseHandle(thread);
    }
    threads.clear();
}

bool write_code(void* destination, const void* source, size_t size) {
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

bool validate_host(std::uint8_t*& module, std::wstring& error) {
    module = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(nullptr));
    if (module == nullptr) {
        error = L"FreeFPS: cannot locate aviutl2.exe.";
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        error = L"FreeFPS: invalid host DOS header.";
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.SizeOfImage != kExpectedImageSize ||
        nt->OptionalHeader.AddressOfEntryPoint != kExpectedEntryRva) {
        error = L"FreeFPS: unsupported AviUtl2 image layout (v2.1.4 required).";
        return false;
    }
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path))) == 0) {
        error = L"FreeFPS: cannot read host executable path.";
        return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA file_data{};
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &file_data)) {
        error = L"FreeFPS: cannot read host executable size.";
        return false;
    }
    ULARGE_INTEGER file_size{};
    file_size.HighPart = file_data.nFileSizeHigh;
    file_size.LowPart = file_data.nFileSizeLow;
    if (file_size.QuadPart != kExpectedFileSize) {
        error = L"FreeFPS: unsupported AviUtl2 executable size.";
        return false;
    }
    if (std::memcmp(module + kTimelineBuilderRva, kExpectedPrologue.data(),
                    kExpectedPrologue.size()) != 0 ||
        std::memcmp(module + kOrdinaryCallerRva, kExpectedCaller.data(),
                    kExpectedCaller.size()) != 0) {
        error = L"FreeFPS: AviUtl2 timeline signature/caller validation failed.";
        return false;
    }
    const auto displacement = *reinterpret_cast<const std::int32_t*>(
        module + kOrdinaryCallerRva + 1);
    const std::uintptr_t call_target = kOrdinaryCallerRva + 5 + displacement;
    if (call_target != kTimelineBuilderRva) {
        error = L"FreeFPS: AviUtl2 timeline caller relationship changed.";
        return false;
    }
    return true;
}

std::uintptr_t __fastcall timeline_builder_detour(
    std::uintptr_t arg1, std::uintptr_t arg2, std::uintptr_t integer_frame,
    std::uintptr_t arg4, std::uintptr_t arg5, std::uintptr_t arg6,
    std::uintptr_t arg7, std::uintptr_t private_flag, double private_coordinate) {
    bool inject = false;
    double coordinate = private_coordinate;
    AcquireSRWLockExclusive(&g_request_lock);
    if (g_request.active &&
        static_cast<int>(integer_frame) == g_request.requested_integer_frame) {
        inject = true;
        coordinate = g_request.target_coordinate;
        ++g_builder_hits;
    }
    ReleaseSRWLockExclusive(&g_request_lock);
    return g_original(arg1, arg2, integer_frame, arg4, arg5, arg6, arg7,
                      inject ? 1u : private_flag,
                      inject ? coordinate : private_coordinate);
}

} // namespace

bool freefps_hook_install(std::wstring& error) {
#if !defined(_M_X64)
    error = L"FreeFPS: the FreeRenderFPS hook is x64-only.";
    return false;
#else
    if (g_installed) {
        return true;
    }
    std::uint8_t* module = nullptr;
    if (!validate_host(module, error)) {
        return false;
    }
    g_target = module + kTimelineBuilderRva;
    const size_t trampoline_size = kPatchSize * 2;
    if (g_trampoline_memory == nullptr) {
        g_trampoline_memory = VirtualAlloc(nullptr, trampoline_size,
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
        if (g_trampoline_memory == nullptr) {
            error = L"FreeFPS: cannot allocate the timeline trampoline.";
            return false;
        }
        std::memcpy(g_trampoline_memory, kExpectedPrologue.data(), kPatchSize);
        const auto jump_back = make_absolute_jump(g_target + kPatchSize);
        std::memcpy(static_cast<std::uint8_t*>(g_trampoline_memory) + kPatchSize,
                    jump_back.data(), jump_back.size());
        DWORD old_protect = 0;
        if (!VirtualProtect(g_trampoline_memory, trampoline_size,
                            PAGE_EXECUTE_READ, &old_protect)) {
            VirtualFree(g_trampoline_memory, 0, MEM_RELEASE);
            g_trampoline_memory = nullptr;
            error = L"FreeFPS: cannot make the timeline trampoline executable.";
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), g_trampoline_memory,
                              trampoline_size);
        g_original = reinterpret_cast<TimelineBuilder>(g_trampoline_memory);
    }
    g_detour_bytes = make_absolute_jump(
        reinterpret_cast<const void*>(&timeline_builder_detour));
    auto threads = suspend_other_threads();
    const bool written = write_code(g_target, g_detour_bytes.data(),
                                    g_detour_bytes.size());
    resume_threads(threads);
    if (!written) {
        // Keep an existing trampoline alive. A thread that entered the detour
        // before restoration may still return through it.
        g_target = nullptr;
        error = L"FreeFPS: cannot install the timeline detour.";
        return false;
    }
    g_installed = true;
    return true;
#endif
}

void freefps_hook_uninstall() {
    if (!g_installed) {
        return;
    }
    AcquireSRWLockExclusive(&g_request_lock);
    g_request = FreeFpsRequestContext{};
    g_builder_hits = 0;
    ReleaseSRWLockExclusive(&g_request_lock);
    auto threads = suspend_other_threads();
    if (std::memcmp(g_target, g_detour_bytes.data(), g_detour_bytes.size()) == 0) {
        write_code(g_target, kExpectedPrologue.data(), kExpectedPrologue.size());
    }
    resume_threads(threads);
    g_installed = false;
    // Do not free the trampoline here. Restoring the entry prevents new
    // detours; retaining the trampoline avoids racing a pre-existing caller.
}

bool freefps_hook_begin_request(const FreeFpsRequestContext& request,
                                std::wstring& error) {
    AcquireSRWLockExclusive(&g_request_lock);
    if (!g_installed || g_request.active) {
        ReleaseSRWLockExclusive(&g_request_lock);
        error = L"FreeFPS: overlapping or inactive timeline request.";
        return false;
    }
    g_request = request;
    g_request.active = true;
    g_builder_hits = 0;
    ReleaseSRWLockExclusive(&g_request_lock);
    return true;
}

int freefps_hook_end_request(std::uint64_t generation) {
    AcquireSRWLockExclusive(&g_request_lock);
    const int hits = g_request.active && g_request.generation == generation
        ? g_builder_hits
        : -1;
    g_request = FreeFpsRequestContext{};
    g_builder_hits = 0;
    ReleaseSRWLockExclusive(&g_request_lock);
    return hits;
}

#pragma managed(pop)
