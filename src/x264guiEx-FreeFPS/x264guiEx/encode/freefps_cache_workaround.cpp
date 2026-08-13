#include "freefps_cache_workaround.h"

#include <cmath>

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

