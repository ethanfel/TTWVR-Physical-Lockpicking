// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace locks {
// TTWVR's Loot Menu preference uses Both, Left, Right, Off in this order.
constexpr unsigned lootHandsWithKit(unsigned preference, unsigned occupied) {
  constexpr unsigned hands[]{3, 1, 2, 0};
  constexpr unsigned preferenceFor[]{3, 1, 2, 0};
  return preference < 4 ? preferenceFor[hands[preference] & ~occupied & 3] : preference;
}

// The host also uses grip to draw a second weapon and trigger to attack.
struct KitWeaponControls {
  unsigned gripsToRelease = 0, triggersToRelease = 0;
  unsigned gripMask = 0, triggerMask = 0;

  bool awaitingRelease() const { return gripsToRelease || triggersToRelease; }

  unsigned update(bool toolsOut, unsigned grips, unsigned triggers,
                  unsigned gripActive, unsigned triggerActive, unsigned kitCapture = 0) {
    const unsigned captured = toolsOut ? 3u : kitCapture;
    // Only an active reading of the same button can confirm its release.
    gripsToRelease = (gripsToRelease & (grips | ~gripActive)) | (grips & captured);
    triggersToRelease = (triggersToRelease & (triggers | ~triggerActive)) | (triggers & captured);
    gripMask = captured | gripsToRelease;
    triggerMask = captured | triggersToRelease;
    return gripMask | triggerMask;
  }

  unsigned blockedHands(unsigned gripBindings, unsigned triggerBindings) const {
    return (gripMask & gripBindings) | (triggerMask & triggerBindings);
  }
};

// Publish the button bits and their sample time together across XR/game threads.
struct KitInputSample {
  uint64_t packed = 0;
  static KitInputSample make(uint64_t time, unsigned bits, unsigned uncertain = 0) {
    return {(time << 10) | ((uncertain & 3u) << 7) | (bits & 0x7fu) | 0x200u};
  }
  unsigned buttons() const { return unsigned(packed & 0x7fu); }
  unsigned uncertainRelease() const { return unsigned(packed >> 7) & 3u; }
  uint64_t time() const { return packed >> 10; }
  bool fresh(uint64_t now) const {
    auto time = this->time();
    return packed && (time >= now || now - time <= 180);
  }
};

// Keep the identity of a held input across independent pose, grip and trigger
// interruptions. A different, idle action cannot confirm its release.
struct KitControlState {
  unsigned heldGrips = 0, heldTriggers = 0, previousTriggers = 0;
  unsigned down = 0, valid = 0, uncertainRelease = 0;
  void update(unsigned grips, unsigned triggers, unsigned gripActive,
              unsigned triggerActive, unsigned tracked, unsigned allowed) {
    const unsigned wasHeld = heldGrips | heldTriggers;
    heldGrips = grips | (heldGrips & ~gripActive);
    heldTriggers = (heldTriggers & ~triggerActive) |
                   (triggers & (heldTriggers | (allowed & tracked & ~previousTriggers)));
    previousTriggers = triggers | (previousTriggers & ~triggerActive);
    const unsigned confirmed = grips | (triggers & heldTriggers);
    const unsigned unknown = (heldGrips & ~gripActive) | (heldTriggers & ~triggerActive);
    valid = tracked & (gripActive | triggerActive) & (~unknown | confirmed);
    down = confirmed & valid;
    // Keep an ambiguous release until another confirmed hold, even if the
    // game thread missed the individual XR samples during the interruption.
    uncertainRelease = (uncertainRelease | (wasHeld & ~valid)) & ~down;
  }
  unsigned capture(unsigned previous, unsigned armed, unsigned allowed, unsigned owned) const {
    // The base game can still see a pressed button when only its pose is lost.
    const unsigned waiting = previous & ~valid & (heldGrips | heldTriggers);
    return waiting | (down & ((allowed & armed) | previous | owned));
  }
};

// One game tick may consume several XR updates. Preserve the time each held
// hand first crossed its button threshold, even if analog pressure changes.
struct KitPressOrder {
  unsigned held = 0;
  int64_t started[2]{};
  int update(unsigned down, const int64_t (&times)[2]) {
    for (int h = 0; h < 2; ++h)
      if ((down & ~held) & (1u << h))
        started[h] = times[h];
    held = down;
    return down == 2 || (down == 3 && started[1] < started[0]) ? 1 : 0;
  }
};

// A role belongs to a hand until the last tool goes home. A broken or
// dropped pin never changes the screwdriver hand.
struct KitRoles {
  int pinHand = -1;
  unsigned drawn = 0; // bit 0 pin, bit 1 screwdriver
  int draw(int hand, bool pins) {
    if (hand < 0 || hand > 1)
      return -1;
    int tool = pinHand < 0 || pinHand == hand ? 0 : 1;
    if ((drawn & (1u << tool)) || (tool == 0 && !pins))
      return -1;
    if (pinHand < 0)
      pinHand = hand;
    drawn |= 1u << tool;
    return tool;
  }
  int handFor(int tool) const { return tool == 0 ? pinHand : 1 - pinHand; }
  void put(int tool) {
    drawn &= ~(1u << tool);
    if (!drawn)
      pinHand = -1;
  }
};
} // namespace locks
