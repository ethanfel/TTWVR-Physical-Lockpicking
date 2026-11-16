// SPDX-License-Identifier: GPL-3.0-only
#include "Math.h"
#include <cassert>
#include <cstdio>
using namespace locks;
bool close(float a, float b) { return std::abs(a - b) < 1e-4f; }
int main() {
  Pose origin;
  auto lock = placeLock(origin);
  assert(close(lock.p.z, -.42f) && close(lock.p.y, -.42f));
  for (float yaw : {0.f, .3f, 1.5f, 3.f}) {
    Pose p{{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)}, {2, 1, -3}};
    Vec v{.2f, -.3f, .4f};
    assert(length(local(p, world(p, v)) - v) < 1e-5f);
  }
  assert(close(pickDegrees(origin, {0, .1f, .1f}), 90));
  assert(close(pickDegrees(origin, {.1f, .1f, .1f}), 135));
  assert(close(pickDegrees(origin, {-.1f, .1f, .1f}), 45));
  assert(close(angleDelta(-179, 179), 2));
  auto p = clip(origin, {0, 0, -1}, -pi / 4, pi / 4, -pi / 4, pi / 4);
  assert(close(p[0], 0) && close(p[1], 0) && p[2] > 0 && p[2] < p[3]);
  auto n = clip(origin, {0, 0, -.01f}, -pi / 4, pi / 4, -pi / 4, pi / 4);
  assert(close(n[2], 0));
  Pose left{{}, {-.032f, 0, 0}}, right{{}, {.032f, 0, 0}};
  auto l = clip(left, {0, 0, -.45f}, -pi / 4, pi / 4, -pi / 4, pi / 4),
       r = clip(right, {0, 0, -.45f}, -pi / 4, pi / 4, -pi / 4, pi / 4);
  assert(l[0] > 0 && r[0] < 0 && close(l[0], -r[0]));
  auto asym = clip(origin, {-.5f, 0, -1}, std::atan(-.5f), std::atan(1.f),
                   -pi / 4, pi / 4);
  assert(close(asym[0] / asym[3], -1));
  Quat clockwise{0, 0, -std::sin(pi / 8), std::cos(pi / 8)};
  assert(close(rollDegrees(origin, clockwise), 45));
  puts("pose transforms, lock reach, pick angles, angle wrap, depth and stereo "
       "projection passed");
}
