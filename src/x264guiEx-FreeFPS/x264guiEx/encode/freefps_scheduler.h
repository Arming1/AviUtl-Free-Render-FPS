#pragma once

#include <cstdint>
#include <string>

#include "auo.h"
#include "freefps_cache_workaround.h"

struct FreeFpsSettings {
    bool enabled;
    int target_rate;
    int target_scale;
};

class FreeFpsSession {
public:
    FreeFpsSession();
    ~FreeFpsSession();

    FreeFpsSession(const FreeFpsSession&) = delete;
    FreeFpsSession& operator=(const FreeFpsSession&) = delete;

    bool initialize(const OUTPUT_INFO* project,
                    const FreeFpsSettings& settings,
                    std::wstring& error);
    bool enabled() const;
    OUTPUT_INFO* effective_output_info();
    int project_frames() const;
    int output_frames() const;
    double coordinate_for(std::int64_t output_index) const;
    int public_frame_for(std::int64_t output_index) const;
    void* request_video(std::int64_t output_index, DWORD format,
                        std::wstring& error);

    static FreeFpsSession* current();

private:
    bool request_at(int public_frame, double coordinate,
                    std::int64_t output_index, DWORD format,
                    void*& frame, std::wstring& error);

    bool enabled_;
    bool registered_;
    OUTPUT_INFO effective_;
    const OUTPUT_INFO* project_;
    int project_frames_;
    int project_rate_;
    int project_scale_;
    int target_rate_;
    int target_scale_;
    int output_frames_;
    std::uint64_t coordinate_numerator_factor_;
    std::uint64_t coordinate_denominator_;
    std::uint64_t generation_;
    FreeFpsCacheWorkaround cache_;
};
