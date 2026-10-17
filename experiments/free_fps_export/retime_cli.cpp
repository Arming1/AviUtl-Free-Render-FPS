// Command line wrapper around the pure re-timer, for tests/crosscheck.py.
// Mirrors the Python tool's arguments and refusals:
//   retime_cli.exe --in IN --out OUT (--factor K | --target-fps RATE) [--force] [--precise [--precise-extra-decimals N]]
// Exit codes: 0 converted, 2 refused or usage error. Writes OUT and OUT.convert.json.

#include <windows.h>
#include <wchar.h>

#include <string>

#include "retime.h"

namespace {

using free_fps::RetimeResult;

void say(const std::string& text) {
    const std::string line = text + "\n";
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

std::wstring full_path(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        return path;
    }
    std::wstring out(needed, L'\0');
    const DWORD length = GetFullPathNameW(path.c_str(), needed, &out[0], nullptr);
    if (length == 0 || length >= needed) {
        return path;
    }
    out.resize(length);
    return out;
}

bool exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool read_file(const std::wstring& path, std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart < (1LL << 31);
    bytes.assign(ok ? static_cast<std::size_t>(size.QuadPart) : 0, '\0');
    DWORD read = 0;
    ok = ok && (bytes.empty() || (ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) &&
                                  read == bytes.size()));
    CloseHandle(file);
    return ok;
}

bool write_file(const std::wstring& path, const std::string& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = bytes.empty() || (WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
                                                nullptr) && written == bytes.size());
    CloseHandle(file);
    return ok == TRUE;
}

int usage() {
    say("usage: retime_cli --in IN --out OUT (--factor K | --target-fps RATE) [--force] [--precise "
        "[--precise-extra-decimals N]]");
    return 2;
}

bool parse_int(const std::string& text, long long& value) {
    if (text.empty() || text.size() > 18) {
        return false;
    }
    std::size_t i = text[0] == '-' ? 1 : 0;
    if (i == text.size()) {
        return false;
    }
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    value = std::stoll(text);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring in_arg;
    std::wstring out_arg;
    std::wstring rate_arg;
    bool have_factor = false;
    bool have_target = false;
    bool force = false;
    bool precise = false;
    long long extra_decimals = 0;
    long long factor = 0;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--force") {
            force = true;
        } else if (arg == L"--precise") {
            precise = true;
        } else if (i + 1 < argc && arg == L"--precise-extra-decimals") {
            std::string text;
            free_fps::wide_to_utf8(argv[++i]).swap(text);
            if (!parse_int(text, extra_decimals)) {
                return usage();
            }
        } else if (i + 1 < argc && arg == L"--in") {
            in_arg = argv[++i];
        } else if (i + 1 < argc && arg == L"--out") {
            out_arg = argv[++i];
        } else if (i + 1 < argc && arg == L"--target-fps") {
            rate_arg = argv[++i];
            have_target = true;
        } else if (i + 1 < argc && arg == L"--factor") {
            std::string text;
            free_fps::wide_to_utf8(argv[++i]).swap(text);
            if (!parse_int(text, factor)) {
                return usage();
            }
            have_factor = true;
        } else {
            return usage();
        }
    }
    if (in_arg.empty() || out_arg.empty() || have_factor == have_target) {
        return usage();
    }

    free_fps::Rational target;
    if (have_factor) {
        if (factor < 2) {
            say("REFUSED: --factor must be an integer >= 2, got " + std::to_string(factor));
            return 2;
        }
    } else {
        std::string error;
        if (!free_fps::parse_target_rate(free_fps::wide_to_utf8(rate_arg), target, error)) {
            say("REFUSED: --target-fps " + error);
            return 2;
        }
    }
    if (extra_decimals < 0 || extra_decimals > 4) {
        say("REFUSED: --precise-extra-decimals must be an integer from 0 to 4, got " +
            std::to_string(extra_decimals));
        return 2;
    }
    if (extra_decimals > 0 && !precise) {
        say("REFUSED: --precise-extra-decimals needs --precise");
        return 2;
    }

    const std::wstring in_path = full_path(in_arg);
    const std::wstring out_path = full_path(out_arg);
    const std::wstring json_path = out_path + L".convert.json";
    if (!exists(in_path)) {
        say("REFUSED: input not found: " + free_fps::wide_to_utf8(in_path));
        return 2;
    }
    if (_wcsicmp(in_path.c_str(), out_path.c_str()) == 0) {
        say("REFUSED: --out must differ from --in");
        return 2;
    }
    if (!force) {
        for (const std::wstring& path : {out_path, json_path}) {
            if (exists(path)) {
                say("REFUSED: " + free_fps::wide_to_utf8(path) + " exists (use --force to overwrite)");
                return 2;
            }
        }
    }
    const std::wstring out_dir = out_path.substr(0, out_path.find_last_of(L'\\'));
    if (!exists(out_dir)) {
        say("REFUSED: output directory does not exist: " + free_fps::wide_to_utf8(out_dir));
        return 2;
    }

    std::string in_bytes;
    if (!read_file(in_path, in_bytes)) {
        say("REFUSED: cannot read input: " + free_fps::wide_to_utf8(in_path));
        return 2;
    }

    const std::size_t extra = static_cast<std::size_t>(extra_decimals);
    RetimeResult result = have_factor ? free_fps::retime_factor(in_bytes, factor, out_path, precise, extra)
                                      : free_fps::retime(in_bytes, target, out_path, precise, extra);
    if (!result.ok) {
        say(free_fps::refusal_message(result));
        return 2;
    }
    if (!write_file(out_path, result.output) ||
        !write_file(json_path, free_fps::sidecar_json(result, in_path, out_path))) {
        say("REFUSED: cannot write output files");
        return 2;
    }
    return 0;
}
