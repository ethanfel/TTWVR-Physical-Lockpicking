// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Shared.h"
namespace locks {
bool InstallWorldPinPose(uintptr_t site, uintptr_t target, uintptr_t sleeveSite, uintptr_t sleeveTarget);
void LimitKeyReach(int hand, Transform &grip);
void PresentKeyGrip(int hand, const Transform &keyWorld);
bool KeyFingerContact(int hand, Vec &world);
bool ParkWorldPin(int hand);
bool NearParkedPin(int hand, float unitsPerMeter);
bool RegrabWorldPin(int hand);
void BeforeWorldToolRemoval(int hand, int tool);
void ResetWorldPin();
void WorldPinFrame(GameState &, const uintptr_t hands[2], int pinHand,
                   unsigned down, unsigned pressed, unsigned valid,
                   unsigned nearKit, int driverHand, float &pinHealth);
} // namespace locks
