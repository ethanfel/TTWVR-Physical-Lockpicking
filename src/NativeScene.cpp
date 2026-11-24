// SPDX-License-Identifier: GPL-3.0-only
#include "NativeScene.h"
#include "GameTransform.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
namespace locks {
namespace {
using Ptr = uintptr_t;
template <class T> T read(Ptr p) {
  return Readable((void *)p, sizeof(T)) ? *reinterpret_cast<T *>(p) : T{};
}
template <class T> T &at(Ptr p) { return *reinterpret_cast<T *>(p); }
struct Element {
  union {
    double number;
    void *form;
  } data{};
  unsigned char type = 0;
};
struct Scripts {
  bool(__cdecl *call)(void *, void *, void *, Element *, uint8_t, ...);
  void *params, *extract, *format, *alt;
  void *(__cdecl *compile)(const char *);
};
Scripts *scripts = nullptr;
void *attach[targetKinds + 1]{}, *detach[targetKinds + 1]{};
DWORD gameThread = 0;
std::mutex mutex;
std::atomic<uintptr_t> presented{0};
struct Packet {
  GameState game;
  Scene scene;
  SceneGeometry mesh;
  uint64_t serial = 0;
};
Packet packet;
Mat xrToWorld;
Vec xrOrigin, worldEye;
Pose kitController[2];
unsigned kitTracked = 0, kitOpen = 0;
uint64_t kitPoseTime = 0;
Transform workspaceWorld;
bool calibrated = false;
constexpr float units = 70.f;
uint64_t serial = 0, applied = 0;
Ptr root = 0, owner = 0, menu = 0;
bool lightingRegistered = false;
unsigned ownerId = 0, kind = 0;
bool failed = false;
struct Batch {
  Ptr node = 0, data = 0;
  unsigned capacity = 0;
  std::vector<SceneVertex> previous;
  LocalVertices localVertices;
  Vec handLight{};
  Ptr material = 0;
  Vec originalEmission{};
  bool visible = false;
};
std::map<unsigned, Batch> batches;
bool object(Ptr p) {
  return Readable((void *)p, 0x9c) && Readable((void *)read<Ptr>(p), 0xdc);
}
void retain(Ptr p) { InterlockedIncrement(reinterpret_cast<LONG *>(p + 4)); }
void unref(Ptr p) {
  if (p && InterlockedDecrement(reinterpret_cast<LONG *>(p + 4)) == 0) {
    using F = void(__thiscall *)(void *);
    reinterpret_cast<F>(read<Ptr>(read<Ptr>(p) + 4))((void *)p);
  }
}
bool isNode(Ptr p) {
  if (!object(p))
    return false;
  using F = void *(__thiscall *)(void *);
  auto f = read<Ptr>(read<Ptr>(p) + 0xc);
  return f && reinterpret_cast<F>(f)((void *)p) == (void *)p;
}
std::string name(Ptr p) {
  auto str = read<Ptr>(p + 8);
  if (!str) return {};
  const auto *begin = reinterpret_cast<const char *>(str);
  constexpr unsigned limit = 96;
  if (Readable(begin, limit)) {
    const auto *end = static_cast<const char *>(std::memchr(begin, 0, limit));
    return end ? std::string(begin, end) : std::string{};
  }
  // A short name may end just before a protection boundary.
  std::string s;
  for (unsigned n = 0; n < limit; n++) {
    char c = read<char>(str + n);
    if (!c)
      return s;
    s += c;
  }
  return {};
}
Ptr find(Ptr p, const std::string &wanted, unsigned depth = 0) {
  if (!object(p) || depth > 24)
    return 0;
  if (name(p) == wanted)
    return p;
  if (!isNode(p))
    return 0;
  auto list = read<Ptr>(p + 0xa0);
  unsigned count = read<uint16_t>(p + 0xa6);
  if (count > 2048 || !Readable((void *)list, count * 4))
    return 0;
  for (unsigned n = 0; n < count; n++)
    if (auto q = find(read<Ptr>(list + 4 * n), wanted, depth + 1))
      return q;
  return 0;
}
Ptr refRoot(Ptr ref) { return read<Ptr>(read<Ptr>(ref + 0x64) + 0x14); }
void hide(Ptr node, bool value) {
  if (object(node)) {
    auto &f = at<uint32_t>(node + 0x30);
    f = (f & ~1u) | (value ? 1u : 0u);
  }
}
// Workspace NIFs declare a separate NiMaterialProperty on each LP mesh.
// Only touch explicitly attached materials; inherited scene materials can
// also belong to ordinary world objects.
Ptr ownMaterial(Ptr mesh) {
  Ptr material = read<Ptr>(mesh + 0xa4);
  if (!Readable((void *)material, 0x44)) return 0;
  Ptr link = read<Ptr>(mesh + 0x24);
  for (unsigned i = 0; link && i < 16; ++i, link = read<Ptr>(link)) {
    if (read<Ptr>(link + 8) == material) return material;
  }
  return 0;
}

void *sampleLight = nullptr;
ULONGLONG nextLightSample = 0;
float previousFill = -1.f;
void updateCloseupLight(float minimum) {
  if (minimum <= 0 || GetTickCount64() < nextLightSample) return;
  nextLightSample = GetTickCount64() + 250;
  if (!sampleLight)
    sampleLight = scripts->compile(
        "int posX\nint posY\nint posZ\nbegin Function {posX posY posZ}\n"
        "SetFunctionValue (GetLightAmountAtPoint posX posY posZ)\nend\n");

  const Vec face = workspaceWorld.position + workspaceWorld.rotation.apply({0, 0, .03f}) * units;
  // In a paused menu a nearby point light can register on the room's light
  // meter even when the renderer omits it from these newly attached meshes.
  // JIP selects the cell by X/Y. Sample below the same cell, outside local
  // light radii, for its ambient and directional contribution.
  constexpr int belowLocalLights = 65536;
  Element sample;
  if (!sampleLight || !scripts->call(sampleLight, nullptr, nullptr, &sample, 3,
        int(std::lround(face.x)), int(std::lround(face.y)), int(std::lround(face.z)) - belowLocalLights) ||
      sample.type != 1 || !std::isfinite(sample.data.number)) {
    Log("closeup_light unavailable=sample");
    nextLightSample = ~ULONGLONG{0};
    return;
  }
  const float deficit = std::max(0.f, minimum - float(sample.data.number));
  if (deficit == previousFill) return;
  // Scene lights still shade the materials normally. Texture and vertex
  // occlusion modulate this additional low-light term, including the keyway.
  for (const auto &[id, batch] : batches) {
    if (!batch.material) continue;
    const float weight = id >= 1000 ? .13f : (id == 210 || id == 211 ? .45f : 1.f);
    const float lift = deficit * .018f * weight;
    at<Vec>(batch.material + 0x28) = batch.originalEmission + Vec{lift, lift, lift};
  }
  if (previousFill < 0 || (previousFill > 0) != (deficit > 0))
    Log("closeup_light ambient=%.1f minimum=%.1f fill=%.1f point=%.0f,%.0f,%.0f",
        sample.data.number, minimum, deficit, face.x, face.y, face.z);
  previousFill = deficit;
}

// On Fallout 1.4.0.525 AddObject queues a retained subtree for the world
// renderer to associate with lights using its current bounds. RemoveObject
// drops the geometry from those lights before the JIP clone is detached.
void registerWorkspaceLighting(bool add) {
  const Ptr base = reinterpret_cast<Ptr>(GetModuleHandleW(nullptr));
  const Ptr shadowScene = read<Ptr>(base + 0xdf91c8);
  if (!shadowScene || !root) return;
  using UpdateReceiver = void(__thiscall *)(Ptr, Ptr);
  const auto updateReceiver = reinterpret_cast<UpdateReceiver>(
      base + (add ? 0x75eeb0 : 0x75b1c0));
  updateReceiver(shadowScene, root);
  lightingRegistered = add;
}

void releaseScene() {
  const auto start = root ? GetTickCount64() : 0;
  presented = 0;
  if (root && object(root)) {
    hide(root, true);
    if (lightingRegistered) registerWorkspaceLighting(false);
  }
  lightingRegistered = false;
  // JIP clones may share cached geometry data. Restore the declared capacity
  // before detaching, so reopening cannot mistake the previous active count
  // for the allocation size.
  for (const auto &entry : batches) {
    const auto &b = entry.second;
    if (b.material && previousFill > 0)
      at<Vec>(b.material + 0x28) = b.originalEmission;
    at<uint16_t>(b.data + 8) = uint16_t(b.capacity);
    at<uint16_t>(b.data + 0x40) = at<uint16_t>(b.data + 0x42) =
        uint16_t(b.capacity / 3);
    at<uint32_t>(b.data + 0x44) = b.capacity;
  }

  if (owner && read<unsigned>(owner + 0xc) == ownerId && scripts &&
      detach[kind]) {
    Element e;
    scripts->call(detach[kind], nullptr, nullptr, &e, 1, (void *)owner);
  }
  // JIP attaches directly to runtime-created references without storing a map
  // entry, so its detach command cannot remove that case. We still own root;
  // detach exactly that node if it remains parented after the command.
  if (root) {
    Ptr parent = read<Ptr>(root + 0x18);
    if (isNode(parent)) {
      using Remove = void(__thiscall *)(void *, void *);
      reinterpret_cast<Remove>(read<Ptr>(read<Ptr>(parent) + 0xe8))(
          (void *)parent, (void *)root);
      using Up = void(__thiscall *)(void *);
      reinterpret_cast<Up>(read<Ptr>(read<Ptr>(parent) + 0xfc))((void *)parent);
      Log("native_scene_direct_detach target=%08x", ownerId);
    }
    unref(root);
    Log("native_scene_detached menu=%08x ref=%08x kind=%u detach_ms=%llu",
        unsigned(menu), ownerId, kind, (unsigned long long)(GetTickCount64() - start));
  }
  root = owner = menu = 0;
  ownerId = 0;
  batches.clear();
  previousFill = -1.f;
  nextLightSample = 0;
  applied = 0;
}
bool prepare(const GameState &g, unsigned k) {
  if (!scripts || !scripts->compile || !scripts->call || !g.target || k > targetKinds)
    return false;
  Log("native_prepare begin kind=%u", k);
  if (!attach[k]) {
    char source[512];
    snprintf(source, sizeof(source),
             "ref rModelRef\nfloat result\nbegin Function {rModelRef}\nlet "
             "result := AttachModel rModelRef 3 "
             "\"TTWVRPhysicalLockpicking\\workspace-%u.nif\"\nSetFunctionValue "
             "result\nend\n",
             k);
    Log("native_prepare compile_attach");
    attach[k] = scripts->compile(source);
    Log("native_prepare compile_attach_done result=%08x",
        unsigned(uintptr_t(attach[k])));
    snprintf(source, sizeof(source),
             "ref rModelRef\nbegin Function {rModelRef}\nAttachModel rModelRef "
             "2 \"TTWVRPhysicalLockpicking\\workspace-%u.nif\"\nend\n",
             k);
    detach[k] = scripts->compile(source);
  }
  if (!attach[k] || !detach[k])
    return false;
  owner = g.target;
  ownerId = g.targetRefId;
  menu = g.menu;
  kind = k;
  Element e;
  Log("native_prepare call_attach");
  if (!scripts->call(attach[k], nullptr, nullptr, &e, 1, (void *)owner) ||
      e.type != 1 || e.data.number != 1)
    return false;
  Log("native_prepare call_attach_done");
  root = find(refRoot(owner), "TTWVRNativeWorkspace");
  if (!root)
    return false;
  retain(root);
  auto children = read<Ptr>(root + 0xa0);
  unsigned count = read<uint16_t>(root + 0xa6);
  if (count > 256 || !Readable((void *)children, count * 4))
    return false;
  for (unsigned index = 0; index < count; index++) {
    Ptr p = read<Ptr>(children + 4 * index);
    unsigned id = 0;
    char suffix = 0;
    if (!object(p) || sscanf(name(p).c_str(), "LP_%u%c", &id, &suffix) != 1)
      return false;
    Ptr d = read<Ptr>(p + 0xb8);
    unsigned capacity = read<uint16_t>(d + 8);
    if (!Readable((void *)d, 0x58) || capacity < 3 || batches.count(id))
      return false;
    auto &batch = batches[id];
    batch.node = p;
    batch.data = d;
    batch.capacity = capacity;
    batch.material = ownMaterial(p);
    if (batch.material) batch.originalEmission = read<Vec>(batch.material + 0x28);
    hide(p, true);
  }
  Log("native_scene_attached root=%08x target=%08x kind=%u batches=%u "
      "thread=%lu",
      unsigned(root), g.targetRefId, k, unsigned(batches.size()),
      GetCurrentThreadId());
  return !batches.empty();
}
bool update(const Packet &p) {
  auto parent = read<Ptr>(root + 0x18);
  if (!object(parent))
    return false;
  Transform parentWorld = read<Transform>(parent + 0x68);
  if (!std::isfinite(parentWorld.scale) || parentWorld.scale < .0001f)
    return false;
  Mat inverse = parentWorld.rotation.transpose();
  Transform transform;
  // The lock pose is fixed in local tracking space for this attachment.
  // Recalibrating its world placement on every head sample makes even the
  // stationary housing move when the engine camera and XR pose are out of phase.
  if (!applied) {
    workspaceWorld.rotation = mul(xrToWorld, matrix(p.scene.lock.q));
    workspaceWorld.position = xrOrigin + xrToWorld.apply(p.scene.lock.p) * units;
  }
  transform.rotation = mul(inverse, workspaceWorld.rotation);
  transform.position = inverse.apply(workspaceWorld.position - parentWorld.position) *
                       (1 / parentWorld.scale);
  transform.scale = units / parentWorld.scale;
  if (!finite(transform.position) || length(transform.position) > 1e7f)
    return false;
  at<Transform>(root + 0x34) = transform;
  for (auto &entry : batches) {
    auto &flags = at<uint32_t>(entry.second.node + 0x30);
    entry.second.visible = !(flags & 1u);
    flags |= 1u;
  }
  bool needsLighting = !lightingRegistered;
  size_t covered = 0;
  unsigned uploads = 0, reused = 0, relit = 0;
  for (const auto &draw : p.mesh.draws) {
    if (draw.begin != covered || !draw.count || draw.count % 3 ||
        (draw.localVertices ? draw.count != draw.localVertices->size()
                            : draw.count > p.mesh.vertices.size() - covered))
      return false;
    if (!draw.localVertices)
      covered += draw.count;
    auto found = batches.find(draw.id);
    if (found == batches.end() || draw.count > found->second.capacity) {
      Log("native_scene_capacity object=%u count=%u", draw.id,
          unsigned(draw.count));
      return false;
    }
    auto &b = found->second;
    needsLighting |= !b.visible;
    Ptr d = b.data;
    size_t n = draw.count;
    at<Transform>(b.node + 0x34) = {matrix(draw.pose.q), draw.pose.p, 1};
    const Vec light = draw.handLighting ? handLocalLight(draw.pose.q) : Vec{};
    if (draw.localVertices && b.localVertices == draw.localVertices) {
      if (draw.handLighting && (light.x != b.handLight.x || light.y != b.handLight.y ||
                                light.z != b.handLight.z)) {
        auto *colors = read<float *>(d + 0x28);
        if (!Readable(colors, n * 4 * sizeof(float)))
          return false;
        for (size_t i = 0; i < n; ++i) {
          float shade = handShade((*draw.localVertices)[i].n, light);
          colors[4 * i] = colors[4 * i + 1] = colors[4 * i + 2] = shade;
        }
        at<uint16_t>(d + 0xe) |= 0x0fff;
        b.handLight = light;
        ++relit;
      }
      at<uint32_t>(b.node + 0x30) &= ~1u;
      ++reused;
      continue;
    }
    std::vector<SceneVertex> vertices;
    vertices.reserve(n);
    auto inv = locks::inverse(draw.pose.q);
    for (size_t i = 0; i < n; i++) {
      auto x = draw.localVertices ? (*draw.localVertices)[i] : p.mesh.vertices[draw.begin + i];
      if (!finite(x.p) || !finite(x.n) || !finite(x.c) || !finite(x.uv))
        return false;
      if (!draw.localVertices) {
        x.p = local(draw.pose, x.p);
        x.n = rotate(inv, x.n);
      }
      unsigned surface = unsigned(x.uv.z + .5f);
      // Original custom shader's slot darkening, expressed as native vertex AO.
      if (surface == 6 || surface == 8 || surface == 9) {
        float cavity = std::clamp((x.p.z + .012f) / .036f, 0.f, 1.f);
        x.c = x.c * (.035f + .965f * cavity * cavity);
        if (surface == 9)
          x.c = x.c * .55f;
      }
      if (surface == 31)
        x.c = x.c * .85f;
      vertices.push_back(x);
    }
    bool changed = draw.handLighting || b.previous.size() != n;
    for (size_t i = 0; !changed && i < n; i++) {
      const auto &a = vertices[i], &z = b.previous[i];
      changed = length(a.p - z.p) > 1e-6f || length(a.n - z.n) > 1e-5f ||
                length(a.uv - z.uv) > 1e-6f || length(a.c - z.c) > 1e-6f;
    }
    b.localVertices = draw.localVertices;
    if (!changed) {
      at<uint32_t>(b.node + 0x30) &= ~1u;
      continue;
    }
    uploads++;
    auto *positions = read<Vec *>(d + 0x20), *normals = read<Vec *>(d + 0x24);
    auto *colors = read<float *>(d + 0x28), *uv = read<float *>(d + 0x2c);
    if (!Readable(positions, b.capacity * sizeof(Vec)) ||
        !Readable(normals, b.capacity * 3 * sizeof(Vec)) ||
        !Readable(colors, b.capacity * 16) || !Readable(uv, b.capacity * 8))
      return false;
    float radius = 0;
    for (size_t t = 0; t < n; t += 3) {
      const auto &a = vertices[t], &v = vertices[t + 1], &c = vertices[t + 2];
      Vec face = normal(cross(v.p - a.p, c.p - a.p));
      Vec e1 = v.p - a.p, e2 = c.p - a.p;
      float du1 = v.uv.x - a.uv.x, dv1 = v.uv.y - a.uv.y, du2 = c.uv.x - a.uv.x,
            dv2 = c.uv.y - a.uv.y, det = du1 * dv2 - du2 * dv1;
      const bool mapped = std::abs(det) > 1e-12f;
      Vec alongU = mapped ? (e1 * dv2 - e2 * dv1) * (1 / det) : Vec{1, 0, 0};
      Vec alongV = mapped ? (e2 * du1 - e1 * du2) * (1 / det) : Vec{};
      for (size_t j = 0; j < 3; j++) {
        size_t i = t + j;
        const auto &x = vertices[i];
        Vec normalV = length(x.n) > .1f ? normal(x.n) : face;
        Vec u = normal(alongU - normalV * dot(normalV, alongU));
        if (length(u) < .5f)
          u = normal(cross(normalV, std::abs(normalV.y) < .9f ? Vec{0, 1, 0}
                                                              : Vec{1, 0, 0}));
        Vec v = cross(normalV, u);
        if (dot(v, alongV) < 0)
          v = v * -1.f;
        positions[i] = x.p;
        normals[i] = normalV;
        // Fallout stores the increasing-V direction first, then increasing-U.
        // Its PP lighting shaders consume the same order on mirrored UV islands.
        normals[n + i] = v;
        normals[2 * n + i] = u;
        uv[2 * i] = x.uv.x;
        uv[2 * i + 1] = x.uv.y;
        float shade = draw.handLighting ? handShade(x.n, light) : 1.f;
        colors[4 * i] = x.c.x * shade;
        colors[4 * i + 1] = x.c.y * shade;
        colors[4 * i + 2] = x.c.z * shade;
        colors[4 * i + 3] = 1;
        radius = std::max(radius, dot(x.p, x.p));
      }
    }
    at<uint16_t>(d + 8) = uint16_t(n);
    at<uint16_t>(d + 0x40) = at<uint16_t>(d + 0x42) = uint16_t(n / 3);
    at<uint32_t>(d + 0x44) = uint32_t(n);
    at<Vec>(d + 0x10) = {};
    at<float>(d + 0x1c) = std::sqrt(radius) + .01f;
    // Low 12 bits mark dirty channels. Preserve the consistency bits:
    // changing those to 0xC000 makes renderer buffer/history identity unstable.
    at<uint16_t>(d + 0xe) |= 0x0fff;
    b.handLight = light;
    if (!draw.handLighting)
      b.previous = std::move(vertices);
    at<uint32_t>(b.node + 0x30) &= ~1u;
  }
  if (covered != p.mesh.vertices.size()) {
    Log("native_scene_failure reason=unmapped_geometry");
    return false;
  }
  updateCloseupLight(p.scene.config.minimumPuzzleLight);
  if (p.serial % 180 == 0)
    Log("native_scene_upload draws=%u uploads=%u cached=%u dynamic_vertices=%u relit=%u",
        unsigned(p.mesh.draws.size()), uploads, reused, unsigned(covered), relit);
  hide(root, false);
  struct Update {
    float time = 0;
    unsigned flags = 0, unknown = 0;
  } u;
  using F = void(__thiscall *)(void *, const Update &, unsigned);
  auto f = read<Ptr>(read<Ptr>(root) + 0xa4);
  reinterpret_cast<F>(f)((void *)root, u, 0);
  // Match NiAVObject::Update: paused menus do not rebuild ancestor bounds.
  using Up = void(__thiscall *)(void *);
  reinterpret_cast<Up>(read<Ptr>(read<Ptr>(parent) + 0xfc))((void *)parent);

  // The file contains tiny, hidden placeholders. Register the actual meshes
  // after the workspace is placed and its world bounds have propagated.
  // Repeat for a newly visible tool, never just because a frame elapsed.
  if (needsLighting) registerWorkspaceLighting(true);
  return true;
}
} // namespace
uintptr_t FindSceneNode(uintptr_t root, const std::string &name) { return find(root, name); }
void UpdateSceneNode(uintptr_t node) {
  if (!object(node)) return;
  struct Update { float time = 0; unsigned flags = 0, unknown = 0; } params;
  using UpdateFn = void(__thiscall *)(void *, const Update &, unsigned);
  reinterpret_cast<UpdateFn>(read<Ptr>(read<Ptr>(node) + 0xa4))((void *)node, params, 0);
}
// Own one reference while a kit mesh is detached from the player skeleton.
bool MountSceneChild(uintptr_t node, uintptr_t parent) {
  if (!object(node) || !isNode(parent)) return false;
  Ptr previous = read<Ptr>(node + 0x18);
  if (!isNode(previous) || previous == parent) return false;
  const Transform world = read<Transform>(node + 0x68);
  const Transform parentWorld = read<Transform>(parent + 0x68);
  if (!(world.scale > 0 && parentWorld.scale > 0)) return false;
  const Transform originalLocal = read<Transform>(node + 0x34);
  using Remove = void(__thiscall *)(Ptr, Ptr);
  using Add = void(__thiscall *)(Ptr, Ptr, bool);
  auto add = [&](Ptr target) {
    reinterpret_cast<Add>(read<Ptr>(read<Ptr>(target) + 0xdc))(target, node, true);
  };
  retain(node);
  reinterpret_cast<Remove>(read<Ptr>(read<Ptr>(previous) + 0xe8))(previous, node);
  at<Transform>(node + 0x34) = relativeTransform(world, parentWorld);
  add(parent);
  if (read<Ptr>(node + 0x18) != parent) {
    at<Transform>(node + 0x34) = originalLocal;
    add(previous);
    unref(node);
    return false;
  }
  UpdateSceneNode(node);
  using Up = void(__thiscall *)(Ptr);
  reinterpret_cast<Up>(read<Ptr>(read<Ptr>(parent) + 0xfc))(parent);
  return true;
}
void RetainSceneNode(uintptr_t node) { retain(node); }
void ReleaseMountedChild(uintptr_t node) {
  if (!node) return;
  Ptr parent = read<Ptr>(node + 0x18);
  if (isNode(parent)) {
    using Remove = void(__thiscall *)(Ptr, Ptr);
    reinterpret_cast<Remove>(read<Ptr>(read<Ptr>(parent) + 0xe8))(parent, node);
    using Up = void(__thiscall *)(Ptr);
    reinterpret_cast<Up>(read<Ptr>(read<Ptr>(parent) + 0xfc))(parent);
  }
  unref(node);
}
// The host skins the hand several times between two input samples. Keep its
// inventoried kit root on the hand, but publish the visible rigid mesh once in
// the mount frame. Retain both sides across cell and skeleton replacements.
namespace {
struct DriverMesh {
  Ptr root=0, part=0, mount=0;
  Transform original;
} driverMesh;
}
void RestoreWorldDriver() {
  auto d=driverMesh;
  driverMesh={};
  if (!d.part) return;
  if (read<Ptr>(d.part+0x18) == d.root) {
    at<Transform>(d.part+0x34) = d.original;
    UpdateSceneNode(d.part);
    unref(d.part);
  } else if (MountSceneChild(d.part,d.root)) {
    at<Transform>(d.part+0x34)=d.original;
    UpdateSceneNode(d.part);
    unref(d.part); // the temporary reference from the move back
    unref(d.part); // the original reference from moving to the mount
  } else ReleaseMountedChild(d.part);
  unref(d.root);
  unref(d.mount);
}
bool PresentWorldDriver(Ptr hand, int handIndex, Ptr mount, const Transform &wrist) {
  if (driverMesh.part && (driverMesh.mount != mount ||
      read<Ptr>(driverMesh.root+0x18) != hand || read<Ptr>(driverMesh.part+0x18) != mount))
    RestoreWorldDriver();
  if (!hand || !mount) return false;
  if (!driverMesh.part) {
    // The kit root stays directly under this wrist while its mesh is on the lock.
    Ptr rootNode = FindSceneNode(hand, handIndex ? "TTWVRKit_1_1" : "TTWVRKit_0_1");
    if (!rootNode || read<Ptr>(rootNode+0x18) != hand) return false;
    Ptr part=FindSceneNode(rootNode,"KitPart2");
    if (!part || read<Ptr>(part+0x18)!=rootNode) return false;
    auto original=read<Transform>(part+0x34);
    if (!MountSceneChild(part,mount)) return false;
    retain(rootNode);
    retain(mount);
    driverMesh={rootNode,part,mount,original};
    Log("live_driver_mesh_bound hand=%d", handIndex);
  }
  const auto &parent=at<Transform>(mount+0x68);
  if (!(parent.scale>.0001f)) return false;
  const auto &rootLocal=at<Transform>(driverMesh.root+0x34);
  Transform world=childWorld(childWorld(wrist,rootLocal),driverMesh.original);
  at<Transform>(driverMesh.part+0x34)=relativeTransform(world,parent);
  UpdateSceneNode(driverMesh.part);
  return true;
}
void InitNativeScene(void *api) { scripts = static_cast<Scripts *>(api); }
void NativeSceneCalibration(Pose rightEye) {
  if (!NativeReady())
    return;
  auto base = uintptr_t(GetModuleHandleW(nullptr));
  Ptr globals = read<Ptr>(base + 0xddea0c);
  Ptr currentCamera = read<Ptr>(globals + 0xa0);
  static Ptr worldCamera = 0;
  if (!Snapshot().menu && object(currentCamera) &&
      currentCamera != worldCamera) {
    retain(currentCamera);
    if (worldCamera)
      unref(worldCamera);
    worldCamera = currentCamera;
    Log("native_scene_world_camera pointer=%08x name=%s", unsigned(worldCamera),
        name(worldCamera).c_str());
  }
  Ptr camera = worldCamera;
  if (!object(camera))
    return;
  Transform cam = read<Transform>(camera + 0x68);
  Mat axes{{1, 0, 0, 0, 0, -1, 0, 1, 0}};
  Mat rotation = mul(mul(cam.rotation, axes), matrix(rightEye.q).transpose());
  Vec origin = cam.position - rotation.apply(rightEye.p) * units;
  if (!finite(origin))
    return;
  std::lock_guard guard(mutex);
  xrToWorld = rotation;
  xrOrigin = origin;
  worldEye = cam.position;
  calibrated = true;
  static unsigned n = 0;
  if (n++ % 600 == 0)
    Log("native_scene_mapping camera=%.3f,%.3f,%.3f xr_eye=%.3f,%.3f,%.3f "
        "units=%.3f thread=%lu",
        cam.position.x, cam.position.y, cam.position.z, rightEye.p.x,
        rightEye.p.y, rightEye.p.z, units, GetCurrentThreadId());
}
void NativeSceneTick(const GameState &g) {
  if (!gameThread) {
    gameThread = GetCurrentThreadId();
    Log("native_scene_game_thread id=%lu", gameThread);
  }
  if (GetCurrentThreadId() != gameThread)
    return;
  std::lock_guard guard(mutex);
  // A slow frame does not close the lock menu. The workspace belongs to the
  // open menu and target; controller freshness is checked separately in Current().
  if (!g.menu || !g.target || g.menu != packet.game.menu ||
      g.target != packet.game.target || g.targetRefId != packet.game.targetRefId) {
    releaseScene();
    failed = false;
    return;
  }
  if (!packet.serial) {
    if (root) hide(root, true);
    return;
  }
  if (failed || !calibrated || applied == packet.serial)
    return;
  unsigned k = packet.scene.targetKind;
  if (root && (menu != g.menu || kind != k || owner != g.target))
    releaseScene();
  const bool firstFrame = !root;
  if (firstFrame && !prepare(g, k)) {
    Log("native_scene_failure reason=attach");
    releaseScene();
    failed = true;
    return;
  }
  if (!update(packet)) {
    Log("native_scene_failure reason=update");
    releaseScene();
    failed = true;
    return;
  }
  applied = packet.serial;
  presented = g.menu;
  if (firstFrame || applied % 180 == 0)
    Log("native_scene_frame serial=%llu dynamic_vertices=%u applied=1 game_thread=1",
        (unsigned long long)applied, unsigned(packet.mesh.vertices.size()));
}
bool NativeScenePublish(const GameState &g, const Scene &scene,
                        SceneGeometry &&mesh) {
  {
    std::lock_guard guard(mutex);
    packet = {g, scene, std::move(mesh), ++serial};
  }
  if (GetCurrentThreadId() == gameThread)
    NativeSceneTick(g);
  return NativeScenePresented(g.menu);
}
bool WorldEye(Vec &p) {
  std::lock_guard guard(mutex);
  if (!calibrated || !finite(worldEye)) return false;
  p=worldEye;
  return true;
}
void SampleKitControllers(const Pose (&poses)[2], unsigned valid, unsigned open) {
  std::lock_guard guard(mutex);
  std::copy(poses, poses + 2, kitController);
  kitTracked = valid;
  kitOpen = open;
  kitPoseTime = GetTickCount64();
}
bool KitControllerWorld(int hand, Transform &world, bool requireOpen) {
  std::lock_guard guard(mutex);
  if (!calibrated || (requireOpen && !(kitOpen & (1u << hand))) || !(kitTracked & (1u << hand)) || GetTickCount64() - kitPoseTime > 180)
    return false;
  const Pose &p = kitController[hand];
  world = {mul(xrToWorld, matrix(p.q)), xrOrigin + xrToWorld.apply(p.p) * units, 1};
  return true;
}
bool WorldToTracking(Vec point, Vec &tracking) {
  std::lock_guard guard(mutex);
  if(!calibrated || !finite(point)) return false;
  tracking=xrToWorld.transpose().apply(point-xrOrigin)*(1.f/units);
  return finite(tracking);
}
void NativeSceneSuspend() {
  std::lock_guard guard(mutex);
  packet.serial = 0;
  presented = 0;
}
void NativeSceneStop() {
  std::lock_guard guard(mutex);
  packet.game.menu = 0;
  presented = 0;
}
bool NativeScenePresented(uintptr_t m) { return m && presented.load() == m; }
} // namespace locks
