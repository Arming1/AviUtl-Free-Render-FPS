#include "freefps_scheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "freefps_aviutl_hook.h"

namespace {

SRWLOCK g_session_lock = SRWLOCK_INIT;
FreeFpsSession* g_session = nullptr;

bool checked_multiply(std::uint64_t a, std::uint64_t b,
                      std::uint64_t& result) {
    if (a != 0 && b > (std::numeric_limits<std::uint64_t>::max)() / a) {
        return false;
    }
    result = a * b;
    return true;
}

void cancel_factor(std::uint64_t& numerator, std::uint64_t& denominator) {
    const std::uint64_t divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
}

bool build_ratio(std::uint64_t numerator_a, std::uint64_t numerator_b,
                 std::uint64_t denominator_a, std::uint64_t denominator_b,
                 std::uint64_t& numerator, std::uint64_t& denominator) {
    cancel_factor(numerator_a, denominator_a);
    cancel_factor(numerator_a, denominator_b);
    cancel_factor(numerator_b, denominator_a);
    cancel_factor(numerator_b, denominator_b);
    return checked_multiply(numerator_a, numerator_b, numerator) &&
           checked_multiply(denominator_a, denominator_b, denominator) &&
           denominator != 0;
}

} // namespace

FreeFpsSession::FreeFpsSession()
    : enabled_(false),
      registered_(false),
      effective_{},
      project_(nullptr),
      project_frames_(0),
      project_rate_(0),
      project_scale_(0),
      target_rate_(0),
      target_scale_(0),
      output_frames_(0),
      coordinate_numerator_factor_(0),
      coordinate_denominator_(1),
      generation_(0),
      cache_(0) {
}

FreeFpsSession::~FreeFpsSession() {
    if (registered_) {
        AcquireSRWLockExclusive(&g_session_lock);
        if (g_session == this) {
            g_session = nullptr;
        }
        ReleaseSRWLockExclusive(&g_session_lock);
    }
    if (enabled_) {
        freefps_hook_uninstall();
    }
}

bool FreeFpsSession::initialize(const OUTPUT_INFO* project,
                                const FreeFpsSettings& settings,
                                std::wstring& error) {
    if (project == nullptr) {
        error = L"FreeFPS: OUTPUT_INFO is null.";
        return false;
    }
    project_ = project;
    effective_ = *project;
    project_frames_ = project->n;
    project_rate_ = project->rate;
    project_scale_ = project->scale;
    target_rate_ = settings.target_rate;
    target_scale_ = settings.target_scale;
    output_frames_ = project_frames_;
    cache_ = FreeFpsCacheWorkaround(project_frames_);

    if (!settings.enabled) {
        return true;
    }
    if (project_frames_ <= 0 || project_rate_ <= 0 || project_scale_ <= 0 ||
        target_rate_ <= 0 || target_scale_ <= 0) {
        error = L"FreeFPS: invalid project or target rational frame rate.";
        return false;
    }

    std::uint64_t duration_numerator = 0;
    std::uint64_t duration_denominator = 0;
    if (!build_ratio(static_cast<std::uint64_t>(project_frames_),
                     static_cast<std::uint64_t>(project_scale_) *
                         static_cast<std::uint64_t>(target_rate_),
                     static_cast<std::uint64_t>(project_rate_),
                     static_cast<std::uint64_t>(target_scale_),
                     duration_numerator, duration_denominator)) {
        error = L"FreeFPS: output frame-count rational overflow.";
        return false;
    }
    const std::uint64_t count = duration_numerator / duration_denominator +
        (duration_numerator % duration_denominator != 0 ? 1 : 0);
    if (count == 0 || count > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) {
        error = L"FreeFPS: derived output frame count is out of range.";
        return false;
    }

    if (!build_ratio(static_cast<std::uint64_t>(project_rate_),
                     static_cast<std::uint64_t>(target_scale_),
                     static_cast<std::uint64_t>(target_rate_),
                     static_cast<std::uint64_t>(project_scale_),
                     coordinate_numerator_factor_, coordinate_denominator_)) {
        error = L"FreeFPS: coordinate rational overflow.";
        return false;
    }

    if (!freefps_hook_install(error)) {
        return false;
    }
    enabled_ = true;
    output_frames_ = static_cast<int>(count);
    effective_.rate = target_rate_;
    effective_.scale = target_scale_;
    effective_.n = output_frames_;
    if (effective_.func_set_buffer_size != nullptr) {
        effective_.func_set_buffer_size(1, 0);
    }

    AcquireSRWLockExclusive(&g_session_lock);
    if (g_session != nullptr) {
        ReleaseSRWLockExclusive(&g_session_lock);
        error = L"FreeFPS: another render session is already active.";
        freefps_hook_uninstall();
        enabled_ = false;
        return false;
    }
    g_session = this;
    registered_ = true;
    ReleaseSRWLockExclusive(&g_session_lock);
    return true;
}

bool FreeFpsSession::enabled() const {
    return enabled_;
}

OUTPUT_INFO* FreeFpsSession::effective_output_info() {
    return enabled_ ? &effective_ : const_cast<OUTPUT_INFO*>(project_);
}

int FreeFpsSession::project_frames() const {
    return project_frames_;
}

int FreeFpsSession::output_frames() const {
    return output_frames_;
}

double FreeFpsSession::coordinate_for(std::int64_t output_index) const {
    if (!enabled_) {
        return static_cast<double>(output_index);
    }
    return static_cast<double>(output_index) *
           static_cast<double>(coordinate_numerator_factor_) /
           static_cast<double>(coordinate_denominator_);
}

int FreeFpsSession::public_frame_for(std::int64_t output_index) const {
    const double coordinate = coordinate_for(output_index);
    const int frame = static_cast<int>(std::floor(coordinate));
    return (std::max)(0, (std::min)(project_frames_ - 1, frame));
}

bool FreeFpsSession::request_at(int public_frame, double coordinate,
                                std::int64_t output_index, DWORD format,
                                void*& frame, std::wstring& error) {
    FreeFpsRequestContext request{};
    request.generation = ++generation_;
    request.output_sample_index = output_index;
    request.requested_integer_frame = public_frame;
    request.target_coordinate = coordinate;
    request.active = true;
    if (!freefps_hook_begin_request(request, error)) {
        return false;
    }
    frame = effective_.func_get_video_ex(public_frame, format);
    const int builder_hits = freefps_hook_end_request(request.generation);
    if (frame == nullptr) {
        error = L"FreeFPS: AviUtl2 returned a null video frame.";
        return false;
    }
    if (builder_hits <= 0) {
        error = L"FreeFPS: timeline builder was bypassed (cache collision).";
        return false;
    }
    return true;
}

void* FreeFpsSession::request_video(std::int64_t output_index, DWORD format,
                                    std::wstring& error) {
    if (!enabled_) {
        return project_->func_get_video_ex(static_cast<int>(output_index), format);
    }
    const double coordinate = coordinate_for(output_index);
    const int public_frame = public_frame_for(output_index);
    if (cache_.needs_eviction(public_frame, coordinate)) {
        const int neighbor = cache_.eviction_frame(public_frame);
        if (neighbor < 0) {
            error = L"FreeFPS: no neighbor frame is available for cache eviction.";
            return nullptr;
        }
        void* ignored = nullptr;
        if (!request_at(neighbor, static_cast<double>(neighbor), -1, format,
                        ignored, error)) {
            return nullptr;
        }
    }
    void* frame = nullptr;
    if (!request_at(public_frame, coordinate, output_index, format, frame, error)) {
        return nullptr;
    }
    cache_.record_target(public_frame, coordinate);
    return frame;
}

FreeFpsSession* FreeFpsSession::current() {
    AcquireSRWLockShared(&g_session_lock);
    FreeFpsSession* result = g_session;
    ReleaseSRWLockShared(&g_session_lock);
    return result;
}
