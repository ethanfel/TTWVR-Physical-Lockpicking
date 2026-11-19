// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Render.h"
#include "Shared.h"
namespace locks {
void InitNativeScene(void *scripts);
void NativeSceneTick(const GameState &state);
void NativeSceneCalibration(Pose rightEye);
bool NativeScenePublish(const GameState &, const Scene &, SceneGeometry &&);
void NativeSceneStop();
bool NativeScenePresented(uintptr_t menu);
} // namespace locks
