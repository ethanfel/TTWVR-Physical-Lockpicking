// SPDX-License-Identifier: GPL-3.0-only
#include "MountedInteraction.h"
#include "LockRules.h"
#include <cassert>
using namespace locks;
int main() {
 // Breaking successive pins resets the plug and durability. Drawing from the
 // holster re-enters the same lock even if its temporary mount was released.
 {
   LiveLockPuzzle puzzle;
   int rolls = 0;
   auto roll = [&] { return ++rolls == 1 ? .72f : .24f; };
   constexpr uint32_t officeDoor = 101, otherDoor = 102;
   for (int pin = 0; pin < 3; ++pin) {
     LockRules rules;
     rules.width = pin == 0 ? 500.f : 1000.f;
     rules.pickPixels = 20;
     rules.driverPixels = 10;
     rules.breakSeconds = 1;
     rules.center = puzzle.centerFor(officeDoor, rules.width, 20, roll);
     assert(rolls == 1 && rules.limit(.72f * 180) == 90);
     float health = 1;
     LockTurn turn;
     const float pick = pin == 2 ? .72f * 180 : 15;
     for (int frame = 0; frame < 300; ++frame)
       turn.step(rules, pick, 90, 1.f / 90, health);
     if (pin < 2) assert(turn.stage == 5 && health == 0);
     else assert(turn.cylinder == 90 && health == 1);
   }
   assert(puzzle.centerFor(otherDoor, 500, 20, roll) == 120 && rolls == 2);
   puzzle = {}; // confirmed unlock or new game/load
   assert(puzzle.centerFor(otherDoor, 500, 20, roll) == 120 && rolls == 3);
 }

 for(auto shape:{LockStyle::Round,LockStyle::Padlock,LockStyle::RoundedSafe}){
  LiveContact c;
  LiveTool tool{{.014f,0,.06f},{0,0,1},{},{0,1,0}};
  auto contact = [&](float x, float y, float z, bool driver=false, float angle=0.f) {
    tool.tip={x,y,z}; c.update(tool,driver,1,angle,shape);
  };
  contact(.014f,0,.06f);
  contact(.014f,0,-.070f);
  assert(!c.inside && c.surface==LiveSurface::Face && c.shown.z>=plugFaceZ);
  auto face=c.shown.z;
  for (int i=0;i<8;++i) contact(.014f,0,-.07f);
  assert(std::abs(c.shown.z-face)<1e-6f);
  contact(.001f,.0027f,-.07f);
  assert(std::abs(c.shown.z-face)<1e-5f);
  contact(.001f,.0027f,-.075f);
  assert(c.inside && std::abs(c.shown.z-(face-.005f))<1e-5f);
  contact(.001f,.0027f,.06f);
  assert(!c.inside && length(c.offset)<1e-6f);
  contact(.001f,.0027f,.010f);
  assert(c.inside && length(c.offset)<1e-6f);
  contact(.001f,.0027f,-.085f);
  assert(c.inside && c.surface==LiveSurface::Bottom);
  const float bottom=c.shown.z;
  assert(std::abs(bottom - (plugFaceZ - plugKeywayDepth + pinWireRadius)) < 1e-5f);
  contact(.001f,.0027f,-.12f);
  assert(c.inside && std::abs(c.shown.z-bottom)<1e-6f);
  contact(.017f,.0027f,.010f);
  assert(c.inside && c.surface==LiveSurface::Wall && c.shown.x<keywayWidth(shape));
  contact(.017f,.0027f,.06f);
  assert(!c.inside && length(c.offset)<1e-6f);
  contact(.12f,0,-.02f);
  assert(!c.inside && length(c.offset)<1e-6f);
  c={};contact(0,-.007f,.060f,true);contact(0,-.007f,-.060f,true);
  assert(c.inside && c.surface==LiveSurface::Bottom && c.shown.z>0);
  contact(0,-.007f,.06f,true);assert(!c.inside && length(c.offset)<1e-6f);
  // The metal shelf above the actual slot used to admit the wire.
  for (float a : {0.f, 42.f, 90.f}) {
    c={};
    auto toWorld=axisAngle({0,0,1},-a*pi/180);
    Vec shelf=rotate(toWorld, {0,.015f,0});
    contact(shelf.x,shelf.y,.06f,false,a);
    contact(shelf.x,shelf.y,-.04f,false,a);
    assert(!c.inside && c.surface==LiveSurface::Face && c.shown.z>=plugFaceZ);
  }
  if (shape==LockStyle::Padlock) {
    MountSurfaceGrid steel;
    steel.depths.fill(-1500);
    steel.measureHeight();
    for (int hand : {0,1}) for (float tilt : {-20.f,0.f,27.f}) {
      MountedInteraction run;
      run.physics.targetBounds.style=shape;
      run.physics.targetBounds.panel=&steel;
      constexpr float opening=.85f, scale=.84f;
      Quat rot=multiply(axisAngle({0,1,0},tilt*pi/180),pinAlignment(opening,0));
      Vec axis=normal(rotate(rot,pinHinge(opening,0)));
      Vec at=mouth(0);
      LiveTool wire{{},axis,{},rotate(rot,{0,1,0}),scale,rotate(rot,{1,0,0})};
      for (int mm=-75; mm<=63; ++mm) {
        wire.tip=at-axis*(mm*.001f);
        wire.grip=wire.tip+rotate(rot,handleCenter(0,opening)*scale);
        run.step(&wire,nullptr,opening,0,false,hand,0,2,1,1.f/90,1,true);
      }
      assert(run.pin && run.physics.tools[0].pinDepth>pinShaftLength*scale*.91f);
      run.step(nullptr,nullptr,opening,0,true,hand,0,2,1,1.f/90,1,true);
      assert(run.pin);
    }
  }
  // Advance diagonally across metal. The raw endpoint is in the hole,
  // but the swept tip encountered the face first.
  c={};contact(.035f,0,.034f);contact(0,0,-.024f);
  assert(!c.inside && c.surface==LiveSurface::Face);
  // Start at the rear of an unselected lock; no correction through its back.
  c={};contact(0,0,-.08f);assert(!c.inside && length(c.offset)<1e-6f);
  // With the working end outside the housing the rest of the wire can
  // still lie across the plug, or across its outer frame.
  c={};tool.shaft={-1,0,0};tool.up={0,1,0};tool.across={0,0,1};
  contact(.065f,.012f,.065f);contact(.065f,.012f,-.008f);
  assert(!c.inside && c.surface==LiveSurface::Face && c.shown.z>=plugFaceZ);
  contact(.065f,.012f,.07f);assert(length(c.offset)<1e-6f);
  // A pin twisted back toward the face can make contact held-end first.
  c={};tool.shaft={1,0,0};tool.up={0,0,-1};tool.across={0,1,0};
  contact(-.064f,.003f,.12f);contact(-.064f,.003f,.055f);
  assert(c.surface==LiveSurface::Face && !c.inside);
  sampleLiveWire(tool,1,[&](Vec at,bool){
    Vec point=c.shown+at;
    assert(point.z+1e-5f>=liveFront(point,pinWireRadius,shape)+pinWireRadius);
  });
  contact(-.064f,.003f,.13f);assert(length(c.offset)<1e-6f);
  // Glancing entry at a turned slot must leave the held wire outside.
  c={};tool.shaft=normal({.09f,0,1});tool.up=normal({.75f,.1f,-.65f});
  tool.across=normal(cross(tool.up,tool.shaft));
  contact(0,0,.075f,false,44);contact(0,0,-.035f,false,44);
  assert(c.inside);
  sampleLiveWire(tool,1,[&](Vec at,bool working){
    Vec point=c.shown+at;
    Vec slot=rotate(axisAngle({0,0,1},44*pi/180),point);
    if(working && std::abs(slot.x)<=keywayWidth(shape)-pinWireRadius+.00001f &&
       std::abs(slot.y)<=keywayHeight(shape)-pinWireRadius+.00001f)return;
    assert(point.z+1e-5f>=liveFront(point,pinWireRadius,shape)+pinWireRadius);
  });
  // The player is in front but the mount was acquired after a fast push.
  // Make contact on that face and require a real approach before insertion.
  c={};tool.shaft={0,0,1};tool.up={0,1,0};tool.across={1,0,0};
  tool.tip={0,.0027f,-.13f};
  c.update(tool,false,1,0,shape,true);
  assert(c.surface==LiveSurface::Face && !c.inside && c.shown.z>=plugFaceZ);
  tool.tip.z=.05f;c.update(tool,false,1,0,shape,true);
  tool.tip.z=.016f;c.update(tool,false,1,0,shape,true);
  assert(c.inside && length(c.offset)<1e-6f);
  // The steel beside the hardware is lower than the lock face. Allow the
  // pin to reach that actual surface and leave the edge of the container.
  MountSurfaceGrid panel;
  panel.depths.fill(INT16_MIN);
  for (int y=25;y<70;++y)
    for (int x=0;x<70;++x) panel.depths[y*panel.side+x]=-2500;
  auto panelContact=[&](float x,float z,bool driver=false){
    tool.tip={x,.0027f,z};c.update(tool,driver,1,0,shape,true,&panel);
  };
  for (bool driver : {false,true}) {
    c={};panelContact(.082f,.05f,driver);panelContact(.082f,-.10f,driver);
    assert(c.surface==LiveSurface::Face && std::abs(c.shown.z+.025f)<.002f);
    panelContact(.082f,.05f,driver);assert(c.surface==LiveSurface::Air);
    panelContact(.152f,-.04f,driver);assert(c.surface==LiveSurface::Air);
    // A shallow entry leaves the slot if the hand moves across the lock.
    c={};panelContact(0,.05f,driver);panelContact(0,.019f,driver);
    assert(c.inside);
    panelContact(.035f,.018f,driver);
    assert(!c.inside && c.surface==LiveSurface::Face && c.shown.x>.034f);
    panelContact(.082f,.018f,driver);assert(length(c.offset)<1e-5f);
  }
  // Release pressure after sliding from metal into the bore. A small pull
  // must move the part of the tool that actually entered on the last push.
  for (bool driver : {false,true}) {
    c={};
    contact(.016f,0,.060f,driver);
    contact(.016f,0,-.030f,driver);
    contact(0,0,-.030f,driver);
    float rim=c.shown.z;
    contact(0,0,-.034f,driver);
    assert(std::abs(c.shown.z - (rim-.004f))<1e-5f);
    contact(0,0,-.031f,driver);
    assert(std::abs(c.shown.z - (rim-.001f))<1e-5f);
    contact(0,0,.05f,driver);
    assert(!c.inside && c.surface==LiveSurface::Air);
  }
  if (shape==LockStyle::Padlock) {
    float atStaple=stapleY+stapleRadius*.9f;
    float ringDepth=std::sqrt(std::pow(stapleRadius+stapleWire,2)-
                              std::pow(atStaple-stapleY,2));
    c={};contact(0,atStaple,.04f);contact(0,atStaple,-.016f);
    assert(c.surface==LiveSurface::Face && c.shown.z>=ringDepth);
    c={};contact(.012f,atStaple,.04f);contact(.012f,atStaple,-.016f);
    assert(c.surface==LiveSurface::Air); // beside the projecting ring
  }
  // Check the early exits against an exhaustive walk of the same rigid wire.
  panel.depths.fill(-1700);panel.depths[40*panel.side+58]=7000;
  panel.measureHeight();
  for (int n=0;n<400;++n) {
    float t=n*.19f, opening=float(n%19)/10.f;
    auto rotation=axisAngle(normal({.31f,std::sin(t),.41f}),t);
    LiveTool bent{{.071f,.004f*std::cos(t),.06f+.073f*std::sin(t)},
                  rotate(rotation,normal(pinHinge(opening,t))),{},
                  rotate(rotation,{0,1,0}),.7f+float(n%7)*.1f,
                  rotate(rotation,{1,0,0})};
    LiveContact actual;
    actual.update(bent,false,opening,37,shape,true,&panel);
    float lift=0, radius=pinWireRadius*bent.scale;
    sampleLiveWire(bent,opening,[&](Vec delta,bool){
      auto pt=bent.tip+delta;
      float z=std::max(panel.front(pt),liveFront(pt,radius,shape));
      lift=std::max(lift,z+radius-pt.z);
    });
    assert(std::abs(actual.offset.z-lift)<.00003f);
  }
  for (int owner : {0,1}) {
    MountedInteraction mounted;
    PhysicalInteraction close;
    close.config.driverMode = DriverMode::SidePush;
    close.config.useHolders=false;
    close.config.requirePreparation=false;
    mounted.physics.config=close.config;
    mounted.physics.targetBounds.style=close.targetBounds.style=shape;
    Pose hands[2]{};
    hands[1-owner].p={2,2,2};
    float held[2]{};
    auto reference=[&] { close.update({},hands[0],hands[1],held[0],held[1],0); };
    reference();
    mounted.step(nullptr,nullptr,1,0,false,owner,0,2,1,1.f/90,1,true);
    Pose home=close.tools[0].pose;
    LiveTool wire;
    auto sample=[&](Pose raw,bool holding) {
      hands[owner]={raw.q,world(raw,handleCenter(0))};
      held[owner]=holding;
      wire={raw.p,rotate(raw.q,{0,0,1}),hands[owner].p,
            rotate(raw.q,{0,1,0}),1,rotate(raw.q,{1,0,0})};
      reference();
      mounted.step(holding ? &wire : nullptr,nullptr,1,0,!holding,owner,
                   0,2,1,1.f/90,1,true);
      const auto &actual=mounted.physics.tools[0];
      const auto &expected=close.tools[0];
      assert(actual.inserted==expected.inserted && actual.owner==expected.owner);
      assert(length(actual.pose.p-expected.pose.p)<.00005f);
      assert(length(rotate(actual.pose.q,{0,1,0})-rotate(expected.pose.q,{0,1,0}))<.001f);
      assert(std::abs(mounted.pick-close.pick)<.1f);
    };
    sample(home,true);
    Pose raw{{},mouth(0)+Vec{0,0,.09f}};
    sample(raw,true);
    raw.p.z=mouth(0).z+.006f;sample(raw,true);
    raw.p.z-=.012f;sample(raw,true);
    assert(mounted.pin);
    float initial=mounted.pick;
    for(int i=0;i<20;++i) {raw.p.x+=.001f; sample(raw,true);}
    assert(std::abs(mounted.pick-initial)>4);
    sample(raw,false);
    assert(mounted.pin && mounted.physics.pickHand<0);
    // With the same actually seated pin left in place, drive both presentations
    // from the same blade samples and native cylinder feedback.
    {
      auto ordinary=close;
      auto live=mounted;
      int d=1-owner;
      Pose controls[2]{};
      controls[owner].p={2,2,2};
      auto driverStep=[&](Pose blade,float cylinder,unsigned stage) {
        controls[d]={blade.q,world(blade,handleCenter(1))};
        ordinary.update({},controls[0],controls[1],d==0,d==1,cylinder,1.f/90,stage);
        LiveTool tool{blade.p,rotate(blade.q,{0,0,1}),controls[d].p,
                      rotate(blade.q,{0,1,0}),1,rotate(blade.q,{1,0,0})};
        live.step(nullptr,&tool,1,0,true,owner,cylinder,stage,1,1.f/90,1,true);
        assert(live.pin && live.blade==ordinary.tools[1].inserted);
        assert(std::abs(live.pick-ordinary.pick)<.1f);
        assert(std::abs(live.demand-ordinary.target)<.05f);
        assert(length(live.shown[1].p-ordinary.tools[1].pose.p)<.0001f);
        assert(length(rotate(live.shown[1].q,{0,0,1})-
                      rotate(ordinary.tools[1].pose.q,{0,0,1}))<.001f);
      };
      driverStep(ordinary.tools[1].pose,0,2);
      Pose blade=aimedTool({{}, {d?.07f:-.07f,-.035f,.150f}},mouth(1,shape));
      Pose above=blade;above.p.z+=.10f;
      driverStep(above,0,2);driverStep(blade,0,2);
      assert(live.blade);
      for (int degree=0;degree<65;++degree) {
        blade.p=blade.p+driverLoadDirection(degree)*(driverSideTravel/90.f);
        driverStep(blade,float(degree),3);
      }
      assert(live.demand>60);
      for (int i=0;i<8;++i) {
        blade.p=blade.p+driverLoadDirection(64)*.0002f;
        driverStep(blade,64,4);
      }
      assert(live.physics.pinResisting && ordinary.pinResisting &&
             std::abs(live.physics.pinTremor-ordinary.pinTremor)<.001f);
      blade.p.z+=.13f;driverStep(blade,64,2);
      assert(live.demand==0 && !live.blade);
    }
    auto supported=close.tools[0].pose;
    sample(supported,true);
    supported.p.z+=.16f;
    sample(supported,true);
    assert(!mounted.pin);
  }
 }
 LockRules rules{1000,500,2,1,100,100};
 assert(rules.limit(90)==90 && rules.limit(100)==75 && rules.limit(110)==30);
 LockTurn turn;
 float health=1;
 assert(turn.step(rules,0,90,.01f,health)&Turn);
 unsigned sounds=0;
 for(int i=0;i<120 && turn.stage!=4;++i) sounds|=turn.step(rules,0,90,1.f/90,health);
 assert(turn.stage==4 && (sounds&Tension) && (sounds&Stop));
 assert(turn.step(rules,0,90,.1f,health)==0 && std::abs(health-.95f)<1e-5f);
 assert(turn.step(rules,0,0,.01f,health)&ReleaseTension);
 turn={};health=1;sounds=0;
 while (turn.stage!=4) sounds|=turn.step(rules,0,90,.01f,health);
 for(int n=0;n<220;++n) sounds|=turn.step(rules,0,90,.01f,health);
 assert(turn.stage==5 && health==0 && (sounds&Break) && (sounds&ReleaseTension));
 assert(turn.step(rules,0,90,.01f,health)==0);
 turn={};health=1;sounds=0;
 for(int i=0;i<120;++i) sounds|=turn.step(rules,90,90,1.f/90,health);
 assert(turn.cylinder==90 && health==1 && sounds==(Turn|SqueakA|SqueakB));
}
