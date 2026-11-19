// SPDX-License-Identifier: GPL-3.0-only
#include "HostInput.h"
#include "Shared.h"
#include <atomic>
#include <cstring>

namespace locks {
namespace {
using Poll = bool (__thiscall *)(void *, HostFrame *);
struct Backend {
  unsigned table, poll, pollBody, pulse, pulseBody;
};
// Each supported TTWVR method has a linker entry and a direct implementation.
constexpr Backend openVR[]{
    {0xad7e40, 0x38b4, 0x6b46e0, 0x225de, 0x6b1ef0},
    {0xaf36d0, 0x38e6, 0x6cb930, 0x22868, 0x6c9140}};
constexpr Backend openXR[]{
    {0xad7cd8, 0x2335d, 0x6d8e70, 0x3edb, 0x6c71a0},
    {0xaf3568, 0x23600, 0x6f00d0, 0x3f0d, 0x6de400}};

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
    const char *name = isOpenVR ? "OpenVR" : "OpenXR";
    void *expected = slots[6];
    if (expected == reinterpret_cast<void *>(poll)) return true;
    const uintptr_t pollRva = reinterpret_cast<uintptr_t>(expected) - image;
    const uintptr_t pulseRva = reinterpret_cast<uintptr_t>(slots[28]) - image;
    if ((pollRva != entry.poll && pollRva != entry.pollBody) ||
        (pulseRva != entry.pulse && pulseRva != entry.pulseBody)) {
      Log("host_input_unavailable backend=%s reason=method_layout poll_rva=%08x pulse_rva=%08x",
          name, unsigned(pollRva), unsigned(pulseRva));
      return false;
    }
    original = reinterpret_cast<Poll>(expected);
    pulse = reinterpret_cast<HostPulse>(slots[28]);
    DWORD before;
    if (!VirtualProtect(slots + 6, sizeof(void *), PAGE_READWRITE, &before)) {
      Log("host_input_unavailable backend=%s reason=table_read_only win32=%lu", name, GetLastError());
      return false;
    }
    void *previous = InterlockedCompareExchangePointer(slots + 6, reinterpret_cast<void *>(poll), expected);
    DWORD unused;
    VirtualProtect(slots + 6, sizeof(void *), before, &unused);
    if (previous != expected)
      Log("host_input_unavailable backend=%s reason=method_changed", name);
    return previous == expected;
  }
};
// Both TTWVR backends feed the same controller-frame dispatcher. A mod may
// already own a backend method, so keep the game's current virtual dispatch.
std::atomic<void **> dispatchOpenVR{nullptr}, dispatchOpenXR{nullptr};
bool __fastcall pollDispatch(void *backend, void *, HostFrame *frame) {
  void **table = *static_cast<void ***>(backend);
  const bool valid = reinterpret_cast<Poll>(table[6])(backend, frame);
  const bool openVR = table == dispatchOpenVR.load();
  if (frame && (openVR || table == dispatchOpenXR.load())) {
    auto pulse = reinterpret_cast<HostPulse>(table[28]);
    ProcessHostInput(backend, pulse, *frame, valid, openVR);
  }
  return valid;
}

bool installDispatch(uintptr_t image, bool version117) {
  // This aligned eight-byte group ends at the instruction testing the poll result.
  // Keep the frame argument and vtable load; replace only the five-byte method call.
  const uintptr_t block = image + (version117 ? 0x6fb698 : 0x6e43f8);
  constexpr unsigned char original[8]{0x52, 0x8b, 0x01, 0x8b, 0x40, 0x18, 0xff, 0xd0};
  unsigned char replacement[8]{0x52, 0x8b, 0x01, 0xe8};
  const uint32_t relative = reinterpret_cast<uintptr_t>(pollDispatch) - (block + 8);
  std::memcpy(replacement + 4, &relative, sizeof(relative));
  if (std::memcmp(reinterpret_cast<void *>(block), replacement, 8) == 0) return true;
  if (std::memcmp(reinterpret_cast<void *>(block), original, 8) != 0) {
    Log("host_input_dispatch installed=0 reason=changed_call");
    return false;
  }
  DWORD protection;
  if (!VirtualProtect(reinterpret_cast<void *>(block), 8, PAGE_EXECUTE_READWRITE, &protection)) {
    Log("host_input_dispatch installed=0 reason=call_read_only win32=%lu", GetLastError());
    return false;
  }
  LONG64 before, after;
  std::memcpy(&before, original, sizeof(before));
  std::memcpy(&after, replacement, sizeof(after));
  const bool installed = InterlockedCompareExchange64(reinterpret_cast<volatile LONG64 *>(block),
                                                       after, before) == before;
  if (installed) FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void *>(block), 8);
  DWORD ignored;
  VirtualProtect(reinterpret_cast<void *>(block), 8, protection, &ignored);
  if (!installed) Log("host_input_dispatch installed=0 reason=changed_call");
  return installed;
}
}
bool InstallHostInput(uintptr_t image, bool version117) {
  const bool vr = Adapter<true>::install(image, openVR[version117]);
  const bool xr = Adapter<false>::install(image, openXR[version117]);
  bool dispatch = false;
  if (!vr || !xr) {
    dispatchOpenVR = vr ? nullptr : reinterpret_cast<void **>(image + openVR[version117].table);
    dispatchOpenXR = xr ? nullptr : reinterpret_cast<void **>(image + openXR[version117].table);
    dispatch = installDispatch(image, version117);
    if (dispatch) Log("host_input_dispatch installed=1 openvr=%d openxr=%d", !vr, !xr);
  }
  Log("host_input_installed openvr=%d openxr=%d", vr || dispatch, xr || dispatch);
  return xr || dispatch;
}
}
