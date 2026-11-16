// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <chrono>
#include <cstdio>
using namespace locks;
int main() {
  assert(housingDistance({.06f, 0, 0}) < 0);
  assert(housingDistance({.045f, 0, .022f}) < 0); // restored plug face
  assert(housingDistance({.054f, 0, .022f}) > 0); // outside its edge
  assert(housingDistance({0, 0, 0}) > 0);      // actual opening
  assert(housingDistance({0, 0, -.060f}) > 0); // no invisible backplate
  assert(housingDistance({.10f, .10f, 0}) > 0);
  for (int k = 0; k < 2; k++) {
    ToolShape shape(k);
    assert(shape.count < shape.balls.size());
    // Approach the front, sides, top and back at speeds high enough to cross
    // the whole object in a single frame. No endpoint-only collision test.
    for (auto pair :
         {std::array<Vec, 2>{{{.06f, 0, .3f}, {.06f, 0, -.3f}}},
          std::array<Vec, 2>{{{.4f, 0, 0.f}, {-.4f, 0, 0.f}}},
          std::array<Vec, 2>{{{-.4f, 0, 0.f}, {.4f, 0, 0.f}}},
          std::array<Vec, 2>{{{.06f, .4f, 0.f}, {.06f, -.4f, 0.f}}},
          std::array<Vec, 2>{{{.06f, 0, -.4f}, {.06f, 0, .3f}}}}) {
      auto c = moveAgainstHousing(shape, {{}, pair[0]}, {{}, pair[1]});
      assert(c.hit);
      assert(shape.clearance(c.pose, 0) >= -.00015f);
      assert(length(c.pose.p - pair[1]) > .05f);
    }
    // Sliding along the front and pulling away both remain possible.
    auto hit =
        moveAgainstHousing(shape, {{}, {.06f, 0, .15f}}, {{}, {.06f, 0, -.1f}});
    Pose across = hit.pose;
    across.p.x += .025f;
    across.p.z -= .05f;
    auto slide = moveAgainstHousing(shape, hit.pose, across);
    assert(slide.hit && slide.pose.p.x > hit.pose.p.x + .020f);
    Pose back = slide.pose;
    back.p.z += .10f;
    auto away = moveAgainstHousing(shape, slide.pose, back);
    assert(length(away.pose.p - back.p) < .001f);
    // An empty-space path must not be blocked by an infinite plane.
    auto clear =
        moveAgainstHousing(shape, {{}, {.4f, 0, .2f}}, {{}, {.4f, -.4f, -.3f}});
    assert(!clear.hit);
  }
  // Every bend the free preparation gesture can produce fits the bounded
  // collision sample storage, including different bend planes.
  for (float opening = 0; opening <= 1.801f; opening += .03f)
    for (float twist : {-pi, -1.f, 0.f, 1.f, pi}) {
      ToolShape shape(0, opening, twist);
      assert(shape.count < shape.balls.size());
    }
  // The straight pin leg fits the aperture; its held leg stays outside.
  for (float angle : {0.f, 45.f, 90.f})
    for (float pick : {0.f, 45.f, 90.f, 135.f, 180.f}) {
      ToolShape pin(0);
      assert(pin.clearance(seatedPose(0, pick, angle), angle) > 0);
    }
  // Mid-shaft contact, including a rotating sideways pin, not only tip/ends.
  ToolShape pin(0);
  Pose a{axisAngle({0, 1, 0}, pi / 2), {-.04f, .06f, .15f}}, b = a;
  b.p.z = -.10f;
  auto hit = moveAgainstHousing(pin, a, b);
  assert(hit.hit && pin.clearance(hit.pose, 0) >= -.00015f);
  b = a;
  b.q = axisAngle({1, 0, 0}, pi);
  b.p = {.02f, .02f, .02f};
  hit = moveAgainstHousing(pin, a, b);
  assert(hit.hit && pin.clearance(hit.pose, 0) >= -.00015f);
  // Gravity lands on the top of the housing and stays bounded at 20/90/180Hz.
  for (float dt : {1.f / 20, 1.f / 90, 1.f / 180}) {
    PhysicalInteraction t;
    t.update({}, {}, {}, 0, 0, 0);
    t.tools[1].holstered = false;
    t.tools[1].pose = {axisAngle({1, 0, 0}, pi / 2), {.06f, .4f, 0.f}};
    bool contact = false;
    for (int n = 0; n < int(2 / dt); n++) {
      t.update({}, {}, {}, 0, 0, 0, dt);
      contact |= t.tools[1].blocked;
      assert(finite(t.tools[1].pose.p));
      assert(ToolShape(1).clearance(t.tools[1].pose, 0) >= -.0004f);
    }
    assert(contact);
  }
  puts("housing front/sides/back, swept wire and rotation, sliding, open "
       "keyway, free space and falling-tool collision passed");
}
