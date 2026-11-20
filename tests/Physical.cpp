// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
bool close(Vec a, Vec b, float epsilon = .001f) {
  return length(a - b) < epsilon;
}
bool close(float a, float b, float epsilon = .001f) {
  return std::abs(a - b) < epsilon;
}
Pose grip(Pose tool, int k, float opening = 1) {
  return {tool.q, world(tool, handleCenter(k, opening))};
}
int main() {
  // Settings are finite and bounded; reset keeps user ergonomics.
  Ergonomics settings;
  settings.load("LockBelowEyesMeters=0.55\nFullTurnWristDegrees=0\n"
                "InsertRadiusMeters=nan\nWithdrawTravelMeters=999\n");
  assert(close(settings.lockBelowEyes, .55f));
  assert(close(settings.fullTurnWrist, 20));
  assert(close(settings.insertRadius, .028f));
  assert(close(settings.withdrawTravel, .1f));
  for (int hand = 0; hand < 2; hand++) {
    PhysicalInteraction t;
    t.config.requirePreparation = false;
    t.config.useHolders = false;
    Pose hands[2];
    auto step = [&](float hold) {
      t.update({}, hands[0], hands[1], hand == 0 ? hold : 0,
               hand == 1 ? hold : 0, 0);
    };
    step(0);
    assert(t.tools[0].opening == 1);
    for (int k = 0; k < 2; k++) {
      assert(t.tools[k].holstered);
      hands[hand] = grip(t.tools[k].pose, k);
      step(1);
      assert(t.tools[k].owner == hand && !t.tools[k].holstered);
      hands[hand].p.y += .1f;
      step(1);
      assert(!t.tools[k].holstered);
      hands[hand].p = holderPoint(t.config, k);
      step(1);
      step(0);
      assert(t.tools[k].holstered && t.tools[k].owner == -1 &&
             t.storedEvents == (1u << k));
      Pose stored = t.tools[k].pose;
      for (int n = 0; n < 90; n++)
        step(0);
      assert(close(stored.p, t.tools[k].pose.p));
    }
    t.update({}, hands[0], hands[1], 0, 0, 0, 1.f / 90, 5, 0);
    assert(!t.tools[0].visible);
    t.update({}, hands[0], hands[1], 0, 0, 0, 1.f / 90, 1, 1);
    assert(t.tools[0].visible && t.tools[0].holstered &&
           t.tools[0].opening == 1);
  }
  // Both tools fit the central keyway throughout the full native turn.
  // The pin's bend stays at the face; its shaft never orbits or pitches.
  for (float pick = 0; pick <= 180; pick += 5)
    for (float cylinder = 0; cylinder <= 90; cylinder += 5) {
      Pose pin = seatedPose(0, pick, cylinder);
      assert(close(world(pin, pinHinge(1)), pinPivot()));
      assert(close(pin.p.z, -.041f));
      for (int k = 0; k < 2; k++) {
        Vec moved = turnWithCylinder({{}, mouth(k)}, cylinder).p;
        Vec slot = rotate(axisAngle({0, 0, 1}, cylinder * pi / 180), moved);
        assert(std::abs(slot.x) + (k == 0 ? .0014f : .0021f) < .005f);
        assert(std::abs(slot.y) + .0021f < .018f);
      }
      for (int n = 0; n <= 40; n++) {
        Vec point = world(pin, pinHandlePoint(1, n / 40.f));
        // Closest possible upper edge of the driver blade at this depth.
        assert(point.y - .0014f > mouth(1).y + .0021f);
        assert(point.z > .024f + .0014f);
      }
    }
  // Preserve explicit regression coverage for the optional legacy attachment.
  // FreeDriver.cpp covers the default contact-only mode, including full turns.
  for (int swap = 0; swap < 2; swap++) {
    PhysicalInteraction i;
    i.config.driverMode = DriverMode::SeatedLever;
    i.config.requirePreparation = true;
    i.config.useHolders = false;
    Pose lock, p, d;
    auto step = [&](float pt, float dt, float cylinder = 0, unsigned stage = 2,
                    float health = 1) {
      i.update(lock, swap ? d : p, swap ? p : d, swap ? dt : pt, swap ? pt : dt,
               cylinder, 1.f / 90, stage, health);
    };
    step(0, 0);
    assert(close(i.tools[0].opening, 0));
    p = grip(i.tools[0].pose, 0, 0);
    d.p = {1, 1, 1};
    step(1, 0);
    assert(i.pickHand == swap && i.screwdriverHand == -1);
    // A folded pin cannot enter the slot, even with a deliberate approach.
    p = grip({{}, mouth(0) + Vec{0, 0, .08f}}, 0, 0);
    step(1, 0);
    p.p.z -= .1f;
    step(1, 0);
    assert(!i.tools[0].inserted);
    // Second-hand contact and increased separation prepare the actual pin.
    p.p = {-.15f, .02f, .2f};
    step(1, 0);
    d = {p.q, i.tools[0].pose.p + Vec{.04f, 0, 0}};
    step(1, 1);
    assert(i.bendHand == 1 - swap && close(i.tools[0].opening, 0));
    p.p.y += .03f;
    d.p.y += .03f;
    step(1, 1);
    assert(close(i.tools[0].opening, 0)); // moving both hands cannot unfold it
    d.p = d.p + pinHinge(0) - pinHinge(.5f);
    step(1, 1);
    assert(close(i.tools[0].opening, .5f) && i.preparedHands == 0);
    Vec heldBefore =
        world(i.tools[0].pose, handleCenter(0, i.tools[0].opening));
    assert(close(heldBefore, p.p));
    step(1, 0);
    assert(i.bendHand == -1 && close(i.tools[0].opening, .5f));
    d.p = i.tools[0].pose.p + Vec{.04f, 0, 0};
    step(1, 1);
    assert(i.bendHand == 1 - swap);
    d.p = d.p + pinHinge(.5f) - pinHinge(1);
    step(1, 1);
    assert(close(i.tools[0].opening, 1) && i.preparedHands == 3);
    assert(close(world(i.tools[0].pose, handleCenter(0)), p.p));
    step(1, 1);
    assert(i.preparedHands == 0);
    step(1, 0);
    d = grip(i.tools[1].pose, 1);
    step(1, 1);
    assert(i.pickHand == swap && i.screwdriverHand == 1 - swap);
    // Natural pointing poses may differ for the two hands, including roll.
    Quat pq{};
    Quat dq = multiply(axisAngle({1, 0, 0}, -.2f), axisAngle({0, 0, 1}, -.4f));
    auto approach = [&](float z, float driverZ) {
      p = grip({pq, mouth(0) + Vec{0, 0, z}}, 0);
      d = grip({dq, mouth(1) + Vec{0, 0, driverZ}}, 1);
      step(1, 1);
    };
    approach(.08f, .08f);
    assert(!i.seated() && close(i.target, 0));
    assert(close(world(i.tools[0].pose, handleCenter(0)), p.p));
    approach(0, 0);
    assert(!i.seated() && i.tools[0].inserted && !i.tools[0].inserting &&
           i.tools[1].inserting);
    assert(i.insertedEvents == 1);
    assert(close(i.tools[0].insertProgress, 0)); // no automatic seating push
    approach(-.065f, 0); // Seat the pin before moving the driver past it.
    approach(-.065f, -.018f);
    assert(i.seated() && i.insertedEvents == 2 && close(i.target, 0));
    step(1, 1);
    assert(i.insertedEvents == 0);
    // Re-grip the supported handle in its lower-slot pose.
    step(1, 0);
    d = grip(i.tools[1].pose, 1);
    step(1, 1);
    // Position, not wrist roll, searches with the pin. Driver uses handle travel around the plug.
    p.q = multiply(pq, axisAngle({0, 0, 1}, -.4f));
    step(1, 1);
    assert(close(i.pick, 90));
    p.p = world(seatedPose(0, 120, 0), handleCenter(0));
    step(1, 1);
    assert(close(i.pick, 120));
    d.q = multiply(dq, axisAngle({0, 0, 1}, -10 * pi / 180));
    step(1, 1);
    assert(close(i.target, 0)); // axial twist cannot apply torque
    Pose leverStart = d;
    d.p = turnWithCylinder(leverStart, 22.5f).p;
    step(1, 1);
    assert(close(i.target, 22.5f));
    for (int n = 0; n < 1000; n++)
      step(1, 1, 20, 4);
    assert(close(i.target, 22.5f) && close(i.pick, 120));
    assert(close(world(i.tools[1].pose, {0, 0, driverInsertionDepth}), turnWithCylinder({{}, mouth(1)},20).p));
    assert(rotate(inverse(turnWithCylinder({},20).q), rotate(i.tools[1].pose.q, {0, 0, 1})).y <= .0001f);
    d.p = turnWithCylinder(leverStart, 90).p;
    step(1, 1, 20, 4);
    assert(close(i.target, 90) && close(capCylinder(20, i.target), 20));
    // Release supports both tools, but removes torque immediately.
    step(0, 0, 20, 3);
    auto pin = turnWithCylinder(i.tools[0].pose,-20).p,
         driver = turnWithCylinder(i.tools[1].pose,-20).p;
    for (int n = 0; n < 90; n++)
      step(0, 0);
    assert(i.seated() && i.pickHand == -1 && i.screwdriverHand == -1);
    assert(close(i.target, 0) && close(i.tools[0].pose.p, pin));
    assert(close(i.tools[1].pose.p, driver));
    // A fresh grip can start at an entirely different roll without a jump.
    p = grip(i.tools[0].pose, 0);
    d = grip(i.tools[1].pose, 1);
    p.q = axisAngle({0, 0, 1}, 1.2f);
    d.q = axisAngle({0, 0, 1}, -.8f);
    step(1, 1);
    assert(close(i.target, 0) && close(i.pick, 120));
    d.q = multiply(d.q, axisAngle({0, 0, 1}, -10 * pi / 180));
    step(1, 1);
    assert(close(i.target, 0));
    d.p = turnWithCylinder(d, 22.5f).p;
    step(1, 1);
    assert(close(i.target, 22.5f));
    // Pulling out retains the hand owner; releasing a loose tool drops it.
    p.p.z += .10f;
    step(1, 1);
    assert(!i.tools[0].inserted && i.pickHand == swap && close(i.target, 0));
    assert(i.withdrawnEvents == 1);
    d.p.z += .10f;
    step(1, 1);
    assert(!i.tools[1].inserted && i.screwdriverHand == 1 - swap);
    assert(i.withdrawnEvents == 2);
    step(0, 0);
    auto dropped = i.tools[0].pose.p;
    for (int n = 0; n < 10; n++)
      step(0, 0);
    assert(i.tools[0].pose.p.y < dropped.y - .02f);
    for (int n = 0; n < 900; n++)
      step(0, 0);
    for (int k = 0; k < 2; k++) {
      assert(finite(i.tools[k].pose.p));
      assert(i.tools[k].holstered);
      assert(length(i.tools[k].velocity) < .12f);
    }
    // Holding away from a handle cannot auto-grab on approach.
    p.p = {2, 2, 2};
    step(1, 0);
    p = grip(i.tools[0].pose, 0);
    step(1, 0);
    assert(i.pickHand == -1);
    step(0, 0);
    step(1, 0);
    assert(i.pickHand == swap);
    p.p = {2, 2, 2};
    step(1, 0);
    step(0, 0);
    unsigned recovered = 0;
    for (int n = 0; n < 250; n++) {
      step(0, 0);
      recovered |= i.recovered;
    }
    assert(recovered & 1);
    assert(close(i.tools[0].opening, 1));
    step(0, 0, 20, 5, 0);
    assert(!i.tools[0].visible && i.tools[1].visible);
    step(0, 0, 0, 1, 1);
    assert(i.tools[0].visible && !i.tools[0].inserted &&
           close(i.tools[0].opening, 0));
    i.config = settings;
    i.reset();
    step(1, 1);
    assert(i.pickHand == -1 && i.screwdriverHand == -1);
    assert(close(i.config.lockBelowEyes, .55f));
  }
  // Pin contact and holding use the fitted fingertip pose, not palm centre.
  {
    PhysicalInteraction test;
    test.config.requirePreparation = true;
    test.config.useHolders = false;
    Pose left, right;
    Pose pinches[2];
    test.update({}, left, right, 0, 0, 0);
    pinches[0] = grip(test.tools[0].pose, 0, 0);
    left.p = {2, 2, 2};
    right.p = pinches[1].p = {-2, -2, -2};
    test.update({}, left, right, 1, 0, 0, 1.f / 90, 2, 1, pinches);
    assert(test.pickHand == 0);
    assert(close(world(test.tools[0].pose, handleCenter(0, 0)), pinches[0].p));
    // Dropping the primary while the helper is gripping clears both owners.
    pinches[1].p = test.tools[0].pose.p + Vec{.04f, 0, 0};
    test.update({}, left, right, 1, 1, 0, 1.f / 90, 2, 1, pinches);
    assert(test.bendHand == 1);
    test.update({}, left, right, 0, 1, 0, 1.f / 90, 2, 1, pinches);
    assert(test.bendHand == -1 && test.pickHand == -1);
    test.reset();
    assert(test.bendHand == -1 && test.tools[0].opening == 0);
  }
  // Placement ignores head pitch and roll, and keeps a reachable vertical
  // panel.
  for (float yaw : {0.f, .8f, 2.f})
    for (float pitch : {-.8f, 0.f, .6f}) {
      Pose head{multiply(axisAngle({0, 1, 0}, yaw),
                         multiply(axisAngle({1, 0, 0}, pitch),
                                  axisAngle({0, 0, 1}, .3f))),
                {1, 1.75f, 2}};
      Pose placement = placeLock(head);
      assert(close(placement.p.y, 1.33f));
      assert(close(rotate(placement.q, {0, 1, 0}), {0, 1, 0}));
      assert(close(placement.p,
                   head.p + Vec{0, -.42f, 0} +
                       rotate(axisAngle({0, 1, 0}, yaw), {0, 0, -.42f})));
    }
  // Gravity stays world-down; there is no invisible tray below the lock.
  PhysicalInteraction i;
  i.config.useHolders = false;
  Pose lock{axisAngle({1, 0, 0}, .5f), {1, 2, 3}};
  i.update(lock, {}, {}, 0, 0, 0);
  i.tools[0].holstered = false;
  i.tools[0].pose.p = i.tools[0].pose.p + rotate(inverse(lock.q), {0, .15f, 0});
  Vec before = world(lock, i.tools[0].pose.p);
  i.update(lock, {}, {}, 0, 0, 0, .01f);
  Vec after = world(lock, i.tools[0].pose.p);
  assert(after.y < before.y && close(after.x, before.x) &&
         close(after.z, before.z));
  // Reversed insertion cannot seat; a dropped tip cannot auto-insert.
  i.reset();
  i.update({}, {}, {}, 0, 0, 0);
  Pose free = grip(i.tools[1].pose, 1);
  i.update({}, {}, free, 0, 1, 0);
  assert(i.screwdriverHand == 1);
  Pose backwards{axisAngle({1, 0, 0}, pi), socket(1) + Vec{0, 0, .08f}};
  free = grip(backwards, 1);
  i.update({}, {}, free, 0, 1, 0);
  backwards.p = socket(1);
  free = grip(backwards, 1);
  i.update({}, {}, free, 0, 1, 0);
  assert(!i.tools[1].inserted);
  i.update({}, {}, free, 0, 0, 0);
  assert(i.screwdriverHand == -1);
  i.tools[1].pose = seatedPose(1, 90, 0);
  i.update({}, {}, {}, 0, 0, 0);
  assert(!i.tools[1].inserted);
  // One owner under simultaneous grabs; release/fresh press permits handoff.
  i.reset();
  i.update({}, {}, {}, 0, 0, 0);
  free = grip(i.tools[1].pose, 1);
  i.update({}, free, free, 1, 1, 0);
  assert(i.screwdriverHand == 0 && !i.grabs[1].held);
  i.update({}, free, free, 1, 0, 0);
  i.update({}, free, free, 0, 1, 0);
  assert(i.screwdriverHand == 1 && !i.grabs[0].held);
  // Free tools fall past the former tray and return to their holders.
  for (float dt : {1.f / 20, 1.f / 90, 1.f / 180}) {
    PhysicalInteraction test;
    test.config.requirePreparation = true;
    test.config.useHolders = false;
    test.update({}, {}, {}, 0, 0, 0);
    test.tools[1].holstered = false;
    test.tools[1].pose.p = {0, 0, .20f};
    bool passed = false;
    for (int n = 0; n < int(4 / dt); n++) {
      test.update({}, {}, {}, 0, 0, 0, dt);
      passed |= test.tools[1].pose.p.y < -.30f;
    }
    assert(passed);
    assert(length(test.tools[1].velocity) < .12f);
    assert(test.tools[1].holstered);
  }
  // A released screwdriver falls clear and is recovered without an invisible
  // surface catching it from underneath.
  {
    PhysicalInteraction test;
    test.config.requirePreparation = true;
    test.config.useHolders = false;
    test.update({}, {}, {}, 0, 0, 0);
    test.tools[1].holstered = false;
    test.tools[1].pose = {axisAngle({0, 0, 1}, -.2f), {0, 0, .123f}};
    unsigned recovered = 0;
    for (int n = 0; n < 900; n++) {
      test.update({}, {}, {}, 0, 0, 0, 1.f / 45);
      recovered |= test.recovered;
    }
    assert(recovered & 2);
    assert(length(test.tools[1].velocity) < .12f);
    assert(test.tools[1].holstered);
  }
  // Free bending is reversible, works in different planes and does not snap.
  for (int swap = 0; swap < 2; swap++) {
    PhysicalInteraction t;
    Pose p, d;
    auto step = [&](float a, float b) {
      t.update({}, swap ? d : p, swap ? p : d, swap ? b : a, swap ? a : b, 0);
    };
    step(0, 0);
    p = grip(t.tools[0].pose, 0, 0);
    step(1, 0);
    d.p = t.tools[0].pose.p + Vec{.04f, 0, 0};
    step(1, 1);
    assert(t.bendHand == 1 - swap);
    Vec origin = d.p;
    for (float degrees : {45.f, 95.f, 150.f, 110.f, 0.f, 90.f}) {
      d.p = origin + rotate(p.q, pinHinge(0) - pinHinge(degrees / 90, .4f));
      step(1, 1);
      assert(close(t.tools[0].opening, degrees / 90, .001f));
      if (degrees > 0)
        assert(close(t.tools[0].bendYaw, .4f));
      float before = t.tools[0].opening;
      step(1, 1);
      assert(close(t.tools[0].opening, before));
    }
    step(1, 0);
    assert(close(t.tools[0].opening, 1));
  }
  for (float degrees = 80; degrees <= 130; degrees += 5)
    for (float twist : {-.8f, 0.f, .8f})
      for (float angle = 0; angle <= 180; angle += 10) {
        auto p = seatedPose(0, angle, 0, degrees / 90, twist);
        assert(close(world(p, pinHinge(degrees / 90, twist)),
                     pinPivot(degrees / 90)));
        for (int n = 0; n <= 40; n++) {
          Vec point = world(p, pinHandlePoint(degrees / 90, n / 40.f, twist));
          assert(point.z - .0015f > .024f);
        }
      }
  assert(!pinUsable(0) && !pinUsable(1.7f) && pinQuality(1) == 1 &&
         pinQuality(1.4f) < .6f);
  // Falling pins request exactly one inventory operation. Failed requests
  // recover the same shape; successful ones provide only an available pin.
  {
    PhysicalInteraction t;
    t.config.requirePreparation = false;
    t.worldDrops = true;
    Pose p, d;
    d.p = {2, 2, 2};
    t.update({}, p, d, 0, 0, 0);
    p = grip(t.tools[0].pose, 0);
    t.update({}, p, d, 1, 0, 0);
    p.p = {0, .05f, .25f};
    t.update({}, p, d, 1, 0, 0);
    t.update({}, p, d, 0, 0, 0);
    unsigned requests = 0;
    for (int n = 0; n < 180; n++) {
      t.update({}, p, d, 0, 0, 0);
      requests += t.droppedEvents;
    }
    assert(requests == 1 && t.dropPending && !t.tools[0].visible);
    t.resolveDrop(false, 5);
    assert(t.tools[0].visible && t.tools[0].holstered &&
           t.tools[0].opening == 1);
    t.resolveDrop(true, 0);
    assert(!t.tools[0].visible);
    t.config.requirePreparation = true;
    t.resolveDrop(true, 2);
    assert(t.tools[0].visible && t.tools[0].opening == 0);
  }
  for (int h = 0; h < 2; h++)
    for (float tilt : {-60.f, -35.f, 0.f, 35.f, 60.f})
      for (float roll : {-90.f, 0.f, 90.f}) {
        PhysicalInteraction t;
        t.config.requirePreparation = false;
        Pose hands[2];
        hands[1 - h].p = {2, 2, 2};
        auto step = [&](float hold) {
          t.update({}, hands[0], hands[1], h == 0 ? hold : 0, h == 1 ? hold : 0,
                   0);
        };
        step(0);
        hands[h] = grip(t.tools[0].pose, 0);
        step(1);
        Quat q = multiply(axisAngle({0, 1, 0}, tilt * pi / 180),
                          axisAngle({0, 0, 1}, roll * pi / 180));
        Vec axis = rotate(q, {0, 0, 1});
        hands[h] = grip({q, mouth(0) + axis * .08f}, 0);
        step(1);
        hands[h] = grip({q, mouth(0)}, 0);
        step(1);
        assert(t.tools[0].inserted && !t.tools[0].inserting &&
               t.insertedEvents == 1);
        // Relax the wrist by 60 degrees: the raw tip is now far from the
        // aperture but the contacted pin must remain guided there.
        hands[h].q = multiply(q, axisAngle({1, 0, 0}, pi / 3));
        step(1);
        assert(t.tools[0].inserted);
        hands[h].p = hands[h].p - axis * .07f;
        step(1);
        assert(t.tools[0].inserted && t.insertedEvents == 0);
        step(0);
        assert(t.tools[0].inserted && t.pickHand == -1);
      }
  // Repeated releases generate one request each and a fresh reachable pin.
  {
    PhysicalInteraction t;
    t.worldDrops = true;
    Pose hand;
    auto step = [&](float hold) {
      t.update({}, hand, {{}, {2, 2, 2}}, hold, 0, 0);
    };
    step(0);
    for (int remaining = 6; remaining >= 0; remaining--) {
      hand = grip(t.tools[0].pose, 0, t.tools[0].opening);
      step(1);
      assert(t.tools[0].owner == 0);
      hand.p = {-.1f, .05f, .25f};
      step(1);
      step(0);
      unsigned requests = 0;
      for (int n = 0; n < 180; n++) {
        step(0);
        requests += t.droppedEvents;
      }
      assert(requests == 1 && t.dropPending);
      t.resolveDrop(true, remaining);
      assert(t.tools[0].visible == (remaining > 0));
      if (remaining > 0) {
        assert(t.tools[0].holstered);
        step(0);
      }
    }
  }
  puts("loose pickup, both-hand insertion, handle leverage, seated "
       "regrip, torque "
       "cancellation, gravity, unobstructed drops, recovery, native "
       "replacement and reset "
       "passed");
}
