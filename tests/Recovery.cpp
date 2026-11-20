// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
Pose grasp(Pose p,float opening,float twist=0) {
  return {p.q,world(p,handleCenter(0,opening,twist))};
}
float qgap(Quat a,Quat b) {return 1-std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);}
int main() {
  // Actual v15 headset fixture, both still engaged and already withdrawn but
  // trapped by the sweep. A short deliberate front pull must work either way.
  for(int h=0;h<2;h++)for(bool inserted:{false,true}) {
    PhysicalInteraction t;t.config.useHolders=false;
    Pose hands[2];hands[1-h].p={2,2,2};
    auto step=[&](float hold){t.update({},hands[0],hands[1],h==0?hold:0,h==1?hold:0,0);};
    step(0);auto &p=t.tools[0];p.holstered=false;p.inserted=inserted;
    p.opening=1.290f;p.bendYaw=-.449f;
    p.pose={unit({-.02769f,-.18307f,-.24259f,-.95230f}),{.00299f,-.01496f,-.00196f}};
    p.pinContact={-.00236f,-.00367f,.02971f};p.pinDepth=.0338f;
    p.entryAxis=normal(rotate(p.pose.q,pinHinge(p.opening,p.bendYaw)));
    hands[h]=grasp(p.pose,p.opening,p.bendYaw);step(1);assert(t.pickHand==h);
    hands[h].p.z+=.065f;unsigned recovered=0,withdrawn=0;
    for(int n=0;n<90;n++){step(1);recovered|=t.recovered;withdrawn|=t.withdrawnEvents;}
    assert(!p.inserted && !p.inserting && !p.blocked && p.owner==h);
    assert(length(world(p.pose,handleCenter(0,p.opening,p.bendYaw))-hands[h].p)<.002f);
    if(inserted) assert(recovered || withdrawn);
    for(int n=0;n<90;n++){step(1);assert(!p.inserted);}
    // Reinsert the same pin after clearing the face; no menu/inventory reset.
    Quat q=pinAlignment(p.opening,p.bendYaw);
    hands[h]=grasp({q,mouth(0)+Vec{0,0,.08f}},p.opening,p.bendYaw);step(1);
    hands[h]=grasp({q,mouth(0)},p.opening,p.bendYaw);step(1);assert(p.inserted);
  }
  // v26 headset trace: an angled pin in the small ammo-box keyway had
  // 36 microns of positive clearance, but even valid shaft rotation froze.
  for (int h = 0; h < 2; h++) {
    PhysicalInteraction t; t.config.useHolders = false;
    t.targetBounds.enabled = true; t.targetBounds.style = LockStyle::Padlock;
    t.targetBounds.low = {-.14217f, -.25486f, -.52288f};
    t.targetBounds.high = {.14211f, .11953f, -.01315f};
    Pose hands[2]; hands[1-h].p = {2, 2, 2};
    auto step = [&](float hold) {
      t.update({}, hands[0], hands[1], h == 0 ? hold : 0, h == 1 ? hold : 0, 0);
    };
    step(0);
    auto &p = t.tools[0]; p.holstered = false; p.inserted = true;
    p.opening = .893f; p.bendYaw = -.468f;
    p.pose = {unit({-.01344f, -.20376f, .09376f, -.97443f}),
              {.00272f, .00657f, -.03464f}};
    p.pinContact = {.00077f, -.00220f, .02878f}; p.pinDepth = .064f;
    p.entryAxis = normal(rotate(p.pose.q, pinHinge(p.opening, p.bendYaw)));
    Pose start = p.pose;
    ToolShape shape(0, p.opening, p.bendYaw, t.targetBounds);
    Quat roll = axisAngle(p.entryAxis, -10 * pi / 180);
    Pose desired{multiply(roll, start.q), p.pinContact + rotate(roll, start.p-p.pinContact)};
    assert(shape.clearance(start, 0) > .00003f && shape.clearance(desired, 0) > 0);
    auto freeRoll = moveAgainstHousing(shape, start, desired);
    for (int n = 0; n < 30; n++) freeRoll = moveAgainstHousing(shape, freeRoll.pose, desired);
    assert(std::abs(pinPoseAngle(freeRoll.pose, 0)-pinPoseAngle(desired, 0)) < .1f);
    // Exercise real grab/regrip and hand movement, not just the collider.
    hands[h] = grasp(p.pose, p.opening, p.bendYaw); step(1);
    assert(p.owner == h);
    hands[h] = grasp(desired, p.opening, p.bendYaw);
    for (int n = 0; n < 90; n++) {
      step(1);
      assert(shape.clearance(p.pose, 0) > -.00015f);
      assert(std::abs(t.pick-pinPoseAngle(p.pose, 0)) < .01f);
    }
    printf("headset contact hand=%d angle=%.3f -> %.3f\n", h, pinPoseAngle(start,0), t.pick);
    assert(p.inserted && t.pick > pinPoseAngle(start, 0)+5);
    step(0); hands[h] = grasp(p.pose, p.opening, p.bendYaw); step(1);
    hands[h].p.z += .09f;
    for (int n = 0; n < 90; n++) step(1);
    assert(!p.inserted && !p.inserting && p.owner == h && !p.blocked);
  }
  // A supported pin's offset contact and orientation follow the rotating
  // keyway, even after releasing it. Its search angle stays cylinder-relative.
  {
    PhysicalInteraction t;t.config.useHolders=false;t.update({}, {}, {},0,0,0);
    auto &p=t.tools[0];p.opening=1;p.holstered=false;p.inserted=true;
    p.pose={axisAngle({0,0,1},-.4f),{.002f,.009f,-.01f}};
    p.pinContact={.002f,.009f,.026f};p.seatPose=p.pose;p.entryAxis={0,0,1};
    Pose original=p.pose;Vec contact=p.pinContact;float angle=pinPoseAngle(original,0);
    for(float cylinder:{0.f,10.f,35.f,60.f,90.f,45.f,0.f}) {
      t.update({},{{},{2,2,2}},{{},{-2,2,2}},0,0,cylinder);
      Pose expected=turnWithCylinder(original,cylinder);
      assert(length(p.pose.p-expected.p)<.00002f && qgap(p.pose.q,expected.q)<.00001f);
      assert(length(p.pinContact-turnWithCylinder({{},contact},cylinder).p)<.00002f);
      assert(std::abs(t.pick-angle)<.001f);
    }
  }
  for(int h=0;h<2;h++) {
    PhysicalInteraction t;t.config.useHolders=false;t.config.requirePreparation=false;
    Pose hands[2];hands[1-h]={{},{0,-.0027f,.155f}};
    t.update({},hands[0],hands[1],0,0,0);
    auto &p=t.tools[0];auto &d=t.tools[1];
    p.pose={Quat{},{.001f,.006f,-.004f}};p.pinContact={.001f,.006f,.026f};p.pinDepth=.03f;
    p.inserted=true;p.holstered=false;p.owner=h;p.seatPose=p.pose;p.entryAxis={0,0,1};
    hands[h]=grasp(p.pose,1);p.seatHand=hands[h].p;p.seatDepth=p.pinDepth;
    d.inserted=true;d.holstered=false;d.owner=1-h;d.pose=aimedTool(hands[1-h],mouth(1));
    hands[1-h]={d.pose.q,world(d.pose,handleCenter(1))};
    t.config.driverMode=DriverMode::SidePush;
    d.driverTracking=true;d.driverTracked=hands[1-h];
    d.seatedRadius=length(hands[1-h].p-mouth(1));
    t.grabs[0].held=t.grabs[1].held=true;
    Pose original=p.pose;Vec originalHand=hands[h].p;
    for(int n=0;n<60;n++) {
      float cyl=n*.5f;
      float a=cyl*pi/180;
      hands[1-h].p=hands[1-h].p+Vec{std::cos(a),-std::sin(a),0}*(.060f/180);
      hands[h].p=originalHand+Vec{.025f*std::sin(n*.2f),.010f*std::cos(n*.2f),0};
      t.update({},hands[0],hands[1],1,1,cyl,1.f/90,3);
      Pose expected=turnWithCylinder(original,cyl);
      assert(p.inserted && length(p.pose.p-expected.p)<.00002f && qgap(p.pose.q,expected.q)<.00001f);
      assert(std::abs(t.pick-90)<.001f);
    }
    // Release tension; movement under pressure must not jump the chosen angle.
    hands[1-h].p.z+=.10f;t.update({},hands[0],hands[1],1,1,0,1.f/90,2);
    assert(!d.inserted && t.target==0);
    assert(std::abs(t.pick-90)<.001f);
    float first=t.pick;
    for(int n=0;n<30;n++) {
      hands[h].p.x+=.0007f;t.update({},hands[0],hands[1],1,1,0);
      assert(std::abs(t.pick-std::clamp(pinPoseAngle(p.pose,0),0.f,180.f))<.001f);
    }
    assert(std::abs(t.pick-first)>5);
  }
  // The same physical angle at different offsets must request the same native
  // angle. Hand position/old centre cannot independently move the solution.
  for(float y:{-.010f,.0027f,.010f}) {
    PhysicalInteraction t;t.config.useHolders=false;t.config.requirePreparation=false;
    Pose other{{},{2,2,2}},hand;t.update({},hand,other,0,0,0);
    hand=grasp(t.tools[0].pose,1);t.update({},hand,other,1,0,0);
    Quat q=axisAngle({0,0,1},-.35f);
    hand=grasp({q,{.001f,y,.09f}},1);t.update({},hand,other,1,0,0);
    hand=grasp({q,{.001f,y,.026f}},1);t.update({},hand,other,1,0,0);
    assert(t.tools[0].inserted && std::abs(t.pick-(90+.35f*180/pi))<.001f);
  }
  // Pulling out is not a fresh pickup: keep the supported orientation even
  // when the wrist was relaxed or regripped, then follow wrist rotation.
  for(int h=0;h<2;h++) for(float opening:{.8f,1.f,1.3f}) {
    PhysicalInteraction t;t.config.useHolders=false;t.config.requirePreparation=false;
    Vec anchor{h?-.03872f:.03872f,-.03996f,-.02932f};
    Pose tracked[2];tracked[0].p=tracked[1].p={2,2,2};
    auto step=[&](float hold) {
      Pose pinches[2]{pinGripPose(tracked[0],anchor,0),pinGripPose(tracked[1],anchor,1)};
      t.update({},tracked[0],tracked[1],h==0?hold:0,h==1?hold:0,0,1.f/90,2,1,pinches);
    };
    auto trackTool=[&](Pose tool) {
      Quat q=unit(multiply(tool.q,inverse(pinGripOrientation(h))));
      return Pose{q,world(tool,handleCenter(0,opening))-rotate(q,anchor)};
    };
    step(0);auto &pin=t.tools[0];pin.opening=opening;
    pin.pose=holderTool(t.config,0,opening);tracked[h]=trackTool(pin.pose);step(1);
    assert(pin.owner==h);
    Quat q=pinAlignment(opening,0);
    tracked[h]=trackTool({q,mouth(0)+Vec{0,0,.08f}});step(1);
    tracked[h]=trackTool({q,mouth(0)});step(1);assert(pin.inserted);
    step(0);tracked[h]={Quat{},world(pin.pose,handleCenter(0,opening))-anchor};step(1);
    assert(pin.inserted&&pin.owner==h);
    Pose beforePull=pin.pose;
    tracked[h].p.z+=.25f;
    for(int n=0;n<60;n++)step(1);
    assert(!pin.inserted&&!pin.inserting&&pin.owner==h);
    assert(qgap(pin.pose.q,beforePull.q)<.000001f);
    Quat wristTurn=axisAngle({0,1,0},.5f);
    Vec pinch=world(tracked[h],anchor);
    tracked[h].q=wristTurn;
    tracked[h].p=pinch-rotate(wristTurn,anchor);
    step(1);
    assert(qgap(pin.pose.q,multiply(wristTurn,beforePull.q))<.000001f);
    assert(length(world(pin.pose,handleCenter(0,opening))-world(tracked[h],anchor))<.0001f);
  }
  puts("headset wedge extraction/reuse, rotating contact, loaded pin, visible/native angle agreement passed");
}
