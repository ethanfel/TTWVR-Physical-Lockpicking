// SPDX-License-Identifier: GPL-3.0-only
#include "MountedInteraction.h"
#include <cassert>

using namespace locks;

static void trial(LockStyle style, int driverHand, bool centered, int sign) {
  MountedInteraction live;
  PhysicalInteraction close;
  close.config.load("UseToolHolders=0\nRequirePinPreparation=0\n");
  live.physics.config = close.config;
  close.targetBounds.style = live.physics.targetBounds.style = style;
  close.doorTurnSign = live.physics.doorTurnSign = sign;
  const int pinHand = 1 - driverHand;
  Pose hands[2];
  hands[pinHand].p = {1, 1, 1};
  close.update({}, hands[0], hands[1], 0, 0, 0);
  live.step(nullptr, nullptr, 1, 0, false, pinHand, 0, 2, 1, .01f, 1, true);

  // Draw, insert and release an actual pin in both presentations.
  Pose pin = close.tools[0].pose;
  auto placePin = [&](bool held) {
    hands[pinHand] = {pin.q, world(pin, handleCenter(0))};
    LiveTool wire{pin.p, rotate(pin.q, {0,0,1}), hands[pinHand].p, rotate(pin.q,{0,1,0}), 1, rotate(pin.q,{1,0,0})};
    close.update({}, hands[0], hands[1], pinHand == 0 && held, pinHand == 1 && held, 0);
    live.step(held ? &wire : nullptr, nullptr, 1, 0, !held, pinHand, 0, 2, 1, .01f, 1, true);
    assert(close.tools[0].inserted == live.pin);
  };
  placePin(true);
  pin = {{}, mouth(0) + Vec{0,centered ? .007f : 0.f,.07f}}; placePin(true);
  pin.p.z = mouth(0).z; placePin(true);
  pin.p.z -= .06f; placePin(true);
  placePin(false);
  assert(live.pin && close.tools[0].owner == -1);
  const Pose parked = close.tools[0].pose;
  Pose blade = close.tools[1].pose;
  auto step = [&](float cylinder = 0, unsigned stage = 2) {
    hands[driverHand] = {blade.q, world(blade, handleCenter(1))};
    LiveTool tool{blade.p, rotate(blade.q,{0,0,1}), hands[driverHand].p,
                  rotate(blade.q,{0,1,0}), 1, rotate(blade.q,{1,0,0})};
    close.update({}, hands[0], hands[1], driverHand == 0, driverHand == 1, cylinder, .01f, stage);
    live.step(nullptr, &tool, 1, 0, true, pinHand, cylinder, stage, 1, .01f, 1, true);
    assert(live.pin && close.tools[0].inserted && live.blade == close.tools[1].inserted);
    assert(std::abs(live.demand - close.target) < .001f);
    assert(length(live.shown[1].p - close.tools[1].pose.p) < .0001f);
    assert(std::abs(close.pick - live.pick) < .01f);
  };
  step();
  auto &driver = close.tools[1];
  blade = {{},{.025f, centered ? 0.f : -.005f, .12f}}; step();
  blade.p.z = .004f; step();
  assert(!driver.inserted && driver.blocked);
  blade.p.z = .12f; step();
  blade.p.x = 0; step();
  blade.p.z = plugFaceZ + .002f; step();
  for (float depth : {.002f, .005f, .008f, .003f, -.002f, .004f, .009f, .013f}) {
    blade.p.z = plugFaceZ - depth; step();
    assert(!driver.inserted && close.target == 0);
    assert(length(driver.pose.p - blade.p) < .0001f);
    assert(length(close.tools[0].pose.p - parked.p) < .0001f);
  }
  blade.p.z = plugFaceZ - .015f; step();
  assert(driver.inserted && driver.owner == driverHand && close.insertedEvents == 2);
  const Vec seated = driver.pose.p;
  blade.p.x += .012f; step();
  assert(close.target == 0);
  blade.p.x -= .012f;
  blade.q = axisAngle({1,0,0}, .16f); step();
  assert(close.target == 0);
  // Wrong-way rotation, even beyond quaternion wrap, cannot open this door.
  for (int angle : {25, 90, 175, 185, 220, 175, 90, 25, 0}) {
    blade.q = axisAngle({0,0,1}, sign*angle*pi/180); step();
    assert(close.target == 0);
  }
  blade.q = axisAngle({0,0,1}, -sign*22.5f*pi/180); step();
  assert(std::abs(close.target - 45) < .01f);
  // A stationary wrist with a few mm of tracking drift holds its progress.
  const float depth = blade.p.z;
  for (int i=0; i<60; ++i) {
    blade.p.z = depth + .005f + .0003f*std::sin(float(i));
    step(45,3); assert(std::abs(close.target-45) < .01f);
  }
  blade.p.z = depth; step(22,4);
  assert(close.pinResisting && live.physics.pinResisting);
  assert(std::abs(wristRoll({},driver.pose.q) - sign*22) < .01f);
  assert(length(driver.pose.p - turnWithCylinder({{},seated},sign*22).p) < .0001f);
  blade.q = {}; step(0); assert(close.target == 0);
  blade.q = axisAngle({0,0,1}, -sign*45*pi/180); step();
  assert(close.target == 90);
  for(int a = 0; a <= 90; a += 3) {
    step(float(a),3);
    assert(close.target == 90 && driver.inserted && close.tools[0].inserted);
    assert(pinDriverClearance(ToolShape(0,1,0,close.targetBounds), close.tools[0].pose, driver.pose) > -.0001f);
  }
  // A seated tool can be picked up at a different wrist angle without
  // issuing fresh torque. The close-up keeps the actual partly withdrawn pose.
  PhysicalInteraction regrip = close;
  auto updateGrip = [&](float grip, float cylinder) {
    regrip.update({}, hands[0], hands[1], driverHand == 0 ? grip : 0,
                  driverHand == 1 ? grip : 0, cylinder);
  };
  updateGrip(0, 60);
  auto &parkedDriver = regrip.tools[1];
  assert(parkedDriver.inserted && parkedDriver.owner == -1 && regrip.target == 0);
  hands[driverHand] = {axisAngle({0,0,1}, .7f), world(parkedDriver.pose, handleCenter(1))};
  updateGrip(1, 60);
  assert(parkedDriver.owner == driverHand && std::abs(regrip.target-60) < .01f);
  hands[driverHand].p.z += .005f; updateGrip(1, 60);
  float partialZ = parkedDriver.pose.p.z;
  updateGrip(0, 60); updateGrip(0, 60);
  assert(parkedDriver.inserted && std::abs(parkedDriver.pose.p.z-partialZ)<.0001f);
  // Back out along the shaft. Tension drops immediately and the
  // visible tool follows the hand through intermediate depths.
  blade.q = {}; step(0);
  blade.p.z += .007f; step();
  assert(driver.inserted && close.target == 0 && driver.pose.p.z > seated.z+.006f);
  blade.p.z += .03f; step();
  assert(!driver.inserted && close.target == 0 && close.withdrawnEvents == 2);
}

// Ammo-box close-up, after the plug returned to zero around a tilted blade.
static void wedgedBlade(bool held) {
  PhysicalInteraction sim;
  sim.targetBounds.style = LockStyle::Padlock;
  sim.targetBounds.enabled = true;
  sim.targetBounds.low = {-.25f, -.25f, -.25f};
  sim.targetBounds.high = {.25f, .25f, -.013f};
  sim.update({}, {}, {}, 0, 0, 0);
  auto &pin = sim.tools[0];
  pin.visible = pin.inserted = true;
  pin.holstered = false;
  pin.opening = .940f;
  pin.bendYaw = 1.378f;
  pin.pose = {unit({.25025f, -.51490f, .54550f, .61211f}), {.00826f, -.00778f, -.03988f}};
  pin.seatPose = pin.pose;
  pin.pinDepth = .0707f;

  auto &driver = sim.tools[1];
  driver.pose = {unit({.22841f, -.04475f, .54769f, -.80366f}), {-.00114f, -.01411f, .01900f}};
  driver.holstered = false;
  driver.owner = held ? 1 : -1;
  driver.velocity = {0, -1.22f, 0};
  Pose requested = driver.pose;
  requested.p.z += .12f;
  Pose hand{requested.q, world(requested, handleCenter(1))};
  ToolShape blade(1, 1, 0, sim.targetBounds);
  assert(blade.clearance(driver.pose, 0) < -.0001f);
  for (int frame = 0; frame < 12; ++frame) {
    sim.update({}, {}, hand, 0, held ? 1 : 0, 0, .05f);
    assert(!driver.inserted && pin.inserted && sim.target == 0);
    assert(blade.clearance(driver.pose, 0) >= 0);
    if (held) assert(length(driver.pose.p - requested.p) < .0001f);
  }
  if (!held) assert(length(driver.velocity) < .15f);
}

int main() {
  wedgedBlade(true);
  wedgedBlade(false);
  Ergonomics cfg;
  assert(cfg.driverMode == DriverMode::AxialTwist && cfg.fullTurnWrist == 45);
  TargetBounds door;
  door.enabled = true;
  door.low.x = -.2f; door.high.x = 1.2f;
  assert(doorTwistDirection(door,false,0) == 1 && doorTwistDirection(door,true,0) == -1);
  assert(doorTwistDirection(door,true,.9f) == 1); // opposite face has its own anchor
  door.low.y=-.8f; door.high.y=1.2f;
  door.low.z=-.12f; door.high.z=-.013f;
  Vec delta{.03f,.04f,-.09f};
  Pose reverse = reverseDoorPose(delta);
  Vec surface{0,0,-.013f};
  assert(length(world(reverse, surface+delta)-surface) < .00001f);
  auto back = reverseDoorBounds(door,reverse);
  assert(back.low.x < -1.1f && back.high.x < .25f && back.low.y < back.high.y &&
         back.low.z < back.high.z && back.high.z < 0);
  assert(doorTwistDirection(back,false,0) == doorTwistDirection(door,true,delta.x));
  door.low.x = -1.2f; door.high.x = .2f;
  assert(doorTwistDirection(door,false,0) == -1 && doorTwistDirection(door,true,0) == 1);
  door.low.x = -.2f;
  assert(doorTwistDirection(door,false,0) == 1 && doorTwistDirection(door,true,0) == 1);
  cfg.load("FreeScrewdriver=0"); assert(cfg.driverMode == DriverMode::SeatedLever);
  cfg.load("FreeScrewdriver=0\nScrewdriverMode=1"); assert(cfg.driverMode == DriverMode::AxialTwist);
  cfg.load("ScrewdriverMode=0\nFreeScrewdriver=0"); assert(cfg.driverMode == DriverMode::SidePush);
  for (auto style : {LockStyle::Round, LockStyle::Padlock, LockStyle::RoundedSafe})
    for (int hand : {0,1})
      for (bool centered : {false, true})
        for (int sign : {1,-1}) trial(style, hand, centered, sign);
}
