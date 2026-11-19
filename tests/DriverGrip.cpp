// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include "LockMesh.h"
#include <cassert>
#include <cstdio>
using namespace locks;
int main() {
  // Holding a screwdriver must not rotate the entire displayed hand away
  // from the tracked controller frame, even for the reported wrist pose.
  Pose localGrip{unit({-.58640856f,0,0,.81001544f}),{.018f,-.037696f,-.0179185f}};
  Quat oldTool=unit({.02323f,.15991f,.24027f,.95716f});
  Pose controller{unit(multiply(oldTool,inverse(localGrip.q))),{.1f,.2f,.3f}};
  Pose shown=driverHandPose(controller),toolFrame=compose(shown,localGrip);
  assert(length(rotate(shown.q,{0,1,0})-rotate(controller.q,{0,1,0}))<.000001f);
  assert(length(rotate(toolFrame.q,{0,0,-1})-rotate(oldTool,{0,0,-1}))<.000001f);
  assert(length(shown.p-controller.p)==0);
  for(Quat turn:{axisAngle({1,0,0},.6f),axisAngle({0,1,0},-.7f),axisAngle({0,0,1},.9f)}) {
    Pose moved{unit(multiply(turn,controller.q)),controller.p+Vec{.1f,-.1f,.2f}};
    Pose calibrated=driverHandPose(moved);
    assert(length(calibrated.p-moved.p)==0);
    assert(length(rotate(calibrated.q,{0,0,1})-rotate(turn,rotate(shown.q,{0,0,1})))<.00001f);
  }

  // v39 recorded wrist at the slot: the shaft was upright, but the flat
  // blade was still horizontal. Only metal rotates; handle fit stays intact.
  Quat atSlot=unit({.20966f,.01255f,.11143f,.97132f});
  Vec oldBroad=rotate(atSlot,{1,0,0});
  Vec newBroad=rotate(atSlot,driverMetalPosition({1,0,0}));
  assert(std::abs(oldBroad.x)>.9f&&std::abs(newBroad.y)>.8f);
  for(float z:{driverHandleStart,driverLength}) {
    Vec p{.01f,.003f,z},n{1,0,0};
    assert(length(driverMetalPosition(p)-p)<1e-6f);
    assert(length(driverMetalNormal(p,n)-n)<1e-6f);
  }
  for(float z:{0.f,.02f,.042f,.07f,.112f,.182f}) {
    Vec p{.004f,.0006f,z},n{.6f,.8f,0};
    assert(finite(driverMetalPosition(p))&&finite(driverMetalNormal(p,n)));
    assert(std::abs(length(driverMetalNormal(p,n))-1)<1e-6f);
  }

  assert(keywayWidth(LockStyle::Padlock) == keywayWidth(LockStyle::Round));
  assert(keywayHeight(LockStyle::Padlock) == keywayHeight(LockStyle::Round));
  assert(keywayHeight(LockStyle::Padlock) < padPlugRadius);
  assert(driverInsertionDepth > .004f && driverInsertionDepth < driverBladeEnd);
  float worstHousing = 1, worstPin = 1;
  for (auto style : {LockStyle::Round, LockStyle::RoundedSafe, LockStyle::Padlock}) {
    TargetBounds bounds; bounds.style = style;
    ToolShape driver(1, 1, 0, bounds), pin(0, 1, 0, bounds);
    for (float x : {-.15f, -.07f, -.02f, 0.f, .02f, .07f, .15f})
      for (float y : {-.15f, -.035f, .035f})
        for (float z : {.08f, .15f, .22f}) {
          Pose tool = aimedTool({{}, {x,y,z}}, mouth(1,style), driverInsertionDepth, style);
          float clearance = driver.clearance(tool,0);
          assert(clearance > 0);
          worstHousing = std::min(worstHousing, clearance);
          for (float angle = -45; angle <= 225; angle += 5) {
            Pose pick = engagedPinPose(angle,.067f,1,0);
            float gap = pinDriverClearance(pin,pick,tool);
            assert(gap > .0015f);
            worstPin = std::min(worstPin,gap);
          }
          for (float cylinder : {0.f,45.f,90.f}) {
            Pose constrained = turnWithCylinder(tool,cylinder);
            assert(driver.clearance(constrained,cylinder) > 0);
            for (float side : {-1.f,1.f}) {
              Pose grip{axisAngle({1,side*.2f,0},-.1f), {side*.014f,-.019f,-.004f}};
              Pose tracked{axisAngle({1,1,0},1.2f), {side*.15f,.02f,.2f}};
              Pose before = tracked;
              Pose shown = presentDriverHand(tracked,constrained,grip,true,.035f,20);
              Pose actualGrip = compose(shown,grip);
              assert(length(actualGrip.p-world(constrained,handleCenter(1))) < 1e-6f);
              assert(length(rotate(actualGrip.q,{0,0,1})-rotate(constrained.q,{0,0,1})) < 1e-5f);
              assert(length(tracked.p-before.p)==0 && tracked.q.w==before.q.w);
              Pose limited = presentDriverHand(tracked,constrained,grip,false,.035f,20);
              assert(length(limited.p-tracked.p) <= .03501f);
            }
          }
        }
  }
  puts("Seated hand stays on handle under resisted movement; tracked input unchanged");
  printf("Deeper seat and full pin search clear both handed approaches: housing %.6f m, pin %.6f m\n",worstHousing,worstPin);
}
