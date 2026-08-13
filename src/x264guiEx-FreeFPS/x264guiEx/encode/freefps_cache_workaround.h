#pragma once

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

