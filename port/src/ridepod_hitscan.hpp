#pragma once
#include <algorithm>
#include <cmath>

// First intersection of a finite muzzle-to-end segment and a hurt sphere.
inline bool RidepodSphereEntry(const float origin[3], const float delta[3],
                              const float center[3], float radius, float &entry) {
    float length2 = 0, b = 0, c = -radius * radius;
    for (int j = 0; j < 3; ++j) {
        float q = origin[j] - center[j];
        length2 += delta[j] * delta[j]; b += q * delta[j]; c += q * q;
    }
    if (length2 <= 1e-6f) return false;
    float disc = b * b - length2 * c;
    if (disc < 0) return false;
    float root = std::sqrt(disc);
    float first = (-b - root) / length2;
    float last = (-b + root) / length2;
    if (last < 0 || first > 1) return false;
    entry = std::max(0.f, first);
    return true;
}
