// SPDX-License-Identifier: GPL-3.0-only
#include "SafeFrameMesh.h"
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
int main() {
  TargetBounds safe;
  safe.style = LockStyle::RoundedSafe;
  assert(housingDistance({.064f, .064f, 0}, 0, safe) < 0);
  assert(housingDistance({.064f, .064f, 0}) > 0);
  assert(housingDistance({.072f, .072f, 0}, 0, safe) > 0);
  assert(housingDistance({.072f, 0, .010f}, 0, safe) < 0);
  assert(housingDistance({.072f, 0, safeFront}, 0, safe) > 0); // bevel
  SafeFrameMesh mesh;
  for (auto v : mesh.body.vertices) {
    assert(finite(v.p) && finite(v.n));
    assert(std::abs(length(v.n) - 1) < .001f);
    assert(housingDistance(v.p, 0, safe) < .0012f);
  }
  for (auto t : mesh.body.triangles)
    for (auto i : t) assert(i < mesh.body.vertices.size());
  for (float angle : {0.f, 45.f, 90.f})
    for (float pick : {0.f, 45.f, 90.f, 135.f, 180.f}) {
      ToolShape pin(0, 1, 0, safe), driver(1, 1, 0, safe);
      assert(pin.clearance(seatedPose(0, pick, angle), angle) > 0);
      assert(driver.clearance(seatedPose(1, 90, angle), angle) > 0);
    }
  for (int k = 0; k < 2; k++) {
    ToolShape tool(k, 1, 0, safe);
    auto c = moveAgainstHousing(tool, {{}, {.064f, .064f, .3f}},
                                      {{}, {.064f, .064f, -.3f}});
    assert(c.hit && tool.clearance(c.pose, 0) >= -.00015f);
    Pose away = c.pose; away.p.z += .12f;
    auto d = moveAgainstHousing(tool, c.pose, away);
    assert(length(d.pose.p - away.p) < .0002f);
  }
  for (int h = 0; h < 2; h++) {
    PhysicalInteraction t;
    t.targetBounds = safe; t.config.useHolders = false;
    Pose hands[2]; hands[1-h].p = {2,2,2};
    auto step = [&](float held) { t.update({}, hands[0], hands[1], h==0?held:0, h==1?held:0, 0); };
    auto grip = [](Pose p) { return Pose{p.q,world(p,handleCenter(0,1))}; };
    step(0); auto &pin=t.tools[0]; pin.opening=1;
    pin.pose=holderTool(t.config,0,1,0); hands[h]=grip(pin.pose); step(1);
    Pose entry{pinAlignment(1,0),{.001f,.007f,.026f}}, outside=entry;
    outside.p.z+=.06f; hands[h]=grip(outside); step(1);
    hands[h]=grip(entry); step(1);
    assert(pin.inserted && length(pin.pose.p-entry.p)<.00015f);
    step(0); hands[h]=grip(pin.pose); step(1);
    hands[h].p=hands[h].p+pin.entryAxis*.10f; step(1);
    assert(!pin.inserted && t.pickHand==h);
  }
  printf("Safe frame: %zu triangles, matching rounded corners/bevel, clear rotating keyway, both-hand insertion and withdrawal passed\n",mesh.body.triangles.size());
}
