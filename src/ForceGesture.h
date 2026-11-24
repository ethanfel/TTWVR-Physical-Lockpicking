// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Interaction.h"

namespace locks {
// Controller centres in the solver's lock frame. Distances used to recognize
// a hand gesture stay in player metres even when the world mount is scaled.
struct ForcePalms {
  Vec point[2]{};
  unsigned open = 0;
  float metresPerUnit = 1;
};

class ForceGesture {
  enum class Phase { Idle, Approach, Contact, Ready, Spent };
  Phase phase = Phase::Idle;
  Vec start, gripStart, previous;
  Quat wrist;
  int owner = -1;
public:
  enum Event { None, Knock, Attempt };
  bool engaged() const { return phase == Phase::Contact || phase == Phase::Ready || phase == Phase::Spent; }
  void reset() { *this = {}; }
  Event step(int hand, Pose cap, Pose grip, Vec palm, float scale, int sign, bool neutral) {
    if (hand != owner) { reset(); owner = hand; }
    Vec offset = local(cap, palm) * scale;
    const float radius = std::hypot(offset.x, offset.y);
    // A controller centre can touch the virtual butt from 32 mm behind it.
    // Acquire the approach farther back, then require real inward palm travel.
    const bool clear = offset.z >= .065f && offset.z <= .16f && radius <= .035f;
    if (phase != Phase::Idle && length(palm - previous) * scale > .125f) reset();
    previous = palm;
    owner = hand;
    if (phase == Phase::Idle) {
      if (neutral && clear) { phase = Phase::Approach; start = palm; gripStart = grip.p; wrist = grip.q; }
    } else if (phase == Phase::Approach) {
      if (!neutral || radius > .038f || offset.z < -.005f || offset.z > .18f ||
          std::abs(shaftTwist(cap,wrist,grip.q)) > 5.f) reset();
      else if (clear) { start = palm; gripStart = grip.p; }
      else if (offset.z <= .032f) {
        Vec motion = rotate(inverse(cap.q), start - palm) * scale;
        // Walking or recentering can translate both hands into a mounted tool.
        float relativeTravel = motion.z - dot(gripStart-grip.p,rotate(cap.q,{0,0,1})) * scale;
        if (motion.z >= .024f && relativeTravel >= .024f &&
            std::hypot(motion.x,motion.y) <= motion.z && radius <= .027f) {
          phase = Phase::Contact;
          return Knock;
        }
        reset();
      }
    } else if (phase == Phase::Contact) {
      // Ignore all rotation until the tapping hand has separated again.
      if (offset.z >= .062f || radius >= .060f) { phase = Phase::Ready; wrist = grip.q; }
    } else if (phase == Phase::Ready) {
      const float angle = sign * shaftTwist(cap,wrist,grip.q);
      if (angle < -15.f) reset();
      else if (angle >= 15.f) { phase = Phase::Spent; return Attempt; }
    }
    return None;
  }
};
} // namespace locks
