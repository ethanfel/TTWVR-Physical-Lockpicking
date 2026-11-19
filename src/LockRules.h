// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace locks {
// LockPickMenu builds six nested pixel intervals from its current XML track,
// Lockpick skill and game settings. Keep the same integer rounding at the
// edges.
struct LockRules {
  float width = 0, center = 0, healthBase = 1, breakSeconds = 0;
  int pickPixels = 0, driverPixels = 0;
  float limit(float angle) const {
    float error = std::abs(angle * width / 180.f - center);
    int half = driverPixels + pickPixels / 2;
    if (error < half) {
      int start = int(center - pickPixels / 2);
      for (int i = 0; i < 6; ++i, start -= driverPixels / 5)
        if (error < center - start)
          return 90.f - 15.f * i;
    }
    float span = width - 2 * half;
    return span > 0
               ? std::clamp(15.f * (width - half - error) / span, 0.f, 15.f)
               : 15.f;
  }
};
// The last live lock worked keeps its solution while the player draws another pin.
// Track the position as a fraction so a new track width preserves the angle.
struct LiveLockPuzzle {
  uint32_t reference = 0;
  float position = 0;

  template <typename Roll>
  float centerFor(uint32_t target, float width, int margin, Roll roll) {
    if (target != reference || !reference) {
      position = std::clamp(roll() * width, float(margin), width - margin) / width;
      reference = target;
    }
    return position * width;
  }
};

enum LockSound : unsigned {
  Turn = 1,
  SqueakA = 2,
  SqueakB = 4,
  Stop = 8,
  Tension = 16,
  ReleaseTension = 32,
  Break = 64
};
struct LockTurn {
  unsigned stage = 2;
  float cylinder = 0, limit = 0;
  unsigned step(const LockRules &rules, float pick, float target, float dt,
                float &health) {
    bool turning = target > .5f;
    float before = cylinder;
    unsigned sound = 0;
    if (stage == 2) {
      limit = rules.limit(pick);
      cylinder = std::max(0.f, cylinder - 90 * dt);
      if (turning) {
        stage = 3;
        sound |= Turn;
      }
    } else if (stage == 3) {
      if (!turning)
        stage = 2;
      else {
        cylinder = std::min({cylinder + 90 * dt, limit, target});
        if (cylinder >= limit && limit < 90.f) {
          stage = 4;
          sound |= Stop | Tension;
        }
      }
    } else if (stage == 4) {
      if (!turning || target < cylinder - .5f) {
        stage = 2;
        sound |= ReleaseTension;
      } else if (rules.breakSeconds > 0) {
        health = std::max(
            0.f, health - dt / (rules.breakSeconds * rules.healthBase));
        if (health <= 0) {
          stage = 5;
          sound |= ReleaseTension | Break;
        }
      }
    }
    if ((before < 30 && cylinder >= 30) || (before > 30 && cylinder <= 30))
      sound |= SqueakA;
    if ((before < 60 && cylinder >= 60) || (before > 60 && cylinder <= 60))
      sound |= SqueakB;
    return sound;
  }
};
} // namespace locks
