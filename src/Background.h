// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ToolGeometry.h"
#include <d3d11.h>
#include <openxr.h>
#include <vector>
namespace locks {
struct BackgroundView {
  ID3D11ShaderResourceView *image = nullptr;
  XrRect2Di rect{};
  unsigned width = 0, height = 0;
  XrFovf fov{};
  Quat rotation{}; // destination-eye rays to source-eye rays
};
// Private GPU copy of one successfully waited application swapchain release.
class ReleasedImage {
  std::vector<ID3D11Texture2D *> textures;
  std::vector<ID3D11ShaderResourceView *> slices;
  unsigned frame = 0;

public:
  ReleasedImage() = default;
  ReleasedImage(const ReleasedImage &) = delete;
  ReleasedImage &operator=(const ReleasedImage &) = delete;
  ~ReleasedImage() { Clear(); }
  void Clear();
  void Invalidate() { frame = 0; }
  bool Save(ID3D11Device *device, ID3D11Texture2D *image, unsigned stamp);
  BackgroundView View(unsigned slice, unsigned stamp) const;
};

class Background {
  ID3D11VertexShader *vs = nullptr;
  ID3D11PixelShader *blur = nullptr, *compose = nullptr;
  ID3D11Buffer *constants = nullptr;
  ID3D11SamplerState *sampler = nullptr;
  ID3D11Texture2D *temporary[2]{};
  ID3D11RenderTargetView *target[2]{};
  ID3D11ShaderResourceView *filtered[2]{};

public:
  ~Background();
  bool Init(ID3D11Device *d);
  bool Draw(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv,
            const BackgroundView &source, XrFovf view, unsigned width, unsigned height,
            float radius, float brightness);
};
} // namespace locks
