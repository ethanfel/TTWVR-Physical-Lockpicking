// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
int main() {
  for (float dt : {1.f / 20, 1.f / 90, 1.f / 180}) {
    PinBox box;
    Ergonomics c;
    box.reconcile(18, c);
    for (int n = 0; n < int(5 / dt); n++)
      box.update(dt, {}, c);
    int asleep = 0;
    for (int n = 0; n < box.count; n++) {
      auto &p = box.pins[n];
      assert(finite(p.pose.p) && length(p.velocity) < .5f);
      asleep += p.sleeping;
      for (auto segment : pinSegments(p))
        for (Vec point : {segment.a, segment.b}) {
          Vec v = point - holderPoint(c, 0);
          assert(v.y > -.102f && v.y < .03f);
          assert(std::abs(v.x) < .030f && v.z > -.089f && v.z < .027f);
        }
    }
    printf("rate %.0f sleeping %d/18\n", 1 / dt, asleep);
    assert(asleep == 18);
    // Selecting actual geometry removes exactly that physical item.
    Vec hand = world(box.pins[7].pose, handleCenter(0));
    int chosen = box.nearest(hand);
    if (chosen < 0)
      hand = box.pins[7].pose.p, chosen = box.nearest(hand);
    assert(chosen >= 0);
    Pose pose = box.pins[chosen].pose;
    auto picked = box.take(chosen);
    assert(box.count == 17 && length(pose.p - picked.pose.p) < 1e-7f);
    box.put(picked);
    assert(box.count == 18);
    box.reconcile(0, c);
    assert(box.count == 0 && box.nearest(hand) == -1);
  }
  // Actual pickup / gravity return, then a world drop, without creating or
  // removing inventory during handling inside the box.
  for (int h = 0; h < 2; h++) {
    PhysicalInteraction t;
    t.syncInventory(true, 5);
    t.worldDrops = true;
    Pose hands[2];
    hands[1 - h].p = {2, 2, 2};
    auto step = [&](float hold) {
      t.update({}, hands[0], hands[1], h == 0 ? hold : 0, h == 1 ? hold : 0, 0);
    };
    for (int n = 0; n < 180; n++)
      step(0);
    assert(t.box.count == 5 && t.tools[0].holstered);
    for (int repeat = 0; repeat < 3; repeat++) {
      auto stored = t.box.pins[repeat % t.box.count];
      hands[h] = {
          stored.pose.q,
          world(stored.pose, handleCenter(0, stored.opening, stored.twist))};
      step(1);
      assert(t.pickHand == h && t.box.count == 4 && !t.tools[0].holstered);
      Pose dropping{axisAngle({1, 0, 0}, pi / 2),
                    holderPoint(t.config, 0) + Vec{0, .035f, .002f}};
      hands[h] = {dropping.q, world(dropping, handleCenter(0, 0))};
      for (int n = 0; n < 30; n++)
        step(1); // stop the hand before release
      step(0);
      unsigned drops = 0, stores = 0;
      for (int n = 0; n < 360; n++) {
        step(0);
        drops += t.droppedEvents;
        stores |= t.storedEvents;
      }
      assert(drops == 0 && (stores & 1) && t.tools[0].holstered &&
             t.box.count == 5 && t.inventory == 5);
    }
    auto stored = t.box.pins[0];
    hands[h] = {
        stored.pose.q,
        world(stored.pose, handleCenter(0, stored.opening, stored.twist))};
    step(1);
    hands[h] = {{}, {-.4f, .05f, .3f}};
    for (int n = 0; n < 30; n++)
      step(1);
    step(0);
    unsigned drops = 0;
    for (int n = 0; n < 180; n++) {
      step(0);
      drops += t.droppedEvents;
    }
    assert(drops == 1 && t.dropPending && t.box.count == 4);
    t.resolveDrop(true, 4);
    t.syncInventory(true, 4);
    step(0);
    assert(t.box.count == 4 && t.tools[0].visible);
    t.syncInventory(true, 0);
    step(0);
    assert(t.box.count == 0 && !t.tools[0].visible);
  }
  puts("fixed box gravity/contacts, physical selection, either-hand returns "
       "and inventory-backed floor drops passed");
}
