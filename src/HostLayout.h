// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string_view>

namespace locks {
// Offsets are relative to the loaded TTWVR.dll. Select a layout only after
// identifying the complete host file, including the known Linux renderer variants.
struct HostLayout {
  struct Backend { unsigned table, poll, pollBody, pulse, pulseBody; };
  struct Call { unsigned site, target; };
  struct Holsters { unsigned bones, hands, bindings, positions, enabled, ready, units, editors, anchorStride; };
  struct WristInventory { unsigned gate, tab, selection; Call commit; };

  const char *version;
  Holsters holsters;
  Backend openVR, openXR;
  unsigned dispatch;
  Call endFrame, center, restoreHand, sleeve;
  unsigned lootSites[3], lootPreference;
  Call autoWrist, hover;
  WristInventory inventory;
};

inline constexpr HostLayout host116{
    "1.1.6",
    {0x10c9d7c, 0x15f6fd0, 0x101cd50, 0x15f7f38, 0x15f7fb8, 0x10cadcc, 0x10ce970, 0x101ce24, 20},
    {0xad7e40, 0x38b4, 0x6b46e0, 0x225de, 0x6b1ef0},
    {0xad7cd8, 0x2335d, 0x6d8e70, 0x3edb, 0x6c71a0}, 0x6e43f8,
    {0x6c4d34, 0x21bd9}, {0x221573, 0x21f4f0}, {0x211945, 0x1ee4e0}, {0x1fe0c5, 0x20eca0},
    {0x1262db, 0x1264a0, 0x13a4aa}, 0xb172, {0x100fa6, 0x2545}, {0x136b3e, 0x11ca70},
    {0x13c49a, 0x122e7b4, 0x122e7f0, {0x13ea9d, 0x139f10}}};

inline constexpr HostLayout host117{
    "1.1.7",
    {0x10e5efc, 0x1613b30, 0x1038d78, 0x1614a98, 0x1614b18, 0x10e6f4c, 0x10eaaf8, 0x1038e4c, 20},
    {0xaf36d0, 0x38e6, 0x6cb930, 0x22868, 0x6c9140},
    {0xaf3568, 0x23600, 0x6f00d0, 0x3f0d, 0x6de400}, 0x6fb698,
    {0x6dbf94, 0x21e5e}, {0x2288c3, 0x226840}, {0x218c85, 0x1f5820}, {0x205405, 0x215fe0},
    {0x12713b, 0x127300, 0x13b32a}, 0xb235, {0x101aa6, 0x255e}, {0x1379be, 0x11d5a0},
    {0x13d31a, 0x124a954, 0x124a990, {0x13fd4d, 0x13ad90}}};

inline constexpr HostLayout host118{
    "1.1.8",
    {0x12f237c, 0x182eee0, 0x1243e88, 0x182fe48, 0x182fec8, 0x12f33cc, 0x12f6f78, 0x1244000, 32},
    {0xcf6450, 0x3ef9, 0x80ae70, 0x275d9, 0x808680},
    {0xcf62e8, 0x28506, 0x82f4f0, 0x45c0, 0x81d8c0}, 0x83c3b8,
    {0x81b457, 0x26a44}, {0x23b496, 0x238420}, {0x22a955, 0x2074f0}, {0x2170d5, 0x227cb0},
    {0x13352b, 0x1336f0, 0x14788a}, 0xc9af, {0x10bcd6, 0x2892}, {0x143e0e, 0x129740},
    {0x149eda, 0x1456de4, 0x1456e20, {0x14c90d, 0x1471e0}}};

inline const HostLayout *identifyHost(std::string_view sha256) {
  if (sha256 == "34a4e692927e331e317be993e5a73c17695634a5442e7f85bd9b3ce547c7ff0d" ||
      sha256 == "ec59aa4b8eb143dad7b0950415aa6b94020d66034749141aafc65cd56d370b39" ||
      sha256 == "ec33def19bafa1ef753a2a33c2509af9948dfa58952cd79ab81c9cb209c8a40c" ||
      sha256 == "5262d92f1e64a5374a702dffd8b779285c49e49091d9582a8fbd5cade3d99805") return &host118;
  if (sha256 == "119a085a5b0101f0618b8e076a421b4b89e30d72d9b6eae874f4e5af7bd3fb27" ||
      sha256 == "ae7dc0d7ba94651af0ccd729122f623d3dbdf0b85c6c787838c04bbc721135d6") return &host117;
  if (sha256 == "503266c4f4c2350b357c8c15021d8bde28973254745d9302b8e3cfde66489a27" ||
      sha256 == "63712922499b2155a3d2c6f54abbdfc1eaf95d08712b937c6276d6a8ae0d774d") return &host116;
  return nullptr;
}
} // namespace locks
