// SPDX-License-Identifier: GPL-3.0-only
#include "HostInput.h"
#include "Shared.h"

namespace locks {
namespace {
using Poll = bool (__thiscall *)(void *, HostFrame *);
struct Backend {
  unsigned table, poll, pulse;
};
constexpr Backend openVR[]{{0xad7e40,0x38b4,0x225de},{0xaf36d0,0x38e6,0x22868}};
constexpr Backend openXR[]{{0xad7cd8,0x2335d,0x3edb},{0xaf3568,0x23600,0x3f0d}};

template<bool isOpenVR> struct Adapter {
  static inline Poll original = nullptr;
  static inline HostPulse pulse = nullptr;
  static bool __fastcall poll(void *self, void *, HostFrame *frame) {
    const bool valid = original(self, frame);
    if (frame) ProcessHostInput(self, pulse, *frame, valid, isOpenVR);
    return valid;
  }
  static bool install(uintptr_t image, Backend entry) {
    auto slots = reinterpret_cast<void **>(image + entry.table);
    const auto expected = reinterpret_cast<void *>(image + entry.poll);
    if (slots[6] != expected || slots[28] != reinterpret_cast<void *>(image + entry.pulse))
      return false;
    original = reinterpret_cast<Poll>(expected);
    pulse = reinterpret_cast<HostPulse>(slots[28]);
    DWORD before;
    if (!VirtualProtect(slots + 6, sizeof(void *), PAGE_READWRITE, &before)) return false;
    void *previous = InterlockedCompareExchangePointer(slots + 6, reinterpret_cast<void *>(poll), expected);
    DWORD unused;
    VirtualProtect(slots + 6, sizeof(void *), before, &unused);
    return previous == expected;
  }
};
}
void InstallHostInput(uintptr_t image, bool version117) {
  // The two tables are independent. Only the selected backend will poll.
  const bool vr = Adapter<true>::install(image, openVR[version117]);
  const bool xr = Adapter<false>::install(image, openXR[version117]);
  Log("host_input_installed openvr=%d openxr=%d", vr, xr);
}
}
