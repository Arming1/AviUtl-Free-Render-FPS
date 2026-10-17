#pragma once

#include <cstdint>
#include <string>

struct FreeFpsRequestContext {
    std::uint64_t generation;
    std::int64_t output_sample_index;
    int requested_integer_frame;
    double target_coordinate;
    bool active;
};

// Read-only check that the running host is one this hook build can patch.
// Does not install or modify anything; safe to call before a session starts.
bool freefps_host_supported();
bool freefps_hook_install(std::wstring& error);
void freefps_hook_uninstall();
bool freefps_hook_begin_request(const FreeFpsRequestContext& request,
                                std::wstring& error);
int freefps_hook_end_request(std::uint64_t generation);
