// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
Pose grasp(Pose p, float opening, float twist = 0) {
  return {p.q, world(p, handleCenter(0, opening, twist))};
}
int main() {
  for (int h = 0; h < 2; h++)
    for (float degrees : {40.f, 65.f, 90.f, 120.f, 150.f, 162.f}) {
      PhysicalInteraction t;
      t.config.requirePreparation = false;
      t.config.useHolders = false;
      Pose hands[2];
      hands[1 - h].p = {2, 2, 2};
      auto step = [&](float hold, unsigned stage = 2) {
        t.update({}, hands[0], hands[1], h == 0 ? hold : 0, h == 1 ? hold : 0,
                 0, 1.f / 90, stage);
      };
      step(0);
      auto &pin = t.tools[0];
      pin.opening = degrees / 90;
      pin.bendYaw = .6f;
      pin.pose = holderTool(t.config, 0, pin.opening, pin.bendYaw);
      hands[h] = grasp(pin.pose, pin.opening, pin.bendYaw);
      step(1);
      Quat q = pinAlignment(pin.opening, pin.bendYaw);
      hands[h] =
          grasp({q, mouth(0) + Vec{0, 0, .07f}}, pin.opening, pin.bendYaw);
      step(1);
      hands[h] = grasp({q, mouth(0)}, pin.opening, pin.bendYaw);
      step(1);
      assert(pin.inserted && !pin.inserting && t.insertedEvents == 1);
      float shallow =
          pinHandlingQuality(pin.opening, pin.alignment, pin.pinDepth);
      float angle = t.pick;
      hands[h].p.z -= .05f;
      step(1);
      assert(pin.inserted && std::abs(t.pick - angle) < .001f);
      assert(pinHandlingQuality(pin.opening, pin.alignment, pin.pinDepth) >=
             shallow);
      // Actual housing contact now bounds depth, including tilted/bent pins.
      assert(ToolShape(0, pin.opening, pin.bendYaw).clearance(pin.pose, 0) > -.0004f);
      step(0);
      hands[h] = grasp(pin.pose, pin.opening, pin.bendYaw);
      step(1);
      assert(pin.inserted);
      hands[h].p.z += .10f;
      step(1);
      assert(!pin.inserted && t.pickHand == h && t.withdrawnEvents == 1);
      step(0);
      for (int n = 0; n < 200; n++)
        step(0);
      assert(pin.visible &&
             pin.holstered); // correction/reuse requires no reset
    }
  assert(pinHandlingQuality(65.f / 90, .7f, .006f) <
         pinHandlingQuality(1, 1, .006f));
  assert(pinHandlingQuality(1, .5f, .006f) < pinHandlingQuality(1, 1, .006f));
  // Reproduce the 65-degree wedge from the headset log. Pulling into clear
  // front space must recover the held tool without restarting the minigame.
  {
    PhysicalInteraction t;
    t.update({}, {}, {}, 0, 0, 0);
    auto &p = t.tools[0];
    p.opening = .719f;
    p.bendYaw = -.574f;
    p.holstered = false;
    p.pose = {unit({.13456f, -.13978f, -.13253f, -.97200f}),
              {.00320f, .01163f, -.01520f}};
    Pose hand = grasp(p.pose, p.opening, p.bendYaw), other{{}, {2, 2, 2}};
    t.update({}, hand, other, 1, 0, 0);
    assert(t.pickHand == 0);
    hand = grasp({{}, {0, .10f, .30f}}, p.opening, p.bendYaw);
    for (int n = 0; n < 90; n++)
      t.update({}, hand, other, 1, 0, 0);
    assert(t.pickHand == 0 && !p.blocked &&
           length(world(p.pose, handleCenter(0, p.opening, p.bendYaw)) -
                  hand.p) < .002f);
  }
  // Tremor is presentation only, driven by native resistance and shared by
  // both eyes. It cannot change pick input, depth or the physics pose.
  {
    PhysicalInteraction t;
    t.config.requirePreparation = false;
    t.update({}, {}, {}, 0, 0, 0);
    auto &p = t.tools[0];
    auto &d = t.tools[1];
    p.inserted = d.inserted = true;
    p.holstered = d.holstered = false;
    p.pinDepth = .03f;
    p.pose = engagedPinPose(90, .03f, 1, 0);
    d.owner = 1;
    d.pose = d.seatPose = aimedTool({{}, {.07f, -.035f, .15f}}, mouth(1));
    Pose rest{d.pose.q, world(d.pose, handleCenter(1))};
    d.leverStartHand = rest.p;
    d.driverTracking = true; d.driverTracked = rest;
    Pose hand = rest; hand.p = hand.p + Vec{.012f, -.002f, 0};
    d.seatedRadius = length(rest.p - mouth(1));
    t.grabs[1].held = true;
    float largest = 0, smallest = 0;
    Pose physics = turnWithCylinder(p.pose, 10);
    for (int n = 0; n < 90; n++) {
      t.update({}, {{}, {2, 2, 2}}, hand, 0, 1, 10, 1.f / 90, 4, 1);
      largest = std::max(largest, t.pinTremor);
      smallest = std::min(smallest, t.pinTremor);
      assert(std::abs(t.pick - 90) < .001f && length(p.pose.p - physics.p) < .00001f &&
             p.pinDepth == .03f);
      auto visual = tremblingPinPose(p.pose, t.pinTremor);
      assert(length(visual.p - p.pose.p) < .001f); // tiny render-only tremor
    }
    assert(largest > 1 && smallest < -1);
    for (int n = 0; n < 45; n++)
      t.update({}, {{}, {2, 2, 2}}, hand, 0, 0, 0, 1.f / 90, 2, 1);
    assert(t.pinTremor == 0);
  }
  // Display count, cap and held pin subtraction do not mutate inventory.
  assert(pinsInBox(0, false) == 0 && pinsInBox(1, false) == 1 &&
         pinsInBox(1, true) == 0);
  assert(pinsInBox(7, false) == 7 && pinsInBox(7, true) == 6);
  assert(pinsInBox(100, false) == pinBoxCapacity && pinsInBox(-1, false) == 1);
  puts("soft engagement, bend/alignment penalties, depth adjustment, wedge "
       "recovery, resistance tremor and visual inventory passed");
}
