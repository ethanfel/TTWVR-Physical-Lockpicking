// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Props.h"
#include "TargetProfiles.h"
#include "TargetBounds.h"
#include "ToolGeometry.h"
#include <string>
namespace locks {
inline unsigned targetKind(std::string path) {
  for (char &c : path) {
    if (c == '/')
      c = '\\';
    if (c >= 'A' && c <= 'Z')
      c += 32;
  }
  if (path.rfind("meshes\\", 0) == 0)
    path.erase(0, 7);
  for (unsigned n = 0; n < targetKinds; n++)
    if (path == targetProfiles[n].path)
      return n + 1;
  return 0;
}
struct TargetPart {
  PropMesh mesh;
  unsigned moving = 0, alpha = 0;
};
struct TargetAsset {
  unsigned kind = 0, materials = 0;
  float duration = 0;
  TargetBounds bounds;
  std::vector<Pose> animation;
  std::vector<TargetPart> parts;
  Pose Motion(float time) const {
    if (animation.empty() || duration <= 0)
      return {};
    float f =
        std::clamp(time / duration, 0.f, 1.f) * float(animation.size() - 1);
    size_t a = size_t(f), b = std::min(a + 1, animation.size() - 1);
    float t = f - a;
    Pose p = animation[a], q = animation[b];
    if (p.q.x * q.q.x + p.q.y * q.q.y + p.q.z * q.q.z + p.q.w * q.q.w < 0)
      q.q = {-q.q.x, -q.q.y, -q.q.z, -q.q.w};
    return {unit({p.q.x + (q.q.x - p.q.x) * t, p.q.y + (q.q.y - p.q.y) * t,
                  p.q.z + (q.q.z - p.q.z) * t, p.q.w + (q.q.w - p.q.w) * t}),
            p.p + (q.p - p.p) * t};
  }
};
inline bool readTarget(FILE *f, unsigned expected, TargetAsset &out) {
  if (!f)
    return false;
  char magic[8];
  uint32_t kind, parts, materials, frames;
  float duration;
  auto read = [&](void *p, size_t n) { return fread(p, 1, n, f) == n; };
  if (!read(magic, 8))
    return false;
  bool styled = memcmp(magic, "TTWTarg2", 8) == 0;
  if ((!styled && memcmp(magic, "TTWTarg1", 8)) || !read(&kind, 4) ||
      kind != expected || !kind || kind > targetKinds || !read(&parts, 4) ||
      !parts || parts > 128 || !read(&materials, 4) || !materials ||
      materials > 8 || !read(&frames, 4) || frames < 2 || frames > 601 ||
      !read(&duration, 4) || !std::isfinite(duration) || duration < .05f ||
      duration > 10)
    return false;
  TargetAsset a;
  a.kind = kind;
  a.materials = materials;
  a.duration = duration;
  uint32_t style = 0;
  if (styled && (!read(&style, 4) || style > unsigned(LockStyle::RoundedSafe) ||
                 style != targetProfiles[kind - 1].style))
    return false;
  a.bounds.style = LockStyle(style);
  if (!read(&a.bounds.low, 12) || !read(&a.bounds.high, 12) ||
      !finite(a.bounds.low) || !finite(a.bounds.high) ||
      length(a.bounds.low) > 5 || length(a.bounds.high) > 5 ||
      a.bounds.low.x >= a.bounds.high.x || a.bounds.low.y >= a.bounds.high.y ||
      a.bounds.low.z >= a.bounds.high.z || a.bounds.high.z > -.005f)
    return false;
  a.bounds.enabled = true;
  a.animation.resize(frames);
  for (auto &p : a.animation) {
    if (!read(&p, sizeof(p)) || !finite(p.p) || length(p.p) > 5 ||
        !std::isfinite(p.q.x) || !std::isfinite(p.q.y) ||
        !std::isfinite(p.q.z) || !std::isfinite(p.q.w))
      return false;
    float n = p.q.x * p.q.x + p.q.y * p.q.y + p.q.z * p.q.z + p.q.w * p.q.w;
    if (std::abs(n - 1) > .005f)
      return false;
  }
  auto first = a.animation.front();
  if (length(first.p) > .0001f || std::abs(first.q.w) < .9999f)
    return false;
  size_t total = 0;
  for (unsigned i = 0; i < parts; i++) {
    TargetPart p;
    uint32_t n, t;
    if (!read(&n, 4) || !n || n > 20000 || !read(&t, 4) || !t || t > 12000 ||
        !read(&p.mesh.material, 4) || p.mesh.material >= materials ||
        !read(&p.moving, 4) || p.moving > 1 || !read(&p.alpha, 4) ||
        p.alpha > 1)
      return false;
    total += t;
    if (total > 12000)
      return false;
    p.mesh.vertices.resize(n);
    p.mesh.triangles.resize(t);
    if (!read(p.mesh.vertices.data(), n * sizeof(PropVertex)) ||
        !read(p.mesh.triangles.data(), t * 12))
      return false;
    for (auto &v : p.mesh.vertices)
      if (!finite(v.p) || length(v.p) > 5 || !finite(v.n) ||
          length(v.n) < .5f || length(v.n) > 1.5f || !std::isfinite(v.u) ||
          !std::isfinite(v.v) || std::abs(v.u) > 32 || std::abs(v.v) > 32)
        return false;
    for (auto tri : p.mesh.triangles)
      for (auto j : tri)
        if (j >= n)
          return false;
    a.parts.push_back(std::move(p));
  }
  if (fgetc(f) != EOF || ferror(f))
    return false;
  out = std::move(a);
  return true;
}
inline float targetPresentationTime(bool nativeScene, float time) {
  return nativeScene ? 0.f : time;
}
// Native success is the only trigger. Cancellation and pin breakage cannot
// animate a target, and a new menu always starts with a closed object.
struct TargetOpening {
  uintptr_t menu = 0;
  unsigned kind = 0;
  bool opened = false;
  float time = 0;
  void Step(uintptr_t m, unsigned k, unsigned stage, float health,
            float cylinder, float dt) {
    if (m != menu || k != kind) {
      menu = m;
      kind = k;
      opened = false;
      time = 0;
    }
    if (!m || !k)
      return;
    if (stage == 7 && health > 0 && cylinder >= 89.9f)
      opened = true;
    if (opened && std::isfinite(dt))
      time = std::min(10.f, time + std::clamp(dt, 0.f, .1f));
  }
};
} // namespace locks
