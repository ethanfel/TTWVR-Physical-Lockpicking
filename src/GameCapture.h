// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Background.h"
#include <array>
namespace locks {
struct CaptureLayer {
  BackgroundView image;
  bool quad = false;
  XrCompositionLayerFlags flags = 0;
  std::array<std::array<float, 4>, 4> corners{};
};
// Private capture surfaces only. Never submitted to OpenXR or drawn into game
// images.
class GameCapture {
  ID3D11VertexShader *vs = nullptr;
  ID3D11PixelShader *ps = nullptr;
  ID3D11Buffer *constants = nullptr;
  ID3D11SamplerState *sampler = nullptr;
  ID3D11BlendState *blend = nullptr;
  ID3D11RasterizerState *raster = nullptr;
  ID3D11DeviceContext *deferred = nullptr;
  ID3D11Texture2D *images[2]{};
  ID3D11RenderTargetView *targets[2]{};
  unsigned size = 0;
  bool Init(ID3D11Device *device, unsigned eyeSize);

public:
  ~GameCapture() { Reset(); }
  void Reset();
  ID3D11Texture2D *Draw(ID3D11Device *device, ID3D11DeviceContext *immediate,
                        unsigned eye, unsigned eyeSize, XrFovf view,
                        const std::vector<CaptureLayer> &layers);
};
} // namespace locks
