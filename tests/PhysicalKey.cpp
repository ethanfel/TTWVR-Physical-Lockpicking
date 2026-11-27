// SPDX-License-Identifier: GPL-3.0-only
#include "PhysicalKey.h"
#include <cassert>
using namespace locks;
static Mat roll(float degrees) {
  float a=degrees*pi/360;
  return matrix({0,0,std::sin(a),std::cos(a)});
}
int main() {
  for (int h : {0,1}) for (int sign : {-1,1}) {
    PhysicalKey key;
    Transform hand[2];
    hand[h].position={0,0,.104f};
    unsigned bit=1u<<h;
    auto step=[&](unsigned tracked,unsigned grips){key.step(hand,tracked,grips,sign,45);};
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
  auto step=[&](unsigned held){key.step(hands,3,held,1,45);};
  step(0); step(2); assert(key.owner==-1);
  step(0); step(1);
  hands[0].position={.065f,0,.050f}; step(1); assert(!key.seated);
  step(0); assert(key.pose.position.z==key.restZ);
  hands[0].position={0,0,.104f}; step(1);
  hands[0].position.z-=.05f; step(1); assert(key.seated);
  hands[0].position.z+=.06f; step(1); assert(!key.seated && !key.complete);
}
