// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Background.h"
#include "GameCapture.h"
#include "LockMesh.h"
#include "Physical.h"
#include "Props.h"
#include "Target.h"
#include "Video.h"
#include <d3d11.h>
#include <openxr.h>
#include <map>
#include <memory>
#include <vector>
namespace locks {
inline Pose pose(XrPosef p) {
  return {{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w},
          {p.position.x, p.position.y, p.position.z}};
}
struct Scene {
  Pose lock;
  float pick = 90, cylinder = 0;
  unsigned targetKind = 0;
  float targetOpenTime = 0;
  bool engaged = false;
  bool pickHeld = false, pickNear = false, screwdriverNear = false;
  Ergonomics config;
  PhysicalTool tools[2];
  Pose hands[2]{};
  float holds[2]{};
  bool tracked = false;
  unsigned pinchHands = 0;
  int pinStock = -1, storedPinCount = 0;
  std::array<StoredPin, pinBoxCapacity> storedPins;
};
struct SceneVertex { Vec p, c, uv, n; };
// A pending game-thread packet owns its immutable geometry even if the renderer switches targets.
using LocalVertices = std::shared_ptr<const std::vector<SceneVertex>>;
struct CachedNativePart {
  const PropMesh *asset = nullptr;
  LockMeshSize size = LockMeshSize::Unchanged;
  float surface = 0, opening = 0, twist = 0;
  LocalVertices vertices;
};
struct SceneDraw {
  unsigned id = 0;
  size_t begin = 0, count = 0;
  Pose pose;
  LocalVertices localVertices;
};
struct SceneGeometry {
  std::vector<SceneVertex> vertices;
  std::vector<SceneDraw> draws;
};
struct Vertex {
  float p[4], color[4], uv[3], normal[3], world[3], view[3];
};
struct HandVertex {
  Vec open, closed;
  float u, v;
  Vec normalOpen, normalClosed, pinched, normalPinched;
};
struct HandMesh {
  Vec pinchPoint;
  Pose driverGrip;
  bool hasDriverGrip = false;
  bool hasPinch = false;
  std::vector<HandVertex> vertices;
  std::vector<std::array<uint32_t, 3>> triangles;
};
class Renderer {
  // Full 18-pin round-wire pile, both hands and the padlock exceed 64K.
  static constexpr size_t vertexCapacity = 131072;
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *deferred = nullptr, *immediate = nullptr;
  ID3D11VertexShader *vs = nullptr;
  ID3D11PixelShader *ps = nullptr;
  ID3D11InputLayout *layout = nullptr;
  ID3D11Buffer *buffer = nullptr;
  ID3D11Texture2D *depth = nullptr;
  ID3D11DepthStencilView *dsv = nullptr;
  unsigned depthWidth = 0, depthHeight = 0;
  bool Depth(unsigned width, unsigned height);
  ID3D11RasterizerState *raster = nullptr;
  ID3D11ShaderResourceView *handTexture = nullptr;
  ID3D11SamplerState *sampler = nullptr;
  HandMesh hands[2];
  Props props;
  std::map<unsigned, CachedNativePart> nativeParts;
  PropMesh plugSkirts[2];
  bool propsReady = false;
  ID3D11ShaderResourceView *propTextures[8]{};
  TargetAsset targets[targetKinds];
  ID3D11ShaderResourceView *targetTextures[targetKinds][16]{};
  ID3D11BlendState *blend = nullptr;
  unsigned loadedTarget = 0;
  ID3D11ShaderResourceView *hardwareTextures[4]{};
  bool LoadProps();
  bool LoadTexture(const std::wstring &path, bool srgb,
                   ID3D11ShaderResourceView **out);
  bool LoadHands();

public:
  void EnsureTarget(unsigned k);
  const TargetAsset *Target(unsigned k) const {
    return k && k <= targetKinds && targets[k - 1].kind ? &targets[k - 1]
                                                        : nullptr;
  }
  Background background;
  VideoCapture video;
  GameCapture gameCapture, stillCapture;
  ID3D11Texture2D *GameFrame(unsigned eye, XrFovf view,
                             const std::vector<CaptureLayer> &layers) {
    return gameCapture.Draw(device, immediate, eye, captureSize, captureSize, view, layers);
  }
  ID3D11Texture2D *StillFrame(unsigned eye, XrFovf view,
                             const std::vector<CaptureLayer> &layers,
                             unsigned width, unsigned height) {
    return stillCapture.Draw(device, immediate, eye, width, height, view, layers);
  }
  ~Renderer();
  bool Init(ID3D11Device *d);
  SceneGeometry BuildScene(const Scene &scene, bool native = false);
  bool Draw(ID3D11Texture2D *image, const XrView &view, const Scene &scene,
            const BackgroundView &backgroundView = {});
  bool Capture(ID3D11Texture2D *image, const wchar_t *path);
  void RecordEye(unsigned eye, ID3D11Texture2D *image, XrFovf fov);
  Vec PinchPoint(int hand) const {
    return hands[hand].hasPinch ? hands[hand].pinchPoint
                                : Vec{hand ? -.039f : .039f, -.040f, -.029f};
  }
  Pose DriverGrip(int hand) const {
    return hands[hand].hasDriverGrip
               ? hands[hand].driverGrip
               : Pose{Quat{}, {hand ? -.015f : .015f, -.025f, 0}};
  }
  static constexpr unsigned captureSize = 1024;
};
} // namespace locks
