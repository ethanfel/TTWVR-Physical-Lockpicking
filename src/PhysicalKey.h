// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "GameTransform.h"

namespace locks {
// The stock key mesh is 60 mm long with its origin at the nose.
struct PhysicalKey {
  static constexpr float restZ = .055f, seatZ = .007f, bowZ = .049f;
  Transform pose{{}, {0,0,restZ}, 1};
  int owner = -1;
  unsigned armed = 0, inReach = 0;
  bool seated = false, complete = false;
  float angle = 0, heldAngle = 0;
  Transform grabHand, grabKey;

  // A confirmed pull from the actual kit sphere starts at the hand on exit.
  void draw(int hand, const Transform &grip) {
    owner = hand;
    grabHand = grip;
    pose = {grip.rotation, grip.position - grip.rotation.apply({0,0,bowZ}), 1};
    grabKey = pose;
    heldAngle = angle = 0;
    seated = false;
  }

  void step(const Transform (&hands)[2], unsigned tracked, unsigned grips,
            int sign, float fullTurn, const Transform &rest, bool loosePickup) {
    inReach = 0;
    if (complete) return;
    const unsigned press = armed & grips & tracked;
    armed = tracked & ~grips;
    if (owner < 0 && !seated) pose = rest;
    const Vec bow = pose.position + pose.rotation.apply({0,0,bowZ});
    for (int h = 0; h < 2; ++h)
      if ((loosePickup || seated) && (tracked & (1u << h)) && length(hands[h].position - bow) <= .065f)
        inReach |= 1u << h;
    if (owner >= 0 && !(tracked & grips & (1u << owner))) {
      owner = -1;
      if (!seated) pose = rest;
      return;
    }
    if (owner < 0) {
      unsigned take = press & inReach;
      if (!take) return;
      owner = take == 3 ? int(length(hands[1].position-bow) < length(hands[0].position-bow))
                        : int(take == 2);
      if (!seated) draw(owner,hands[owner]);
      grabHand = hands[owner];
      grabKey = pose;
      heldAngle = angle;
    }
    const Transform &hand = hands[owner];
    const Mat delta = mul(hand.rotation, grabHand.rotation.transpose());
    Vec tip = hand.position + delta.apply(grabKey.position - grabHand.position);
    if (!seated) {
      const float previous = pose.position.z;
      pose = {mul(delta, grabKey.rotation), tip, 1};
      // A blade approaching from the side hits the face. Seat only after
      // moving inward through the keyway with the blade roughly aligned.
      const bool aligned = std::hypot(tip.x,tip.y) < .014f &&
                           pose.rotation.v[8] > .86f && std::abs(pose.rotation.v[4]) > .65f;
      if (tip.z < .025f && tip.z > -.045f && std::hypot(tip.x,tip.y) < .045f && !aligned) pose.position.z = .025f;
      if (!aligned || previous < seatZ || tip.z > seatZ || tip.z < -.045f)
        return;
      seated = true;
      pose = {{}, {0,0,seatZ}, 1};
      grabHand = hand;
      grabKey = pose;
      return;
    }
    if (tip.z > seatZ + .045f || std::hypot(tip.x,tip.y) > .085f) {
      seated = false;
      angle = heldAngle = 0;
      pose = {mul(delta,grabKey.rotation),tip,1};
      grabHand = hand;
      grabKey = pose;
      return;
    }
    // Signed twist about the keyway. Holding still preserves the turn.
    float twist = std::atan2(delta.v[1]-delta.v[3],delta.v[0]+delta.v[4])*180/pi;
    angle = std::clamp(heldAngle + sign * twist * 90 / fullTurn, 0.f, 90.f);
    const float rad = -angle * sign * pi / 180;
    pose.rotation = matrix({0,0,std::sin(rad/2),std::cos(rad/2)});
    complete = angle >= 89.95f;
  }
};
}
