// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
int main() {
  assert(canDistance({0, -.154f, 0}) < 0);
  assert(canDistance({0, -.1f, 0}) > 0);
  assert(canDistance({.030f, -.1f, 0}) < 0);
  assert(canDistance({0, .01f, 0}) > 0);
  for (float dt : {1.f / 20, 1.f / 90, 1.f / 180}) {
    PhysicalInteraction t;
    t.update({}, {}, {}, 0, 0, 0, dt);
    auto &driver = t.tools[1];
    Pose start = driver.pose;
    for (int n = 0; n < int(5 / dt); n++)
      t.update({}, {}, {}, 0, 0, 0, dt);
    Vec rel = driver.pose.p - holderPoint(t.config, 1),
        axis = rotate(driver.pose.q, {0, 0, 1});
    printf("dt %.5f tip %.5f %.5f %.5f lean %.3f vel %.4f angular %.4f\n", dt,
           rel.x, rel.y, rel.z, acos(axis.y) * 180 / pi,
           length(driver.velocity), length(driver.angular));
    fflush(stdout);
    assert(!driver.holstered && driver.owner < 0);
    assert(rel.y > -.153f && rel.y < -.135f && hypot(rel.x, rel.z) < .029f);
    assert(length(driver.pose.p - start.p) > .002f);
    assert(acos(axis.y) > .05f && acos(axis.y) < .6f);
    assert(length(driver.velocity) < .03f && length(driver.angular) < .2f);
    Pose rest = driver.pose;
    for (int n = 0; n < int(2 / dt); n++)
      t.update({}, {}, {}, 0, 0, 0, dt);
    assert(length(driver.pose.p - rest.p) < .003f);
    // Drop into the can off-centre: no teleport to an anchor on release.
    driver.pose = looseDriverStart(t.config);
    driver.pose.p.y += .10f;
    driver.velocity = {};
    driver.angular = {};
    t.update({}, {}, {}, 0, 0, 0, dt);
    assert(driver.pose.p.y > holderPoint(t.config, 1).y - .10f);
    for (int n = 0; n < int(4 / dt); n++)
      t.update({}, {}, {}, 0, 0, 0, dt);
    assert(driver.pose.p.y > holderPoint(t.config, 1).y - .153f);
  }
  puts("Open can, loose settling, rim lean, stability and free drop passed");
}
