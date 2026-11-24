// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Math.h"
#include <bitset>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <windows.h>
namespace locks {
struct DoorTurn {
  int sign = 1;
  std::optional<Pose> reverseFace;
};
struct GameState {
  uintptr_t menu = 0, target = 0;
  unsigned stage = 0, level = 0, skill = 0;
  float pick = 0, cylinder = 0, health = 0;
  int kitPinHand = -1;
  DoorTurn doorTurn;
  unsigned kitSlot = 0, kitDrawn = 0, kitHands = 0;
  Vec kitCenters[8]{};
  float kitRadii[8]{};
  bool kitGameplay = false, holsterEditing = false, drawToolsByButton = false;
  Vec kitPalms[2]{}, kitFingerPads[2]{};
  unsigned kitFingerPadsValid = 0;
  unsigned kitPalmsValid = 0;
  float kitOpening = 0, kitTwist = 0;
  unsigned worldSeated=0,worldStage=0,worldPrepared=0;
  unsigned worldSurface[2]{};
  bool worldPinBroken=false;
  uint64_t worldKnock = 0;
  int worldPick=-1;
  float worldHealth=1,worldDemand=0,worldCylinder=0;
  float worldUnitsPerMeter=70.f;
  unsigned kitBendNear = 0, kitRegrabNear = 0;
  int pinCount = -1;
  bool inventoryReady = false, targetIsDoor = false;
  uintptr_t targetTextureSwaps = 0;
  std::optional<std::bitset<128>> closeupHiddenParts;
  unsigned targetKind = 0, targetRefId = 0, targetBaseId = 0;
};
struct Control {
  uintptr_t menu = 0;
  bool valid = false, turning = false, fallback = false;
  float pick = 0, torque = 0, targetAngle = 0;
  uint64_t stamp = 0;
};
extern std::wstring folder;
extern std::mutex sharedMutex;
extern GameState game;
extern Control control;
#ifdef __GNUC__
__attribute__((format(gnu_printf, 1, 2)))
#endif
void Log(const char *format, ...);
void InitFolder();
bool NativeReady();
uintptr_t FindSceneNode(uintptr_t root, const std::string &name);
struct Transform;
void SampleKitControllers(const Pose (&poses)[2], unsigned valid, unsigned open);
bool KitControllerWorld(int hand, Transform &world, bool requireOpen = false);
void RequestForceLock(const GameState &);
void RequestForceSeatSound(const GameState &);
bool NativeForceLock(const GameState &);
bool MountSceneChild(uintptr_t node, uintptr_t parent);
void RetainSceneNode(uintptr_t node);
void ReleaseMountedChild(uintptr_t node);
void RestoreWorldDriver();
bool PresentWorldDriver(uintptr_t hand, int handIndex, uintptr_t mount, const Transform &wrist);
void UpdateSceneNode(uintptr_t node);
struct NativeCall { uintptr_t site, target; void *replacement; };
bool InstallVerifiedCalls(const NativeCall *, size_t count);
bool Readable(const void *, size_t);
void InitInventory(void *scriptInterface);
void UpdateInventory(GameState &state);
void UpdateTarget(GameState &state);
std::optional<std::bitset<128>> CloseupHiddenParts(const GameState &state);
uint64_t RequestPinDrop(uintptr_t menu, uintptr_t target);
bool PinDropResult(uint64_t id, bool &committed, int &remaining);
GameState Snapshot();
bool WorldEye(Vec &p);
bool WorldToTracking(Vec world, Vec &tracking);
} // namespace locks
