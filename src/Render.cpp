// SPDX-License-Identifier: GPL-3.0-only
#include "Render.h"
#include "PadlockMesh.h"
#include "SafeFrameMesh.h"
#include "Shared.h"
#include <cstdio>
#include <cstring>
#include <d3dcompiler.h>
namespace locks {
namespace {
template <class T> void release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}
struct Mesh {
  using V = SceneVertex;
  std::vector<V> v;
  std::vector<SceneDraw> draws;
  unsigned object = 0;
  std::map<unsigned, CachedNativePart> *nativeParts = nullptr;
  void localDraw(const LocalVertices &vertices, Pose pose, bool handLighting = false) {
    draws.push_back({object++, v.size(), vertices->size(), pose, vertices, handLighting});
  }
  void finish(size_t first, Pose pose = {}) {
    draws.push_back({object++, first, v.size()-first, pose});
  }
  void tri(Vec a, Vec b, Vec c, Vec color) {
    Vec n = normal(cross(b - a, c - a));
    float light = .50f + .5f * std::abs(dot(n, normal({-.3f, .6f, 1})));
    color = color * light;
    v.push_back({a, color, {}, n});
    v.push_back({b, color, {}, n});
    v.push_back({c, color, {}, n});
  }
  void prop(const PropMesh &asset, Pose pose = {}, float surface = 0,
            LockMeshSize size = LockMeshSize::Unchanged) {
    if (nativeParts) {
      auto &part = (*nativeParts)[object];
      if (!part.vertices || part.asset != &asset || part.size != size || part.surface != surface) {
        auto vertices = std::make_shared<std::vector<V>>();
        vertices->reserve(asset.triangles.size() * 3);
        for (const auto &tri : asset.triangles)
          for (auto index : tri) {
            auto x = fitLockVertex(asset.vertices[index], size);
            vertices->push_back({x.p, {1, 1, 1},
                                 {x.u, x.v, surface ? surface : float(asset.material + 2)}, x.n});
          }
        part = {&asset, size, surface, 0, 0, vertices};
      }
      localDraw(part.vertices, pose);
      return;
    }
    size_t first = v.size();
    for (auto tri : asset.triangles)
      for (auto index : tri) {
        const auto x = fitLockVertex(asset.vertices[index], size);
        v.push_back({world(pose, x.p),
                     {1, 1, 1},
                     {x.u, x.v, surface ? surface : float(asset.material + 2)},
                     rotate(pose.q, x.n)});
      }
    finish(first, pose);
  }
  void texturedPin(const Props &props, float opening, float twist, Pose pose) {
    Vec hinge = pinHinge(opening, twist);
    prop(props[PinShaft], compose(pose, {shortestArc({0, 0, 1}, hinge), {}}),
         5);
    prop(props[PinHandle],
         compose(pose,
                 {{}, hinge + Vec{.006f * std::max(0.f, 1 - opening), 0, 0}}),
         5);
    if (nativeParts) {
      auto &part = (*nativeParts)[object];
      if (!part.vertices || part.opening != opening || part.twist != twist) {
        Mesh bend;
        bend.pinBend(opening, twist, {});
        part = {nullptr, LockMeshSize::Unchanged, 0, opening, twist,
                std::make_shared<std::vector<V>>(std::move(bend.v))};
      }
      localDraw(part.vertices, pose);
      return;
    }
    size_t bendBegin = v.size();
    pinBend(opening, twist, pose);
    finish(bendBegin, pose);
  }
  void pinBend(float opening, float twist, Pose pose) {
    Vec previous = pinBendPoint(opening, 0, twist);
    for (int i = 1; i <= 8; i++) {
      Vec next = pinBendPoint(opening, i / 8.f, twist);
      size_t first = v.size();
      rod(world(pose, previous), world(pose, next), pinWireRadius, {1, 1, 1}, 6);
      for (size_t n = first; n < v.size(); ++n) {
        v[n].c = {1, 1, 1};
        v[n].uv = {(i - .5f) / 8, .5f, 5};
      }
      previous = next;
    }
  }
  void box(Vec a, Vec b, Vec color) {
    Vec p[8];
    for (int i = 0; i < 8; i++)
      p[i] = {i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z};
    int faces[][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1},
                      {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (auto &f : faces) {
      tri(p[f[0]], p[f[1]], p[f[2]], color);
      tri(p[f[0]], p[f[2]], p[f[3]], color);
    }
  }
  void rod(Vec a, Vec b, float r, Vec color, int steps = 24) {
    Vec axis = normal(b - a),
        u = normal(
            cross(axis, std::abs(axis.y) < .9f ? Vec{0, 1, 0} : Vec{1, 0, 0})),
        w = cross(axis, u);
    for (int i = 0; i < steps; i++) {
      float t = i * 2 * pi / steps, s = (i + 1) * 2 * pi / steps;
      Vec x = (u * std::cos(t) + w * std::sin(t)) * r,
          y = (u * std::cos(s) + w * std::sin(s)) * r;
      tri(a + x, b + x, b + y, color);
      tri(a + x, b + y, a + y, color);
      tri(a, a + y, a + x, color);
      tri(b, b + x, b + y, color);
    }
  }
};
Mesh mesh(const Scene &s, HandMesh (&hands)[2], const Props *props,
          const TargetAsset *target, const PropMesh *plugSkirts,
          std::map<unsigned, CachedNativePart> *nativeParts, bool native) {
  Mesh m;
  m.nativeParts = nativeParts;
  Vec brass{.48f, .27f, .075f}, steel{.44f, .5f, .52f};
  float openTime = targetPresentationTime(s.config.nativeScene, s.targetOpenTime);
  Pose opening = target ? target->Motion(openTime) : Pose{};
  m.object = 1000;
  if (target)
    for (const auto &part : target->parts)
      m.prop(part.mesh, part.moving ? compose(s.targetFace, opening) : s.targetFace,
             float((part.alpha ? 20 : 10) + part.mesh.material));
  bool padlock = target && target->bounds.style == LockStyle::Padlock;
  bool safe = target && target->bounds.style == LockStyle::RoundedSafe;
  size_t lockBegin = m.v.size(), lockDrawBegin = m.draws.size();
  if (padlock) {
    static const PadlockMesh pad;
    m.object = 4;
    m.prop(pad.body, {}, 30);
    m.prop(pad.shackle, padlockShackleMotion(openTime), 30);
    m.prop(pad.staple, opening, 30);
    if (!props)
      for (size_t n = lockBegin; n < m.v.size(); n++) {
        auto &v = m.v[n];
        v.uv = {};
        v.c = Vec{.34f, .32f, .26f} *
              (.5f + .5f * std::abs(dot(v.n, normal({-.3f, .6f, 1}))));
      }
  }
  if (safe) {
    static const SafeFrameMesh frame;
    m.object = 7;
    m.prop(frame.body, {}, 31);
    if (!props)
      for (size_t n = lockBegin; n < m.v.size(); n++) {
        auto &v = m.v[n];
        v.uv = {};
        v.c = Vec{.065f, .072f, .075f} *
              (.5f + .5f * std::abs(dot(v.n, normal({-.3f, .6f, 1}))));
      }
  }
  if (props) {
    m.object = 1;
    if (!padlock && !safe)
      m.prop((*props)[Frame], {}, 0, LockMeshSize::Round);
    m.object = 2;
    m.prop((*props)[Cylinder], turnWithCylinder({}, s.cylinder), padlock ? 8 : safe ? 9 : 6,
           padlock ? LockMeshSize::Padlock : LockMeshSize::RoundPlug);
    m.prop(plugSkirts[padlock ? 1 : 0],turnWithCylinder({},s.cylinder),padlock ? 8 : safe ? 9 : 6);
  } else {
    if (!padlock && !safe) {
      m.rod({0, 0, -.012f}, {0, 0, .014f}, roundFrameRadius, brass, 48);
      m.rod({0, 0, .014f}, {0, 0, .02f}, roundShoulderRadius, {.25f, .28f, .27f}, 48);
    }
    // Cylinder face, key slot and tool all rotate independently of the housing.
    float c = -s.cylinder * pi / 180;
    Vec side{std::cos(c), std::sin(c), 0}, up{-std::sin(c), std::cos(c), 0};
    m.rod({0, 0, .02f}, {0, 0, .024f}, plugRadius(padlock ? LockStyle::Padlock : LockStyle::Round),
          safe ? Vec{.12f, .13f, .14f} : Vec{.6f, .37f, .12f}, 40);
    auto style = padlock ? LockStyle::Padlock : LockStyle::Round;
    float width = keywayWidth(style), height = keywayHeight(style);
    Vec center{0, 0, .025f};
    Vec a = center + side * width + up * height,
        b = center - side * width + up * height,
        d = center - side * width - up * height,
        e = center + side * width - up * height;
    m.tri(a, b, d, {.005f, .008f, .006f});
    m.tri(a, d, e, {.005f, .008f, .006f});
  }
  if (target && !padlock && openTime > 0) {
    for (size_t n = lockBegin; n < m.v.size(); n++) {
      m.v[n].p = world(opening, m.v[n].p);
      m.v[n].n = rotate(opening.q, m.v[n].n);
    }
    for(size_t n=lockDrawBegin;n<m.draws.size();n++)
      m.draws[n].pose = compose(opening,m.draws[n].pose);
  }
  size_t begin;
  if (s.config.useHolders)
    for (int k = 0; k < 2; k++) {
      if (props) {
        m.object = 10 + k;
        m.prop((*props)[k == 0 ? PinBoxProp : DriverCan],
               {{}, holderPoint(s.config, k)});
        continue;
      }
      Vec p = holderPoint(s.config, k), leather{.18f, .105f, .05f};
      float back = k == 0 ? -.09f : -.027f, bottom = k == 0 ? -.105f : -.155f;
      m.box(p + Vec{-.031f, bottom, back},
            p + Vec{.031f, bottom + .007f, .028f}, leather);
      m.box(p + Vec{-.031f, bottom, back}, p + Vec{-.025f, -.025f, .028f},
            leather);
      m.box(p + Vec{.025f, bottom, back}, p + Vec{.031f, -.025f, .028f},
            leather);
      m.box(p + Vec{-.031f, bottom, back}, p + Vec{.031f, -.025f, back + .006f},
            leather);
      m.box(p + Vec{-.031f, bottom, .022f}, p + Vec{.031f, -.025f, .028f},
            leather);
      m.rod(p + Vec{-.027f, -.024f, .029f}, p + Vec{.027f, -.024f, .029f},
            .002f, brass, 8);
    }
  if (s.config.useHolders) {
    for (int slot = 0; slot < s.storedPinCount; slot++) {
      const auto &stored = s.storedPins[slot];
      if (props) {
        m.object = 100 + slot * 3;
        m.texturedPin(*props, stored.opening, stored.twist, stored.pose);
        continue;
      }
      size_t start = m.v.size();
      Vec wire{.64f, .53f, .30f},
          previous = pinBendPoint(stored.opening, 0, stored.twist);
      m.rod({}, previous, pinWireRadius, wire, 6);
      for (int n = 1; n <= 4; n++) {
        Vec next = pinBendPoint(stored.opening, n / 4.f, stored.twist);
        m.rod(previous, next, pinWireRadius, wire, 6);
        previous = next;
      }
      for (int n = 1; n <= 6; n++) {
        Vec next = pinHandlePoint(stored.opening, n / 6.f, stored.twist);
        m.rod(previous, next, pinWireRadius, wire, 6);
        previous = next;
      }
      m.rod(previous, previous + Vec{0, .002f, 0}, .0021f, wire, 6);
      Pose p = stored.pose;
      for (size_t j = start; j < m.v.size(); j++)
        m.v[j].p = world(p, m.v[j].p);
    }
  }
  for (int k = 0; k < 2; k++) {
    const auto &tool = s.tools[k];
    if (!tool.visible || (k == 0 && tool.holstered && s.config.useHolders))
      continue;
    if (props) {
      m.object = k == 0 ? 200 : 203;
      if (k == 0)
        m.texturedPin(*props, tool.opening, tool.bendYaw, tool.pose);
      else
        m.prop((*props)[Driver], tool.pose);
      continue;
    }
    begin = m.v.size();
    auto handle = toolHandle(k, tool.opening, tool.bendYaw);
    bool nearby = k == 0 ? s.pickNear : s.screwdriverNear;
    Vec color = tool.owner >= 0 ? Vec{.1f, .6f, .2f}
                : nearby        ? Vec{.9f, .65f, .15f}
                : k == 0        ? Vec{.65f, .46f, .12f}
                                : Vec{.12f, .22f, .14f};
    if (k == 0) {
      Vec wire{.64f, .53f, .30f};
      Vec previous = pinBendPoint(tool.opening, 0, tool.bendYaw);
      m.rod({}, previous, pinWireRadius, wire, 10);
      for (int n = 1; n <= 12; n++) {
        Vec next = pinBendPoint(tool.opening, n / 12.f, tool.bendYaw);
        m.rod(previous, next, pinWireRadius, wire, 10);
        previous = next;
      }
      for (int n = 1; n <= 40; n++) {
        Vec next = pinHandlePoint(tool.opening, n / 40.f, tool.bendYaw);
        m.rod(previous, next, pinWireRadius, n > 23 ? color : wire, 10);
        previous = next;
      }
      m.rod(previous, previous + Vec{0, .002f, 0}, .0021f, wire, 10);
    } else {
      size_t bladeBegin=m.v.size();
      m.box({-driverBladeHalfWidth, -driverBladeHalfThickness, 0},
            {driverBladeHalfWidth, driverBladeHalfThickness, driverBladeEnd}, steel);
      for(size_t j=bladeBegin;j<m.v.size();j++) {
        auto &v=m.v[j];
        float width=driverBladeTipHalfWidth+(driverBladeHalfWidth-driverBladeTipHalfWidth)*v.p.z/driverBladeEnd;
        v.p.x*=width/driverBladeHalfWidth;
      }
      m.rod({0, 0, driverBladeEnd}, {0, 0, driverShaftStart}, .002f, steel, 12);
      m.rod({0, 0, driverShaftStart}, handle.a, driverShaftRadius, steel, 12);
      for(size_t j=bladeBegin;j<m.v.size();j++) {
        auto &v=m.v[j];v.n=driverMetalNormal(v.p,v.n);v.p=driverMetalPosition(v.p);
      }
      m.rod(handle.a, handle.b, driverHandleRadius, color, 20);
    }
    if (k == 1) {
      for (int n = 0; n < 6; n++) {
        Vec radial = screwdriverRadial(n * 60.f) * driverHandleRadius;
        m.rod(handle.a + Vec{0, 0, .006f} + radial,
              handle.b - Vec{0, 0, .006f} + radial, .0018f,
              n == 0 ? Vec{.8f, .57f, .18f} : Vec{.045f, .09f, .05f}, 8);
      }
    }
    for (size_t j = begin; j < m.v.size(); j++)
      m.v[j].p = world(tool.pose, m.v[j].p);
  }
  if (s.tracked)
    for (int n = 0; n < 2; n++) {
      if (!hands[n].triangles.empty()) {
        size_t first = m.v.size();
        m.object = 210 + n;
        auto &asset = hands[n];
        Pose hand = relative(s.lock, s.hands[n]);
        float curl = std::clamp(s.holds[n], 0.f, 1.f);
        bool pinch = (s.pinchHands & (1u << n)) && asset.hasPinch;
        if (native) {
          m.localDraw(asset.nativeVertices(curl, pinch), hand, true);
          continue;
        }
        for (auto tri : asset.triangles) {
          Vec points[3];
          for (int j = 0; j < 3; j++) {
            const auto &v = asset.vertices[tri[j]];
            points[j] = world(hand, v.open * (1 - curl) +
                                        (pinch ? v.pinched : v.closed) * curl);
          }
          m.tri(points[0], points[1], points[2], {1, 1, 1});
          for (int j = 0; j < 3; j++) {
            const auto &v = asset.vertices[tri[j]];
            auto &vertex = m.v[m.v.size() - 3 + j];
            vertex.uv = {v.u, v.v, 1};
            Vec n = normal(rotate(
                hand.q, v.normalOpen * (1 - curl) +
                            (pinch ? v.normalPinched : v.normalClosed) * curl));
            float light =
                .55f + .45f * std::abs(dot(n, normal({-.3f, .6f, 1})));
            vertex.c = {light, light, light};
            vertex.n = n;
          }
        }
        m.finish(first,hand);
      } else {
        Vec hand = local(s.lock, s.hands[n].p);
        Vec color = n ? Vec{.12f, .3f, .45f} : Vec{.5f, .32f, .08f};
        m.rod(hand + Vec{0, -.018f, 0}, hand + Vec{0, .018f, 0}, .009f, color,
              12);
        m.rod(hand + Vec{-.018f, 0, 0}, hand + Vec{.018f, 0, 0}, .009f, color,
              12);
      }
    }
  return m;
}
} // namespace
Renderer::~Renderer() {
  for (auto &t : hardwareTextures) release(t);
  for (auto &set : targetTextures)
    for (auto &texture : set)
      release(texture);
  release(blend);
  video.Stop();
  release(buffer);
  release(layout);
  release(vs);
  release(ps);
  release(dsv);
  release(depth);
  release(raster);
  release(handTexture);
  for (auto &t : propTextures)
    release(t);
  release(sampler);
  release(deferred);
  release(immediate);
  release(device);
}
bool Renderer::LoadHands() {
  auto path = folder + L"\\TTWVRPhysicalLockpicking\\hands.bin";
  auto *file = _wfopen(path.c_str(), L"rb");
  if (!file) {
    Log("hand_mesh loaded=0 reason=missing_local_assets");
    return false;
  }
  HandMesh loaded[2];
  char magic[8]{};
  bool ok = fread(magic, 1, 8, file) == 8;
  bool fitted = ok && memcmp(magic, "TTWHand4", 8) == 0;
  bool pinch = fitted || (ok && memcmp(magic, "TTWHand3", 8) == 0);
  ok &= pinch || memcmp(magic, "TTWHand2", 8) == 0;
  for (int h = 0; h < 2 && ok; h++) {
    uint32_t n = 0, t = 0;
    ok = fread(&n, 4, 1, file) == 1 && fread(&t, 4, 1, file) == 1 && n > 0 &&
         n <= 10000 && t > 0 && t <= 10000;
    if (!ok)
      break;
    auto &mesh = loaded[h];
    mesh.hasPinch = pinch;
    if (pinch) {
      ok = fread(&mesh.pinchPoint, sizeof(Vec), 1, file) == 1 &&
           finite(mesh.pinchPoint) && length(mesh.pinchPoint) < .2f;
      if (!ok)
        break;
    }
    mesh.hasDriverGrip = fitted;
    if (fitted) {
      ok = fread(&mesh.driverGrip, sizeof(Pose), 1, file) == 1 &&
           finite(mesh.driverGrip.p) && length(mesh.driverGrip.p) < .2f;
      auto q = mesh.driverGrip.q;
      float norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
      ok &= std::isfinite(norm) && std::abs(norm - 1) < .001f;
      if (!ok)
        break;
    }
    mesh.vertices.resize(n);
    mesh.triangles.resize(t);
    if (pinch)
      ok = fread(mesh.vertices.data(), sizeof(HandVertex), n, file) == n;
    else
      for (auto &v : mesh.vertices) {
        if (fread(&v, 56, 1, file) != 1) {
          ok = false;
          break;
        }
        v.pinched = v.closed;
        v.normalPinched = v.normalClosed;
      }
    ok = ok && fread(mesh.triangles.data(), 12, t, file) == t;
    for (const auto &v : mesh.vertices)
      ok &= finite(v.open) && finite(v.closed) && length(v.open) < .5f &&
            length(v.closed) < .5f && finite(v.pinched) &&
            length(v.pinched) < .5f && finite(v.normalPinched) &&
            finite(v.normalOpen) && finite(v.normalClosed) &&
            std::isfinite(v.u) && std::isfinite(v.v);
    for (auto tri : mesh.triangles)
      for (auto index : tri)
        ok &= index < n;
  }
  fclose(file);
  if (!ok)
    return false;
  if (!device) {
    for (int h = 0; h < 2; ++h) hands[h] = std::move(loaded[h]);
    return true;
  }
  path = folder + L"\\TTWVRPhysicalLockpicking\\hands.rgba";
  file = _wfopen(path.c_str(), L"rb");
  if (!file)
    return false;
  uint32_t width = 0, height = 0;
  ok = fread(&width, 4, 1, file) == 1 && fread(&height, 4, 1, file) == 1 &&
       width > 0 && width <= 1024 && height > 0 && height <= 1024;
  std::vector<unsigned char> rgba;
  if (ok) {
    rgba.resize(width * height * 4);
    ok = fread(rgba.data(), 1, rgba.size(), file) == rgba.size();
  }
  fclose(file);
  if (!ok)
    return false;
  D3D11_TEXTURE2D_DESC td{};
  td.Width = width;
  td.Height = height;
  td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  td.Usage = D3D11_USAGE_IMMUTABLE;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA source{rgba.data(), width * 4, 0};
  ID3D11Texture2D *texture = nullptr;
  if (FAILED(device->CreateTexture2D(&td, &source, &texture)))
    return false;
  auto result =
      device->CreateShaderResourceView(texture, nullptr, &handTexture);
  release(texture);
  if (FAILED(result))
    return false;
  for (int h = 0; h < 2; h++)
    hands[h] = std::move(loaded[h]);
  Log("hand_driver fitted=%d", fitted);
  Log("hand_pinch available=%d left=%.5f,%.5f,%.5f right=%.5f,%.5f,%.5f", pinch,
      hands[0].pinchPoint.x, hands[0].pinchPoint.y, hands[0].pinchPoint.z,
      hands[1].pinchPoint.x, hands[1].pinchPoint.y, hands[1].pinchPoint.z);
  Log("hand_mesh loaded=1 left_vertices=%u right_vertices=%u grip_curl=1",
      unsigned(hands[0].vertices.size()), unsigned(hands[1].vertices.size()));
  return true;
}
bool Renderer::LoadTexture(const std::wstring &path, bool srgb,
                           ID3D11ShaderResourceView **out) {
  FILE *f = _wfopen(path.c_str(), L"rb");
  if (!f)
    return false;
  uint32_t w = 0, h = 0;
  bool ok = fread(&w, 4, 1, f) == 1 && fread(&h, 4, 1, f) == 1 && w > 0 &&
            h > 0 && w <= 4096 && h <= 4096;
  std::vector<unsigned char> pixels;
  if (ok) {
    pixels.resize(size_t(w) * h * 4);
    ok = fread(pixels.data(), 1, pixels.size(), f) == pixels.size() &&
         fgetc(f) == EOF && !ferror(f);
  }
  fclose(f);
  if (!ok)
    return false;
  D3D11_TEXTURE2D_DESC td{};
  td.Width = w;
  td.Height = h;
  td.ArraySize = td.SampleDesc.Count = 1;
  td.Format =
      srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
  ID3D11Texture2D *texture = nullptr;
  if (FAILED(device->CreateTexture2D(&td, nullptr, &texture)))
    return false;
  auto result = device->CreateShaderResourceView(texture, nullptr, out);
  if (SUCCEEDED(result)) {
    deferred->UpdateSubresource(texture, 0, nullptr, pixels.data(), w * 4, 0);
    deferred->GenerateMips(*out);
  }
  release(texture);
  return SUCCEEDED(result);
}
bool Renderer::LoadProps() {
  auto root = folder + L"\\TTWVRPhysicalLockpicking\\";
  auto *f = _wfopen((root + L"props.bin").c_str(), L"rb");
  Props loaded;
  bool ok = readProps(f, loaded);
  if (f)
    fclose(f);
  if (ok && device)
    for (int i = 0; i < 8; i++) {
      std::wstring name = i >= 6 ? (i == 6 ? L"pin-metal.rgba" : L"pin-metal-n.rgba") :
          L"props-" + std::to_wstring(i % 3) + (i < 3 ? L".rgba" : L"-n.rgba");
      if (!LoadTexture(root + name, i < 3 || i == 6, &propTextures[i])) {
        ok = false;
        break;
      }
    }
  // Texture uploads are recorded on our context; execute with game state saved.
  ID3D11CommandList *commands = nullptr;
  if (deferred && SUCCEEDED(deferred->FinishCommandList(FALSE, &commands))) {
    immediate->ExecuteCommandList(commands, TRUE);
    release(commands);
  } else if (deferred)
    ok = false;
  if (!ok) {
    for (auto &t : propTextures)
      release(t);
    Log("prop_mesh loaded=0 reason=missing_or_invalid_local_assets "
        "fallback=procedural");
    return false;
  }
  for(auto &v:loaded[Driver].vertices) {
    fitPreparedDriverBlade(v.p, v.n);
    v.n=driverMetalNormal(v.p,v.n);v.p=driverMetalPosition(v.p);
  }
  props = std::move(loaded);
  plugSkirts[0]=makePlugSkirt(props[Cylinder],false);
  plugSkirts[1]=makePlugSkirt(props[Cylinder],true);
  Log("plug_skirts round_triangles=%u padlock_triangles=%u",unsigned(plugSkirts[0].triangles.size()),unsigned(plugSkirts[1].triangles.size()));
  unsigned count = 0;
  for (const auto &m : props)
    count += unsigned(m.triangles.size());
  Log("prop_mesh loaded=1 parts=%u triangles=%u textures=8 mipmaps=1",
      unsigned(props.size()), count);
  return true;
}
void Renderer::EnsureTarget(unsigned k) {
  if (k == loadedTarget) return;
  nativeParts.clear();
  if (loadedTarget && loadedTarget <= targetKinds) {
    targets[loadedTarget - 1] = {};
    for (auto &t : targetTextures[loadedTarget - 1]) release(t);
  }
  loadedTarget = k;
  if (!k || k > targetKinds) return;
  auto root = folder + L"\\TTWVRPhysicalLockpicking\\";
  {
    auto prefix = L"target-" + std::to_wstring(k);
    auto *f = _wfopen((root + prefix + L".bin").c_str(), L"rb");
    TargetAsset asset;
    bool ok = readTarget(f, k, asset);
    if (f)
      fclose(f);
    if (ok && device)
      for (unsigned m = 0; m < asset.materials && ok; m++)
        for (unsigned channel = 0; channel < 2; channel++) {
          auto path = root + prefix + L"-" + std::to_wstring(m) +
                      (channel ? L"-n.rgba" : L".rgba");
          if (!LoadTexture(path, channel == 0,
                           &targetTextures[k - 1][m + channel * 8]))
            ok = false;
        }
    ID3D11CommandList *commands = nullptr;
    if (deferred && SUCCEEDED(deferred->FinishCommandList(FALSE, &commands))) {
      immediate->ExecuteCommandList(commands, TRUE);
      release(commands);
    } else if (deferred)
      ok = false;
    if (ok) {
      targets[k - 1] = std::move(asset);
      Log("target_asset kind=%u loaded=1 parts=%u duration=%.3f style=%u", k,
          unsigned(targets[k - 1].parts.size()), targets[k - 1].duration,
          unsigned(targets[k - 1].bounds.style));
    } else {
      for (auto &t : targetTextures[k - 1])
        release(t);
      Log("target_asset kind=%u loaded=0 fallback=standalone", k);
    }
  }
}
bool Renderer::InitNative() {
  if (!LoadHands()) return false;
  propsReady = LoadProps();
  return propsReady;
}
bool Renderer::Init(ID3D11Device *d) {
  device = d;
  d->AddRef();
  d->GetImmediateContext(&immediate);
  if (FAILED(d->CreateDeferredContext(0, &deferred)))
    return false;
  if (!background.Init(device))
    Log("background_init failed=1 fallback=dark");
  video.Init(device, immediate, captureSize);
  LoadHands(); // Optional local game assets; markers remain the safe fallback.
  propsReady = LoadProps();
  // Only the selected target is loaded; expanding coverage must not upload
  // the entire catalogue's textures into a 32-bit game process.
  for (unsigned i=0;i<4;i++) {
    auto path = folder + L"\\TTWVRPhysicalLockpicking\\target-" +
      std::to_wstring(i<2?2:1) + L"-0" + (i%2?L"-n.rgba":L".rgba");
    LoadTexture(path, i%2==0, &hardwareTextures[i]);
  }
  ID3D11CommandList *hardwareCommands=nullptr;
  if (SUCCEEDED(deferred->FinishCommandList(FALSE,&hardwareCommands))) {
    immediate->ExecuteCommandList(hardwareCommands,TRUE);release(hardwareCommands);
  }
  Log("lock_dimensions round_diameter=%.4f safe_width=%.4f plug_diameter=%.4f "
      "pad_plug_diameter=%.4f slot_width=%.4f round_slot_height=%.4f pad_slot_height=%.4f",
      2 * roundFrameRadius, 2 * safeHalf, 2 * lockPlugRadius, 2 * padPlugRadius,
      2 * keywayWidth(LockStyle::Round), 2 * keywayHeight(LockStyle::Round),
      2 * keywayHeight(LockStyle::Padlock));
  D3D11_BLEND_DESC blendDesc{};
  auto &b = blendDesc.RenderTarget[0];
  b.BlendEnable = TRUE;
  b.SrcBlend = D3D11_BLEND_SRC_ALPHA;
  b.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
  b.BlendOp = D3D11_BLEND_OP_ADD;
  b.SrcBlendAlpha = D3D11_BLEND_ONE;
  b.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  b.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  b.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(d->CreateBlendState(&blendDesc, &blend)))
    return false;
  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_ANISOTROPIC;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
  sd.MaxAnisotropy = 16;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(d->CreateSamplerState(&sd, &sampler)))
    return false;
  const char *shader = R"(
Texture2D handTex:register(t0);
Texture2D lockTex:register(t1); Texture2D boxTex:register(t2); Texture2D canTex:register(t3);
Texture2D lockNormal:register(t4); Texture2D boxNormal:register(t5); Texture2D canNormal:register(t6);
Texture2D targetTex[8]:register(t7); Texture2D targetNormal[8]:register(t15);
Texture2D pinTex:register(t23); Texture2D pinNormal:register(t24);
Texture2D hardwareTex[4]:register(t25);
SamplerState texSampler:register(s0);
struct V { float4 p:POSITION; float4 c:COLOR; float3 uv:TEXCOORD0;
           float3 n:NORMAL; float3 w:TEXCOORD1; float3 view:TEXCOORD2; };
struct O { float4 p:SV_Position; float4 c:COLOR; float3 uv:TEXCOORD0;
           float3 n:NORMAL; float3 w:TEXCOORD1; float3 view:TEXCOORD2; };
O vs(V v) { O o; o.p=v.p; o.c=v.c; o.uv=v.uv; o.n=v.n; o.w=v.w; o.view=v.view; return o; }
float4 ps(O o):SV_Target {
  float3 c=o.c.rgb;float alpha=1;
  if(o.uv.z>1.5) {
    float4 tex=float4(1,1,1,1), bump=float4(.5,.5,1,1);
    if(o.uv.z>4.5 && o.uv.z<5.5) { tex=pinTex.Sample(texSampler,o.uv.xy); bump=pinNormal.Sample(texSampler,o.uv.xy); }
    else if(o.uv.z>=9.5) {
      bool targetMetal=o.uv.z>29.5;
      int material=targetMetal?0:int(floor(o.uv.z+.5))-(o.uv.z>=19.5?20:10);
      [unroll] for(int i=0;i<8;i++)if(i==material) {tex=targetTex[i].Sample(texSampler,o.uv.xy);bump=targetNormal[i].Sample(texSampler,o.uv.xy);}
      if(targetMetal) { if(o.uv.z>30.5) { tex=hardwareTex[2].Sample(texSampler,o.uv.xy); bump=hardwareTex[3].Sample(texSampler,o.uv.xy); } else { tex=hardwareTex[0].Sample(texSampler,o.uv.xy); bump=hardwareTex[1].Sample(texSampler,o.uv.xy); } }
      if(o.uv.z>=19.5 && !targetMetal) {alpha=tex.a;clip(alpha-.05);}
    } else
    if(o.uv.z<2.5 || o.uv.z>4.5) { tex=lockTex.Sample(texSampler,o.uv.xy); bump=lockNormal.Sample(texSampler,o.uv.xy); }
    else if(o.uv.z<3.5) { tex=boxTex.Sample(texSampler,o.uv.xy); bump=boxNormal.Sample(texSampler,o.uv.xy); }
    else { tex=canTex.Sample(texSampler,o.uv.xy); bump=canNormal.Sample(texSampler,o.uv.xy); }
    float3 N=normalize(o.n), V=normalize(o.view);
    if(dot(N,V)<0) N=-N;
    float3 px=ddx(o.w), py=ddy(o.w); float2 tx=ddx(o.uv.xy), ty=ddy(o.uv.xy);
    float det=tx.x*ty.y-tx.y*ty.x;
    float3 T=(px*ty.y-py*tx.y)*(det<0?-1:1);
    float3 B=(py*tx.x-px*ty.x)*(det<0?-1:1);
    T-=N*dot(N,T); B-=N*dot(N,B);
    float3 detail=bump.xyz*2-1;
    // DDS tangent-space normals use the same UV handedness as the mesh.
    if(dot(T,T)>1e-14 && dot(B,B)>1e-14)
      N=normalize(normalize(T)*detail.x+normalize(B)*detail.y+N*max(.1,detail.z));
    float3 L=normalize(float3(-.65,.4,.65));
    float diffuse=.20+.90*saturate(dot(N,L));
    float specularStrength=o.uv.z>2.5 && o.uv.z<3.5 ? .04 : .18;
    bool padPlug=o.uv.z>7.5 && o.uv.z<8.5;
    if(padPlug) {
      float grey=dot(tex.rgb,float3(.2126,.7152,.0722));
      tex.rgb=lerp(tex.rgb,grey.xxx,.85)*float3(1.10,1.04,.89);
    }
    bool safePlug=o.uv.z>8.5 && o.uv.z<9.5;
    if(safePlug) {
      float grey=dot(tex.rgb,float3(.2126,.7152,.0722));
      tex.rgb=lerp(tex.rgb,grey.xxx,.65)*.55;
    }
    if(padPlug || o.uv.z>29.5) specularStrength=.055;
    if(o.uv.z>30.5) tex.rgb*=.85;
    if(safePlug || o.uv.z>30.5) specularStrength=.035;
    float spec=pow(saturate(dot(N,normalize(L+V))),28)*specularStrength*bump.a;
    bool pin=o.uv.z>4.5 && o.uv.z<5.5;
    if(pin) {
      // The original pin uses an environment reflection. Approximate a soft
      // studio reflection here so the narrow bronze strip stays legible.
      tex.rgb=tex.rgb*1.65+.018;
      spec=pow(saturate(dot(N,normalize(L+V))),18)*.38*bump.a;
      spec+=pow(1-saturate(dot(N,V)),3)*.045;
    }
    c*=tex.rgb*diffuse; c+=spec;
    if((o.uv.z>5.5 && o.uv.z<6.5) || padPlug || safePlug) {
      // Fixed cavity shading belongs to the plug's local depth. It follows
      // the rotating mesh identically in both eyes, without a screen mask.
      float cavity=saturate((o.w.z+.012)/.036);
      c*=.035+.965*cavity*cavity;
    }
  } else if(o.uv.z>.5) c*=handTex.Sample(texSampler,o.uv.xy).rgb;
  return float4(c,alpha);
})";
  ID3DBlob *vb = nullptr, *pb = nullptr, *error = nullptr;
  HRESULT h =
      D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "vs",
                 "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vb, &error);
  if (error) {
    Log("shader_vs %s", (char *)error->GetBufferPointer());
    release(error);
  }
  if (FAILED(h))
    return false;
  h = D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "ps",
                 "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &pb, &error);
  if (error) {
    Log("shader_ps %s", (char *)error->GetBufferPointer());
    release(error);
  }
  if (FAILED(h)) {
    release(vb);
    return false;
  }
  D3D11_INPUT_ELEMENT_DESC elements[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 44,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, 56,
       D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, 68,
       D3D11_INPUT_PER_VERTEX_DATA, 0}};
  bool ok = SUCCEEDED(d->CreateVertexShader(
                vb->GetBufferPointer(), vb->GetBufferSize(), nullptr, &vs)) &&
            SUCCEEDED(d->CreatePixelShader(
                pb->GetBufferPointer(), pb->GetBufferSize(), nullptr, &ps)) &&
            SUCCEEDED(d->CreateInputLayout(elements, 6, vb->GetBufferPointer(),
                                           vb->GetBufferSize(), &layout));
  release(vb);
  release(pb);
  if (!ok)
    return false;
  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = sizeof(Vertex) * vertexCapacity;
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(d->CreateBuffer(&bd, nullptr, &buffer)))
    return false;
  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_NONE;
  rd.DepthClipEnable = TRUE;
  return SUCCEEDED(d->CreateRasterizerState(&rd, &raster));
}
bool Renderer::Depth(unsigned width, unsigned height) {
  if (dsv && width == depthWidth && height == depthHeight) return true;
  release(dsv); release(depth);
  D3D11_TEXTURE2D_DESC td{};
  td.Width = width; td.Height = height;
  td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
  td.Format = DXGI_FORMAT_D32_FLOAT;
  td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
  if (FAILED(device->CreateTexture2D(&td, nullptr, &depth)) ||
      FAILED(device->CreateDepthStencilView(depth, nullptr, &dsv))) return false;
  depthWidth = width; depthHeight = height;
  return true;
}
void Renderer::RecordEye(unsigned eye, ID3D11Texture2D *image, XrFovf fov) {
  if (!video.WantsFrame()) return;
  D3D11_TEXTURE2D_DESC desc{}; image->GetDesc(&desc);
  ID3D11ShaderResourceView *srv = nullptr;
  if (FAILED(device->CreateShaderResourceView(image, nullptr, &srv))) {
    Log("video_error workspace_srv=1"); video.Stop(); return;
  }
  CaptureLayer layer;
  layer.image.image = srv; layer.image.width = desc.Width; layer.image.height = desc.Height;
  layer.image.rect.extent = {int32_t(desc.Width), int32_t(desc.Height)};
  layer.image.fov = fov;
  auto *small = GameFrame(eye, fov, {layer});
  if (small) video.Copy(eye, small);
  else { Log("video_error workspace_downsample=1"); video.Stop(); }
  release(srv);
}
SceneGeometry Renderer::BuildScene(const Scene &scene, bool native) {
  Scene shown = scene;
  const TargetAsset *target =
      scene.config.useTargetObject ? Target(scene.targetKind) : nullptr;
  if (target && target->bounds.style != LockStyle::Padlock &&
      targetPresentationTime(scene.config.nativeScene, scene.targetOpenTime) > 0)
    for (auto &tool : shown.tools)
      if (tool.inserted)
        tool.pose = compose(target->Motion(scene.targetOpenTime), tool.pose);
  auto m =
      mesh(shown, hands,
           propsReady && scene.config.useGameProps ? &props : nullptr, target, plugSkirts,
           native && propsReady && scene.config.useGameProps ? &nativeParts : nullptr, native);
  return {std::move(m.v),std::move(m.draws)};
}
bool Renderer::Draw(ID3D11Texture2D *image, const XrView &view,
                    const Scene &scene, const BackgroundView &backgroundView) {
  D3D11_TEXTURE2D_DESC dimensions{}; image->GetDesc(&dimensions);
  if (!deferred || !Depth(dimensions.Width, dimensions.Height))
    return false;
  ID3D11RenderTargetView *rtv = nullptr;
  if (FAILED(device->CreateRenderTargetView(image, nullptr, &rtv)))
    return false;
  auto geometry = BuildScene(scene);
  const auto &vertices = geometry.vertices;
  const auto *target = scene.config.useTargetObject ? Target(scene.targetKind) : nullptr;
  Vec eye = local(scene.lock, pose(view.pose).p);
  std::vector<Vertex> v;
  v.reserve(vertices.size());
  for (auto &x : vertices) {
    auto p = clip(pose(view.pose), world(scene.lock, x.p), view.fov.angleLeft,
                  view.fov.angleRight, view.fov.angleDown, view.fov.angleUp);
    v.push_back({{p[0], p[1], p[2], p[3]},
                 {x.c.x, x.c.y, x.c.z, 1},
                 {x.uv.x, x.uv.y, x.uv.z},
                 {x.n.x, x.n.y, x.n.z},
                 {x.p.x, x.p.y, x.p.z},
                 {eye.x - x.p.x, eye.y - x.p.y, eye.z - x.p.z}});
  }
  if (v.size() > vertexCapacity) {
    Log("mesh_capacity_exceeded vertices=%u capacity=%u", unsigned(v.size()),
        unsigned(vertexCapacity));
    release(rtv);
    return false;
  }
  deferred->ClearState();
  D3D11_MAPPED_SUBRESOURCE map{};
  if (FAILED(deferred->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map))) {
    release(rtv);
    return false;
  }
  memcpy(map.pData, v.data(), v.size() * sizeof(Vertex));
  deferred->Unmap(buffer, 0);
  float clear[4]{.012f, .018f, .014f, 1.f};
  deferred->ClearRenderTargetView(rtv, clear);
  background.Draw(deferred, rtv, backgroundView, view.fov, dimensions.Width, dimensions.Height,
                  scene.config.backgroundBlur,
                  scene.config.backgroundBrightness);
  deferred->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1, 0);
  deferred->OMSetRenderTargets(1, &rtv, dsv);
  deferred->RSSetState(raster);
  D3D11_VIEWPORT viewport{0, 0, float(dimensions.Width), float(dimensions.Height), 0, 1};
  deferred->RSSetViewports(1, &viewport);
  unsigned stride = sizeof(Vertex), offset = 0;
  deferred->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
  deferred->IASetInputLayout(layout);
  deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  deferred->VSSetShader(vs, nullptr, 0);
  deferred->PSSetShader(ps, nullptr, 0);
  deferred->PSSetShaderResources(0, 1, &handTexture);
  deferred->PSSetShaderResources(1, 6, propTextures);
  deferred->PSSetShaderResources(23, 2, propTextures + 6);
  deferred->PSSetShaderResources(25, 4, hardwareTextures);
  if (target)
    deferred->PSSetShaderResources(7, 16, targetTextures[target->kind - 1]);
  deferred->OMSetBlendState(blend, nullptr, 0xffffffff);
  deferred->PSSetSamplers(0, 1, &sampler);
  deferred->Draw(v.size(), 0);
  ID3D11CommandList *commands = nullptr;
  auto h = deferred->FinishCommandList(FALSE, &commands);
  if (SUCCEEDED(h)) {
    immediate->ExecuteCommandList(commands, TRUE);
    release(commands);
  }
  release(rtv);
  return SUCCEEDED(h);
}
bool Renderer::Capture(ID3D11Texture2D *image, const wchar_t *path) {
  D3D11_TEXTURE2D_DESC d;
  image->GetDesc(&d);
  d.BindFlags = d.MiscFlags = 0;
  d.Usage = D3D11_USAGE_STAGING;
  d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D *read = nullptr;
  if (FAILED(device->CreateTexture2D(&d, nullptr, &read)))
    return false;
  immediate->CopyResource(read, image);
  D3D11_MAPPED_SUBRESOURCE map{};
  bool ok = false;
  if (SUCCEEDED(immediate->Map(read, 0, D3D11_MAP_READ, 0, &map))) {
    if (auto *f = _wfopen(path, L"wb")) {
      ok = fprintf(f, "P6\n%u %u\n255\n", d.Width, d.Height) > 0;
      // The source has four channels and a driver-supplied row pitch. Pack
      // several RGB rows per write so a high-resolution still doesn't issue
      // one C runtime call per pixel on the rendering thread.
      constexpr unsigned blockRows = 16;
      size_t rowBytes = size_t(d.Width) * 3;
      std::vector<unsigned char> rgb(rowBytes * blockRows);
      for (unsigned y = 0; ok && y < d.Height; y += blockRows) {
        unsigned rows = std::min(blockRows, d.Height - y);
        for (unsigned row = 0; row < rows; ++row) {
          auto *src = static_cast<const unsigned char *>(map.pData) +
                      size_t(y + row) * map.RowPitch;
          auto *dst = rgb.data() + row * rowBytes;
          for (unsigned x = 0; x < d.Width; ++x, src += 4, dst += 3) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
          }
        }
        size_t bytes = rows * rowBytes;
        ok = fwrite(rgb.data(), 1, bytes, f) == bytes;
      }
      ok = fclose(f) == 0 && ok;
    }
    immediate->Unmap(read, 0);
  }
  release(read);
  return ok;
}
} // namespace locks
