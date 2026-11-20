// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
Pose grip(Pose tool) { return {tool.q, world(tool, handleCenter(1))}; }
void samePose(Pose a, Pose b) {
  assert(length(a.p-b.p)<.00001f);
  assert(length(rotate(a.q,{0,0,1})-rotate(b.q,{0,0,1}))<.0001f);
}
int main() {
  // A continuous press into the flat housing produces one impact. Separating
  // and pressing the same point again must produce a second impact.
  {
    PhysicalInteraction t;
    t.config.driverMode = DriverMode::SidePush;
    t.config.useHolders=false;
    Pose other{{},{2,2,2}}, h{};
    auto step=[&](float hold){t.update({},h,other,hold,0,0);return bool(t.housingHands&1);};
    step(0); h=grip(t.tools[1].pose);step(1);
    for (int pass=0;pass<2;++pass) {
      Pose blade{{},{.024f,0,.105f}};
      h=grip(blade);
      for (int n=0;n<90;++n) step(1);
      int taps=0;
      for (int n=0;n<175;++n) {
        blade.p.z-=.0007f;h=grip(blade);taps+=step(1);
      }
      Pose admitted=t.tools[1].pose;
      for (int n=0;n<60;++n) taps+=step(1);
      assert(taps==1 && length(t.tools[1].pose.p-admitted.p)<.0001f);
    }
  }
  for (auto style:{LockStyle::Round,LockStyle::RoundedSafe,LockStyle::Padlock})
  for (int h=0;h<2;h++) {
    PhysicalInteraction t;
    t.config.driverMode = DriverMode::SidePush;
    t.targetBounds.style=style;
    t.config.useHolders=false;t.config.requirePreparation=false;
    Pose hands[2];hands[1-h].p={2,2,2};
    auto step=[&](float hold,float cylinder=0,unsigned stage=2) {
      t.update({},hands[0],hands[1],h==0?hold:0,h==1?hold:0,cylinder,1.f/90,stage,1);
    };
    step(0);auto &driver=t.tools[1],&pin=t.tools[0];
    hands[h]=grip(driver.pose);step(1);assert(driver.owner==h);
    // All wrist poses remain possible away from the physical housing.
    for(float roll:{-110.f,-55.f,0.f,50.f,105.f,180.f,0.f}) {
      Pose free{axisAngle({0,0,1},roll*pi/180),{.20f,0,.35f}};
      hands[h]=grip(free);step(1);assert(driver.owner==h);samePose(driver.pose,free);
    }
    pin.holstered=false;pin.inserted=true;pin.pinDepth=.067f;
    pin.pose=engagedPinPose(90,.067f,1,0);
    Pose blade=aimedTool({{}, {h?.07f:-.07f,-.035f,.150f}},mouth(1,style));
    ToolShape shape(1,1,0,t.targetBounds);
    Pose outside=blade;outside.p.z+=.10f;hands[h]=grip(outside);step(1);
    samePose(driver.pose,outside);assert(!driver.inserted);
    hands[h]=grip(blade);step(1);
    assert(driver.owner==h&&driver.inserted&&t.target==0);
    // Native cylinder motion and both physical tools must stay coherent.
    for(int angle=1;angle<=90;angle++) {
      float a=(angle-1)*pi/180;
      hands[h].p=hands[h].p+Vec{std::cos(a),-std::sin(a),0}*(driverSideTravel/90);
      step(1,float(angle-1),3);
      if(!driver.inserted || (angle>1 && std::abs(t.target-angle)>.15f))
        fprintf(stderr,"turn style=%d h=%d angle=%d target=%.3f contact=%d clear=%.6f\n",int(style),h,angle,t.target,driver.inserted,shape.clearance(driver.pose,float(angle-1)));
      assert(driver.owner==h&&driver.inserted);
      Pose fitted = presentDriverHand(hands[h],driver.pose,{},true,0,0);
      assert(length(fitted.p-world(driver.pose,handleCenter(1)))<.00001f);
      assert(std::abs(length(world(driver.pose,{0,0,driverLength})-driver.pose.p)-driverLength)<.00001f);
      assert(angle<=1||std::abs(t.target-angle)<.15f);
      if(shape.clearance(driver.pose,float(angle-1))<=-.0003f) fprintf(stderr,"pivot clearance style=%d hand=%d angle=%d gap=%.8f tip=%.5f,%.5f,%.5f q=%.5f,%.5f,%.5f,%.5f pair=%.7f\n",int(style),h,angle,shape.clearance(driver.pose,float(angle-1)),driver.pose.p.x,driver.pose.p.y,driver.pose.p.z,driver.pose.q.x,driver.pose.q.y,driver.pose.q.z,driver.pose.q.w,pinDriverClearance(ToolShape(0,pin.opening,pin.bendYaw),pin.pose,driver.pose));
      assert(shape.clearance(driver.pose,float(angle-1))>-.0003f);
    }
    for(int angle=89;angle>=20;angle--) {
      float a=angle*pi/180;
      hands[h].p=hands[h].p-Vec{std::cos(a),-std::sin(a),0}*(driverSideTravel/90);
      step(1,float(angle),2);
    }
    // Hard resistance cannot tunnel, force a drop, or separate hand and handle.
    for(int angle=21;angle<=50;angle++) {
      hands[h].p=hands[h].p+Vec{std::cos(20*pi/180),-std::sin(20*pi/180),0}*(driverSideTravel/90);step(1,20,4);
      if(!(driver.owner==h&&driver.inserted&&t.target>20&&t.pinResisting)) fprintf(stderr,"resistance angle=%d owner=%d contact=%d target=%.5f strain=%d\n",angle,driver.owner,driver.inserted,t.target,t.pinResisting);
      assert(driver.owner==h&&driver.inserted&&t.target>20&&t.pinResisting);
      assert(shape.clearance(driver.pose,20)>-.0003f);
    }
    // Reverse translation responds immediately, even with a large angular
    // mismatch. It need not first undo the controller's blocked travel.
    Pose stopped=driver.pose;
    hands[h].p.z+=.003f;step(1,20,4);
    assert(driver.owner==h && grip(driver.pose).p.z>grip(stopped).p.z+.001f);
    hands[h].p.z+=.12f;step(1,20,2);
    assert(driver.owner==h&&!driver.inserted&&t.target==0);
    // Once both are clear, the hand/tool smoothly return to the tracked frame.
    for(int n=0;n<120;n++)step(1,20,2);
    samePose(driver.pose,heldTool(hands[h],1));
    // Approach with the recorded headset orientation at the LOWER slot and push through the internal
    // bearing by 60 mm. Only real controller motion passes the stop.
    Pose reported{unit({.20966f,h?.01255f:-.01255f,h?.11143f:-.11143f,.97132f}),
                  {h?.00067f:-.00067f,mouth(1,style).y,.00994f}};
    Pose front=reported;front.p.z+=.12f;hands[h]=grip(front);step(1,0,2);
    for(int n=0;n<120;n++)step(1,0,2);
    bool bearing=false;
    for(int n=0;n<=180;n++) {
      Pose push=reported;push.p.z+=.12f-n*.001f;
      hands[h]=grip(push);step(1,0,2);
      assert(driver.owner==h);
      assert(shape.clearance(driver.pose,0)>-.0003f);
      bearing|=driver.blocked&&driver.inserted;
    }
    if(!bearing||!driver.inserted) fprintf(stderr,"bearing style=%d h=%d tip %.5f %.5f %.5f clear %.7f pair %.7f pin %.5f %.5f %.5f\n",int(style),h,driver.pose.p.x,driver.pose.p.y,driver.pose.p.z,shape.clearance(driver.pose,0),pinDriverClearance(ToolShape(0,pin.opening,pin.bendYaw),pin.pose,driver.pose),pin.pose.p.x,pin.pose.p.y,pin.pose.p.z);
    assert(bearing&&driver.inserted);
    assert(driver.pose.p.z>=mouth(1,style).z-driverBearingDepth-.0003f);
    assert(length(grip(driver.pose).p-hands[h].p)>.04f);
    stopped=driver.pose;hands[h].p.z+=.002f;step(1,0,2);
    fprintf(stderr,"backoff style=%d h=%d z %.7f -> %.7f clear %.7f contact %.7f\n",int(style),h,grip(stopped).p.z,grip(driver.pose).p.z,shape.clearance(driver.pose,0),pinDriverClearance(ToolShape(0,pin.opening,pin.bendYaw),pin.pose,driver.pose));
    assert(grip(driver.pose).p.z>grip(stopped).p.z+.001f);
    hands[h].p.z+=.08f;step(1,0,2);
    assert(driver.owner==h&&!driver.inserted&&t.target==0);
    for(int n=0;n<120;n++)step(1,0,2);
    // Releasing the button drops the driver and leaves the supported pin.
    step(0,0,2);assert(driver.owner==-1&&!driver.inserted&&pin.inserted);
    float y=world(driver.pose,toolCenter(1)).y;
    for(int n=0;n<15;n++)step(0,0,2);
    assert(world(driver.pose,toolCenter(1)).y<y-.001f);
    // Face contact away from the keyway cannot apply unlocking torque.
    for(Vec delta:{Vec{.024f,0,0},Vec{0,.024f,0}}) {
      Pose a=blade;a.p=a.p+delta+Vec{0,0,.10f};Pose b=a;b.p.z-=.15f;
      auto contact=moveAgainstHousing(shape,a,b,0);
      assert(contact.hit&&shape.clearance(contact.pose,0)>-.0003f);
      assert(!driverContact(contact.pose,0,style).touching);
    }
  }
  // A natural, near-straight approach puts the grip inside 25 mm of the
  // cylinder centreline. Translating the handle sideways with NO wrist twist
  // must still apply torque; the old legacy radius gate discarded it.
  for (auto style:{LockStyle::Round,LockStyle::RoundedSafe,LockStyle::Padlock})
  for (int h=0;h<2;h++) {
    PhysicalInteraction t;
    t.config.driverMode = DriverMode::SidePush;
    t.targetBounds.style=style;
    t.config.useHolders=false;t.config.requirePreparation=false;
    Pose hands[2];hands[1-h].p={2,2,2};
    auto step=[&](float hold,float cylinder=0,unsigned stage=2) {
      t.update({},hands[0],hands[1],h==0?hold:0,h==1?hold:0,cylinder,1.f/90,stage,1);
    };
    step(0);hands[h]=grip(t.tools[1].pose);step(1);
    auto &driver=t.tools[1],&pin=t.tools[0];
    pin.holstered=false;pin.inserted=true;pin.pinDepth=.067f;
    pin.pose=engagedPinPose(90,.067f,1,0);
    Pose blade{{},mouth(1,style)-Vec{0,0,.009f}},outside=blade;
    outside.p.z+=.10f;hands[h]=grip(outside);step(1);
    hands[h]=grip(blade);step(1);
    assert(driver.inserted&&driver.owner==h&&t.target==0);
    assert(std::hypot(hands[h].p.x,hands[h].p.y)<.025f);
    Quat heldWrist=hands[h].q;
    for(float roll:{10.f,-10.f,0.f}) {
      hands[h].q=unit(multiply(axisAngle({0,0,1},roll*pi/180),heldWrist));
      step(1);assert(driver.owner==h && driver.inserted && t.target==0);
    }
    // Downward and axial movements at an upright slot must not turn it.
    hands[h].p.y-=.002f;step(1);assert(t.target==0);
    hands[h].p.y+=.002f;step(1);assert(t.target==0);
    hands[h].p.z-=.003f;step(1);assert(t.target==0);
    hands[h].p.z+=.003f;step(1);assert(t.target==0);
    Pose leverStart=driver.pose;
    Vec handleStart=grip(leverStart).p;
    for(int n=0;n<6;n++) {hands[h].p.x+=.0005f;step(1);}
    // Hand right, working tip left, through the actual offset entry point.
    // The native cylinder stays at zero here: this is blade leverage, not
    // displacement caused by an already rotating lock.
    assert(grip(driver.pose).p.x>handleStart.x+.001f);
    assert(driver.pose.p.x<leverStart.p.x-.00005f);
    assert(std::abs(driver.pose.p.z-leverStart.p.z)<.001f);
    fprintf(stderr,"straight lever style=%d hand=%d target=%.3f contact=%d\n",int(style),h,t.target,driver.inserted);
    assert(driver.inserted&&driver.owner==h&&std::abs(t.target-22.5f)<.02f);
    float requested=t.target;
    for(int n=0;n<40;n++)step(1);
    assert(std::abs(t.target-requested)<.05f); // holding still cannot wind up
    hands[h].p.x-=.003f;step(1);
    assert(t.target<2.f); // counter-pressure unloads without a wrist twist
    // Recorded native failure: a completed gesture accumulated 89.9998,
    // leaving the game below its exact completion endpoint indefinitely.
    driver.driverDemand=89.9998f;step(1);
    assert(t.target==90.f);
    hands[h].p.z+=.10f;step(1);
    assert(driver.owner==h&&!driver.inserted&&t.target==0);
  }
  // A blade can catch in the rotated aperture while being pulled out.
  // Withdrawal must unload native pressure before escape is geometrically
  // possible, so a stopped lock cannot keep damaging the pin during the pull.
  for(auto style:{LockStyle::Round,LockStyle::RoundedSafe,LockStyle::Padlock})
  for(int h=0;h<2;h++) {
    PhysicalInteraction t;
    t.config.driverMode = DriverMode::SidePush;
    t.targetBounds.style=style;
    t.config.useHolders=false;t.config.requirePreparation=false;
    Pose hands[2];hands[1-h].p={2,2,2};
    auto step=[&](float c,unsigned stage=2) {
      t.update({},hands[0],hands[1],h==0?1:0,h==1?1:0,c,1.f/90,stage,1);
    };
    t.update({},hands[0],hands[1],0,0,0);
    hands[h]=grip(t.tools[1].pose);step(0);
    auto &pin=t.tools[0],&d=t.tools[1];
    pin.holstered=false;pin.inserted=true;pin.pinDepth=.067f;
    pin.pose=engagedPinPose(90,.067f,1,0);
    Pose blade{{},mouth(1,style)-Vec{0,0,.009f}},outside=blade;
    outside.p.z+=.10f;hands[h]=grip(outside);step(0);
    hands[h]=grip(blade);step(0);assert(d.inserted);
    for(int c=0;c<75;c++) {
      float a=c*pi/180;
      hands[h].p=hands[h].p+Vec{std::cos(a),-std::sin(a),0}*(driverSideTravel/90);
      step(float(c),3);
    }
    hands[h].p=hands[h].p+Vec{std::cos(75*pi/180),-std::sin(75*pi/180),0}*.004f;
    step(75,4);assert(d.inserted && t.target>75 && t.pinResisting);
    for(int n=0;n<8;n++){hands[h].p.z+=.0005f;step(75,4);}
    assert(d.owner==h && t.target==0 && !t.pinResisting);
    for(int c=74;c>=0;c--)step(float(c));
    for(int n=0;n<100;n++){hands[h].p.z+=.001f;step(0);}
    assert(d.owner==h && !d.inserted && t.target==0 && pin.inserted);
  }
  // A braced tool preserves the actual off-centre entry, never a prefab
  // socket. Axial withdrawal and pure wrist changes retain their normal path.
  for(float x:{-.002f,.001f}) {
    Pose original{{},{x,-.011f,.014f}};
    Pose request=original;request.p.x+=.003f;
    Pose lever=driverLeverPose(original,request,0,LockStyle::Round);
    Vec axis=rotate(lever.q,{0,0,1});
    Vec entry=lever.p+axis*((.024f-lever.p.z)/axis.z);
    assert(std::abs(entry.x-x)<.00001f && std::abs(entry.y+.011f)<.00001f);
    assert(lever.p.x<original.p.x && grip(lever).p.x>grip(original).p.x);
    assert(length(lever.p-original.p)<.001f);
    Pose pull=original;pull.p.z+=.01f;
    samePose(driverLeverPose(original,pull,0,LockStyle::Round),pull);
    Pose wrist=heldTool({axisAngle({0,0,1},.2f),grip(original).p},1);
    samePose(driverLeverPose(original,wrist,0,LockStyle::Round),wrist);
    Pose outside=original;outside.p.z=.030f;
    request=outside;request.p.x+=.003f;
    samePose(driverLeverPose(outside,request,0,LockStyle::Round),request);
  }
  // Seated safe entry sampled in v51 (tick 280400784). Apply a controlled
  // side stroke with a fixed wrist and check actual tip depth during the turn.
  {
    PhysicalInteraction session;
    session.config.driverMode = DriverMode::SidePush;
    session.targetBounds.style = LockStyle::RoundedSafe;
    session.config.useHolders = false;
    session.config.requirePreparation = false;
    Pose unused{{}, {2,2,2}}, right{};
    float cylinder = 0;
    auto tick = [&](float hold) { session.update({},unused,right,0,hold,cylinder); };
    tick(0);
    auto &driver = session.tools[1], &pin = session.tools[0];
    right = grip(driver.pose); tick(1);
    pin.holstered = false; pin.inserted = true;
    pin.opening = .934f; pin.bendYaw = 1.672f; pin.pinDepth = .0724f;
    pin.pose = {unit({.58391f,-.55004f,.44423f,.39895f}),{-.00328f,.01623f,-.04161f}};
    Pose blade{unit({-.04203f,.19127f,.04055f,.97980f}),{-.00004f,-.00618f,.01697f}};
    Pose approach = blade; approach.p.z += .12f;
    right = grip(approach); tick(1);
    right = grip(blade); tick(1);
    for (int frame = 0; frame < 500 && cylinder < 90; ++frame) {
      right.p = right.p + driverLoadDirection(cylinder)*.0001f;
      tick(1);
      assert(driver.inserted && driver.pose.p.z < .024f);
      cylinder = std::min(cylinder+1,session.target);
    }
    assert(cylinder == 90);
  }
  // The control frame is the keyway, including tilted or wall-mounted locks.
  // No XY hand radius or axial wrist angle participates in the calculation.
  // The same millimetre of side travel loads and unloads 7.5 degrees
  // anywhere on the arc. Pushing axially or holding still adds no turn.
  for (int angle = 0; angle <= 90; ++angle) {
    Vec side = driverLoadDirection(float(angle));
    assert(std::abs(driverSideTurn(side*.001f,float(angle))-7.5f)<.0001f);
    assert(std::abs(driverSideTurn(side*-.001f,float(angle))+7.5f)<.0001f);
    assert(std::abs(driverSideTurn({-side.y*.001f,side.x*.001f,0},float(angle)))<.0001f);
    assert(driverSideTurn({0,0,.01f},float(angle))==0);
    assert(driverSideTurn({},float(angle))==0);
  }
  puts("Both hands/all styles: solid bearing and face, persistent grip, full native turn, resistance, immediate backoff after overtravel, clear-space reconciliation and button-release gravity passed");
}
