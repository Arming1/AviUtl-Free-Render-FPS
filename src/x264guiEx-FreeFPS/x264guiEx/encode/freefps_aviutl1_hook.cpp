#include "freefps_aviutl1_hook.h"
#include "freefps_code_patch.h"

#include <windows.h>
#include <wincrypt.h>
#include <tlhelp32.h>
#include <intrin.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

// Diagnostic trace from the ExEdit reverse-engineering work.  It appends to
// freerenderfps_aviutl1_probe.log in the current directory on every request,
// so it must stay out of release builds.  Define FREEFPS_AVIUTL1_PROBE_LOG=1
// only for a local diagnostic build.
#ifndef FREEFPS_AVIUTL1_PROBE_LOG
#define FREEFPS_AVIUTL1_PROBE_LOG 0
#endif

#pragma managed(push, off)

namespace {

// Official AviUtl 1.10 and ExEdit 0.92 identifiers.  The legacy SDK only
// exposes integer frame requests, so an unknown binary must be rejected rather
// than allowing a stale internal hook to corrupt the host.
constexpr DWORD kAviUtlImageSize = 0x338000;
constexpr DWORD kAviUtlEntryRva = 0x63168;
constexpr DWORD kAviUtlTimestamp = 0x5D95ECCC;
constexpr std::uint64_t kAviUtlFileSize = 610816;
constexpr std::array<std::uint8_t, 32> kAviUtlSha256{
    0x37, 0x37, 0xD8, 0xB0, 0xBC, 0x13, 0x36, 0x62,
    0x4B, 0xFB, 0x7F, 0x7C, 0x54, 0x42, 0x5C, 0x81,
    0x9A, 0x20, 0x8B, 0x5F, 0xEE, 0xE5, 0x52, 0xF5,
    0xCE, 0x94, 0x43, 0xDD, 0x4C, 0x6A, 0x1A, 0x04,
};

constexpr DWORD kExEditImageSize = 0x261000;
constexpr DWORD kExEditEntryRva = 0x9236E;
constexpr DWORD kExEditTimestamp = 0x51E3FD29;
constexpr std::uint64_t kExEditFileSize = 868352;
constexpr std::array<std::uint8_t, 32> kExEditSha256{
    0x06, 0xF2, 0xD8, 0x9D, 0x31, 0xD6, 0xB8, 0xF5,
    0xD1, 0xD8, 0xB0, 0x79, 0xA2, 0x87, 0x2A, 0x46,
    0x7F, 0x3C, 0x70, 0x4E, 0xD3, 0x83, 0x55, 0xE1,
    0xF1, 0x9F, 0x1F, 0xBD, 0x95, 0x29, 0xEE, 0x3A,
};

// ExEdit 0.92 RVA 0x47e30 is on the video evaluation path.  Its first five
// bytes are copied into the trampoline and checked before patching.
constexpr std::uintptr_t kRenderPrepRva = 0x47E30;
constexpr size_t kPatchSize = 5;
constexpr std::array<std::uint8_t, kPatchSize> kRenderPrepPrologue{
    0x51, 0x8B, 0x4C, 0x24, 0x0C,
};

// ExEdit fills FilterProcInfo->frame and ->subframe near the end of the
// render-preparation routine.  At this point the calculated subframe is read
// by the filter dispatcher, so the injection must happen after ExEdit's
// calculation and before that read.  The 0x114 field is a 0..99 centiframe
// remainder (the original routine divides its internal time by 100).
constexpr std::uintptr_t kSubframeApplyRva = 0x48180;
constexpr size_t kSubframePatchSize = 7;
constexpr size_t kRelativeJumpSize = 5;
constexpr std::array<std::uint8_t, kSubframePatchSize>
    kSubframeApplyPrologue{
        0x8B, 0x43, 0x10, 0x8B, 0x4C, 0x24, 0x10,
    };

using RenderPrep = void (__cdecl *)(
    std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
    std::uintptr_t, std::uintptr_t, std::uintptr_t);

#if FREEFPS_AVIUTL1_PROBE_LOG
constexpr LONG kMaxProbeLines = 2000;

struct ProbeFields {
    bool readable;
    std::int32_t frame;
    std::int32_t frame_n;
    std::int32_t frame_num;
    std::int32_t framerate_nu;
    std::int32_t framerate_de;
    std::int32_t subframe;
    std::int32_t object_start_frame;
    std::int32_t v_func_idx;
    std::int32_t obj_index;
    std::int32_t obj_num;
};

SRWLOCK g_log_lock = SRWLOCK_INIT;
LONG g_probe_lines = 0;
#endif

SRWLOCK g_request_lock = SRWLOCK_INIT;
FreeFpsRequestContext g_request{};
LONG g_builder_hits = 0;

std::uint8_t* g_exedit_module = nullptr;
std::uint8_t* g_target = nullptr;
void* g_trampoline = nullptr;
RenderPrep g_original = nullptr;
std::array<std::uint8_t, kPatchSize> g_detour{};

std::uint8_t* g_subframe_target = nullptr;
void* g_subframe_trampoline = nullptr;
std::array<std::uint8_t, kSubframePatchSize> g_subframe_detour{};
extern "C" volatile std::uintptr_t g_freefps_subframe_trampoline = 0;
bool g_installed = false;

std::array<std::uint8_t, kPatchSize> make_rel32_jump(
    const void* target, const void* destination, bool& representable) {
    std::array<std::uint8_t, kPatchSize> bytes{};
    bytes[0] = 0xE9;
    const auto target_address = reinterpret_cast<std::uintptr_t>(target);
    const auto destination_address = reinterpret_cast<std::uintptr_t>(destination);
    const auto delta = static_cast<std::int64_t>(destination_address) -
                       static_cast<std::int64_t>(target_address + kRelativeJumpSize);
    representable = delta >= (std::numeric_limits<std::int32_t>::min)() &&
                    delta <= (std::numeric_limits<std::int32_t>::max)();
    if (representable) {
        const auto displacement = static_cast<std::int32_t>(delta);
        std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
    }
    return bytes;
}

std::array<std::uint8_t, kSubframePatchSize> make_subframe_detour(
    const void* target, const void* destination, bool& representable) {
    std::array<std::uint8_t, kSubframePatchSize> bytes{};
    bytes.fill(0x90);
    const auto jump = make_rel32_jump(target, destination, representable);
    std::memcpy(bytes.data(), jump.data(), jump.size());
    return bytes;
}

bool module_path(HMODULE module, std::wstring& path) {
    wchar_t buffer[32768]{};
    const DWORD length = GetModuleFileNameW(module, buffer,
                                            static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return false;
    }
    path.assign(buffer, length);
    return true;
}

bool file_size(const std::wstring& path, std::uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    ULARGE_INTEGER value{};
    value.HighPart = data.nFileSizeHigh;
    value.LowPart = data.nFileSizeLow;
    size = value.QuadPart;
    return true;
}

bool sha256_file(const std::wstring& path,
                 std::array<std::uint8_t, 32>& digest) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool success = false;
    do {
        if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
                                  CRYPT_VERIFYCONTEXT) ||
            !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
            break;
        }
        file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            break;
        }
        std::array<std::uint8_t, 64 * 1024> buffer{};
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                          &read, nullptr)) {
                break;
            }
            if (read == 0) {
                DWORD length = static_cast<DWORD>(digest.size());
                success = CryptGetHashParam(hash, HP_HASHVAL, digest.data(),
                                             &length, 0) &&
                          length == digest.size();
                break;
            }
            if (!CryptHashData(hash, buffer.data(), read, 0)) {
                break;
            }
        }
    } while (false);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    if (hash != 0) {
        CryptDestroyHash(hash);
    }
    if (provider != 0) {
        CryptReleaseContext(provider, 0);
    }
    return success;
}

bool validate_module(HMODULE module, DWORD image_size, DWORD entry_rva,
                     DWORD timestamp, std::uint64_t expected_size,
                     const std::array<std::uint8_t, 32>& expected_hash,
                     const wchar_t* name, std::wstring& error) {
    if (module == nullptr) {
        error = std::wstring(L"FreeFPS: ") + name + L" is not loaded.";
        return false;
    }
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        error = std::wstring(L"FreeFPS: invalid ") + name + L" DOS header.";
        return false;
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.SizeOfImage != image_size ||
        nt->OptionalHeader.AddressOfEntryPoint != entry_rva ||
        nt->FileHeader.TimeDateStamp != timestamp) {
        error = std::wstring(L"FreeFPS: unsupported ") + name +
                L" image layout/version.";
        return false;
    }
    std::wstring path;
    if (!module_path(module, path)) {
        error = std::wstring(L"FreeFPS: cannot read ") + name + L" path.";
        return false;
    }
    std::uint64_t actual_size = 0;
    if (!file_size(path, actual_size) || actual_size != expected_size) {
        error = std::wstring(L"FreeFPS: unsupported ") + name + L" file size.";
        return false;
    }
    std::array<std::uint8_t, 32> actual_hash{};
    if (!sha256_file(path, actual_hash) || actual_hash != expected_hash) {
        error = std::wstring(L"FreeFPS: unsupported ") + name + L" file hash.";
        return false;
    }
    return true;
}

HMODULE find_exedit_module() {
    HMODULE module = GetModuleHandleW(L"exedit.auf");
    if (module != nullptr) {
        return module;
    }
    module = GetModuleHandleW(L"exedit");
    if (module != nullptr) {
        return module;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    HMODULE result = nullptr;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, L"exedit.auf") == 0 ||
                _wcsicmp(entry.szModule, L"exedit") == 0) {
                result = entry.hModule;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

#if FREEFPS_AVIUTL1_PROBE_LOG
void write_probe_text(const char* text) {
    AcquireSRWLockExclusive(&g_log_lock);
    wchar_t directory[32768]{};
    const DWORD length = GetCurrentDirectoryW(
        static_cast<DWORD>(std::size(directory)), directory);
    if (length != 0 && length < std::size(directory)) {
        std::wstring path(directory, length);
        path += L"\\freerenderfps_aviutl1_probe.log";
        HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, text, static_cast<DWORD>(std::strlen(text)),
                      &written, nullptr);
            CloseHandle(file);
        }
    }
    ReleaseSRWLockExclusive(&g_log_lock);
}

bool read_fields(std::uintptr_t address, ProbeFields& fields) {
    fields = ProbeFields{};
    if (address == 0) {
        return false;
    }
    __try {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
        fields.frame = *reinterpret_cast<const std::int32_t*>(bytes + 0x1C);
        fields.frame_n = *reinterpret_cast<const std::int32_t*>(bytes + 0x20);
        fields.frame_num = *reinterpret_cast<const std::int32_t*>(bytes + 0xA8);
        fields.framerate_nu = *reinterpret_cast<const std::int32_t*>(bytes + 0xFC);
        fields.framerate_de = *reinterpret_cast<const std::int32_t*>(bytes + 0x100);
        fields.subframe = *reinterpret_cast<const std::int32_t*>(bytes + 0x114);
        fields.object_start_frame = *reinterpret_cast<const std::int32_t*>(bytes + 0x118);
        fields.v_func_idx = *reinterpret_cast<const std::int32_t*>(bytes + 0x11C);
        fields.obj_index = *reinterpret_cast<const std::int32_t*>(bytes + 0x120);
        fields.obj_num = *reinterpret_cast<const std::int32_t*>(bytes + 0x124);
        fields.readable = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fields = ProbeFields{};
    }
    return fields.readable;
}

void format_fields(char* output, size_t output_size, const char* label,
                   std::uintptr_t address, const ProbeFields& fields) {
    if (!fields.readable) {
        _snprintf_s(output, output_size, _TRUNCATE,
                    "%s=0x%08x unreadable", label,
                    static_cast<unsigned int>(address));
        return;
    }
    _snprintf_s(output, output_size, _TRUNCATE,
                "%s=0x%08x f=%d fn=%d frame_num=%d frn=%d frd=%d sub=%d obj_start=%d v=%d oi=%d on=%d",
                label, static_cast<unsigned int>(address), fields.frame,
                fields.frame_n, fields.frame_num, fields.framerate_nu,
                fields.framerate_de, fields.subframe, fields.object_start_frame,
                fields.v_func_idx, fields.obj_index, fields.obj_num);
}
#endif

extern "C" void __cdecl freefps_subframe_inject(
    std::uintptr_t filter_info) {
    if (filter_info == 0) {
        return;
    }
    double coordinate = 0.0;
    bool active = false;
    AcquireSRWLockShared(&g_request_lock);
    active = g_request.active;
    coordinate = g_request.target_coordinate;
    ReleaseSRWLockShared(&g_request_lock);
    if (!active || !std::isfinite(coordinate)) {
        return;
    }

    const double integer_part = std::floor(coordinate);
    const double fraction = (std::max)(0.0, (std::min)(0.999999, coordinate - integer_part));
    const auto centiframe = static_cast<std::int32_t>(
        (std::min)(99.0, std::floor(fraction * 100.0 + 1.0e-9)));
    __try {
        *reinterpret_cast<volatile std::int32_t *>(filter_info + 0x114) =
            centiframe;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // An unexpected ExEdit pointer must fail closed instead of taking
        // down the host. The version/signature guard remains the primary
        // protection; this is the last-resort pointer guard.
    }
}

#if defined(_M_IX86)
// This is entered by a JMP from inside ExEdit, not by a normal C++ call.
// Preserve the live register set, pass the FilterProcInfo pointer from EDI
// to the helper, then continue through the copied original instructions.
// PUSHAD stores registers at ESP+00=EDI, +04=ESI, +08=EBP, +0C=original
// ESP, +10=EBX, +14=EDX, +18=ECX, +1C=EAX.  The old +1C access passed the
// saved EAX value instead of FilterProcInfo and made the guarded write a
// no-op in the legacy host.
extern "C" __declspec(naked) void freefps_subframe_detour() {
    __asm {
        pushad
        mov eax, [esp]
        push eax
        call freefps_subframe_inject
        add esp, 4
        popad
        mov eax, dword ptr [g_freefps_subframe_trampoline]
        jmp eax
    }
}
#endif

#if FREEFPS_AVIUTL1_PROBE_LOG
void log_render_prep(std::uintptr_t arg1, std::uintptr_t arg2,
                     std::uintptr_t arg3, std::uintptr_t arg4,
                     std::uintptr_t arg5, std::uintptr_t arg6,
                     std::uintptr_t arg7, double target_coordinate) {
    const LONG line = InterlockedIncrement(&g_probe_lines);
    if (line > kMaxProbeLines) {
        return;
    }
    ProbeFields sixth{};
    ProbeFields seventh{};
    read_fields(arg6, sixth);
    read_fields(arg7, seventh);
    char sixth_text[512]{};
    char seventh_text[512]{};
    format_fields(sixth_text, sizeof(sixth_text), "a6", arg6, sixth);
    format_fields(seventh_text, sizeof(seventh_text), "a7", arg7, seventh);
    char text[1400]{};
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto base = reinterpret_cast<std::uintptr_t>(g_exedit_module);
    const auto caller_rva = caller >= base ? caller - base : 0;
    _snprintf_s(text, sizeof(text), _TRUNCATE,
                "prep=%ld caller_rva=0x%08x a1=0x%08x a2=0x%08x a3=0x%08x a4=0x%08x a5=0x%08x %s %s target=%0.12f\r\n",
                line, static_cast<unsigned int>(caller_rva),
                static_cast<unsigned int>(arg1), static_cast<unsigned int>(arg2),
                static_cast<unsigned int>(arg3), static_cast<unsigned int>(arg4),
                static_cast<unsigned int>(arg5), sixth_text, seventh_text,
                target_coordinate);
    write_probe_text(text);
}
#endif

void __cdecl render_prep_detour(
    std::uintptr_t arg1, std::uintptr_t arg2, std::uintptr_t arg3,
    std::uintptr_t arg4, std::uintptr_t arg5, std::uintptr_t arg6,
    std::uintptr_t arg7) {
    g_original(arg1, arg2, arg3, arg4, arg5, arg6, arg7);

    // This detour only counts render-prep hits so a cache-bypassed request
    // can be rejected.  The actual sub-frame value is injected by
    // freefps_subframe_detour at the ExEdit dispatch boundary, after this
    // routine has calculated the ordinary integer-frame state.
    bool active = false;
    double target_coordinate = 0.0;
    AcquireSRWLockShared(&g_request_lock);
    active = g_request.active;
    target_coordinate = g_request.target_coordinate;
    if (active) {
        InterlockedIncrement(&g_builder_hits);
    }
    ReleaseSRWLockShared(&g_request_lock);
#if FREEFPS_AVIUTL1_PROBE_LOG
    if (active) {
        log_render_prep(arg1, arg2, arg3, arg4, arg5, arg6, arg7,
                        target_coordinate);
    }
#else
    (void)target_coordinate;
#endif
}

} // namespace

bool freefps_host_supported() {
    // Same checks as freefps_hook_install, minus the patching.
    std::wstring error;
    if (!validate_module(GetModuleHandleW(nullptr), kAviUtlImageSize,
                         kAviUtlEntryRva, kAviUtlTimestamp, kAviUtlFileSize,
                         kAviUtlSha256, L"AviUtl 1.10", error)) {
        return false;
    }
    const HMODULE exedit = find_exedit_module();
    if (!validate_module(exedit, kExEditImageSize, kExEditEntryRva,
                         kExEditTimestamp, kExEditFileSize, kExEditSha256,
                         L"ExEdit 0.92", error)) {
        return false;
    }
    const auto* exedit_base = reinterpret_cast<const std::uint8_t*>(exedit);
    return std::memcmp(exedit_base + kRenderPrepRva, kRenderPrepPrologue.data(),
                       kPatchSize) == 0 &&
           std::memcmp(exedit_base + kSubframeApplyRva,
                       kSubframeApplyPrologue.data(), kSubframePatchSize) == 0;
}

bool freefps_hook_install(std::wstring& error) {
    if (g_installed) {
        return true;
    }
    if (!validate_module(GetModuleHandleW(nullptr), kAviUtlImageSize,
                         kAviUtlEntryRva, kAviUtlTimestamp, kAviUtlFileSize,
                         kAviUtlSha256, L"AviUtl 1.10", error)) {
        return false;
    }
    HMODULE exedit = find_exedit_module();
    if (!validate_module(exedit, kExEditImageSize, kExEditEntryRva,
                         kExEditTimestamp, kExEditFileSize, kExEditSha256,
                         L"ExEdit 0.92", error)) {
        return false;
    }

    g_exedit_module = reinterpret_cast<std::uint8_t*>(exedit);
    g_target = g_exedit_module + kRenderPrepRva;
    if (std::memcmp(g_target, kRenderPrepPrologue.data(), kPatchSize) != 0) {
        error = L"FreeFPS: ExEdit 0.92 render-prep signature validation failed.";
        g_exedit_module = nullptr;
        g_target = nullptr;
        return false;
    }
    g_subframe_target = g_exedit_module + kSubframeApplyRva;
    if (std::memcmp(g_subframe_target, kSubframeApplyPrologue.data(),
                    kSubframePatchSize) != 0) {
        error = L"FreeFPS: ExEdit 0.92 subframe-apply signature validation failed.";
        g_exedit_module = nullptr;
        g_target = nullptr;
        g_subframe_target = nullptr;
        return false;
    }
    bool representable = false;
    const auto detour = make_rel32_jump(
        g_target, reinterpret_cast<const void*>(&render_prep_detour),
        representable);
    if (!representable) {
        error = L"FreeFPS: AviUtl 1.x detour is outside the x86 rel32 range.";
        g_exedit_module = nullptr;
        g_target = nullptr;
        g_subframe_target = nullptr;
        return false;
    }
    bool subframe_representable = false;
    const auto subframe_detour = make_subframe_detour(
        g_subframe_target,
        reinterpret_cast<const void *>(&freefps_subframe_detour),
        subframe_representable);
    if (!subframe_representable) {
        error = L"FreeFPS: AviUtl 1.x subframe detour is outside the x86 rel32 range.";
        g_exedit_module = nullptr;
        g_target = nullptr;
        g_subframe_target = nullptr;
        return false;
    }

    constexpr size_t trampoline_size = kPatchSize * 2;
    if (g_trampoline == nullptr) {
        g_trampoline = VirtualAlloc(nullptr, trampoline_size,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (g_trampoline == nullptr) {
            error = L"FreeFPS: cannot allocate the AviUtl 1.x trampoline.";
            g_exedit_module = nullptr;
            g_target = nullptr;
            g_subframe_target = nullptr;
            return false;
        }
        std::memcpy(g_trampoline, kRenderPrepPrologue.data(), kPatchSize);
        bool jump_back_ok = false;
        const auto jump_back = make_rel32_jump(
            static_cast<std::uint8_t*>(g_trampoline) + kPatchSize,
            g_target + kPatchSize, jump_back_ok);
        if (!jump_back_ok) {
            VirtualFree(g_trampoline, 0, MEM_RELEASE);
            g_trampoline = nullptr;
            error = L"FreeFPS: AviUtl 1.x trampoline jump is outside the x86 rel32 range.";
            g_exedit_module = nullptr;
            g_target = nullptr;
            g_subframe_target = nullptr;
            return false;
        }
        std::memcpy(static_cast<std::uint8_t*>(g_trampoline) + kPatchSize,
                    jump_back.data(), jump_back.size());
        DWORD old_protect = 0;
        if (!VirtualProtect(g_trampoline, trampoline_size, PAGE_EXECUTE_READ,
                            &old_protect)) {
            VirtualFree(g_trampoline, 0, MEM_RELEASE);
            g_trampoline = nullptr;
            error = L"FreeFPS: cannot make the AviUtl 1.x trampoline executable.";
            g_exedit_module = nullptr;
            g_target = nullptr;
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), g_trampoline,
                              trampoline_size);
        g_original = reinterpret_cast<RenderPrep>(g_trampoline);
    }

    if (g_subframe_trampoline == nullptr) {
        constexpr size_t subframe_trampoline_size =
            kSubframePatchSize + kRelativeJumpSize;
        g_subframe_trampoline = VirtualAlloc(
            nullptr, subframe_trampoline_size, MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE);
        if (g_subframe_trampoline == nullptr) {
            error = L"FreeFPS: cannot allocate the AviUtl 1.x subframe trampoline.";
            g_target = nullptr;
            g_subframe_target = nullptr;
            g_exedit_module = nullptr;
            return false;
        }
        std::memcpy(g_subframe_trampoline, kSubframeApplyPrologue.data(),
                    kSubframePatchSize);
        bool jump_back_ok = false;
        const auto jump_back = make_rel32_jump(
            static_cast<std::uint8_t *>(g_subframe_trampoline) +
                kSubframePatchSize,
            g_subframe_target + kSubframePatchSize, jump_back_ok);
        if (!jump_back_ok) {
            VirtualFree(g_subframe_trampoline, 0, MEM_RELEASE);
            g_subframe_trampoline = nullptr;
            error = L"FreeFPS: AviUtl 1.x subframe trampoline jump is outside the x86 rel32 range.";
            g_target = nullptr;
            g_subframe_target = nullptr;
            g_exedit_module = nullptr;
            return false;
        }
        std::memcpy(static_cast<std::uint8_t *>(g_subframe_trampoline) +
                        kSubframePatchSize,
                    jump_back.data(), jump_back.size());
        DWORD old_protect = 0;
        if (!VirtualProtect(g_subframe_trampoline, subframe_trampoline_size,
                            PAGE_EXECUTE_READ, &old_protect)) {
            VirtualFree(g_subframe_trampoline, 0, MEM_RELEASE);
            g_subframe_trampoline = nullptr;
            error = L"FreeFPS: cannot make the AviUtl 1.x subframe trampoline executable.";
            g_target = nullptr;
            g_subframe_target = nullptr;
            g_exedit_module = nullptr;
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), g_subframe_trampoline,
                              subframe_trampoline_size);
        g_freefps_subframe_trampoline = reinterpret_cast<std::uintptr_t>(
            g_subframe_trampoline);
    }

    g_detour = detour;
    g_subframe_detour = subframe_detour;
    const FreeFpsPatchRange patches[] = {
        {g_target, kPatchSize},
        {g_subframe_target, kSubframePatchSize},
    };
    FreeFpsThreadFreeze freeze;
    if (!freeze.freeze(patches, std::size(patches))) {
        g_target = nullptr;
        g_subframe_target = nullptr;
        g_exedit_module = nullptr;
        error = L"FreeFPS: cannot pause AviUtl 1.x threads safely for the detour.";
        return false;
    }
    const bool main_written =
        freefps_write_code(g_target, g_detour.data(), g_detour.size());
    const bool subframe_written =
        main_written &&
        freefps_write_code(g_subframe_target, g_subframe_detour.data(),
                           g_subframe_detour.size());
    if (main_written && !subframe_written) {
        freefps_write_code(g_target, kRenderPrepPrologue.data(),
                           kRenderPrepPrologue.size());
    }
    const bool written = main_written && subframe_written;
    freeze.thaw();
    if (!written) {
        g_target = nullptr;
        g_subframe_target = nullptr;
        g_exedit_module = nullptr;
        error = L"FreeFPS: cannot install the AviUtl 1.x render-prep detour.";
        return false;
    }
    g_installed = true;
#if FREEFPS_AVIUTL1_PROBE_LOG
    write_probe_text("FreeRenderFPS AviUtl1 render-prep probe installed\r\n");
#endif
    return true;
}

void freefps_hook_uninstall() {
    if (!g_installed) {
        return;
    }
    AcquireSRWLockExclusive(&g_request_lock);
    g_request = FreeFpsRequestContext{};
    g_builder_hits = 0;
    ReleaseSRWLockExclusive(&g_request_lock);
    const FreeFpsPatchRange patches[] = {
        {g_target, kPatchSize},
        {g_subframe_target, kSubframePatchSize},
    };
    FreeFpsThreadFreeze freeze;
    if (!freeze.freeze(patches, std::size(patches))) {
        // Restoring bytes while other threads run could tear an instruction.
        // With no active request both detours are pass-through, so leave them
        // installed; the next session reuses them.
        return;
    }
    if (std::memcmp(g_target, g_detour.data(), g_detour.size()) == 0) {
        freefps_write_code(g_target, kRenderPrepPrologue.data(),
                           kRenderPrepPrologue.size());
    }
    if (g_subframe_target != nullptr &&
        std::memcmp(g_subframe_target, g_subframe_detour.data(),
                    g_subframe_detour.size()) == 0) {
        freefps_write_code(g_subframe_target, kSubframeApplyPrologue.data(),
                           kSubframeApplyPrologue.size());
    }
    freeze.thaw();
    g_installed = false;
    g_target = nullptr;
    g_subframe_target = nullptr;
    g_exedit_module = nullptr;
}

bool freefps_hook_begin_request(const FreeFpsRequestContext& request,
                                std::wstring& error) {
    AcquireSRWLockExclusive(&g_request_lock);
    if (!g_installed || g_request.active) {
        ReleaseSRWLockExclusive(&g_request_lock);
        error = L"FreeFPS: overlapping or inactive AviUtl 1.x timeline request.";
        return false;
    }
    g_request = request;
    g_request.active = true;
    g_builder_hits = 0;
#if FREEFPS_AVIUTL1_PROBE_LOG
    g_probe_lines = 0;
#endif
    ReleaseSRWLockExclusive(&g_request_lock);
#if FREEFPS_AVIUTL1_PROBE_LOG
    char text[512]{};
    _snprintf_s(text, sizeof(text), _TRUNCATE,
                "request generation=%llu output=%lld public=%d target=%0.12f\r\n",
                static_cast<unsigned long long>(request.generation),
                static_cast<long long>(request.output_sample_index),
                request.requested_integer_frame, request.target_coordinate);
    write_probe_text(text);
#endif
    return true;
}

int freefps_hook_end_request(std::uint64_t generation) {
    AcquireSRWLockExclusive(&g_request_lock);
    const int hits = g_request.active && g_request.generation == generation
        ? static_cast<int>(g_builder_hits)
        : -1;
    g_request = FreeFpsRequestContext{};
    g_builder_hits = 0;
    ReleaseSRWLockExclusive(&g_request_lock);
    return hits;
}

#pragma managed(pop)
