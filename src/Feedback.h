// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace locks {
enum class Cue { None, Housing, Prepare, Insert, Turn, Contact, Strain, Break };
inline const char *cueName(Cue cue) {
  switch (cue) {
  case Cue::Housing:
    return "housing";
  case Cue::Prepare:
    return "prepare";
  case Cue::Insert:
    return "insert";
  case Cue::Turn:
    return "turn";
  case Cue::Contact:
    return "contact";
  case Cue::Strain:
    return "strain";
  case Cue::Break:
    return "break";
  default:
    return "none";
  }
}
struct Pulse {
  Cue cue = Cue::None;
  float amplitude = 0;
  uint32_t durationMs = 0, intervalMs = 0;
};
struct FeedbackPlan {
  std::array<Pulse, 2> pulses{};
  unsigned stop = 0;
  float pressure = 0;
};
class HapticFeedback {
  std::array<uint64_t, 2> until{};
  std::array<Cue, 2> playing{};
  uint64_t lastPulse = 0;
  int lastDriver = -1, lastPick = -1;
  unsigned lastStage = 0;
  float lastCondition = 1;
  bool broken = false, touching = false;
  int touchHand = -1;
  float stopAngle = 0;
  uint64_t contactAt = 0, lastStrain = 0;

public:
  unsigned active(uint64_t now) const {
    return (until[0] > now ? 1u : 0u) | (until[1] > now ? 2u : 0u);
  }
  void reset() { *this = {}; }
  FeedbackPlan update(uint64_t now, unsigned stage, float condition,
                      float cylinder, float target, int driver, int pick,
                      unsigned heldButtons, unsigned insertedHands = 0,
                      unsigned preparedHands = 0, unsigned housingHands = 0) {
    FeedbackPlan plan;
    condition =
        std::isfinite(condition) ? std::clamp(condition, 0.f, 1.f) : 1.f;
    bool snapped = condition <= 0 && (stage == 4 || stage == 5 || stage == 6);
    bool turning = driver >= 0 && driver < 2 &&
                   (heldButtons & (1u << driver)) && target > .5f;
    bool manipulating = stage == 2 || stage == 3 || stage == 4;
    // Contact comes from the game's jam state, including zero overshoot.
    // Keep a latch across tiny wrist/state fluctuations; a clear back-off or
    // release rearms it, rather than truncating/retriggering the contact tap.
    if (touching && (snapped || !turning || !manipulating ||
                     driver != touchHand || target < stopAngle - 1.5f))
      touching = false;
    bool atStop = !snapped && turning && stage == 4 && target >= cylinder - .5f;
    bool onset = atStop && !touching;
    if (onset) {
      touching = true;
      touchHand = driver;
      stopAngle = cylinder;
      contactAt = now;
      lastStrain = 0;
    }
    bool straining = touching && turning && stage == 4;
    bool moving = turning && stage == 3 && target > cylinder + .3f && !touching;
    if (straining)
      plan.pressure = std::clamp((target - cylinder) / 20.f, 0.f, 1.f);
    for (int h = 0; h < 2; h++) {
      bool keep =
          (playing[h] == Cue::Prepare || playing[h] == Cue::Housing)
              ? (heldButtons & (1u << h)) && manipulating
          : playing[h] == Cue::Insert
              ? (heldButtons & (1u << h)) && (h == pick || h == driver) &&
                    manipulating
          : playing[h] == Cue::Break
              ? snapped && (heldButtons & (1u << h))
              : h == driver && (playing[h] == Cue::Turn ? moving : touching);
      if (until[h] > now && !keep) {
        plan.stop |= 1u << h;
        until[h] = 0;
        playing[h] = Cue::None;
      }
    }
    auto pulse = [&](int hand, Cue cue, float amplitude, uint32_t duration,
                     uint32_t interval = 0) {
      if (hand < 0 || hand > 1 || !(heldButtons & (1u << hand)))
        return;
      plan.pulses[hand] = {cue, amplitude, duration, interval};
      playing[hand] = cue;
      until[hand] = now + duration;
    };
    if (snapped && !broken && lastCondition > 0 && lastStage >= 2 &&
        lastStage <= 4) {
      // The pick hand feels the snap, the screwdriver hand the sudden release.
      pulse(driver >= 0 ? driver : lastDriver, Cue::Break, 1.f, 100);
      pulse(pick >= 0 ? pick : lastPick, Cue::Break, 1.f, 140);
      broken = true;
    } else if (onset) {
      pulse(driver, Cue::Contact, 1.f, 70);
    } else if (straining && now - contactAt >= 160) {
      // Angular overshoot is a proxy for pushing against the stopped cylinder,
      // not a force sensor. Wear can also raise urgency on a damaged pick.
      float urgency = std::max(plan.pressure, 1 - condition);
      uint32_t interval = uint32_t(220 - 160 * urgency);
      if (!lastStrain || now - lastStrain >= interval) {
        pulse(driver, Cue::Strain, .85f + .15f * urgency, 40, interval);
        lastStrain = now;
      }
    } else if (moving) {
      if (driver != lastDriver || playing[driver] != Cue::Turn || !lastPulse ||
          now - lastPulse >= 120) {
        pulse(driver, Cue::Turn, .45f, 28, 120);
        lastPulse = now;
      }
    }
    for (int h = 0; h < 2; h++)
      if ((insertedHands & (1u << h)) && !snapped &&
          plan.pulses[h].cue == Cue::None && until[h] <= now)
        pulse(h, Cue::Insert, .65f, 35);
    for (int h = 0; h < 2; h++)
      if ((preparedHands & (1u << h)) && !snapped &&
          plan.pulses[h].cue == Cue::None && until[h] <= now)
        pulse(h, Cue::Prepare, .55f, 45);
    for (int h = 0; h < 2; h++)
      if ((housingHands & (1u << h)) && manipulating && !snapped &&
          plan.pulses[h].cue == Cue::None && until[h] <= now)
        pulse(h, Cue::Housing, .55f, 25);
    if (condition > 0 && (stage == 0 || stage == 1 || stage == 2))
      broken = false;
    if (stage == 2 || stage == 3 || stage == 4) {
      lastDriver = driver;
      lastPick = pick;
    }
    if (!turning)
      lastPulse = lastStrain = 0;
    lastCondition = condition;
    lastStage = stage;
    return plan;
  }
};
} // namespace locks
