// Host-free unit tests for FreeFpsSession (freefps_scheduler.cpp).
// The AviUtl host is replaced by fake_host.cpp. No framework: one PASS/FAIL line
// per check, non-zero exit status if any check fails.

#include <cstdio>
#include <string>

#include "freefps_scheduler.h"
#include "fake_host.h"

namespace {

int g_failures = 0;

const DWORD kFormatPA64 = fake_fourcc('P', 'A', '6', '4');
const DWORD kFormatRgb = 0;

std::string narrow(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) {
        out.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    return out;
}

bool contains(const std::wstring& haystack, const wchar_t* needle) {
    return haystack.find(needle) != std::wstring::npos;
}

void report(bool ok, const std::string& name, const std::string& detail = std::string()) {
    std::printf("%s %s", ok ? "PASS" : "FAIL", name.c_str());
    if (!detail.empty()) {
        std::printf(" :: %s", detail.c_str());
    }
    std::printf("\n");
    if (!ok) {
        ++g_failures;
    }
}

void noop_set_buffer_size(int, int) {
}

OUTPUT_INFO make_project(int rate, int scale, int frames, int width, int height) {
    OUTPUT_INFO info{};
    info.w = width;
    info.h = height;
    info.rate = rate;
    info.scale = scale;
    info.n = frames;
    info.func_get_video_ex = &fake_get_video;
#if AVIUTL_TARGET_VER == 2
    info.func_set_buffer_size = &noop_set_buffer_size;
#endif
    return info;
}

FreeFpsSettings enabled_target(int rate, int scale) {
    FreeFpsSettings settings{};
    settings.enabled = true;
    settings.target_rate = rate;
    settings.target_scale = scale;
    return settings;
}

// Expected coordinate of output `index`: index * Pr * Ts / (Tr * Ps).
double expected_coordinate(std::int64_t index, int project_rate, int project_scale,
                           int target_rate, int target_scale) {
    const long double value = static_cast<long double>(index) * project_rate *
                              target_scale /
                              (static_cast<long double>(target_rate) * project_scale);
    return static_cast<double>(value);
}

struct RateCase {
    int project_rate;
    int project_scale;
    int project_frames;
    int target_rate;
    int target_scale;
    int expected_outputs;
    // Host renders allowed for the whole export. Outputs that land exactly on
    // an integer coordinate rendered by an earlier cache eviction reuse that
    // render, so integer-ratio cases cost one render per output.
    int max_renders;
};

std::string rate_text(int rate, int scale) {
    return std::to_string(rate) + "/" + std::to_string(scale);
}

void check_rate_matrix(const RateCase& c, DWORD format, const char* format_label,
                       int width, int height) {
    const std::string name = std::string("rate_matrix ") + format_label + " w" +
                             std::to_string(width) + " " +
                             rate_text(c.project_rate, c.project_scale) + " f" +
                             std::to_string(c.project_frames) + " -> " +
                             rate_text(c.target_rate, c.target_scale);

    fake_reset(1, width, height);
    OUTPUT_INFO project = make_project(c.project_rate, c.project_scale,
                                       c.project_frames, width, height);
    FreeFpsSession session;
    std::wstring error;
    if (!session.initialize(&project,
                            enabled_target(c.target_rate, c.target_scale), error)) {
        report(false, name, "initialize failed: " + narrow(error));
        return;
    }
    const int outputs = session.output_frames();
    if (outputs != c.expected_outputs) {
        report(false, name, "output_frames=" + std::to_string(outputs) +
                                " expected " + std::to_string(c.expected_outputs));
        return;
    }

    const std::size_t bytes = fake_frame_bytes(format, width, height);
    std::string detail;
    bool ok = true;
    for (int i = 0; i < outputs && ok; ++i) {
        std::wstring frame_error;
        void* frame = session.request_video(i, format, frame_error);
        if (frame == nullptr) {
            ok = false;
            detail = "output " + std::to_string(i) + ": null frame: " + narrow(frame_error);
            break;
        }
        const double expected = expected_coordinate(i, c.project_rate, c.project_scale,
                                                    c.target_rate, c.target_scale);
        if (!fake_verify_frame(frame, bytes, expected)) {
            ok = false;
            detail = "output " + std::to_string(i) + ": content mismatch, got coordinate " +
                     std::to_string(fake_read_coordinate(frame)) + " expected " +
                     std::to_string(expected);
        }
    }

    const double ratio = outputs > 0 ? static_cast<double>(fake_renders) / outputs : 0.0;
    char stats[160];
    std::snprintf(stats, sizeof(stats), "outputs=%d renders=%d ratio=%.3f", outputs,
                  fake_renders, ratio);
    if (ok && fake_renders > c.max_renders) {
        ok = false;
        detail = "render budget exceeded, max " + std::to_string(c.max_renders);
    }
    report(ok, name, ok ? std::string(stats) : detail + " (" + stats + ")");
}

void check_disabled_mode() {
    fake_reset(1);
    OUTPUT_INFO project = make_project(30, 1, 150, 64, 36);
    FreeFpsSettings settings{};
    settings.enabled = false;
    settings.target_rate = 60;
    settings.target_scale = 1;

    FreeFpsSession session;
    std::wstring error;
    const bool init_ok = session.initialize(&project, settings, error);
    report(init_ok && !session.enabled() && session.effective_output_info() == &project,
           "disabled: initialize ok, enabled()==false, effective_output_info is project",
           narrow(error));

    bool frames_ok = true;
    std::string detail;
    const std::size_t bytes = fake_frame_bytes(kFormatPA64, 64, 36);
    for (int i = 0; i < project.n && frames_ok; ++i) {
        std::wstring frame_error;
        void* frame = session.request_video(i, kFormatPA64, frame_error);
        if (!fake_verify_frame(frame, bytes, static_cast<double>(i))) {
            frames_ok = false;
            detail = "frame " + std::to_string(i) + " is not integer coordinate " +
                     std::to_string(i) + " " + narrow(frame_error);
        }
    }
    report(frames_ok, "disabled: request_video(i) returns integer frame i", detail);

    report(fake_install_calls == 0, "disabled: hook install never called",
           "install_calls=" + std::to_string(fake_install_calls));
}

void check_second_session_rejected() {
    fake_reset(1);
    OUTPUT_INFO project = make_project(30, 1, 150, 64, 36);
    const std::size_t bytes = fake_frame_bytes(kFormatPA64, 64, 36);
    std::wstring error;

    {
        FreeFpsSession first;
        const bool first_ok = first.initialize(&project, enabled_target(60, 1), error);
        report(first_ok, "second session: first session initializes", narrow(error));
        if (first_ok) {
            const int uninstall_before = fake_uninstall_calls;
            {
                FreeFpsSession second;
                std::wstring second_error;
                const bool second_ok =
                    second.initialize(&project, enabled_target(60, 1), second_error);
                report(!second_ok && contains(second_error, L"another render session"),
                       "second session: rejected with 'another render session'",
                       narrow(second_error));
                report(fake_installed, "second session: rejection leaves hook installed");
                report(fake_uninstall_calls == uninstall_before,
                       "second session: rejection does not uninstall the hook",
                       "uninstall_calls before=" + std::to_string(uninstall_before) +
                           " after=" + std::to_string(fake_uninstall_calls));
            }

            std::wstring frame_error;
            void* frame = first.request_video(0, kFormatPA64, frame_error);
            report(frame != nullptr && fake_verify_frame(frame, bytes, 0.0),
                   "second session: first session still renders frame 0 correctly",
                   narrow(frame_error));
        }
    }
    report(!fake_installed, "second session: hook uninstalled after first session destroyed");
}

void check_install_failure() {
    fake_reset(1);
    fake_set_install_fails(true);
    OUTPUT_INFO project = make_project(30, 1, 150, 64, 36);
    std::wstring error;
    {
        FreeFpsSession session;
        const bool ok = session.initialize(&project, enabled_target(60, 1), error);
        report(!ok && !error.empty(), "install failure: initialize returns false with error",
               narrow(error));
        report(FreeFpsSession::current() == nullptr,
               "install failure: FreeFpsSession::current() is nullptr");
    }
    fake_set_install_fails(false);
}

void check_fail_closed_on_cache_collision() {
    fake_reset(2);
    OUTPUT_INFO project = make_project(30, 1, 150, 64, 36);
    const std::size_t bytes = fake_frame_bytes(kFormatPA64, 64, 36);
    FreeFpsSession session;
    std::wstring error;
    if (!session.initialize(&project, enabled_target(60, 1), error)) {
        report(false, "fail closed (cache depth 2): initialize", narrow(error));
        return;
    }

    int closed = 0;
    int stale = 0;
    int unexpected = 0;
    std::string first_problem;
    for (int i = 0; i < session.output_frames(); ++i) {
        std::wstring frame_error;
        void* frame = session.request_video(i, kFormatPA64, frame_error);
        if (frame == nullptr) {
            if (contains(frame_error, L"cache collision") || contains(frame_error, L"bypassed")) {
                ++closed;
            } else {
                ++unexpected;
                if (first_problem.empty()) {
                    first_problem = "output " + std::to_string(i) + " unexpected error: " +
                                    narrow(frame_error);
                }
            }
            continue;
        }
        const double expected = expected_coordinate(i, 30, 1, 60, 1);
        if (!fake_verify_frame(frame, bytes, expected)) {
            ++stale;
            if (first_problem.empty()) {
                first_problem = "output " + std::to_string(i) + " returned frame with coordinate " +
                                std::to_string(fake_read_coordinate(frame)) + " expected " +
                                std::to_string(expected);
            }
        }
    }
    const std::string stats = "fail_closed=" + std::to_string(closed) +
                              " stale_frames=" + std::to_string(stale) +
                              " unexpected_errors=" + std::to_string(unexpected);
    report(stale == 0 && unexpected == 0 && closed > 0,
           "fail closed (cache depth 2): never returns a stale frame, returns null on collision",
           first_problem.empty() ? stats : first_problem + " (" + stats + ")");
}

void check_one_frame_project() {
    fake_reset(1);
    OUTPUT_INFO project = make_project(30, 1, 1, 64, 36);
    const std::size_t bytes = fake_frame_bytes(kFormatPA64, 64, 36);
    FreeFpsSession session;
    std::wstring error;
    const bool init_ok = session.initialize(&project, enabled_target(60, 1), error);
    report(init_ok && session.output_frames() == 2, "one-frame project: 30/1 n=1 -> 2 outputs",
           "outputs=" + std::to_string(session.output_frames()) + " " + narrow(error));
    if (!init_ok) {
        return;
    }

    std::wstring frame_error;
    void* frame0 = session.request_video(0, kFormatPA64, frame_error);
    report(frame0 != nullptr && fake_verify_frame(frame0, bytes, 0.0),
           "one-frame project: output 0 renders frame 0 at coordinate 0", narrow(frame_error));

    std::wstring frame1_error;
    void* frame1 = session.request_video(1, kFormatPA64, frame1_error);
    report(frame1 == nullptr && contains(frame1_error, L"neighbor"),
           "one-frame project: output 1 fails with 'neighbor' error (documented limitation)",
           narrow(frame1_error));
}

} // namespace

int main() {
    const RateCase matrix[] = {
        {30, 1, 150, 60, 1, 300, 300},
        {66, 1, 330, 60, 1, 300, 300},
        {66, 1, 330, 60000, 1001, 300, 300},
        {66, 1, 330, 24, 1, 120, 120},
        {66, 1, 330, 120, 1, 600, 870},
        {120, 1, 600, 60, 1, 300, 300},
        {30, 1, 150, 120, 1, 600, 752},
    };
    for (const RateCase& c : matrix) {
        check_rate_matrix(c, kFormatPA64, "PA64", 64, 36);
    }
    // Odd width exercises the RGB24 DIB row stride (w*3 padded to 4 bytes).
    const RateCase rgb_case = {30, 1, 150, 60, 1, 300, 300};
    check_rate_matrix(rgb_case, kFormatRgb, "RGB0", 63, 36);

    check_disabled_mode();
    check_second_session_rejected();
    check_install_failure();
    check_fail_closed_on_cache_collision();
    check_one_frame_project();

    std::printf("%s (%d failing check(s))\n", g_failures == 0 ? "ALL PASS" : "FAILED",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
