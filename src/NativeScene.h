// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Render.h"
#include "Shared.h"
namespace locks {
void InitNativeScene(void *scripts);
void NativeSceneTick(const GameState &state);
void NativeSceneCalibration(Pose rightEye);
bool NativeScenePublish(const GameState &, const Scene &, SceneGeometry &&);
void NativeSceneSuspend();
bool KeyCloseupPlacement(const Ergonomics &, Transform &);
bool PaintKeyCloseup(const GameState &, const Ergonomics &, const Transform &, float signedAngle);
void EndKeyCloseup();
void NativeSceneStop();
bool NativeScenePresented(uintptr_t menu);
} // namespace locks
