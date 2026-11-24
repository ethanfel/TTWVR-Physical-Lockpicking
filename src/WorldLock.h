// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "GameTransform.h"
#include "LiveContact.h"
#include "Shared.h"
namespace locks {
struct MountedCorrection { Vec rawTip, shownTip; Mat turn; bool valid=false; };
bool IsWorldUnlockPending(uintptr_t ref);
bool CompleteWorldLock(const GameState &);
void InitWorldLocks(void *scripts, void *arrays, void *commands);
int HoverLockLevel(unsigned refId);
void TickVisibleLocks(uintptr_t closeupTarget);
DoorTurn CloseupDoorTurn(const GameState &);
bool ParkLivePin(uintptr_t mesh);
bool HasParkedPin();
uintptr_t WorldDriverMount();
bool ParkedPinPose(Transform &world);
void TakeLivePin();
void ResetWorldLock(bool unloading);
bool WorldLockFrame(GameState &, const LiveTool *pin,
                    const Transform *driverHand, int pinHand, float opening, float twist,
                    float &pinHealth, MountedCorrection (&corrections)[2]);
} // namespace locks
