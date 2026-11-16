// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
#include <cstdio>
using namespace locks;
Pose gripAt(Pose p, float opening) {
  return {p.q, world(p, handleCenter(0, opening))};
}
float qdistance(Quat a, Quat b) {
  return 1 - std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
}
int main() {
  // Neither approaching orientation nor lateral position is canonicalized.
  for (int h=0;h<2;h++)
    for (float opening : {65.f/90, 1.f, 1.5f})
      for (float tilt : {-35.f, 0.f, 35.f}) {
        PhysicalInteraction t;
        t.config.useHolders=false;
        Pose hand[2];hand[1-h].p={2,2,2};
        auto step=[&](float hold){t.update({},hand[0],hand[1],h==0?hold:0,h==1?hold:0,0);};
        step(0);
        auto &pin=t.tools[0];pin.opening=opening;
        pin.pose=holderTool(t.config,0,opening,0);
        hand[h]=gripAt(pin.pose,opening);step(1);
        Quat q=multiply(axisAngle({1,0,0},tilt*pi/180),
                 multiply(axisAngle({0,0,1},.45f),pinAlignment(opening,0)));
        Pose approach{q,{.001f,.007f,.026f}};
        Pose outside=approach;outside.p.z+=.06f;
        hand[h]=gripAt(outside,opening);step(1);
        hand[h]=gripAt(approach,opening);step(1);
        assert(pin.inserted);
        assert(length(pin.pose.p-approach.p)<.00015f);
        assert(qdistance(pin.pose.q,approach.q)<.00001f);
        // It also stays put on the next frame; no delayed snap.
        step(1);
        assert(length(pin.pose.p-approach.p)<.00015f);
        assert(qdistance(pin.pose.q,approach.q)<.00001f);
        step(0);
        hand[h]=gripAt(pin.pose,opening);hand[h].q=axisAngle({0,0,1},1.1f);
        Pose supported=pin.pose;step(1);
        assert(length(pin.pose.p-supported.p)<.0002f && qdistance(pin.pose.q,supported.q)<.00001f);
        hand[h].p=hand[h].p+pin.entryAxis*.10f;step(1);
        assert(!pin.inserted && t.pickHand==h);
      }
  // The restored original keyhole is wider/taller, with the same wire
  // clearance margin. Contacts just inside work; just outside cannot latch.
  for (Vec tip : {Vec{.005f, .024f, .026f}, Vec{-.005f, -.024f, .026f},
                  Vec{.008f, 0, .026f}, Vec{0, .031f, .026f}}) {
    PhysicalInteraction t;
    t.config.useHolders = false; t.config.requirePreparation = false;
    Pose other{{}, {2, 2, 2}}, hand;
    t.update({}, hand, other, 0, 0, 0);
    hand = gripAt(t.tools[0].pose, 1); t.update({}, hand, other, 1, 0, 0);
    hand = gripAt({{}, tip + Vec{0, 0, .06f}}, 1); t.update({}, hand, other, 1, 0, 0);
    hand = gripAt({{}, tip}, 1); t.update({}, hand, other, 1, 0, 0);
    bool inside = std::abs(tip.x) < keywayHalfWidth - pinEntryMargin &&
                  std::abs(tip.y) < keywayHalfHeight - pinEntryMargin;
    assert(t.tools[0].inserted == inside);
    if (inside) assert(length(t.tools[0].pose.p - tip) < .0002f);
  }
  // The old magnetic area beside the hole cannot capture a pin.
  {
    PhysicalInteraction t;t.config.useHolders=false;t.config.requirePreparation=false;
    Pose other{{},{2,2,2}},hand;
    t.update({},hand,other,0,0,0);hand=gripAt(t.tools[0].pose,1);
    t.update({},hand,other,1,0,0);
    hand=gripAt({{}, {.020f,.0027f,.09f}},1);t.update({},hand,other,1,0,0);
    hand=gripAt({{}, {.020f,.0027f,.026f}},1);t.update({},hand,other,1,0,0);
    assert(!t.tools[0].inserted && t.tools[0].pose.p.x>.015f);
  }
  for (float hz : {20.f,90.f,180.f}) {
    PinBox box;Ergonomics c;box.reconcile(18,c);float dt=1/hz;
    for(int n=0;n<int(5*hz);n++)box.update(dt,{},c);
    auto before=box.pins;Vec anchor=holderPoint(c,0);
    Segment last{anchor+Vec{0,.02f,-.03f},anchor+Vec{0,0,-.03f}};
    bool touched=false;
    for(int n=1;n<=int(hz);n++) {
      float t=n/hz;
      Segment current{anchor+Vec{.018f*std::sin(t*2*pi),.02f-.075f*t,-.03f},
                      anchor+Vec{.018f*std::sin(t*2*pi),-.075f*t,-.03f}};
      touched|=box.rummage(last,current,dt,c);
      box.update(dt,{},c);last=current;
    }
    float movement=0;
    for(int n=0;n<18;n++)movement+=length(box.pins[n].pose.p-before[n].pose.p);
    assert(touched && movement>.01f && box.count==18);
    for(int n=0;n<int(8*hz);n++)box.update(dt,{},c);
    int asleep=0;
    for(int n=0;n<18;n++) {
      auto &p=box.pins[n];asleep+=p.sleeping;
      assert(finite(p.pose.p) && length(p.velocity)<.5f);
    }
    printf("rummage %.0f Hz motion %.4f asleep %d\n",hz,movement,asleep);
    assert(asleep==18);
    before=box.pins;
    assert(!box.rummage(last,last,dt,c));
    Segment far{{2,2,2},{2.1f,2,2}};
    assert(!box.rummage(last,far,dt,c)); // tracking jump has no impulse
    assert(!box.rummage(far,{{2.01f,2,2},{2.11f,2,2}},dt,c));
    for(int n=0;n<18;n++)assert(length(box.pins[n].pose.p-before[n].pose.p)==0);
  }
  // Both empty hands stir without a grab or inventory change.
  for(int h=0;h<2;h++) {
    PhysicalInteraction t;t.syncInventory(true,7);
    Pose palms[2],pinches[2];palms[1-h].p=pinches[1-h].p={2,2,2};
    Vec a=holderPoint(t.config,0);unsigned contacts=0;
    for(int n=0;n<270;n++) {
      float depth=std::clamp((n-180)/90.f,0.f,1.f)*.092f;
      palms[h].p=a+Vec{0,.02f-depth,-.03f};pinches[h].p=a+Vec{0,-depth,-.03f};
      t.update({},palms[0],palms[1],0,0,0,1.f/90,2,1,pinches);
      contacts|=t.rummagedHands;
      assert(t.inventory==7 && t.box.count==7 && !t.droppedEvents && t.pickHand<0);
    }
    printf("hand %d contacts %u first pin y %.4f anchor %.4f\n",h,contacts,t.box.pins[0].pose.p.y,a.y);
    assert(contacts==(1u<<h));
  }
  puts("no entry/regrip snap, actual aperture, both-hand rummaging and stock preservation passed");
}
