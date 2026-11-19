// SPDX-License-Identifier: GPL-3.0-only
#include "Target.h"
#include "Shared.h"
namespace locks {
// Read immutable metadata on the game's main loop. These layouts are from
// xNVSE/JIP for the version already accepted by NVSEPlugin_Query.
void UpdateTarget(GameState &s) {
  if (!s.target)
    return;
  auto *ref = reinterpret_cast<const unsigned char *>(s.target);
  if (!Readable(ref, 0x68))
    return;
  auto *base = *reinterpret_cast<const unsigned char *const *>(ref + 0x20);
  if (!Readable(base, 0x18))
    return;
  unsigned type = base[4];
  s.targetIsDoor = type == 0x1c;
  size_t model = type == 0x1b ? 0x48 : type == 0x1c ? 0x3c : 0;
  if (!model || !Readable(base, model + 0x20))
    return;
  s.targetRefId = *reinterpret_cast<const uint32_t *>(ref + 0xc);
  s.targetBaseId = *reinterpret_cast<const uint32_t *>(base + 0xc);
  // A texture swap changes the material on the existing geometry. World
  // hardware uses that geometry, while a close-up copy needs matching materials.
  s.targetHasTextureSwap = *reinterpret_cast<const uintptr_t *>(base + model + 0x18) != 0;
  auto *path = *reinterpret_cast<const char *const *>(base + model + 4);
  std::string name;
  const bool readablePath = Readable(path, 256);
  for (size_t n = 0; n < 256; n++) {
    if (!readablePath && !Readable(path + n, 1))
      return;
    unsigned char c = path[n];
    if (!c) {
      s.targetKind = targetKind(name);
      return;
    }
    if (c < 32 || c > 126)
      return;
    name.push_back(char(c));
  }
}
} // namespace locks
