// SPDX-License-Identifier: GPL-3.0-only
#include "PhysicalKey.h"
#include <cassert>
using namespace locks;
static Mat roll(float degrees) {
  float a=degrees*pi/360;
  return matrix({0,0,std::sin(a),std::cos(a)});
}
int main() {
  const Transform rest{{},{0,0,PhysicalKey::restZ},1};
  for (int h : {0,1}) for (int sign : {-1,1}) {
    PhysicalKey key;
    Transform hand[2];
    hand[h].position={0,0,.104f};
    unsigned bit=1u<<h;
    auto step=[&](unsigned tracked,unsigned grips){key.step(hand,tracked,grips,sign,45,rest,true);};
    step(bit,bit); assert(key.owner==-1); // Already squeezing on approach.
    step(bit,0); step(bit,bit); assert(key.owner==h && !key.seated);
    hand[h].position.z-=.050f; step(bit,bit); assert(key.seated && !key.complete);
    hand[h].rotation=roll(sign*34); step(bit,bit); assert(key.angle==0);
    hand[h].rotation=roll(-sign*22.5f); step(bit,bit);
    assert(std::abs(key.angle-45)<.001f);
    step(bit,bit); assert(std::abs(key.angle-45)<.001f);
    step(0,0); assert(key.owner==-1 && key.seated);
    step(bit,bit); assert(key.owner==-1);
    step(bit,0); step(bit,bit); assert(key.owner==h && !key.complete);
    hand[h].rotation=roll(-sign*46); step(bit,bit); assert(key.complete);
  }
  PhysicalKey key;
  Transform hands[2];
  hands[0].position={0,0,.104f}; hands[1].position={.4f,0,0};
  auto step=[&](unsigned held){key.step(hands,3,held,1,45,rest,true);};
  step(0); step(2); assert(key.owner==-1);
  step(0); step(1);
  hands[0].position={.065f,0,.050f}; step(1); assert(!key.seated);
  step(0); assert(key.pose.position.z==key.restZ);
  hands[0].position={0,0,.104f}; step(1);
  hands[0].position.z-=.05f; step(1); assert(key.seated);
  hands[0].position.z+=.06f; step(1); assert(!key.seated && !key.complete);
  // Live key acquired only by a completed holster pull. On release it
  // returns to the kit, while a seated key can still be regripped at the lock.
  for (int h : {0,1}) {
    PhysicalKey live;
    unsigned bit=1u<<h;
    hands[h].position={0,0,.104f};
    auto liveStep=[&](unsigned held){live.step(hands,bit,held,1,45,{},false);};
    liveStep(0); liveStep(bit); assert(live.owner==-1 && live.inReach==0);
    hands[h].position={.24f,-.38f,.08f};
    live.draw(h,hands[h]); liveStep(bit);
    assert(live.owner==h && !live.seated);
    liveStep(0); assert(live.owner==-1);
    live.draw(h,hands[h]);
    hands[h].position={0,0,.095f}; liveStep(bit);
    hands[h].position.z=.055f; liveStep(bit); assert(live.seated);
    liveStep(0); liveStep(bit); assert(live.owner==h);
  }
  // A loose close-up key starts and returns to the box position.
  PhysicalKey closeup;
  const Transform rack{{},{-.22f,-.18f,.26f},1};
  hands[0].position=rack.position+Vec{0,0,PhysicalKey::bowZ};
  closeup.step(hands,1,0,1,45,rack,true);
  closeup.step(hands,1,1,1,45,rack,true); assert(closeup.owner==0);
  closeup.step(hands,1,0,1,45,rack,true);
  assert(closeup.owner==-1 && length(closeup.pose.position-rack.position)<.001f);
}
