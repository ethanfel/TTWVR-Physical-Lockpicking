// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Math.h"
#include <cstddef>
#include <cstdint>

namespace locks {
// Tracking sample produced by both TTWVR 1.1.6/1.1.7 backends, in metres.
struct HostPose {
  Vec position;
  Quat orientation;
  uint8_t valid, tracked, padding[2];
  Pose pose() const { return {orientation, position}; }
  bool usable() const {
    const auto &q = orientation;
    float norm = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    return valid && finite(position) && std::isfinite(norm) && std::abs(norm - 1.f) < .05f;
  }
};
struct HostFrame {
  HostPose head;
  struct Hand { HostPose aim, grip; } hands[2];
  struct { HostPose pose; float fov[4]; } eyes[2];
  float sticks[4], trigger[2], squeeze[2];
  uint8_t buttons[12], valid, padding[3];
};
static_assert(offsetof(HostFrame::Hand, aim) == 0 && offsetof(HostFrame::Hand, grip) == 0x20);
static_assert(sizeof(HostPose) == 0x20 && offsetof(HostPose, valid) == 0x1c);
static_assert(sizeof(HostFrame) == 0x130 && offsetof(HostFrame, hands) == 0x20 &&
              offsetof(HostFrame, eyes) == 0xa0 && offsetof(HostFrame, trigger) == 0x110 &&
              offsetof(HostFrame, squeeze) == 0x118 && offsetof(HostFrame, valid) == 0x12c);
using HostPulse = void (__thiscall *)(void *, bool, float, float);
// Returns whether TTWVR can supply OpenXR input.
bool InstallHostInput(uintptr_t image, bool version117);
void InstallHostXrSubmission(uintptr_t image, bool version117);
void ProcessHostInput(void *backend, HostPulse pulse, HostFrame &frame, bool valid, bool openVR);
bool HostMenuPresented(uintptr_t menu);
}
