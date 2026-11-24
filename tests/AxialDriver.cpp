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
  // Check the stroke in player metres across close-up and world mount scales.
  for (float metresPerUnit : {.7f, 1.f, 1.3f}) {
    auto c = close;
    auto m = live;
    ForcePalms palms;
    palms.open = 1u << pinHand;
    palms.metresPerUnit = metresPerUnit;
    auto forceStep = [&](float behind, float angle, float pull = 0.f) {
      Pose raw{axisAngle({0,0,1},-sign*angle*pi/180), blade.p+Vec{0,0,pull/metresPerUnit}};
      Pose h[2]{hands[0],hands[1]};
      h[driverHand] = {raw.q,world(raw,handleCenter(1))};
      palms.point[pinHand] = c.tools[1].seatPose.p + Vec{0,0,driverLength+behind/metresPerUnit};
      LiveTool tool{raw.p,rotate(raw.q,{0,0,1}),h[driverHand].p,rotate(raw.q,{0,1,0}),
                    1,rotate(raw.q,{1,0,0})};
      c.update({},h[0],h[1],driverHand==0,driverHand==1,0,.01f,2,1,nullptr,nullptr,&palms);
      m.step(nullptr,&tool,1,0,true,pinHand,0,2,1,.01f,metresPerUnit,true,&palms);
      assert(m.physics.forceEvent == c.forceEvent && std::abs(c.target-m.demand)<.001f);
      assert(length(c.tools[1].pose.p - m.shown[1].p) < .0001f);
      return c.forceEvent;
    };
    c.config.enableForceLock = m.physics.config.enableForceLock = false;
    forceStep(.085f,0);
    assert(forceStep(.025f,0) == ForceGesture::None);
    c.config.enableForceLock = m.physics.config.enableForceLock = true;
    assert(forceStep(.025f,0) == ForceGesture::None); // initial overlap
    forceStep(.085f,0);
    palms.open=0; forceStep(.045f,0); // tracking loss, regrip or button press
    palms.open=1u<<pinHand;
    assert(forceStep(.025f,0) == ForceGesture::None);
    forceStep(.085f,0);
    assert(forceStep(.025f,0) == ForceGesture::Knock);
    const Vec forcedPosition = driver.pose.p - Vec{0,0,.012f/metresPerUnit};
    assert(length(c.tools[1].pose.p - forcedPosition) < .0001f);
    assert(length(c.tools[0].pose.p - parked.p) < .0001f);
    assert(forceStep(.075f,0,.008f) == ForceGesture::None); // backing out cancels
    assert(forceStep(.075f,20,.008f) == ForceGesture::None && c.target>0);
    forceStep(.09f,0);
    for (int mm=88; mm>32; mm-=2) assert(forceStep(mm*.001f,0) == ForceGesture::None);
    // A second tap can rearm forcing but cannot drive the tool past its first stop.
    bool knocked=false;
    for (int mm=32; mm>=20; --mm) knocked |= forceStep(mm*.001f,0) == ForceGesture::Knock;
    assert(knocked && length(c.tools[1].pose.p - forcedPosition) < .0001f);
    assert(forceStep(.021f,12) == ForceGesture::None && c.target == 0);
    forceStep(.085f,12); // establish a fresh angle after separation
    assert(forceStep(.085f,8) == ForceGesture::None);
    assert(forceStep(.085f,25) == ForceGesture::None);
    assert(forceStep(.085f,29) == ForceGesture::Attempt && c.target == 0);
    for (int i=0; i<10; ++i) assert(forceStep(.085f,29) == ForceGesture::None);
  }
  const Vec seated = driver.pose.p;
  // A player can lean on a seated blade while twisting against resistance.
  // Neither lateral movement nor pressing into the stop withdraws the tip.
  const Pose heldAtSeat = blade;
  for (Vec offset : {Vec{.080f, 0, 0}, Vec{0, 0, -.080f}}) {
    blade.p = heldAtSeat.p + offset;
    step();
    assert(driver.inserted && !close.withdrawnEvents && close.target == 0);
    blade.q = axisAngle({0,0,1}, -sign*15*pi/180);
    step(14, 4);
    assert(driver.inserted && !close.withdrawnEvents && !close.insertedEvents);
    assert(std::abs(close.target - 30.f) < .01f);
    assert(length(driver.pose.p - turnWithCylinder({{},seated}, sign*14).p) < .0001f);
    blade = heldAtSeat;
    step();
    assert(driver.inserted && !close.insertedEvents && close.target == 0);
  }
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
  blade.p.z = plugFaceZ + .002f; step();
  blade.p.z = plugFaceZ - .015f; step();
  assert(driver.inserted && close.insertedEvents == 2 && close.target == 0);
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

// Germantown door: the moving cylinder left the parked pin touching an
// unseated blade at the front face. A pull along Z used to produce no motion.
static void withdrawPastParkedPin() {
  PhysicalInteraction sim;
  sim.config.load("UseToolHolders=0\nRequirePinPreparation=0\n");
  sim.update({}, {}, {}, 0, 0, 0);
  auto &pin = sim.tools[0];
  pin.visible = pin.inserted = true;
  pin.holstered = false;
  pin.opening = 1.174f;
  pin.bendYaw = 1.640f;
  pin.pose = {unit({-.20740f,-.72044f,-.22384f,.62277f}), {-.00303f,-.01050f,-.04711f}};
  pin.seatPose = pin.pose;
  pin.pinDepth = .0747f;
  auto &driver = sim.tools[1];
  driver.owner = 1;
  driver.holstered = false;
  driver.pose = {unit({-.00908f,-.07384f,-.00440f,-.99722f}), {.01031f,.00814f,.02493f}};
  const auto start = driver.pose;
  ToolShape wire(0,pin.opening,pin.bendYaw), blade(1);
  blade.driverStopZ = plugFaceZ - .018f;
  const Pose parked = pin.pose;
  const float initialGap = pinDriverClearance(wire, parked, start);
  assert(initialGap < 0 && initialGap > -.0001f);
  Pose requested = start;
  for (int frame = 0; frame < 45; ++frame) {
    if (frame < 35) requested.p.z += .001f;
    sim.update({}, {}, {requested.q,world(requested,handleCenter(1))}, 0, 1, 0);
    assert(driver.owner == 1 && !driver.inserted && pin.inserted && sim.target == 0);
    assert(length(pin.pose.p-parked.p) < .00001f);
    assert(blade.clearance(driver.pose,0) >= -.00001f);
    assert(pinDriverClearance(wire, parked, driver.pose) >= initialGap - .00001f);
    if (frame == 9) assert(driver.pose.p.z > start.p.z + .004f);
  }
  assert(driver.pose.p.z > start.p.z + .03f);
}

// A controller can keep moving after the rigid tool hits the door. Its
// first motion back out must move the tool even while the hand is behind it.
static void easeOffDoorFace(float dt) {
  PhysicalInteraction sim;
  sim.config.load("UseToolHolders=0\nRequirePinPreparation=0\n");
  sim.update({}, {}, {}, 0, 0, 0);
  auto &driver = sim.tools[1];
  driver.owner = 1;
  driver.holstered = false;
  Pose handTool{{},{.022f,0,.060f}};
  driver.pose = handTool;
  auto step = [&] {
    sim.update({}, {}, {handTool.q, world(handTool,handleCenter(1))}, 0, 1, 0, dt);
    assert(driver.owner == 1 && !driver.inserted && sim.target == 0);
  };
  for (int i = 0; i < 75; ++i) { handTool.p.z -= .001f; step(); }
  const Vec stopped = driver.pose.p;
  assert(stopped.z > plugFaceZ && handTool.p.z < 0);
  for (int i = 0; i < 30; ++i) {
    step();
    assert(length(driver.pose.p - stopped) < .0004f);
  }
  for (int i = 0; i < 8; ++i) { handTool.p.y += .001f; step(); }
  assert(driver.pose.p.y > stopped.y + .007f && driver.pose.p.z > plugFaceZ);
  handTool.p.z += .004f; step();
  assert(driver.pose.p.z > stopped.z + .003f);
  for (int i = 0; i < 40; ++i) { handTool.p.z += .003f; step(); }
  for (int i = 0; i < 30; ++i) step();
  assert(length(driver.pose.p - handTool.p) < .0001f);
}

int main() {
  ForceGesture gesture;
  Pose cap{};
  auto move=[&](Vec palm, float wrist=0.f, bool neutral=true) {
    return gesture.step(1,cap,{axisAngle({0,0,1},wrist*pi/180),{}},palm,1,1,neutral);
  };
  move({0,0,.080f});
  cap.p.z=.052f;
  assert(move({0,0,.080f})==ForceGesture::None); // handle advances into resting palm
  cap={}; move({0,0,.080f});
  assert(gesture.step(1,cap,{{},{0,0,-.055f}},{0,0,.025f},1,1,true)==ForceGesture::None);
  move({0,0,.08f}); // shared two-hand translation cannot count as a tap
  assert(move({.060f,0,.04f})==ForceGesture::None);
  assert(move({.002f,0,.024f})==ForceGesture::None); // side brush
  move({0,0,.08f});
  assert(move({0,0,.025f},9)==ForceGesture::None); // wrist already twisting
  move({0,0,.08f});
  assert(move({0,0,.025f},0,false)==ForceGesture::None); // normal torque
  move({0,0,.08f});
  assert(move({0,0,.025f})==ForceGesture::Knock);
  move({0,0,.08f});
  assert(move({0,0,.08f},22)==ForceGesture::None); // wrong direction
  assert(move({0,0,.08f},-22)==ForceGesture::None);
  withdrawPastParkedPin();
  for (float hz : {45.f, 90.f, 120.f}) easeOffDoorFace(1.f / hz);
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
  assert(cfg.enableForceLock && cfg.forceLockChanceOverride == -1);
  cfg.load("EnableForceLock=0\nForceLockChanceOverride=100");
  assert(!cfg.enableForceLock && cfg.forceLockChanceOverride == 100);
  cfg.load("EnableForceLock=1\nForceLockChanceOverride=0");
  assert(cfg.enableForceLock && cfg.forceLockChanceOverride == 0);
  cfg.load("ForceLockChanceOverride=37");
  assert(cfg.forceLockChanceOverride == 37);
  cfg.load("ForceLockChanceOverride=37.5");
  assert(cfg.forceLockChanceOverride == -1);
  cfg.load("ForceLockChanceOverride=101");
  assert(cfg.forceLockChanceOverride == -1);
  cfg.load("FreeScrewdriver=0\nScrewdriverMode=1"); assert(cfg.driverMode == DriverMode::AxialTwist);
  cfg.load("ScrewdriverMode=0\nFreeScrewdriver=0"); assert(cfg.driverMode == DriverMode::SidePush);
  for (auto style : {LockStyle::Round, LockStyle::Padlock, LockStyle::RoundedSafe})
    for (int hand : {0,1})
      for (bool centered : {false, true})
        for (int sign : {1,-1}) trial(style, hand, centered, sign);
}
