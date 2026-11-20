// SPDX-License-Identifier: GPL-3.0-only
#include "Physical.h"
#include <cassert>
using namespace locks;

static void exercise(int first) {
  PhysicalInteraction p;
  p.config.useHolders = false;
  p.sharedKit = true;
  p.kitNear = 3;
  Pose hand[2]{{{}, {-.23f, -.2f, .32f}}, {{}, {.23f, -.2f, .32f}}};
  int other = 1 - first;
  auto step = [&](unsigned held, unsigned stage = 2, float health = 1) {
    p.syncInventory(true, 5);
    p.update({}, hand[0], hand[1], held & 1 ? 1.f : 0.f, held & 2 ? 1.f : 0.f,
             0, 1.f / 90, stage, health);
  };
  step(0);
  assert(!p.tools[0].visible && !p.tools[1].visible);
  step(1u << first);
  assert(p.pickHand == first && p.screwdriverHand == -1);
  step(3);
  assert(p.pickHand == first && p.screwdriverHand == other);
  step(1u << other, 5, 0);
  step(1u << other);
  assert(!p.tools[0].visible && p.screwdriverHand == other &&
         p.kit.pinHand == first);
  step(3);
  assert(p.pickHand == first && p.screwdriverHand == other);
  step(0);
  assert(!p.tools[0].visible && !p.tools[1].visible && p.kit.pinHand == -1);
  step(1u << other);
  assert(p.pickHand == other);
  // Both tools carried in from gameplay, grips never released.
  PhysicalInteraction q;
  q.config.useHolders = false;
  q.sharedKit = true;
  q.kit = {first, 3};
  q.kitHandoff = 3;
  q.syncInventory(true, 5);
  q.update({}, hand[0], hand[1], 1, 1, 0);
  assert(q.pickHand == first && q.screwdriverHand == other);
}
int main() {
  KitWeaponControls weapons;
  assert(weapons.update(false, 0, 1, 3, 3) == 0);
  assert(weapons.update(true, 2, 1, 3, 3) == 3);
  assert(weapons.update(false, 0, 1, 3, 3) == 1);
  assert(weapons.update(false, 0, 0, 3, 2) == 1);
  assert(weapons.update(false, 0, 0, 3, 3) == 0);
  assert(weapons.update(false, 2, 0, 3, 3) == 0);
  // Popup interrupts a left-hand pin and a right-hand trigger. The player
  // can confirm the popup with a fresh left trigger while keeping the grip.
  weapons.update(true, 1, 2, 3, 3);
  assert(weapons.awaitingRelease());
  weapons.update(false, 0, 0, 0, 0);
  assert(weapons.blockedHands(3, 0) == 1 && weapons.blockedHands(0, 3) == 2);
  weapons.update(false, 1, 3, 3, 3);
  assert(weapons.blockedHands(0, 1) == 0 && weapons.blockedHands(0, 2) == 2);
  weapons.update(false, 1, 0, 3, 3);
  assert(weapons.blockedHands(3, 0) == 1 && weapons.blockedHands(0, 3) == 0);
  weapons.update(false, 0, 2, 3, 3);
  assert(!weapons.awaitingRelease() && !weapons.blockedHands(3, 3));
  // A menu can open between the initial captured draw and publication of the hand mesh.
  weapons.update(false, 0, 1, 3, 3, 1);
  weapons.update(false, 0, 1, 3, 3);
  assert(weapons.blockedHands(0, 1) == 1 && weapons.blockedHands(1, 0) == 0);
  for (unsigned preference = 0; preference < 4; ++preference) {
    assert(lootHandsWithKit(preference, 0) == preference);
    assert(lootHandsWithKit(preference, 3) == 3);
  }
  assert(lootHandsWithKit(0, 1) == 2 && lootHandsWithKit(0, 2) == 1);
  assert(lootHandsWithKit(1, 1) == 3 && lootHandsWithKit(1, 2) == 1);
  assert(lootHandsWithKit(2, 1) == 2 && lootHandsWithKit(2, 2) == 3);

  exercise(0);
  exercise(1);
  const auto sample=KitInputSample::make(1000,0x6b);
  assert(sample.buttons()==0x6b && sample.fresh(999) && sample.fresh(1180));
  assert(!sample.fresh(1181) && !KitInputSample{}.fresh(0));
  for(unsigned hand : {1u,2u}) {
    const unsigned other=3u ^ hand;
    for(bool useTrigger : {false,true}) {
      KitControlState input;
      unsigned g=useTrigger ? 0 : hand, t=useTrigger ? hand : 0;
      input.update(g,t,3,3,3,hand);
      assert(input.down==hand && input.capture(0,3,hand,0)==hand);
      input.update(g,t,3,3,other,0);
      assert(input.down==0 && input.valid==other && input.uncertainRelease==hand);
      assert(input.capture(hand,0,0,hand)==hand);
      assert(input.capture(0,0,0,0)==0);
      input.update(g,t,3,3,3,0);
      assert(input.down==hand && input.uncertainRelease==0);
      input.update(0,0,useTrigger ? 3 : other,useTrigger ? other : 3,3,0);
      assert(input.valid==other && input.uncertainRelease==hand);
      input.update(0,0,3,3,3,0);
      assert(input.valid==3 && !input.down && input.uncertainRelease==hand);
      assert(input.capture(hand,0,0,hand)==0);
      input.update(0,0,3,3,3,0);
      assert(KitInputSample::make(1234,0,input.uncertainRelease).uncertainRelease()==hand);
      input.update(g,t,3,3,3,hand);
      assert(input.down==hand && !input.uncertainRelease);
    }
    KitControlState outside;
    outside.update(0,hand,3,3,3,0);
    outside.update(0,hand,3,3,3,hand);
    assert(!outside.down);
  }
  for (unsigned hand : {1u, 2u}) {
    const unsigned other = 3u ^ hand;
    HolsterGrip holster;
    KitControlState input;
    KitWeaponControls host;
    Vec poses[2]{};
    uint64_t now = 100;
    const int h = hand == 1 ? 0 : 1, o = 1-h;
    auto tick = [&](unsigned grips, unsigned triggers, unsigned near, bool editing = false,
                    unsigned active = 3, unsigned tracked = 3, unsigned slot = 4) {
      holster.update(slot, grips, active, tracked, near, 0, editing, poses, now++);
      input.update(grips & ~holster.nativeGrips, triggers, active, 3, tracked, 0);
      host.update(holster.drawn != 0, grips, triggers, active, 3, holster.drawn, holster.nativeGrips);
    };
    tick(0, 0, hand);
    tick(0, hand, hand);  // Trigger alone never draws.
    assert(!input.down && !holster.drawn);
    tick(hand, hand, hand);
    assert(holster.nativeGrips == hand && !input.down && !holster.drawn);
    tick(hand, 0, 0);  // A moving sphere alone cannot draw from a stationary hand.
    assert(!holster.drawn);
    poses[h].x += .12f;
    tick(hand, 0, 0);
    assert(input.down == hand && holster.drawn == hand && !holster.nativeGrips);
    tick(3, 0, other);
    assert(!host.blockedHands(other, 0) && holster.drawn == hand);
    poses[o].x += .12f;
    tick(3, 0, 0);
    assert(holster.drawn == 3 && input.down == 3);
    const auto sample = KitInputSample::make(456, 0x55, other, holster.drawn);
    assert(sample.holsterDraw() == 3 && sample.buttons() == 0x55 &&
           sample.uncertainRelease() == other && sample.time() == 456);
    tick(0, 0, 0);
    tick(hand, 0, hand);
    now += 3000;
    tick(hand, 0, hand);
    poses[h].x += .15f;
    tick(hand, 0, 0);
    assert(!holster.drawn && !host.blockedHands(hand, 0));
    tick(3, other, 0, true);
    assert(holster.nativeGrips == 3 && !input.down && !holster.drawn);
    tick(other, 0, 0);
    assert(holster.nativeGrips == other && !holster.drawn);
    tick(0, 0, 0);
    tick(hand, 0, 0);
    tick(hand, 0, hand);
    poses[h].x += .10f;
    tick(hand, 0, 0);
    assert(!holster.drawn);  // A grip that started outside cannot draw on crossing.
    for (int interrupt = 0; interrupt < 4; ++interrupt) {
      tick(0, 0, hand);
      tick(hand, 0, hand);
      tick(hand, 0, hand, interrupt == 0, interrupt == 1 ? other : 3,
           interrupt == 2 ? other : 3, interrupt == 3 ? 0 : 4);
      poses[h].x += .1f;
      tick(hand, 0, 0);
      assert(!holster.drawn);
    }
  }
  KitPressOrder order;
  int64_t times[]{140,120};
  assert(order.update(3,times)==1);
  times[1]=155; // A later analog pressure change must preserve the first hand.
  assert(order.update(3,times)==1);
  order.update(0,times);
  assert(order.update(1,times)==0);
  times[0]=160;times[1]=158;
  assert(order.update(3,times)==0);
  KitRoles k;
  assert(k.draw(1, false) < 0 && k.pinHand == -1);
  assert(k.draw(0, true) == 0 && k.draw(0, true) < 0);
  assert(k.draw(1, true) == 1);
  k.put(0);
  assert(k.draw(1, true) < 0 && k.draw(0, false) < 0 && k.draw(0, true) == 0);
}
