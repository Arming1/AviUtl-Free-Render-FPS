#include "freefps_cache_workaround.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr std::uint32_t make_fourcc(char c0, char c1, char c2, char c3) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(c0)) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c1)) << 8) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c2)) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c3)) << 24);
}

// Extra zeroed bytes past the logical frame end; the encoder's SIMD
// converters may read slightly beyond the last pixel.
constexpr size_t kStashSlackBytes = 4096;

} // namespace

size_t freefps_host_frame_bytes(std::uint32_t format, int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const std::uint64_t w = static_cast<std::uint64_t>(width);
    const std::uint64_t h = static_cast<std::uint64_t>(height);
    std::uint64_t row_bytes = 0;
    if (format == 0) {
        row_bytes = (w * 3 + 3) & ~static_cast<std::uint64_t>(3);
    } else if (format == make_fourcc('Y', 'U', 'Y', '2')) {
        if (width % 2 != 0) {
            return 0;
        }
        row_bytes = w * 2;
    } else if (format == make_fourcc('Y', 'C', '4', '8') ||
               format == make_fourcc('L', 'W', '4', '8')) {
        row_bytes = w * 6;
    } else if (format == make_fourcc('P', 'A', '6', '4') ||
               format == make_fourcc('H', 'F', '6', '4')) {
        row_bytes = w * 8;
    } else {
        return 0;
    }
    const std::uint64_t max_bytes =
        static_cast<std::uint64_t>((std::numeric_limits<size_t>::max)());
    if (row_bytes > max_bytes / h) {
        return 0;
    }
    return static_cast<size_t>(row_bytes * h);
}

void FreeFpsFrameStash::store(int frame, double coordinate, const void* data,
                              size_t bytes) {
    if (data == nullptr || bytes == 0) {
        clear();
        return;
    }
    data_.resize(bytes + kStashSlackBytes);
    const auto* source = static_cast<const std::uint8_t*>(data);
    std::copy_n(source, bytes, data_.begin());
    std::fill_n(data_.begin() + static_cast<std::ptrdiff_t>(bytes),
                kStashSlackBytes, static_cast<std::uint8_t>(0));
    valid_ = true;
    frame_ = frame;
    coordinate_ = coordinate;
}

const void* FreeFpsFrameStash::take(int frame, double coordinate) {
    if (!valid_ || frame != frame_ ||
        std::fabs(coordinate - coordinate_) > 1.0e-12) {
        return nullptr;
    }
    valid_ = false;
    return data_.data();
}

void FreeFpsFrameStash::clear() {
    valid_ = false;
    frame_ = -1;
}

FreeFpsCacheWorkaround::FreeFpsCacheWorkaround(int project_frames)
    : project_frames_(project_frames),
      have_last_target_(false),
      last_frame_(-1),
      last_coordinate_(0.0) {
}

bool FreeFpsCacheWorkaround::needs_eviction(int requested_frame,
                                            double coordinate) const {
    return have_last_target_ && last_frame_ == requested_frame &&
           std::fabs(last_coordinate_ - coordinate) > 1.0e-12;
}

int FreeFpsCacheWorkaround::eviction_frame(int requested_frame) const {
    if (project_frames_ <= 1) {
        return -1;
    }
    return requested_frame + 1 < project_frames_
        ? requested_frame + 1
        : requested_frame - 1;
}

void FreeFpsCacheWorkaround::record_target(int requested_frame,
                                           double coordinate) {
    have_last_target_ = true;
    last_frame_ = requested_frame;
    last_coordinate_ = coordinate;
}

