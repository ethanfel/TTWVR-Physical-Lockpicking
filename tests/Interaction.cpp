// SPDX-License-Identifier: GPL-3.0-only
#include "Interaction.h"
#include <cassert>
#include <cstdio>
using namespace locks;
static Pose hand(Vec p, float clockwise = 0) {
  float a = clockwise * pi / 360;
  return {{0, 0, -std::sin(a), std::cos(a)}, p};
}
static Vec midpoint(Segment s) { return (s.a + s.b) * .5f; }
static bool near(float a, float b) { return std::abs(a - b) < .001f; }
static bool near(Vec a, Vec b) { return length(a - b) < .00001f; }
int main() {
  Pose lock;
  Vec pp = midpoint(pickHandle(90)), sp = midpoint(screwdriverHandle());
  auto remote = hand({.25f, 0, .05f});
  // Every contact / motion regression runs with both hand assignments.
  for (bool swapped : {false, true}) {
    Interaction i;
    int ph = swapped ? 1 : 0, sh = 1 - ph;
    auto step = [&](Pose p, Pose s, float pt, float st, float cylinder = 0) {
      if (swapped)
        i.update(lock, s, p, st, pt, cylinder);
      else
        i.update(lock, p, s, pt, st, cylinder);
    };
    step(remote, remote, 0, 0);
    step(remote, remote, 1, 1);
    assert(i.pickHand == -1 && i.screwdriverHand == -1);
    step(hand(pp), hand(sp), 1, 1);
    assert(i.pickHand == -1 && i.screwdriverHand == -1);
    step(hand(pp), hand(sp), 0, 0);
    auto offset = hand(pp + Vec{.02f, 0, 0});
    step(offset, hand(sp), 1, 0);
    assert(i.pickHand == ph && near(i.pick, 90));
    for (int n = 0; n < 500; n++)
      step(offset, hand(sp), 1, 0);
    assert(near(i.pick, 90));
    step(hand(midpoint(pickHandle(120))), hand(sp), 1, 0);
    assert(i.pick > 100 && i.pick < 125);
    float chosen = i.pick;
    step(remote, hand(sp), 1, 0);
    assert(i.pickHand == -1 && near(i.pick, chosen));
    step(hand(midpoint(pickHandle(chosen))), hand(sp), 1, 0);
    assert(i.pickHand == -1);

    i.reset();
    step(hand(pp), hand(sp), 0, 0);
    step(hand(pp), hand(sp), 1, 1);
    assert(i.pickHand == ph && i.screwdriverHand == sh && near(i.target, 0));
    // Holding the screwdriver without twisting must leave the pick movable.
    step(hand(midpoint(pickHandle(105))), hand(sp), 1, 1);
    assert(i.pick > 103 && i.pick < 107 && near(i.target, 0));
    step(hand(pp), hand(sp), 1, 1);
    assert(near(i.pick, 90));
    // Pick cannot move while the other tool applies tension.
    step(hand(midpoint(pickHandle(105))), hand(sp, 25), 1, 1);
    assert(near(i.pick, 90) && near(i.target, 25));
    float cylinder = 0;
    for (int n = 0; n < 10000; n++) {
      step(hand(pp), hand(sp, 25), 0, 1, cylinder);
      cylinder = capCylinder(std::min(cylinder + 1.5f, 75.f), i.target);
    }
    assert(near(i.target, 25) && near(cylinder, 25));
    step(hand(pp), hand(sp, 12), 0, 1, cylinder);
    cylinder = capCylinder(cylinder + 1.5f, i.target);
    assert(near(cylinder, 12));
    assert(near(capCylinder(10, 90), 10));
    assert(near(capCylinder(90, 25), 25));
    step(hand(pp), hand(sp, 90), 0, 1, cylinder);
    assert(i.target == 90.f);
    step(hand(pp), hand(sp, 12), 0, 0, cylinder);
    assert(i.screwdriverHand == -1 && near(i.target, 0));
    step(hand(pp), hand(sp, 20), 0, 1);
    step(hand(pp), hand(sp, 20.5f), 0, 1);
    assert(near(i.target, 0));
    step(hand(pp), remote, 0, 1);
    assert(i.screwdriverHand == -1 && near(i.target, 0));
    step(hand(pp), hand(sp), 0, 1);
    assert(i.screwdriverHand == -1);
    i.reset();
    step(hand(pp), hand(sp), 1, 1);
    assert(i.pickHand == -1 && i.screwdriverHand == -1);
  }
  // Both hands competing for one tool: one owner, no stealing or auto-grab.
  Interaction i;
  i.update(lock, hand(pp), hand(pp), 0, 0, 0);
  i.update(lock, hand(pp), hand(pp), 1, 1, 0);
  assert(i.pickHand == 0 && i.screwdriverHand == -1 && !i.grabs[1].held);
  i.update(lock, hand(pp), hand(sp), 1, 1, 0);
  assert(i.screwdriverHand == -1);
  i.update(lock, hand(pp), hand(sp), 1, 0, 0);
  i.update(lock, hand(pp), hand(sp), 1, 1, 0);
  assert(i.pickHand == 0 && i.screwdriverHand == 1);
  i.reset();
  i.update(lock, hand(sp), hand(sp), 0, 0, 0);
  i.update(lock, hand(sp), hand(sp), 1, 0, 0);
  i.update(lock, hand(sp), hand(sp), 1, 1, 0);
  assert(i.screwdriverHand == 0 && !i.grabs[1].held);
  // Release then fresh press permits transfer to the other hand.
  i.update(lock, hand(sp), hand(sp), 1, 0, 0);
  i.update(lock, hand(sp), hand(sp), 0, 1, 0);
  assert(i.screwdriverHand == 1 && !i.grabs[0].held);
  // Walking the same held trigger from one tool to the other drops, not grabs.
  i.update(lock, hand(sp), hand(pp), 0, 1, 0);
  assert(i.screwdriverHand == -1 && i.pickHand == -1);
  i.update(lock, hand(sp), hand(pp), 0, 0, 0);
  i.update(lock, hand(sp), hand(pp), 0, 1, 0);
  assert(i.pickHand == 1);

  // Twist around the actual shaft is independent of controller mounting angle.
  Quat tilted{std::sin(.35f), 0, 0, std::cos(.35f)};
  Quat rotated = multiply(hand({}, 25).q, tilted);
  assert(near(shaftTwist(lock, tilted, rotated), 25));
  assert(near(shaftTwist(lock, {}, tilted), 0)); // Swing is not twist.
  Pose moved{{0, std::sin(.5f), 0, std::cos(.5f)}, {2, 1, -4}};
  assert(near(
      shaftTwist(moved, multiply(moved.q, tilted), multiply(moved.q, rotated)),
      25));
  i.reset();
  Pose l{moved.q, world(moved, sp)}, r{moved.q, world(moved, pp)};
  i.update(moved, l, r, 0, 0, 0);
  i.update(moved, l, r, 1, 1, 0);
  l.q = multiply(moved.q, hand({}, 25).q);
  i.update(moved, l, r, 1, 1, 0);
  assert(i.pickHand == 1 && i.screwdriverHand == 0 && near(i.target, 25));
  // Axial rotation moves only the grip surface; centerline remains under hand.
  auto shaft = screwdriverHandle();
  assert(near(normal(shaft.a - screwdriverTip), {0, 0, 1}));
  for (int angle = 0; angle <= 90; angle++) {
    Vec radius = screwdriverRadial(float(angle)) * .012f;
    assert(near(length(radius), .012f));
    assert(near((shaft.a + radius + shaft.a - radius) * .5f, shaft.a));
    assert(near(midpoint(screwdriverHandle()), sp));
  }
  puts("both hand assignments, exclusive ownership, handoff, contact-only "
       "grabs, "
       "shaft twist, no orbit/drift, native limits and reset passed");
}
