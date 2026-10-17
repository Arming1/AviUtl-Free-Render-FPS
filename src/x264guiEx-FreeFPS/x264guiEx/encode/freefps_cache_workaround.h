#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class FreeFpsCacheWorkaround {
public:
    explicit FreeFpsCacheWorkaround(int project_frames);

    bool needs_eviction(int requested_frame, double coordinate) const;
    int eviction_frame(int requested_frame) const;
    void record_target(int requested_frame, double coordinate);

private:
    int project_frames_;
    bool have_last_target_;
    int last_frame_;
    double last_coordinate_;
};

// Bytes of one video frame in the layout the host returns for `format`
// (0 = BI_RGB 24-bit DIB, or the FOURCCs YUY2/YC48/LW48/PA64/HF64), or 0 when
// the format is unknown, the size is not positive, the computation would
// overflow, or the layout is ambiguous. 0 disables frame reuse.
size_t freefps_host_frame_bytes(std::uint32_t format, int width, int height);

// One saved host render, reused at most once for the identical
// (integer frame, coordinate) request. The pointer returned by take() stays
// valid until the next store() or clear().
class FreeFpsFrameStash {
public:
    void store(int frame, double coordinate, const void* data, size_t bytes);
    const void* take(int frame, double coordinate);
    void clear();

private:
    std::vector<std::uint8_t> data_;
    bool valid_ = false;
    int frame_ = -1;
    double coordinate_ = 0.0;
};

