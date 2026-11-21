// SPDX-License-Identifier: GPL-3.0-only
#include "Target.h"
#include "Shared.h"
#include <filesystem>
#include <fstream>
#include <sstream>
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
  auto *swaps = reinterpret_cast<const uintptr_t *>(base + model + 0x18);
  if (*swaps) s.targetTextureSwaps = reinterpret_cast<uintptr_t>(swaps);
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

// Called on the game thread once per lock menu, before attaching the close-up.
std::optional<std::bitset<128>> CloseupHiddenParts(const GameState &s) {
  std::bitset<128> hidden;
  if (!s.targetTextureSwaps) return hidden;
  if (!s.targetKind || s.targetKind > targetKinds) return std::nullopt;

  // Each line lists the named source shapes in the same order as target-N.bin.
  static const auto catalogue = [] {
    std::array<std::vector<std::string>, targetKinds + 1> models;
    std::ifstream file(std::filesystem::path(folder) / L"TTWVRPhysicalLockpicking" / L"target-parts.tsv");
    std::string line;
    for (unsigned i = 1; i <= targetKinds && std::getline(file, line); ++i) {
      std::istringstream row(line);
      std::string shape;
      while (std::getline(row, shape, '\t')) models[i].push_back(shape);
    }
    return models;
  }();
  const auto &names = catalogue[s.targetKind];
  if (names.empty() || names.size() > hidden.size()) return std::nullopt;
  auto loaded = *reinterpret_cast<const uintptr_t *>(s.target + 0x64);
  if (!loaded || !Readable(reinterpret_cast<void *>(loaded + 0x14), sizeof(uintptr_t))) return std::nullopt;
  const auto root = *reinterpret_cast<const uintptr_t *>(loaded + 0x14);
  if (!root) return std::nullopt;

  struct Entry { uint32_t texture, index; char shape[128]; };
  struct Link { const Entry *entry; const Link *next; };
  auto *link = reinterpret_cast<const Link *>(s.targetTextureSwaps);
  for (size_t n = 0; link; link = link->next, ++n) {
    if (n >= hidden.size() || !Readable(link, sizeof(Link)) || !Readable(link->entry, sizeof(Entry)))
      return std::nullopt;
    const auto &entry = *link->entry;
    const char *end = static_cast<const char *>(std::memchr(entry.shape, 0, sizeof(entry.shape)));
    if (!end || end == entry.shape) return std::nullopt;
    const std::string shape(entry.shape, end);
    const auto part = std::find(names.begin(), names.end(), shape);
    if (part == names.end()) return std::nullopt;
    const auto node = FindSceneNode(root, shape);
    // NullTextureSet culls the real shape. A visible replacement would need
    // its material copied too, so keep the existing standalone fallback there.
    if (!node || !(*reinterpret_cast<const uint32_t *>(node + 0x30) & 1)) return std::nullopt;
    hidden.set(size_t(part - names.begin()));
  }
  if (hidden.count() == names.size()) return std::nullopt;
  Log("closeup_hidden_parts ref=%08x kind=%u hidden=%u parts=%u", s.targetRefId,
      s.targetKind, unsigned(hidden.count()), unsigned(names.size()));
  return hidden;
}
} // namespace locks
