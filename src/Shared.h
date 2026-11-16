// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <windows.h>
namespace locks {
struct GameState {
  uintptr_t menu = 0, target = 0;
  unsigned stage = 0, level = 0, skill = 0;
  float pick = 0, cylinder = 0, health = 0;
  int pinCount = -1;
  bool inventoryReady = false;
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
bool Readable(const void *, size_t);
void InitInventory(void *scriptInterface);
void UpdateInventory(GameState &state);
void UpdateTarget(GameState &state);
uint64_t RequestPinDrop(uintptr_t menu, uintptr_t target);
bool PinDropResult(uint64_t id, bool &committed, int &remaining);
GameState Snapshot();
} // namespace locks
