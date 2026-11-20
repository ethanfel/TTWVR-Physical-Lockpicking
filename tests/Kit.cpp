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
    KitControlState input;
    KitWeaponControls host;
    auto tick = [&](unsigned grips, unsigned triggers, unsigned near, unsigned busy = 0) {
      input.update(grips, triggers, 3, 3, 3, near, near, busy);
      unsigned captured = input.capture(0, 3, near, busy);
      host.update(busy != 0, grips, triggers, 3, 3, captured, input.nativeGrips);
    };

    tick(hand, hand, hand);  // Simultaneous trigger and grip on the sphere.
    assert(input.nativeGrips == hand && !input.down && !input.holsterTriggers);
    assert(!host.blockedHands(hand, 0));
    tick(hand, 0, 0);
    tick(3, other, 0);  // Second grip resizes while carrying away from the old center.
    assert(input.nativeGrips == 3 && !input.down && !input.holsterTriggers);
    assert(!host.blockedHands(3, 0));
    tick(0, other, other);
    assert(!input.down && !input.holsterTriggers);
    tick(0, 0, hand);
    tick(0, hand, hand);
    assert(input.down == hand && input.holsterTriggers == hand);
    tick(0, 3, other, hand);
    assert(input.holsterTriggers == 3 && input.down == 3);
    const auto sample = KitInputSample::make(456, 0x55, other, input.holsterTriggers);
    assert(sample.holsterDraw() == 3 && sample.buttons() == 0x55 &&
           sample.uncertainRelease() == other && sample.time() == 456);
    tick(0, hand, 0, hand);
    tick(other, hand, other, hand);  // The occupied hand keeps its tool while the free grip edits.
    assert(input.nativeGrips == other && input.down == hand);
    assert(!host.blockedHands(other, 0) && host.blockedHands(0, hand) == hand);
    tick(0, 0, 0);
    tick(0, hand, 0);
    tick(0, hand, hand);
    assert(!input.holsterTriggers && !input.down);  // Held trigger enters from outside.
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
