// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ToolGeometry.h"
#include <d3d11.h>
#include <map>
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
class Background {
  struct Copy {
    std::vector<ID3D11Texture2D *> textures;
    std::vector<ID3D11ShaderResourceView *> slices;
    unsigned frame = 0;
  };
  std::map<XrSwapchain, Copy> copies;
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *immediate = nullptr;
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
  void Erase(XrSwapchain key);
  void Clear() { while (!copies.empty()) Erase(copies.begin()->first); }
  bool Save(XrSwapchain key, ID3D11Texture2D *image, unsigned frame);
  BackgroundView View(XrSwapchain key, unsigned slice, unsigned frame);
  bool Draw(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv,
            const BackgroundView &source, XrFovf view, unsigned size,
            float radius, float brightness);
};
} // namespace locks
