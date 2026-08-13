#include <windows.h>
#include <tlhelp32.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <string>
#include <unordered_map>

namespace {

constexpr DWORD kExpectedImageSize = 5'402'624;
constexpr DWORD64 kOutputBridgeRva = 0x22a6c0;
constexpr DWORD64 kTimelineBuilderRva = 0x2662d0;
constexpr DWORD64 kCacheHitRva = 0x221aba;
// Confirmed dynamically in the observe pass for the normal output path.
constexpr DWORD64 kOrdinaryCallerReturnRva = 0x2657e8;
constexpr DWORD64 kOrdinaryCallRva = 0x2657e3;
constexpr int kOutputSamples = 60;
constexpr int kDirectRequests = 63;
constexpr int kEvictRequests = 65;

constexpr std::array<unsigned char, 15> kOutputBridgePrefix{
    0x48, 0x89, 0x5c, 0x24, 0x10,
    0x48, 0x89, 0x74, 0x24, 0x18,
    0x48, 0x89, 0x7c, 0x24, 0x20,
};
constexpr std::array<unsigned char, 17> kTimelineBuilderPrefix{
    0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56,
    0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
};
constexpr std::array<unsigned char, 5> kOrdinaryCallBytes{
    0xe8, 0xe8, 0x0a, 0x00, 0x00,
};
constexpr std::array<unsigned char, 5> kCacheHitBytes{
    0x48, 0x85, 0xed, 0x74, 0x09,
};

struct SavedDebugRegisters {
    DWORD64 dr0 = 0;
    DWORD64 dr1 = 0;
    DWORD64 dr2 = 0;
    DWORD64 dr3 = 0;
    DWORD64 dr6 = 0;
    DWORD64 dr7 = 0;
};

struct ActiveRequest {
    bool valid = false;
    int ordinal = -1;
    int requested_frame = -1;
    double target_coordinate = 0.0;
    const char* kind = "none";
    DWORD output_thread = 0;
    DWORD64 return_address = 0;
    int candidate_hits = 0;
    int modified_hits = 0;
    int cache_hit_events = 0;
};

DWORD g_pid = 0;
HANDLE g_process = nullptr;
DWORD64 g_module_base = 0;
DWORD64 g_output_bridge = 0;
DWORD64 g_timeline_builder = 0;
DWORD64 g_cache_hit = 0;
bool g_schedule = false;
bool g_reverse_cache_sequence = false;
bool g_evict_mode = false;
int g_total_requests = kDirectRequests;
bool g_detach_requested = false;
bool g_completed = false;
bool g_mutation_allowed = true;
bool g_breakpoints_restored = false;
int g_next_ordinal = 0;
ULONGLONG g_completion_tick = 0;
ActiveRequest g_active;
std::unordered_map<DWORD, SavedDebugRegisters> g_saved_registers;
std::ofstream g_log;

void log_prefix(const char* event) {
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    g_log << "event=" << event << " qpc=" << qpc.QuadPart;
}

template <typename T>
bool read_target(DWORD64 address, T& value) {
    SIZE_T read = 0;
    return ReadProcessMemory(g_process, reinterpret_cast<const void*>(address),
                             &value, sizeof(value), &read) != FALSE &&
           read == sizeof(value);
}

template <typename T>
bool write_target(DWORD64 address, const T& value) {
    SIZE_T written = 0;
    return WriteProcessMemory(g_process, reinterpret_cast<void*>(address),
                              &value, sizeof(value), &written) != FALSE &&
           written == sizeof(value);
}

template <size_t N>
bool verify_bytes(DWORD64 address,
                  const std::array<unsigned char, N>& expected) {
    std::array<unsigned char, N> actual{};
    SIZE_T read = 0;
    return ReadProcessMemory(g_process, reinterpret_cast<const void*>(address),
                             actual.data(), actual.size(), &read) != FALSE &&
           read == actual.size() && actual == expected;
}

bool locate_and_verify_image() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE |
                                                TH32CS_SNAPMODULE32, g_pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    bool found = false;
    if (Module32FirstW(snapshot, &module)) {
        do {
            const wchar_t* name = wcsrchr(module.szExePath, L'\\');
            name = name == nullptr ? module.szExePath : name + 1;
            if (_wcsicmp(name, L"aviutl2.exe") != 0) {
                continue;
            }
            g_module_base = reinterpret_cast<DWORD64>(module.modBaseAddr);
            g_output_bridge = g_module_base + kOutputBridgeRva;
            g_timeline_builder = g_module_base + kTimelineBuilderRva;
            g_cache_hit = g_module_base + kCacheHitRva;
            const bool size_ok = module.modBaseSize == kExpectedImageSize;
            const bool output_ok = verify_bytes(g_output_bridge,
                                                kOutputBridgePrefix);
            const bool timeline_ok = verify_bytes(g_timeline_builder,
                                                  kTimelineBuilderPrefix);
            const bool call_ok = verify_bytes(g_module_base + kOrdinaryCallRva,
                                              kOrdinaryCallBytes);
            const bool cache_hit_ok = verify_bytes(g_cache_hit,
                                                   kCacheHitBytes);
            log_prefix("image_check");
            g_log << " base=0x" << std::hex << g_module_base << std::dec
                  << " image_size=" << module.modBaseSize
                  << " size_ok=" << size_ok
                  << " output_prefix_ok=" << output_ok
                  << " timeline_prefix_ok=" << timeline_ok
                  << " ordinary_call_ok=" << call_ok
                  << " cache_hit_ok=" << cache_hit_ok << "\n";
            found = size_ok && output_ok && timeline_ok && call_ok &&
                    cache_hit_ok;
            break;
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    return found;
}

constexpr DWORD64 kEnableMask0123 = 0xffull;
constexpr DWORD64 kConditionMask0123 = 0xffff0000ull;
constexpr DWORD64 kSlotMask0123 = kEnableMask0123 | kConditionMask0123;

void configure_base_breakpoints(CONTEXT& context) {
    context.Dr0 = g_output_bridge;
    context.Dr1 = g_timeline_builder;
    context.Dr2 = 0;
    context.Dr3 = g_cache_hit;
    context.Dr6 &= ~0xfull;
    context.Dr7 &= ~kSlotMask0123;
    context.Dr7 |= (1ull << 0) | (1ull << 2) | (1ull << 6);
}

void configure_return_breakpoint(CONTEXT& context, DWORD64 address) {
    context.Dr2 = address;
    context.Dr7 &= ~((1ull << 4) | (1ull << 5) |
                     (3ull << 24) | (3ull << 26));
    context.Dr7 |= (1ull << 4);
}

void disable_return_breakpoint(CONTEXT& context) {
    context.Dr2 = 0;
    context.Dr6 &= ~(1ull << 2);
    context.Dr7 &= ~((1ull << 4) | (1ull << 5) |
                     (3ull << 24) | (3ull << 26));
}

bool arm_thread(DWORD thread_id, HANDLE supplied = nullptr) {
    // DebugActiveProcess can report CREATE_THREAD for threads already covered by
    // the initial snapshot. Do not mistake our own armed slots for foreign ones.
    if (g_saved_registers.find(thread_id) != g_saved_registers.end()) {
        return true;
    }
    HANDLE thread = supplied;
    bool close_thread = false;
    if (thread == nullptr) {
        thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                THREAD_QUERY_INFORMATION | THREAD_SUSPEND_RESUME,
                            FALSE, thread_id);
        close_thread = true;
    }
    if (thread == nullptr) {
        return false;
    }
    DWORD suspend_count = 0;
    bool suspended_here = false;
    if (close_thread) {
        suspend_count = SuspendThread(thread);
        suspended_here = suspend_count != static_cast<DWORD>(-1);
        if (!suspended_here) {
            CloseHandle(thread);
            return false;
        }
    }
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    bool ok = GetThreadContext(thread, &context) != FALSE;
    if (ok && (context.Dr7 & kEnableMask0123) != 0) {
        log_prefix("arm_refused_occupied_slots");
        g_log << " thread_id=" << thread_id << " dr7=0x" << std::hex
              << context.Dr7 << std::dec << "\n";
        ok = false;
    }
    if (ok) {
        g_saved_registers.emplace(thread_id, SavedDebugRegisters{
            context.Dr0, context.Dr1, context.Dr2, context.Dr3,
            context.Dr6, context.Dr7
        });
        configure_base_breakpoints(context);
        ok = SetThreadContext(thread, &context) != FALSE;
    }
    if (close_thread) {
        ResumeThread(thread);
        CloseHandle(thread);
    }
    return ok;
}

size_t arm_existing_threads() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    size_t count = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == g_pid &&
                arm_thread(entry.th32ThreadID)) {
                ++count;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

size_t restore_debug_registers() {
    size_t restored = 0;
    for (const auto& item : g_saved_registers) {
        HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                       THREAD_SUSPEND_RESUME,
                                   FALSE, item.first);
        if (thread == nullptr) {
            continue;
        }
        const DWORD suspend_count = SuspendThread(thread);
        if (suspend_count == static_cast<DWORD>(-1)) {
            CloseHandle(thread);
            continue;
        }
        CONTEXT context{};
        context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(thread, &context)) {
            const SavedDebugRegisters& saved = item.second;
            context.Dr0 = saved.dr0;
            context.Dr1 = saved.dr1;
            context.Dr2 = saved.dr2;
            context.Dr3 = saved.dr3;
            context.Dr6 = (context.Dr6 & ~0xfull) | (saved.dr6 & 0xfull);
            context.Dr7 = (context.Dr7 & ~kSlotMask0123) |
                          (saved.dr7 & kSlotMask0123);
            if (SetThreadContext(thread, &context)) {
                ++restored;
            }
        }
        ResumeThread(thread);
        CloseHandle(thread);
    }
    g_breakpoints_restored = true;
    return restored;
}

void expected_mapping(int ordinal, int& frame, double& coordinate,
                      const char*& kind) {
    if (ordinal < kOutputSamples) {
        frame = ordinal;
        coordinate = static_cast<double>(ordinal) * 0.5;
        kind = "sample";
        return;
    }
    constexpr std::array<double, 3> cache_a{80.0, 80.5, 80.0};
    constexpr std::array<double, 3> cache_b{80.5, 80.0, 80.5};
    const auto& cache = g_reverse_cache_sequence ? cache_b : cache_a;
    const int relative = ordinal - kOutputSamples;
    if (g_evict_mode && (relative == 1 || relative == 3)) {
        frame = 79;
        coordinate = 79.0;
        kind = "eviction";
        return;
    }
    const int target_index = g_evict_mode ? relative / 2 : relative;
    frame = 80;
    coordinate = cache[static_cast<size_t>(target_index)];
    kind = "cache";
}

void handle_output_entry(DWORD thread_id, HANDLE thread, CONTEXT& context) {
    DWORD64 return_address = 0;
    const bool return_ok = read_target(context.Rsp, return_address);
    const int requested_frame = static_cast<int>(context.Rcx & 0xffffffffu);
    const DWORD format = static_cast<DWORD>(context.Rdx & 0xffffffffu);
    const int ordinal = g_next_ordinal++;
    int expected_frame = -1;
    double target = 0.0;
    const char* kind = "unexpected";
    if (ordinal < g_total_requests) {
        expected_mapping(ordinal, expected_frame, target, kind);
    }

    const bool overlap = g_active.valid;
    const bool order_ok = ordinal < g_total_requests &&
                          requested_frame == expected_frame;
    log_prefix("output_request_enter");
    g_log << " ordinal=" << ordinal << " kind=" << kind
          << " thread_id=" << thread_id
          << " requested_frame=" << requested_frame
          << " expected_frame=" << expected_frame
          << " target_coordinate=" << std::setprecision(17) << target
          << " format=0x" << std::hex << format
          << " return_address=0x" << return_address << std::dec
          << " return_ok=" << return_ok << " overlap=" << overlap
          << " order_ok=" << order_ok << "\n";

    if (overlap || !return_ok || !order_ok) {
        g_mutation_allowed = false;
        context.Dr6 = 0;
        context.EFlags |= (1u << 16);
        SetThreadContext(thread, &context);
        g_log.flush();
        return;
    }

    g_active = ActiveRequest{true, ordinal, requested_frame, target, kind,
                             thread_id, return_address, 0, 0};
    configure_return_breakpoint(context, return_address);
    context.Dr6 = 0;
    context.EFlags |= (1u << 16);
    SetThreadContext(thread, &context);
    g_log.flush();
}

void handle_timeline_entry(DWORD thread_id, HANDLE thread, CONTEXT& context) {
    if (!g_active.valid) {
        context.Dr6 = 0;
        context.EFlags |= (1u << 16);
        SetThreadContext(thread, &context);
        return;
    }

    DWORD64 caller_return = 0;
    unsigned char private_flag = 0;
    double private_double = 0.0;
    const bool caller_ok = read_target(context.Rsp, caller_return);
    const bool flag_ok = read_target(context.Rsp + 0x40, private_flag);
    const bool double_ok = read_target(context.Rsp + 0x48, private_double);
    const DWORD64 caller_rva = caller_ok && caller_return >= g_module_base
        ? caller_return - g_module_base
        : 0;
    const int integer_input = static_cast<int>(context.R8 & 0xffffffffu);
    const bool caller_match = caller_rva == kOrdinaryCallerReturnRva;
    const bool frame_match = integer_input == g_active.requested_frame;
    ++g_active.candidate_hits;

    log_prefix("timeline_input_enter");
    g_log << " ordinal=" << g_active.ordinal
          << " kind=" << g_active.kind
          << " thread_id=" << thread_id
          << " output_thread_id=" << g_active.output_thread
          << " caller_return_rva=0x" << std::hex << caller_rva << std::dec
          << " rcx=0x" << std::hex << context.Rcx
          << " rdx=0x" << context.Rdx << std::dec
          << " integer_input=" << integer_input
          << " private_flag=" << static_cast<unsigned>(private_flag)
          << " private_double=" << std::setprecision(17) << private_double
          << " target_coordinate=" << g_active.target_coordinate
          << " caller_ok=" << caller_ok
          << " flag_ok=" << flag_ok
          << " double_ok=" << double_ok
          << " caller_match=" << caller_match
          << " frame_match=" << frame_match << "\n";

    if (g_schedule && g_mutation_allowed && caller_match && frame_match &&
        flag_ok && double_ok) {
        const DWORD64 double_address = context.Rsp + 0x48;
        const DWORD64 flag_address = context.Rsp + 0x40;
        const double target = g_active.target_coordinate;
        const unsigned char enabled = 1;
        const bool write_double_ok = write_target(double_address, target);
        const bool write_flag_ok = write_double_ok &&
                                   write_target(flag_address, enabled);
        double double_readback = 0.0;
        unsigned char flag_readback = 0;
        const bool readback_ok = read_target(double_address, double_readback) &&
                                 read_target(flag_address, flag_readback);
        const bool applied = write_flag_ok && readback_ok &&
                             flag_readback == enabled &&
                             std::fabs(double_readback - target) < 1e-12;
        if (applied) {
            ++g_active.modified_hits;
        } else {
            g_mutation_allowed = false;
        }
        log_prefix("timeline_input_map");
        g_log << " ordinal=" << g_active.ordinal
              << " thread_id=" << thread_id
              << " double_address=0x" << std::hex << double_address
              << " flag_address=0x" << flag_address << std::dec
              << " before_flag=" << static_cast<unsigned>(private_flag)
              << " before_double=" << std::setprecision(17) << private_double
              << " after_flag=" << static_cast<unsigned>(flag_readback)
              << " after_double=" << double_readback
              << " applied=" << applied << "\n";
    }

    context.Dr6 = 0;
    context.EFlags |= (1u << 16);
    SetThreadContext(thread, &context);
    g_log.flush();
}

void handle_output_return(DWORD thread_id, HANDLE thread, CONTEXT& context) {
    if (!g_active.valid || thread_id != g_active.output_thread ||
        context.Rip != g_active.return_address) {
        g_mutation_allowed = false;
        context.Dr6 = 0;
        context.EFlags |= (1u << 16);
        SetThreadContext(thread, &context);
        return;
    }
    log_prefix("output_request_return");
    g_log << " ordinal=" << g_active.ordinal
          << " kind=" << g_active.kind
          << " thread_id=" << thread_id
          << " requested_frame=" << g_active.requested_frame
          << " target_coordinate=" << std::setprecision(17)
          << g_active.target_coordinate
          << " candidate_hits=" << g_active.candidate_hits
          << " modified_hits=" << g_active.modified_hits
          << " cache_hit_events=" << g_active.cache_hit_events
          << " mutation_allowed=" << g_mutation_allowed << "\n";

    const int finished_ordinal = g_active.ordinal;
    g_active = ActiveRequest{};
    disable_return_breakpoint(context);
    context.Dr6 = 0;
    context.EFlags |= (1u << 16);
    SetThreadContext(thread, &context);

    if (finished_ordinal == g_total_requests - 1) {
        g_completed = true;
        const size_t restored = restore_debug_registers();
        g_completion_tick = GetTickCount64();
        log_prefix("breakpoints_restored_after_last_request");
        g_log << " thread_count=" << restored << "\n";
    }
    g_log.flush();
}

void handle_cache_hit(DWORD thread_id, HANDLE thread, CONTEXT& context) {
    std::int32_t key = -1;
    std::int32_t node_key = -1;
    const bool key_ok = read_target(context.R14, key);
    const bool node_ok = read_target(context.Rax + 0x10, node_key);
    if (g_active.valid) {
        ++g_active.cache_hit_events;
    }
    log_prefix("integer_cache_hit");
    g_log << " ordinal=" << (g_active.valid ? g_active.ordinal : -1)
          << " kind=" << (g_active.valid ? g_active.kind : "none")
          << " thread_id=" << thread_id
          << " output_thread_id="
          << (g_active.valid ? g_active.output_thread : 0)
          << " key=" << key << " node_key=" << node_key
          << " key_ok=" << key_ok << " node_ok=" << node_ok
          << " table=0x" << std::hex << context.R15
          << " node=0x" << context.Rax << std::dec << "\n";
    context.Dr6 = 0;
    context.EFlags |= (1u << 16);
    SetThreadContext(thread, &context);
    g_log.flush();
}

BOOL WINAPI console_handler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT ||
        type == CTRL_CLOSE_EVENT) {
        g_detach_requested = true;
        return TRUE;
    }
    return FALSE;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 4 || argc > 6 || (_wcsicmp(argv[3], L"observe") != 0 &&
                      _wcsicmp(argv[3], L"schedule") != 0)) {
        std::fwprintf(stderr,
                      L"usage: subframe_scheduler_watch.exe <pid> <log> "
                      L"<observe|schedule> [A|B] [direct|evict]\n");
        return 2;
    }
    g_pid = wcstoul(argv[1], nullptr, 10);
    g_schedule = _wcsicmp(argv[3], L"schedule") == 0;
    if (argc >= 5) {
        if (_wcsicmp(argv[4], L"A") != 0 && _wcsicmp(argv[4], L"B") != 0) {
            return 2;
        }
        g_reverse_cache_sequence = _wcsicmp(argv[4], L"B") == 0;
    }
    if (argc >= 6) {
        if (_wcsicmp(argv[5], L"direct") != 0 &&
            _wcsicmp(argv[5], L"evict") != 0) {
            return 2;
        }
        g_evict_mode = _wcsicmp(argv[5], L"evict") == 0;
    }
    g_total_requests = g_evict_mode ? kEvictRequests : kDirectRequests;
    g_log.open(argv[2], std::ios::binary | std::ios::trunc);
    if (!g_log) {
        return 2;
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
    g_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                                PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                            FALSE, g_pid);
    if (g_process == nullptr || !DebugActiveProcess(g_pid)) {
        if (g_process != nullptr) {
            CloseHandle(g_process);
        }
        return 3;
    }
    DebugSetProcessKillOnExit(FALSE);
    log_prefix("attached");
    g_log << " pid=" << g_pid
          << " mode=" << (g_schedule ? "schedule" : "observe")
          << " sequence=" << (g_reverse_cache_sequence ? "B" : "A")
          << " cache_mode=" << (g_evict_mode ? "evict" : "direct")
          << " expected_requests=" << g_total_requests << "\n";
    g_log.flush();

    bool armed = false;
    if (!locate_and_verify_image()) {
        g_detach_requested = true;
    } else {
        const size_t count = arm_existing_threads();
        log_prefix("breakpoints_armed");
        g_log << " trigger=immediate_thread_snapshot"
              << " thread_count=" << count
              << " output_rva=0x" << std::hex << kOutputBridgeRva
              << " timeline_rva=0x" << kTimelineBuilderRva
              << " ordinary_return_rva=0x"
              << kOrdinaryCallerReturnRva << std::dec << "\n";
        armed = count > 0;
    }
    g_log.flush();
    while (!g_detach_requested) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 250)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT) {
                if (g_completion_tick != 0 &&
                    GetTickCount64() - g_completion_tick >= 1000) {
                    g_detach_requested = true;
                }
                continue;
            }
            break;
        }
        DWORD continue_status = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && !armed) {
            if (!locate_and_verify_image()) {
                g_detach_requested = true;
            } else {
                const size_t count = arm_existing_threads();
                log_prefix("breakpoints_armed");
                g_log << " trigger=create_process"
                      << " thread_count=" << count
                      << " output_rva=0x" << std::hex << kOutputBridgeRva
                      << " timeline_rva=0x" << kTimelineBuilderRva
                      << " cache_hit_rva=0x" << kCacheHitRva
                      << " ordinary_return_rva=0x"
                      << kOrdinaryCallerReturnRva << std::dec << "\n";
                armed = count > 0;
            }
        } else if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT && armed &&
            !g_breakpoints_restored) {
            arm_thread(event.dwThreadId, event.u.CreateThread.hThread);
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            const DWORD code = event.u.Exception.ExceptionRecord.ExceptionCode;
            if (code == EXCEPTION_BREAKPOINT && !armed) {
                if (!locate_and_verify_image()) {
                    g_detach_requested = true;
                } else {
                    const size_t count = arm_existing_threads();
                    log_prefix("breakpoints_armed");
                    g_log << " thread_count=" << count
                          << " output_rva=0x" << std::hex << kOutputBridgeRva
                          << " timeline_rva=0x" << kTimelineBuilderRva
                          << " ordinary_return_rva=0x"
                          << kOrdinaryCallerReturnRva << std::dec << "\n";
                    armed = count > 0;
                }
            } else if (code == EXCEPTION_SINGLE_STEP && armed) {
                HANDLE thread = OpenThread(THREAD_GET_CONTEXT |
                                               THREAD_SET_CONTEXT |
                                               THREAD_QUERY_INFORMATION,
                                           FALSE, event.dwThreadId);
                if (thread != nullptr) {
                    CONTEXT context{};
                    context.ContextFlags = CONTEXT_ALL |
                                           CONTEXT_DEBUG_REGISTERS;
                    if (GetThreadContext(thread, &context)) {
                        if ((context.Dr6 & (1ull << 2)) != 0) {
                            handle_output_return(event.dwThreadId, thread,
                                                 context);
                        } else if ((context.Dr6 & (1ull << 3)) != 0) {
                            handle_cache_hit(event.dwThreadId, thread,
                                             context);
                        } else if ((context.Dr6 & (1ull << 0)) != 0) {
                            handle_output_entry(event.dwThreadId, thread,
                                                context);
                        } else if ((context.Dr6 & (1ull << 1)) != 0) {
                            handle_timeline_entry(event.dwThreadId, thread,
                                                  context);
                        } else {
                            continue_status = DBG_EXCEPTION_NOT_HANDLED;
                        }
                    }
                    CloseHandle(thread);
                }
            } else if (code != EXCEPTION_BREAKPOINT) {
                continue_status = DBG_EXCEPTION_NOT_HANDLED;
            }
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            log_prefix("process_exit");
            g_log << " code=" << event.u.ExitProcess.dwExitCode << "\n";
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId,
                               continue_status);
            break;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId,
                           continue_status);
    }

    if (!g_breakpoints_restored) {
        const size_t restored = restore_debug_registers();
        log_prefix("breakpoints_restored_at_detach");
        g_log << " thread_count=" << restored << "\n";
    }
    DebugActiveProcessStop(g_pid);
    log_prefix("detached");
    g_log << " completed=" << g_completed
          << " requests_seen=" << g_next_ordinal
          << " mutation_allowed=" << g_mutation_allowed
          << " mode=" << (g_schedule ? "schedule" : "observe") << "\n";
    g_log.flush();
    CloseHandle(g_process);
    return g_completed ? 0 : 5;
}
