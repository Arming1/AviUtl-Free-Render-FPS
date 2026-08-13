#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct ModuleInfo {
    DWORD64 base{};
    DWORD size{};
    std::wstring path;
};

DWORD g_pid = 0;
HANDLE g_process = nullptr;
std::wstring g_probe_log;
std::ofstream g_log;
DWORD64 g_watch_address = 0;
DWORD64 g_source_address = 0;
DWORD64 g_entry_address = 0;
size_t g_hit_count = 0;
size_t g_max_hits = 16;
bool g_detach_requested = false;

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

void log_qpc_prefix(const char* event) {
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    g_log << "event=" << event << " qpc=" << qpc.QuadPart;
}

std::vector<ModuleInfo> enumerate_modules() {
    std::vector<ModuleInfo> modules;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE |
                                                TH32CS_SNAPMODULE32,
                                                g_pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return modules;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            modules.push_back(ModuleInfo{
                reinterpret_cast<DWORD64>(entry.modBaseAddr),
                entry.modBaseSize,
                entry.szExePath,
            });
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return modules;
}

std::string format_address(DWORD64 address,
                           const std::vector<ModuleInfo>& modules) {
    std::ostringstream output;
    output << "0x" << std::hex << address;
    for (const auto& module : modules) {
        if (address >= module.base && address < module.base + module.size) {
            const wchar_t* name = wcsrchr(module.path.c_str(), L'\\');
            name = name == nullptr ? module.path.c_str() : name + 1;
            output << " " << narrow(name) << "+0x"
                   << std::hex << (address - module.base);
            break;
        }
    }
    return output.str();
}

bool parse_watch_address() {
    std::ifstream input(narrow(g_probe_log), std::ios::binary);
    if (!input) {
        return false;
    }
    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    const std::string marker = "time_addr=";
    size_t position = contents.rfind(marker);
    if (position == std::string::npos) {
        return false;
    }
    position += marker.size();
    char* end = nullptr;
    const char* text = contents.c_str() + position;
    const int base = text[0] == '0' && (text[1] == 'x' || text[1] == 'X')
                         ? 0
                         : 16;
    const unsigned long long value = std::strtoull(text, &end, base);
    if (value == 0 || end == contents.c_str() + position) {
        return false;
    }
    g_watch_address = static_cast<DWORD64>(value);
    return true;
}

bool locate_entry_address() {
    for (const auto& module : enumerate_modules()) {
        const wchar_t* name = wcsrchr(module.path.c_str(), L'\\');
        name = name == nullptr ? module.path.c_str() : name + 1;
        if (_wcsicmp(name, L"aviutl2.exe") == 0) {
            g_entry_address = module.base + 0x209990;
            return true;
        }
    }
    return false;
}

bool set_watchpoint_on_thread(DWORD thread_id, HANDLE supplied = nullptr) {
    HANDLE thread = supplied;
    bool close_thread = false;
    if (thread == nullptr) {
        thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                                THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION,
                            FALSE, thread_id);
        close_thread = true;
    }
    if (thread == nullptr) {
        log_qpc_prefix("watch_thread_error");
        g_log << " thread_id=" << thread_id
              << " stage=open error=" << GetLastError() << "\n";
        return false;
    }

    CONTEXT context{};
    context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    bool ok = GetThreadContext(thread, &context) != FALSE;
    if (ok) {
        context.Dr0 = g_watch_address;
        context.Dr1 = g_source_address;
        context.Dr2 = g_entry_address;
        context.Dr6 = 0;
        context.Dr7 &= ~((3ull << 16) | (3ull << 18) | 1ull |
                         (3ull << 20) | (3ull << 22) | (1ull << 2) |
                         (3ull << 24) | (3ull << 26) | (1ull << 4));
        context.Dr7 |= 1ull | (1ull << 16) | (2ull << 18);
        if (g_source_address != 0) {
            context.Dr7 |= (1ull << 2) | (1ull << 20) | (2ull << 22);
        }
        if (g_entry_address != 0) {
            context.Dr7 |= (1ull << 4);
        }
        ok = SetThreadContext(thread, &context) != FALSE;
    }
    if (!ok) {
        log_qpc_prefix("watch_thread_error");
        g_log << " thread_id=" << thread_id
              << " stage=context error=" << GetLastError() << "\n";
    }
    if (close_thread) {
        CloseHandle(thread);
    }
    return ok;
}

void arm_existing_threads() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    size_t armed = 0;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == g_pid &&
                set_watchpoint_on_thread(entry.th32ThreadID)) {
                ++armed;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    log_qpc_prefix("watch_armed");
    g_log << " address=0x" << std::hex << g_watch_address << std::dec
          << " size=8 access=write thread_count=" << armed << "\n";
    if (g_entry_address != 0) {
        g_log << "entry_watch=0x" << std::hex << g_entry_address << std::dec
              << " access=execute rva=0x209990\n";
    }
    g_log.flush();
}

void clear_debug_registers() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
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
            CONTEXT context{};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(thread, &context)) {
                context.Dr0 = 0;
                context.Dr1 = 0;
                context.Dr2 = 0;
                context.Dr3 = 0;
                context.Dr6 = 0;
                context.Dr7 = 0;
                SetThreadContext(thread, &context);
            }
            CloseHandle(thread);
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

template <typename T>
bool read_target(DWORD64 address, T& value) {
    SIZE_T read = 0;
    return ReadProcessMemory(g_process, reinterpret_cast<void*>(address),
                             &value, sizeof(value), &read) != FALSE &&
           read == sizeof(value);
}

void dump_time_inputs(const CONTEXT& context) {
    const DWORD64 state = context.Rdx;
    DWORD64 scene = 0;
    DWORD64 object_record = 0;
    int integer_position = 0;
    double double_position = 0.0;
    int local_offset = 0;
    int object_start = 0;
    int object_end = 0;
    int rate = 0;
    int scale = 0;
    int sdk_frame = 0;
    int sdk_frame_total = 0;
    double sdk_time = 0.0;
    double sdk_time_total = 0.0;

    read_target(state, scene);
    read_target(state + 0x0c, integer_position);
    read_target(state + 0x10, double_position);
    read_target(state + 0x28, local_offset);
    read_target(state + 0x30, object_record);
    if (object_record != 0) {
        read_target(object_record + 0x48, object_start);
        read_target(object_record + 0x4c, object_end);
    }
    if (scene != 0) {
        read_target(scene + 0x74, rate);
        read_target(scene + 0x78, scale);
    }
    read_target(g_watch_address - 8, sdk_frame);
    read_target(g_watch_address - 4, sdk_frame_total);
    read_target(g_watch_address, sdk_time);
    read_target(g_watch_address + 8, sdk_time_total);

    g_log << std::setprecision(17)
          << "time_inputs state=0x" << std::hex << state
          << " scene=0x" << scene
          << " object_record=0x" << object_record << std::dec
          << " integer_position=" << integer_position
          << " double_position=" << double_position
          << " local_offset=" << local_offset
          << " object_start=" << object_start
          << " object_end=" << object_end
          << " rate=" << rate
          << " scale=" << scale
          << " sdk_frame=" << sdk_frame
          << " sdk_frame_total=" << sdk_frame_total
          << " sdk_time=" << sdk_time
          << " sdk_time_total=" << sdk_time_total << "\n";
}

void dump_bytes(DWORD64 rip) {
    constexpr SIZE_T before = 32;
    constexpr SIZE_T total = 64;
    unsigned char bytes[total]{};
    SIZE_T read = 0;
    const DWORD64 start = rip >= before ? rip - before : 0;
    if (!ReadProcessMemory(g_process, reinterpret_cast<void*>(start), bytes,
                           sizeof(bytes), &read)) {
        g_log << "bytes_error=" << GetLastError() << "\n";
        return;
    }
    g_log << "bytes_start=0x" << std::hex << start << " bytes=";
    for (SIZE_T i = 0; i < read; ++i) {
        g_log << std::setw(2) << std::setfill('0')
              << static_cast<unsigned>(bytes[i]);
    }
    g_log << std::dec << "\n";
}

void dump_stack(HANDLE thread, CONTEXT context,
                const std::vector<ModuleInfo>& modules) {
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (unsigned index = 0; index < 24; ++index) {
        if (index > 0 && !StackWalk64(IMAGE_FILE_MACHINE_AMD64, g_process,
                                      thread, &frame, &context, nullptr,
                                      SymFunctionTableAccess64,
                                      SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            break;
        }
        char symbol_buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        g_log << "stack[" << index << "]="
              << format_address(frame.AddrPC.Offset, modules);
        if (SymFromAddr(g_process, frame.AddrPC.Offset, &displacement, symbol)) {
            g_log << " symbol=" << symbol->Name << "+0x" << std::hex
                  << displacement << std::dec;
        }
        g_log << "\n";
    }
}

void handle_watch_hit(DWORD thread_id, HANDLE thread, bool source_hit,
                      bool entry_hit) {
    CONTEXT context{};
    context.ContextFlags = CONTEXT_ALL | CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(thread, &context)) {
        return;
    }
    const auto modules = enumerate_modules();
    if (source_hit) {
        bool in_aviutl2 = false;
        for (const auto& module : modules) {
            const wchar_t* name = wcsrchr(module.path.c_str(), L'\\');
            name = name == nullptr ? module.path.c_str() : name + 1;
            if (_wcsicmp(name, L"aviutl2.exe") == 0 &&
                context.Rip >= module.base &&
                context.Rip < module.base + module.size) {
                in_aviutl2 = true;
                break;
            }
        }
        if (!in_aviutl2) {
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            context.Dr6 = 0;
            SetThreadContext(thread, &context);
            return;
        }
    }
    ++g_hit_count;
    log_qpc_prefix(entry_hit ? "dispatch_entry_hit" :
                   (source_hit ? "source_write_hit" : "write_hit"));
    g_log << " hit=" << g_hit_count << " thread_id=" << thread_id
          << " watched=0x" << std::hex
          << (entry_hit ? g_entry_address :
              (source_hit ? g_source_address : g_watch_address))
          << " rip=" << format_address(context.Rip, modules)
          << " rsp=0x" << context.Rsp << " rbp=0x" << context.Rbp
          << " rax=0x" << context.Rax << " rbx=0x" << context.Rbx
          << " rcx=0x" << context.Rcx << " rdx=0x" << context.Rdx
          << " rsi=0x" << context.Rsi << " rdi=0x" << context.Rdi
          << " r8=0x" << context.R8 << " r9=0x" << context.R9
          << " r10=0x" << context.R10 << " r11=0x" << context.R11
          << " r12=0x" << context.R12 << " r13=0x" << context.R13
          << " r14=0x" << context.R14 << " r15=0x" << context.R15
          << " dr6=0x" << context.Dr6 << " dr7=0x" << context.Dr7
          << std::dec << "\n";
    if (!source_hit && !entry_hit) {
        dump_time_inputs(context);
    }
    dump_bytes(context.Rip);
    dump_stack(thread, context, modules);
    g_log.flush();

    if (entry_hit) {
        g_source_address = context.R8 + 0x10;
        arm_existing_threads();
        log_qpc_prefix("source_watch_armed");
        g_log << " address=0x" << std::hex << g_source_address << std::dec
              << " size=8 access=write derived_from=dispatch_r8_plus_0x10\n";
        g_log.flush();
    } else if (source_hit || g_source_address != 0) {
        // The state object is short-lived and its allocation is reused after
        // dispatch. Disable DR1 immediately after a useful hit (or after the
        // OBJECT_INFO write proves no earlier write occurred) to avoid logging
        // unrelated heap reuse.
        g_source_address = 0;
        arm_existing_threads();
    }

    // arm_existing_threads() may have changed DR1 after the context snapshot
    // above. Re-read instead of restoring stale debug-register state.
    CONTEXT resume{};
    resume.ContextFlags = CONTEXT_CONTROL | CONTEXT_DEBUG_REGISTERS;
    if (GetThreadContext(thread, &resume)) {
        resume.Dr6 = 0;
        if (entry_hit) {
            // Resume past the instruction execution breakpoint once. Without
            // RF, x64 immediately traps again at the same RIP.
            resume.EFlags |= (1u << 16);
        }
        SetThreadContext(thread, &resume);
    }
    if (g_hit_count >= g_max_hits) {
        g_detach_requested = true;
    }
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
    if (argc < 4 || argc > 6) {
        std::fwprintf(stderr,
                      L"usage: object_time_watch.exe <pid> <probe-log> "
                      L"<observer-log> [max-hits] [initial-source-hex]\n");
        return 2;
    }
    g_pid = wcstoul(argv[1], nullptr, 10);
    g_probe_log = argv[2];
    if (argc == 5) {
        g_max_hits = (std::max)(size_t{1},
                                static_cast<size_t>(wcstoul(argv[4], nullptr, 10)));
    }
    if (argc == 6) {
        g_max_hits = (std::max)(size_t{1},
                                static_cast<size_t>(wcstoul(argv[4], nullptr, 10)));
        g_source_address = wcstoull(argv[5], nullptr, 16);
    }
    g_log.open(narrow(argv[3]), std::ios::binary | std::ios::trunc);
    if (!g_log) {
        std::fwprintf(stderr, L"cannot open observer log\n");
        return 2;
    }
    SetConsoleCtrlHandler(console_handler, TRUE);
    g_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                            FALSE, g_pid);
    if (g_process == nullptr) {
        std::fwprintf(stderr, L"OpenProcess failed: %lu\n", GetLastError());
        return 3;
    }

    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(g_process, nullptr, TRUE);
    if (!DebugActiveProcess(g_pid)) {
        std::fwprintf(stderr, L"DebugActiveProcess failed: %lu\n",
                      GetLastError());
        SymCleanup(g_process);
        CloseHandle(g_process);
        return 4;
    }
    DebugSetProcessKillOnExit(FALSE);
    log_qpc_prefix("attached");
    g_log << " pid=" << g_pid << " max_hits=" << g_max_hits << "\n";
    g_log.flush();

    while (!g_detach_requested) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 250)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT) {
                continue;
            }
            break;
        }
        DWORD continue_status = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT &&
            g_watch_address != 0) {
            set_watchpoint_on_thread(event.dwThreadId,
                                     event.u.CreateThread.hThread);
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            const DWORD code = event.u.Exception.ExceptionRecord.ExceptionCode;
            if (code == EXCEPTION_BREAKPOINT) {
                log_qpc_prefix("breakpoint");
                g_log << " thread_id=" << event.dwThreadId
                      << " first_chance=" << event.u.Exception.dwFirstChance
                      << " address=0x" << std::hex
                      << reinterpret_cast<DWORD64>(
                             event.u.Exception.ExceptionRecord.ExceptionAddress)
                      << std::dec << "\n";
                if (g_watch_address == 0 && parse_watch_address() &&
                    locate_entry_address()) {
                    arm_existing_threads();
                }
            } else if (code == EXCEPTION_SINGLE_STEP &&
                       g_watch_address != 0) {
                HANDLE thread = OpenThread(THREAD_GET_CONTEXT |
                                               THREAD_SET_CONTEXT |
                                               THREAD_QUERY_INFORMATION,
                                           FALSE, event.dwThreadId);
                if (thread != nullptr) {
                    CONTEXT debug_context{};
                    debug_context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                    bool source_hit = false;
                    bool entry_hit = false;
                    if (GetThreadContext(thread, &debug_context)) {
                        source_hit = (debug_context.Dr6 & (1ull << 1)) != 0;
                        entry_hit = (debug_context.Dr6 & (1ull << 2)) != 0;
                    }
                    handle_watch_hit(event.dwThreadId, thread, source_hit,
                                     entry_hit);
                    CloseHandle(thread);
                }
            } else {
                continue_status = DBG_EXCEPTION_NOT_HANDLED;
            }
        } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            log_qpc_prefix("process_exit");
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
    log_qpc_prefix("detached");
    g_log << " hit_count=" << g_hit_count << "\n";
    g_log.flush();
    SymCleanup(g_process);
    CloseHandle(g_process);
    return 0;
}
