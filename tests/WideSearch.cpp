// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
Pose pinGrip(Pose p) { return {p.q, world(p, handleCenter(0))}; }
int main() {
  Ergonomics settings;
  assert(settings.pinSearchDegrees == 270);
  settings.load("PinSearchDegrees=180\n");
  assert(settings.pinSearchDegrees == 180);
  settings.load("PinSearchDegrees=900\n");
  assert(settings.pinSearchDegrees == 270);
  settings.load("PinSearchDegrees=nan\n");
  assert(settings.pinSearchDegrees == 270);
  for (float span : {180.f, 225.f, 270.f}) {
    float previous = -1;
    for (int n = 0; n <= 1000; n++) {
      float physical = pinSearchMin(span) + span * n / 1000;
      float native = nativePinSearch(physical, span);
      assert(native > previous);
      previous = native;
      assert(std::abs(physicalPinSearch(native, span) - physical) < .0001f);
    }
    assert(nativePinSearch(pinSearchMin(span), span) == 0);
    assert(nativePinSearch(pinSearchMax(span), span) == 180);
  }
  assert(boundPinSearch(-89, 270, 225) == 225); // no teleport through lower gap
  assert(boundPinSearch(269, 270, -45) == -45);
  ToolShape shape(0);
  for (float angle = -45; angle <= 225; angle += 5) {
    Pose pin = engagedPinPose(angle, .067f, 1, 0),
         driver = seatedPose(1, 90, 0);
    float gap = pinDriverClearance(shape, pin, driver);
    assert(gap > 0);
    for (float cylinder : {0.f, 30.f, 60.f, 90.f}) {
      Pose p = turnWithCylinder(pin, cylinder),
           d = turnWithCylinder(driver, cylinder);
      assert(std::abs(pinDriverClearance(shape, p, d) - gap) < .00001f);
      assert(std::abs(pinPoseAngle(p, cylinder) - angle) < .001f);
    }
  }
  // v29 headset report: the old round blade proxy blocked clear pin motion.
  // Cover a central insertion and the recorded inclined, under-bent pin.
  for (bool trace : {false, true}) {
    ToolShape wire(0, trace ? .856f : 1.f, trace ? -.509f : 0.f);
    Pose start = trace ? Pose{unit({-.06053f,-.2787f,.20275f,-.93678f}),
                               {-.00079f,.01603f,-.04543f}} : engagedPinPose(90,.067f,1,0);
    Pose blade = seatedPose(1,90,0);
    Vec pivot = trace ? Vec{-.00217f,.00531f,.02779f} : Vec{0,-.0009f,.026f};
    if (!trace) start.p.y = -.0009f;
    for (float degrees : {-10.f,-5.f,5.f,10.f}) {
      Quat turn = axisAngle(normal(rotate(start.q,pinHinge(trace?.856f:1.f,trace?-.509f:0.f))),degrees*pi/180);
      Pose end{multiply(turn,start.q),pivot+rotate(turn,start.p-pivot)};
      for(int n=0;n<=100;n++)
        assert(pinDriverClearance(wire,interpolatePose(start,end,n/100.f),blade)>0);
      auto result=sweepToolContact(wire,start,end,blade,blade);
      assert(!result.hit && length(result.pin.p-end.p)<1e-6f);
      assert(length(rotate(result.pin.q,{0,1,0})-rotate(end.q,{0,1,0}))<1e-6f);
    }
  }
  assert(driverDistance({.0015f,0,.012f}) > .0008f); // clear beside flat blade
  assert(driverDistance({0,0,.012f}) < 0); // the blade still has solid contact
  // Swept contact catches a screwdriver moved through the pin, and permits
  // reversing away from that contact. No endpoint-only tunnel.
  Pose pin = engagedPinPose(90, .067f, 1, 0), driver = seatedPose(1, 90, 0),
       crossing = driver;
  crossing.p.y += .03f;
  auto hit = sweepToolContact(shape, pin, pin, driver, crossing);
  assert(hit.hit && pinDriverClearance(shape, hit.pin, hit.driver) > 0);
  auto away = sweepToolContact(shape, hit.pin, hit.pin, hit.driver, driver);
  assert(!away.hit && length(away.driver.p - driver.p) < .0001f);
  for (auto style : {LockStyle::Round, LockStyle::RoundedSafe, LockStyle::Padlock})
  for (int h = 0; h < 2; h++)
  // Each full unlock ends the real session. Start a fresh lock per solution
  // instead of carrying collision offsets from several completed turns.
  for (float target : {225.f, 90.f, -45.f, 90.f}) {
    PhysicalInteraction t;
    t.targetBounds.style = style;
    t.config.useHolders = false;
    t.config.requirePreparation = false;
    Pose hands[2];
    hands[1 - h].p = {2, 2, 2};
    t.update({}, hands[0], hands[1], 0, 0, 0);
    auto &p = t.tools[0];
    auto &d = t.tools[1];
    p.pose = pin;
    p.opening = 1;
    p.inserted = true;
    p.holstered = false;
    p.owner = h;
    p.pinContact = mouth(0);
    p.pinDepth = p.seatDepth = .067f;
    p.entryAxis = {0, 0, 1};
    p.seatPose = p.pose;
    hands[h] = pinGrip(p.pose);
    p.seatHand = hands[h].p;
    d.pose = d.seatPose = aimedTool({{}, {.07f, -.035f, .15f}}, mouth(1, style), driverInsertionDepth, style);
    d.inserted = true;
    d.holstered = false;
    d.owner = 1 - h;
    hands[1 - h] = {d.pose.q, world(d.pose, handleCenter(1))};
    d.leverStartHand = hands[1 - h].p;
    d.seatedRadius = length(hands[1 - h].p - mouth(1, style));
    t.grabs[0].held = t.grabs[1].held = true;
    d.driverTracking=true;d.driverTracked=hands[1-h];
      float from = t.pick;
      for (int n = 1; n <= 100; n++) {
        float a = from + (target - from) * n / 100;
        hands[h] = pinGrip(engagedPinPose(a, .067f, 1, 0));
        t.update({}, hands[0], hands[1], 1, 1, 0);
        if(!t.seated()) fprintf(stderr,"search failure style=%d hand=%d a=%.3f pick=%d driver=%d owner=%d blocked=%d pos %.5f %.5f %.5f\n",int(style),h,a,p.inserted,d.inserted,d.owner,d.blocked,d.pose.p.x,d.pose.p.y,d.pose.p.z);
        assert(t.seated());
        if(pinDriverClearance(shape,p.pose,d.pose)<=-.00001f) fprintf(stderr,"pair overlap style=%d hand=%d search=%.1f cylinder=%.1f depth=%.8f pin=%.6f,%.6f,%.6f driver=%.6f,%.6f,%.6f\n",int(style),h,t.pick,t.cylinderAngle,pinDriverClearance(shape,p.pose,d.pose),p.pose.p.x,p.pose.p.y,p.pose.p.z,d.pose.p.x,d.pose.p.y,d.pose.p.z);
        assert(pinDriverClearance(shape, p.pose, d.pose) > -.00001f);
        assert(ToolShape(0, 1, 0, t.targetBounds).clearance(p.pose, 0) > -.0004f);
        assert(ToolShape(1, 1, 0, t.targetBounds).clearance(d.pose, 0) > -.0004f);
        assert(std::abs(t.pick - pinPoseAngle(p.pose, 0)) < .001f);
      }
      if(std::abs(t.pick-target)>=.1f) fprintf(stderr,"search mismatch style=%d hand=%d actual=%.3f desired=%.1f demand=%.3f turn=%.3f\n",int(style),h,t.pick,target,d.driverDemand,t.target);
      assert(std::abs(t.pick - target) < .1f);
      // Wider-angle solutions must remain usable through the entire native
      // turn while the screwdriver hand loads the rotating keyway's side.
      for (int c = 0; c <= 90; c++) {
        float a=c*pi/180;
        hands[1-h].p=hands[1-h].p+Vec{std::cos(a),-std::sin(a),0}*(.060f/90);
        t.update({}, hands[0], hands[1], 1, 1, float(c), 1.f / 90, 3);
        assert(t.seated() && std::abs(t.pick - target) < .1f);
        if(pinDriverClearance(shape,p.pose,d.pose)<=-.00001f) fprintf(stderr,"pair overlap style=%d hand=%d search=%.1f cylinder=%.1f depth=%.8f pin=%.6f,%.6f,%.6f driver=%.6f,%.6f,%.6f\n",int(style),h,t.pick,t.cylinderAngle,pinDriverClearance(shape,p.pose,d.pose),p.pose.p.x,p.pose.p.y,p.pose.p.z,d.pose.p.x,d.pose.p.y,d.pose.p.z);
        assert(pinDriverClearance(shape, p.pose, d.pose) > -.00001f);
        assert(ToolShape(0, 1, 0, t.targetBounds).clearance(p.pose, c) > -.0004f);
        assert(ToolShape(1, 1, 0, t.targetBounds).clearance(d.pose, c) > -.0004f);
      }
      for(int c=90;c>=0;c--) {
        float a=c*pi/180;
        hands[1-h].p=hands[1-h].p-Vec{std::cos(a),-std::sin(a),0}*(.060f/90);
        t.update({},hands[0],hands[1],1,1,float(c),1.f/90,2);
      }
    hands[h].p.z += .10f;
    t.update({}, hands[0], hands[1], 1, 1, 0);
    assert(!p.inserted && p.owner == h);
  }
  puts("270-degree search, monotonic native mapping, lower-gap continuity, "
       "both-hand sweep and tool separation passed");
}
