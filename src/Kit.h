// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Math.h"
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
                  unsigned gripActive, unsigned triggerActive, unsigned kitCapture = 0,
                  unsigned nativeGrips = 0, unsigned reservedTriggers = 0) {
    const unsigned captured = toolsOut ? 3u : kitCapture;
    // Only an active reading of the same button can confirm its release.
    gripsToRelease = ((gripsToRelease & (grips | ~gripActive)) | (grips & captured)) & ~nativeGrips;
    triggersToRelease = (triggersToRelease & (triggers | ~triggerActive)) | (triggers & (captured | reservedTriggers));
    gripMask = (captured & ~nativeGrips) | gripsToRelease;
    triggerMask = captured | reservedTriggers | triggersToRelease;
    return gripMask | triggerMask;
  }

  unsigned blockedHands(unsigned gripBindings, unsigned triggerBindings) const {
    return (gripMask & gripBindings) | (triggerMask & triggerBindings);
  }
};

// Publish the button bits and their sample time together across XR/game threads.
struct KitInputSample {
  uint64_t packed = 0;
  static KitInputSample make(uint64_t time, unsigned bits, unsigned uncertain = 0, unsigned draw = 0, unsigned pinDraw = 0) {
    return {(time << 14) | ((pinDraw & draw & 3u) << 12) | ((draw & 3u) << 10) | ((uncertain & 3u) << 7) | (bits & 0x7fu) | 0x200u};
  }
  unsigned buttons() const { return unsigned(packed & 0x7fu); }
  unsigned uncertainRelease() const { return unsigned(packed >> 7) & 3u; }
  unsigned holsterDraw() const { return unsigned(packed >> 10) & 3u; }
  unsigned pinDraw() const { return unsigned(packed >> 12) & 3u; }
  uint64_t time() const { return packed >> 14; }
  bool fresh(uint64_t now) const {
    auto time = this->time();
    return packed && (time >= now || now - time <= 180);
  }
};

// A grip started on a numbered sphere belongs to TTWVR until the hand
// withdraws. A completed native edit owns both the carry and resize grips.
struct HolsterPull {
  unsigned slot = 0, ready = 0, pending = 0, reserved = 0, drawn = 0;
  uint64_t started[2]{};
  Vec origin[2]{};

  void update(unsigned activeSlot, unsigned buttons, unsigned active, unsigned tracked,
              unsigned inRange, unsigned busy, bool editing, const Vec (&hands)[2], uint64_t now,
              bool canReposition = true) {
    if (slot != activeSlot) {
      slot = activeSlot;
      pending = drawn = ready = 0;
    }
    const unsigned pressed = ready & buttons & active & tracked;
    ready = (ready & ~active) | (active & tracked & ~buttons);
    reserved &= buttons | ~active;
    drawn &= buttons | ~active;
    pending &= buttons & active & tracked;
    if (!slot) return;
    if (editing) {
      pending = 0;
      reserved |= buttons & ~busy & ~drawn;
      return;
    }
    const unsigned begin = pressed & inRange & ~busy & ~reserved;
    pending |= begin;
    reserved |= begin;
    for (unsigned hand = 0; hand < 2; ++hand) {
      const unsigned bit = 1u << hand;
      if (!(pending & bit)) continue;
      if (begin & bit) {
        started[hand] = now;
        origin[hand] = hands[hand];
      } else if (canReposition && now - started[hand] >= 3000) {
        // Leave the grip with the host even if its edit flag arrives a frame late.
        pending &= ~bit;
      } else if (!(inRange & bit) && length(hands[hand] - origin[hand]) >= .025f) {
        pending &= ~bit;
        reserved &= ~bit;
        drawn |= bit;
      }
    }
  }
};

// Keep the identity of a held input across independent pose, grip and trigger
// interruptions. A different, idle action cannot confirm its release.
struct KitControlState {
  unsigned heldGrips = 0, heldTriggers = 0, previousTriggers = 0;
  unsigned down = 0, valid = 0, uncertainRelease = 0;
  void update(unsigned grips, unsigned triggers, unsigned gripActive,
              unsigned triggerActive, unsigned tracked, unsigned allowed, unsigned pulledTriggers = 0) {
    const unsigned wasHeld = heldGrips | heldTriggers;
    heldGrips = grips | (heldGrips & ~gripActive);
    heldTriggers = (heldTriggers & ~triggerActive) |
                   (triggers & (heldTriggers | pulledTriggers | (allowed & tracked & ~previousTriggers)));
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

// The two held tools always occupy different hands. In pin-first mode the
// first hand keeps its role until both tools return to the kit.
struct KitRoles {
  int pinHand = -1;
  unsigned drawn = 0; // bit 0 pin, bit 1 screwdriver
  int draw(int hand, bool pins, int requested = -1, bool otherToolReleased = false) {
    if (hand < 0 || hand > 1)
      return -1;
    int tool = requested < 0 ? int(pinHand >= 0 && pinHand != hand) : requested;
    if (tool > 1 || (drawn & (1u << tool)) || (tool == 0 && !pins))
      return -1;
    int nextPinHand = tool == 0 ? hand : 1 - hand;
    // A released tool (including a wire parked in the lock) leaves its former hand free.
    if (drawn && pinHand != nextPinHand && !otherToolReleased)
      return -1;
    pinHand = nextPinHand;
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
