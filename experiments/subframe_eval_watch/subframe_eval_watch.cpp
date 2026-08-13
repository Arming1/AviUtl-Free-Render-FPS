#include <windows.h>
#include <tlhelp32.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <string>

namespace {

// PE SizeOfImage for the verified v2.1.4 executable. The on-disk file size is
// 5,228,544 bytes; Toolhelp's modBaseSize reports the mapped SizeOfImage.
constexpr DWORD kExpectedImageSize = 5'402'624;
constexpr DWORD64 kCandidateEntryRva = 0x2662d0;
constexpr DWORD64 kAfterInitialStoreRva = 0x2663c5;
constexpr DWORD64 kAfterFinalSelectionRva = 0x266642;

enum class TemporaryStage {
    none,
    after_initial_store,
    after_final_selection,
};

DWORD g_pid = 0;
HANDLE g_process = nullptr;
DWORD64 g_module_base = 0;
DWORD64 g_candidate_entry = 0;
DWORD64 g_after_initial_store = 0;
DWORD64 g_after_final_selection = 0;
DWORD g_active_thread = 0;
TemporaryStage g_stage = TemporaryStage::none;
int g_target_frame = 80;
bool g_inject = false;
double g_modified_position = 80.5;
bool g_detach_requested = false;
bool g_experiment_completed = false;
bool g_cleanup_only = false;
ULONGLONG g_completion_tick = 0;
std::ofstream g_log;

std::string narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(),
                        static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

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

bool locate_aviutl2() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE |
                                                TH32CS_SNAPMODULE32,
                                                g_pid);
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
            if (module.modBaseSize != kExpectedImageSize) {
                log_prefix("image_rejected");
                g_log << " path=" << narrow(module.szExePath)
                      << " image_size=" << module.modBaseSize
                      << " expected=" << kExpectedImageSize << "\n";
                break;
            }
            g_module_base = reinterpret_cast<DWORD64>(module.modBaseAddr);
            g_candidate_entry = g_module_base + kCandidateEntryRva;
            g_after_initial_store = g_module_base + kAfterInitialStoreRva;
            g_after_final_selection = g_module_base + kAfterFinalSelectionRva;
            log_prefix("image_accepted");
            g_log << " path=" << narrow(module.szExePath)
                  << " base=0x" << std::hex << g_module_base << std::dec
                  << " image_size=" << module.modBaseSize << "\n";
            found = true;
            break;
        } while (Module32NextW(snapshot, &module));
    }
    CloseHandle(snapshot);
    return found;
}

void configure_entry_breakpoint(CONTEXT& context) {
    context.Dr0 = g_candidate_entry;
    context.Dr6 = 0;
    context.Dr7 &= ~((1ull << 0) | (3ull << 16) | (3ull << 18));
    context.Dr7 |= (1ull << 0); // local DR0, execute, length 1
}

void disable_temporary_breakpoint(CONTEXT& context) {
    context.Dr1 = 0;
    context.Dr7 &= ~((1ull << 2) | (3ull << 20) | (3ull << 22));
}

void configure_temporary_breakpoint(CONTEXT& context, DWORD64 address) {
    context.Dr1 = address;
    context.Dr7 &= ~((1ull << 2) | (3ull << 20) | (3ull << 22));
    context.Dr7 |= (1ull << 2); // local DR1, execute, length 1
}

bool arm_thread(DWORD thread_id, HANDLE supplied = nullptr) {
    HANDLE thread = supplied;
    bool close_thread = false;
    if (thread == nullptr) {
        thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                THREAD_QUERY_INFORMATION,
                            FALSE, thread_id);
        close_thread = true;
    }
    if (thread == nullptr) {
        return false;
    }
    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    bool ok = GetThreadContext(thread, &context) != FALSE;
    if (ok) {
        const DWORD64 occupied = context.Dr7 & 0x0full;
        if (occupied != 0) {
            log_prefix("thread_breakpoint_slots_occupied");
            g_log << " thread_id=" << thread_id
                  << " dr7=0x" << std::hex << context.Dr7 << std::dec
                  << "\n";
            ok = false;
        }
    }
    if (ok) {
        configure_entry_breakpoint(context);
        disable_temporary_breakpoint(context);
        ok = SetThreadContext(thread, &context) != FALSE;
    }
    if (close_thread) {
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
    THREADENTRY32 thread{};
    thread.dwSize = sizeof(thread);
    if (Thread32First(snapshot, &thread)) {
        do {
            if (thread.th32OwnerProcessID == g_pid &&
                arm_thread(thread.th32ThreadID)) {
                ++count;
            }
        } while (Thread32Next(snapshot, &thread));
    }
    CloseHandle(snapshot);
    return count;
}

size_t clear_debug_registers() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    size_t cleared_thread_count = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID != g_pid) {
                continue;
            }
            HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                           THREAD_SUSPEND_RESUME,
                                       FALSE, entry.th32ThreadID);
            if (thread == nullptr) {
                continue;
            }
            const DWORD previous_suspend_count = SuspendThread(thread);
            if (previous_suspend_count == static_cast<DWORD>(-1)) {
                CloseHandle(thread);
                continue;
            }
            CONTEXT context{};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context)) {
                bool changed = false;
                if (context.Dr0 == g_candidate_entry) {
                    context.Dr0 = 0;
                    context.Dr6 &= ~(1ull << 0);
                    context.Dr7 &= ~((1ull << 0) | (1ull << 1) |
                                     (3ull << 16) | (3ull << 18));
                    changed = true;
                }
                if (context.Dr1 == g_after_initial_store ||
                    context.Dr1 == g_after_final_selection) {
                    context.Dr1 = 0;
                    context.Dr6 &= ~(1ull << 1);
                    context.Dr7 &= ~((1ull << 2) | (1ull << 3) |
                                     (3ull << 20) | (3ull << 22));
                    changed = true;
                }
                if (changed) {
                    if (SetThreadContext(thread, &context)) {
                        ++cleared_thread_count;
                    }
                }
            }
            ResumeThread(thread);
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return cleared_thread_count;
}

void log_state(const char* event, DWORD thread_id, const CONTEXT& context) {
    int integer_position = 0;
    double double_position = 0.0;
    double residual_position = 0.0;
    const DWORD64 integer_address = context.Rbp + 0x17c;
    const DWORD64 double_address = context.Rbp + 0x180;
    const DWORD64 residual_address = context.Rbp + 0x188;
    const bool integer_ok = read_target(integer_address, integer_position);
    const bool double_ok = read_target(double_address, double_position);
    const bool residual_ok = read_target(residual_address, residual_position);
    log_prefix(event);
    g_log << " thread_id=" << thread_id
          << " rip_rva=0x" << std::hex << (context.Rip - g_module_base)
          << " rbp=0x" << context.Rbp
          << " integer_address=0x" << integer_address
          << " double_address=0x" << double_address << std::dec
          << " integer_read=" << integer_ok
          << " double_read=" << double_ok
          << " residual_read=" << residual_ok
          << " integer_position=" << integer_position
          << " double_position=" << std::setprecision(17) << double_position
          << " residual_position=" << residual_position
          << "\n";
}

void handle_entry(DWORD thread_id, HANDLE thread, CONTEXT& context) {
    const int incoming_frame = static_cast<int>(context.R8 & 0xffffffffu);
    unsigned char private_flag = 0;
    double private_double = 0.0;
    read_target(context.Rsp + 0x40, private_flag);
    read_target(context.Rsp + 0x48, private_double);

    if (incoming_frame == g_target_frame && g_stage == TemporaryStage::none) {
        log_prefix("candidate_entry_match");
        g_log << " thread_id=" << thread_id
              << " rip_rva=0x" << std::hex
              << (context.Rip - g_module_base) << std::dec
              << " incoming_r8d=" << incoming_frame
              << " stack_arg8_flag=" << static_cast<unsigned>(private_flag)
              << " stack_arg9_double=" << std::setprecision(17)
              << private_double
              << " rsp=0x" << std::hex << context.Rsp << std::dec << "\n";
        g_active_thread = thread_id;
        g_stage = TemporaryStage::after_initial_store;
        configure_temporary_breakpoint(context, g_after_initial_store);
    }
    context.Dr6 = 0;
    context.EFlags |= (1u << 16); // RF: resume past execute breakpoint
    SetThreadContext(thread, &context);
    g_log.flush();
}

void handle_temporary(DWORD thread_id, HANDLE thread, CONTEXT& context) {
    if (thread_id != g_active_thread) {
        context.Dr6 = 0;
        context.EFlags |= (1u << 16);
        SetThreadContext(thread, &context);
        return;
    }

    if (g_stage == TemporaryStage::after_initial_store) {
        log_state("after_initial_store", thread_id, context);
        int integer_position = 0;
        double double_position = 0.0;
        const DWORD64 integer_address = context.Rbp + 0x17c;
        const DWORD64 double_address = context.Rbp + 0x180;
        const bool state_ok = read_target(integer_address, integer_position) &&
                              read_target(double_address, double_position);
        if (g_inject) {
            if (!state_ok || integer_position != g_target_frame ||
                std::fabs(double_position - g_target_frame) > 1e-9) {
                log_prefix("injection_refused");
                g_log << " thread_id=" << thread_id
                      << " state_ok=" << state_ok
                      << " integer_position=" << integer_position
                      << " double_position=" << std::setprecision(17)
                      << double_position << "\n";
                disable_temporary_breakpoint(context);
                g_detach_requested = true;
            } else {
                const bool write_ok = write_target(double_address,
                                                   g_modified_position);
                double readback = 0.0;
                const bool readback_ok = read_target(double_address, readback);
                log_prefix("double_coordinate_modified");
                g_log << " thread_id=" << thread_id
                      << " address=0x" << std::hex << double_address << std::dec
                      << " before=" << std::setprecision(17) << double_position
                      << " requested_after=" << g_modified_position
                      << " write_ok=" << write_ok
                      << " readback_ok=" << readback_ok
                      << " readback=" << readback << "\n";
                if (!write_ok || !readback_ok ||
                    std::fabs(readback - g_modified_position) > 1e-12) {
                    disable_temporary_breakpoint(context);
                    g_detach_requested = true;
                }
            }
        }
        if (!g_detach_requested) {
            g_stage = TemporaryStage::after_final_selection;
            configure_temporary_breakpoint(context, g_after_final_selection);
        }
    } else if (g_stage == TemporaryStage::after_final_selection) {
        log_state("after_final_selection", thread_id, context);
        disable_temporary_breakpoint(context);
        context.Dr0 = 0;
        context.Dr6 &= ~3ull;
        context.Dr7 &= ~((1ull << 0) | (1ull << 1) |
                         (3ull << 16) | (3ull << 18));
        g_stage = TemporaryStage::none;
        g_experiment_completed = true;
    }

    context.Dr6 = 0;
    context.EFlags |= (1u << 16);
    SetThreadContext(thread, &context);
    if (g_experiment_completed && g_completion_tick == 0) {
        // The debugger event has all target threads stopped. Clear the owned
        // hardware slots now, then remain attached briefly so an in-flight
        // output callback can finish before DebugActiveProcessStop.
        clear_debug_registers();
        g_completion_tick = GetTickCount64();
        log_prefix("breakpoints_disabled_after_match");
        g_log << " thread_id=" << thread_id << "\n";
    }
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
    if (argc == 4 && _wcsicmp(argv[3], L"cleanup") == 0) {
        g_cleanup_only = true;
    } else if (argc != 5) {
        std::fwprintf(stderr,
                      L"usage: subframe_eval_watch.exe <pid> <observer-log> "
                      L"<target-frame> <log|modified-double>\n"
                      L"       subframe_eval_watch.exe <pid> <observer-log> "
                      L"cleanup\n");
        return 2;
    }
    g_pid = wcstoul(argv[1], nullptr, 10);
    if (!g_cleanup_only) {
        g_target_frame = static_cast<int>(wcstol(argv[3], nullptr, 10));
    }
    if (!g_cleanup_only && _wcsicmp(argv[4], L"log") != 0) {
        wchar_t* end = nullptr;
        g_modified_position = wcstod(argv[4], &end);
        g_inject = end != argv[4] && *end == L'\0';
        if (!g_inject || !std::isfinite(g_modified_position) ||
            g_modified_position <= g_target_frame ||
            g_modified_position >= g_target_frame + 1.0) {
            std::fwprintf(stderr,
                          L"modified-double must be within target frame and "
                          L"the next integer frame\n");
            return 2;
        }
    }
    g_log.open(narrow(argv[2]), std::ios::binary | std::ios::trunc);
    if (!g_log) {
        std::fwprintf(stderr, L"cannot open observer log\n");
        return 2;
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
    g_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
                                PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                            FALSE, g_pid);
    if (g_process == nullptr) {
        std::fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError());
        return 3;
    }
    if (!DebugActiveProcess(g_pid)) {
        std::fwprintf(stderr, L"DebugActiveProcess failed: %lu\n",
                      GetLastError());
        CloseHandle(g_process);
        return 4;
    }
    DebugSetProcessKillOnExit(FALSE);
    log_prefix("attached");
    g_log << " pid=" << g_pid << " target_frame=" << g_target_frame
          << " mode=" << (g_cleanup_only ? "cleanup" :
                           (g_inject ? "modify" : "log"))
          << " modified_position=" << std::setprecision(17)
          << g_modified_position << "\n";
    g_log.flush();

    bool armed = false;
    while (!g_detach_requested) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 250)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT) {
                if (g_completion_tick != 0 &&
                    GetTickCount64() - g_completion_tick >= 2000) {
                    g_detach_requested = true;
                }
                continue;
            }
            break;
        }
        DWORD continue_status = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT && armed &&
            !g_experiment_completed) {
            arm_thread(event.dwThreadId, event.u.CreateThread.hThread);
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            const DWORD code = event.u.Exception.ExceptionRecord.ExceptionCode;
            if (code == EXCEPTION_BREAKPOINT && !armed) {
                if (locate_aviutl2()) {
                    // A debugger that detaches with active hardware breakpoints
                    // can leave the debug-register values in target threads.
                    // Remove only addresses owned by this observer before arming.
                    const size_t stale_thread_count = clear_debug_registers();
                    if (g_cleanup_only) {
                        log_prefix("owned_breakpoints_cleaned");
                        g_log << " thread_count=" << stale_thread_count << "\n";
                        g_experiment_completed = true;
                        g_detach_requested = true;
                        armed = true;
                        ContinueDebugEvent(event.dwProcessId, event.dwThreadId,
                                           continue_status);
                        break;
                    }
                    const size_t thread_count = arm_existing_threads();
                    log_prefix("breakpoints_armed");
                    g_log << " thread_count=" << thread_count
                          << " entry_rva=0x" << std::hex << kCandidateEntryRva
                          << " post_store_rva=0x" << kAfterInitialStoreRva
                          << " final_rva=0x" << kAfterFinalSelectionRva
                          << std::dec << "\n";
                    armed = true;
                } else {
                    g_detach_requested = true;
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
                        const bool temporary_hit = (context.Dr6 & (1ull << 1)) != 0;
                        const bool entry_hit = (context.Dr6 & (1ull << 0)) != 0;
                        if (temporary_hit) {
                            handle_temporary(event.dwThreadId, thread, context);
                        } else if (entry_hit) {
                            handle_entry(event.dwThreadId, thread, context);
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

    clear_debug_registers();
    DebugActiveProcessStop(g_pid);
    log_prefix("detached");
    g_log << " completed=" << g_experiment_completed
          << " mode=" << (g_cleanup_only ? "cleanup" :
                           (g_inject ? "modify" : "log")) << "\n";
    g_log.flush();
    CloseHandle(g_process);
    return g_experiment_completed ? 0 : 5;
}
