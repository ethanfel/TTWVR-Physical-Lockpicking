// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace locks {
constexpr float pi = 3.14159265358979323846f;
struct Vec {
  float x = 0, y = 0, z = 0;
  Vec operator+(Vec b) const { return {x + b.x, y + b.y, z + b.z}; }
  Vec operator-(Vec b) const { return {x - b.x, y - b.y, z - b.z}; }
  Vec operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline float dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec cross(Vec a, Vec b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec a) { return std::sqrt(dot(a, a)); }
inline Vec normal(Vec a) {
  float n = length(a);
  return n > 1e-8f ? a * (1 / n) : Vec{};
}
struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
};
inline Quat inverse(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }
inline Vec rotate(Quat q, Vec v) {
  Vec t = cross({q.x, q.y, q.z}, v) * 2;
  return v + t * q.w + cross({q.x, q.y, q.z}, t);
}
struct Pose {
  Quat q;
  Vec p;
};
inline Vec world(Pose p, Vec v) { return p.p + rotate(p.q, v); }
inline Vec local(Pose p, Vec v) { return rotate(inverse(p.q), v - p.p); }
inline bool finite(Vec p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline Pose placeLock(Pose head, float distance=.42f, float belowEyes=.42f) {
  // Only heading is captured. Looking down or rolling the head cannot tilt
  // the lock, alter its height or turn insertion into a gaze-dependent task.
  Vec back=rotate(head.q,{0,0,1});
  float yaw=std::hypot(back.x,back.z)>1e-5f ? std::atan2(back.x,back.z) : 0;
  Quat upright{0,std::sin(yaw/2),0,std::cos(yaw/2)};
  return {upright,head.p+Vec{0,-belowEyes,0}+rotate(upright,{0,0,-distance})};
}
inline float pickDegrees(Pose lock, Vec hand) {
  Vec v = local(lock, hand);
  return std::clamp(90.f + std::atan2(v.x, std::max(.015f, v.y)) * 180 / pi,
                    0.f, 180.f);
}
inline float rollDegrees(Pose lock, Quat hand) {
  Vec up = rotate(inverse(lock.q), rotate(hand, {0, 1, 0}));
  return std::atan2(up.x, up.y) * 180 / pi;
}
inline float angleDelta(float a, float b) {
  return std::remainder(a - b, 360.f);
}
inline std::array<float, 4> clip(Pose eye, Vec point, float left, float right,
                                 float down, float up) {
  Vec v = local(eye, point);
  float l = std::tan(left), r = std::tan(right), d = std::tan(down),
        u = std::tan(up), nearPlane = .01f, farPlane = 20.f;
  return {2 * v.x / (r - l) + (r + l) * v.z / (r - l),
          2 * v.y / (u - d) + (u + d) * v.z / (u - d),
          -farPlane * v.z / (farPlane - nearPlane) -
              farPlane * nearPlane / (farPlane - nearPlane),
          -v.z};
}
} // namespace locks
