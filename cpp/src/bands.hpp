#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

// Log-spaced band edges over rFFT bins 1 .. n_fft/2 (skip DC), mirroring Python _log_band_slices.

inline std::vector<std::pair<int, int>> log_band_slices(int n_fft, int n_bands) {
    const int hi = n_fft / 2;
    if (hi < 2) {
        return {{1, hi + 1}};
    }
    std::vector<int> edges(static_cast<std::size_t>(n_bands) + 1);
    for (int i = 0; i <= n_bands; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(n_bands);
        double v = std::pow(static_cast<double>(hi), t);
        int e = static_cast<int>(std::llround(v));
        e = std::clamp(e, 1, hi);
        edges[static_cast<std::size_t>(i)] = e;
    }
    // Unique sorted (numpy.unique on rounded geomspace)
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::vector<std::pair<int, int>> out;
    for (std::size_t i = 0; i + 1 < edges.size(); ++i) {
        int lo_b = edges[i];
        int hi_b = edges[i + 1];
        if (hi_b <= lo_b) {
            continue;
        }
        out.emplace_back(lo_b, hi_b + 1);
    }
    if (out.empty()) {
        out.emplace_back(1, hi + 1);
    }
    return out;
}
