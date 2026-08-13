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

bool freefps_hook_install(std::wstring& error);
void freefps_hook_uninstall();
bool freefps_hook_begin_request(const FreeFpsRequestContext& request,
                                std::wstring& error);
int freefps_hook_end_request(std::uint64_t generation);

