#include "fake_host.h"

#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <utility>
#include <vector>

#include "freefps_aviutl_hook.h"

int fake_renders = 0;
int fake_install_calls = 0;
int fake_uninstall_calls = 0;
bool fake_installed = false;

namespace {

int g_cache_depth = 1;
int g_width = 64;
int g_height = 36;
bool g_install_fails = false;

// Insertion order of keys currently in the cache (oldest at front).
std::deque<int> g_cache_order;
std::map<int, std::vector<unsigned char>> g_cache;

bool g_request_active = false;
FreeFpsRequestContext g_request{};
int g_request_hits = 0;

unsigned char fill_byte(std::size_t index, double coordinate) {
    const int value = static_cast<int>(index) * 31 +
                      static_cast<int>(coordinate * 1000.0);
    return static_cast<unsigned char>(value);
}

} // namespace

void fake_reset(int cache_depth, int width, int height) {
    g_cache_depth = cache_depth < 1 ? 1 : cache_depth;
    g_width = width;
    g_height = height;
    g_install_fails = false;
    g_cache_order.clear();
    g_cache.clear();
    g_request_active = false;
    g_request = FreeFpsRequestContext{};
    g_request_hits = 0;
    fake_renders = 0;
    fake_install_calls = 0;
    fake_uninstall_calls = 0;
    fake_installed = false;
}

void fake_set_install_fails(bool fails) {
    g_install_fails = fails;
}

std::size_t fake_frame_bytes(DWORD format, int width, int height) {
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    switch (format) {
    case 0: // RGB24 DIB: rows padded to 4 bytes
        return static_cast<std::size_t>(((width * 3 + 3) & ~3) * height);
    case fake_fourcc('Y', 'U', 'Y', '2'):
        return w * 2 * h;
    case fake_fourcc('Y', 'C', '4', '8'):
    case fake_fourcc('L', 'W', '4', '8'):
        return w * 6 * h;
    case fake_fourcc('P', 'A', '6', '4'):
    case fake_fourcc('H', 'F', '6', '4'):
        return w * 8 * h;
    default:
        return 0;
    }
}

void* fake_get_video(int frame, DWORD format) {
    // Cache hit: the builder does not run, so the stored buffer is returned.
    auto cached = g_cache.find(frame);
    if (cached != g_cache.end()) {
        return cached->second.data();
    }

    const std::size_t bytes = fake_frame_bytes(format, g_width, g_height);
    if (bytes < 8) {
        return nullptr;
    }

    // Builder run: evaluate at the request's coordinate when it targets this frame.
    double coordinate = static_cast<double>(frame);
    if (g_request_active && frame == g_request.requested_integer_frame) {
        coordinate = g_request.target_coordinate;
        ++g_request_hits;
    }
    ++fake_renders;

    while (!g_cache_order.empty() &&
           static_cast<int>(g_cache_order.size()) >= g_cache_depth) {
        g_cache.erase(g_cache_order.front());
        g_cache_order.pop_front();
    }

    std::vector<unsigned char> buffer(bytes);
    std::memcpy(buffer.data(), &coordinate, sizeof(coordinate));
    for (std::size_t i = 8; i < bytes; ++i) {
        buffer[i] = fill_byte(i, coordinate);
    }
    g_cache_order.push_back(frame);
    std::vector<unsigned char>& slot = g_cache[frame];
    slot = std::move(buffer);
    return slot.data();
}

double fake_read_coordinate(const void* p) {
    double coordinate = 0.0;
    std::memcpy(&coordinate, p, sizeof(coordinate));
    return coordinate;
}

bool fake_verify_frame(const void* p, std::size_t bytes, double expected_coordinate) {
    if (p == nullptr || bytes < 8) {
        return false;
    }
    const unsigned char* b = static_cast<const unsigned char*>(p);
    const double stored = fake_read_coordinate(b);
    if (!(std::fabs(stored - expected_coordinate) <= 1e-9)) {
        return false;
    }
    for (std::size_t i = 8; i < bytes; ++i) {
        if (b[i] != fill_byte(i, stored)) {
            return false;
        }
    }
    return true;
}

// ---- Fake implementation of freefps_aviutl_hook.h ----

bool freefps_hook_install(std::wstring& error) {
    ++fake_install_calls;
    if (g_install_fails) {
        error = L"fake install failure";
        return false;
    }
    fake_installed = true;
    return true;
}

void freefps_hook_uninstall() {
    ++fake_uninstall_calls;
    fake_installed = false;
    g_request_active = false;
    g_request = FreeFpsRequestContext{};
    g_request_hits = 0;
}

bool freefps_hook_begin_request(const FreeFpsRequestContext& request,
                                std::wstring& error) {
    if (!fake_installed) {
        error = L"fake: hook is not installed";
        return false;
    }
    if (g_request_active) {
        error = L"fake: a request is already active";
        return false;
    }
    g_request = request;
    g_request.active = true;
    g_request_active = true;
    g_request_hits = 0;
    return true;
}

int freefps_hook_end_request(std::uint64_t generation) {
    const int hits = (g_request_active && g_request.generation == generation)
        ? g_request_hits
        : -1;
    g_request_active = false;
    g_request = FreeFpsRequestContext{};
    g_request_hits = 0;
    return hits;
}
