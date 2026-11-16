// SPDX-License-Identifier: GPL-3.0-only
#include "Target.h"
#include "Physical.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace locks;
FILE *fixture(unsigned kind = 1, float duration = 1, unsigned index = 2,
              int style = -1) {
  FILE *f = tmpfile();
  assert(f);
  fwrite(style < 0 ? "TTWTarg1" : "TTWTarg2", 1, 8, f);
  uint32_t header[]{kind, 1, 1, 2};
  fwrite(header, 4, 4, f);
  fwrite(&duration, 4, 1, f);
  if (style >= 0) {
    unsigned value = unsigned(style);
    fwrite(&value, 4, 1, f);
  }
  Vec bounds[]{{-.3f, -.3f, -.6f}, {.3f, .3f, -.013f}};
  fwrite(bounds, 12, 2, f);
  Pose frames[]{{}, {axisAngle({0, 1, 0}, .2f), {.01f, 0, 0}}};
  fwrite(frames, sizeof(Pose), 2, f);
  uint32_t part[]{3, 1, 0, 1, 0};
  fwrite(part, 4, 5, f);
  PropVertex v[]{{{0, 0, -.02f}, {0, 0, 1}, 0, 0},
                 {{.1f, 0, -.02f}, {0, 0, 1}, 1, 0},
                 {{0, .1f, -.02f}, {0, 0, 1}, 0, 1}};
  fwrite(v, sizeof(PropVertex), 3, f);
  uint32_t tri[]{0, 1, index};
  fwrite(tri, 4, 3, f);
  rewind(f);
  return f;
}
int main(int argc, char **argv) {
  assert(targetKind("Meshes/Clutter/Security/GenericSafe01.NIF") == 1);
  assert(targetKind("clutter\\ammo\\ammobox01.nif") == 2);
  assert(!targetKind("../clutter/ammo/ammobox01.nif"));
  assert(!targetKind("clutter/security/genericsafewall01.nif"));
  TargetAsset a;
  FILE *f = fixture();
  assert(readTarget(f, 1, a));
  fclose(f);
  assert(a.parts.size() == 1 && a.parts[0].moving && a.bounds.enabled);
  assert(length(a.Motion(0).p) < 1e-6 && a.Motion(1).p.x == .01f);
  assert(std::abs(a.Motion(.5f).p.x - .005f) < 1e-6);
  for (auto args :
       {std::array<float, 3>{2, 1, 2}, std::array<float, 3>{1, -1, 2},
        std::array<float, 3>{1, 1, 3},
        std::array<float, 3>{1, std::numeric_limits<float>::quiet_NaN(), 2}}) {
    f = fixture(unsigned(args[0]), args[1], unsigned(args[2]));
    assert(!readTarget(f, 1, a));
    fclose(f);
  }
  f = fixture(2, 1, 2, 1);
  assert(readTarget(f, 2, a));
  fclose(f);
  assert(a.bounds.style == LockStyle::Padlock);
  f = fixture(1, 1, 2, 2);
  assert(readTarget(f, 1, a));
  fclose(f);
  assert(a.bounds.style == LockStyle::RoundedSafe);
  f = fixture(2, 1, 2);
  assert(readTarget(f, 2, a));
  fclose(f);
  assert(a.bounds.style == LockStyle::Round); // old caches remain usable
  for (auto pair : {std::array<int, 2>{1, 1}, std::array<int, 2>{2, 2},
                    std::array<int, 2>{3, 2}, std::array<int, 2>{1, 3}}) {
    f = fixture(unsigned(pair[0]), 1, 2, pair[1]);
    assert(!readTarget(f, unsigned(pair[0]), a));
    fclose(f);
  }
  TargetOpening o;
  o.Step(10, 1, 2, 1, 0, .1f);
  assert(!o.opened);
  o.Step(10, 1, 4, 1, 60, .1f);
  assert(!o.opened);
  o.Step(10, 1, 7, 0, 90, .1f);
  assert(!o.opened); // broken pin, not success
  o.Step(10, 1, 7, 1, 45, .1f);
  assert(!o.opened);
  o.Step(10, 1, 6, 1, 90, .1f);
  assert(!o.opened);
  o.Step(10, 1, 7, 1, 90, .1f);
  assert(o.opened && o.time > .09f);
  o.Step(0, 0, 0, 0, 0, .1f);
  assert(!o.opened && !o.time);
  o.Step(10, 1, 2, 1, 0, .1f);
  assert(!o.opened);
  TargetBounds b{{-.3f, -.3f, -.6f}, {.3f, .3f, -.013f}, true};
  assert(housingDistance({.15f, 0, -.03f}, 0, b) < 0);
  assert(housingDistance({.15f, 0, -.03f}) > 0);
  assert(housingDistance({0, 0, -.05f}, 0, b) > 0);
  ToolShape pin(0, 1, 0, b), driver(1, 1, 0, b);
  assert(pin.clearance(seatedPose(0, 90, 0), 0) > 0);
  assert(driver.clearance(seatedPose(1, 90, 0), 0) > 0);
  auto hit =
      moveAgainstHousing(driver, {{}, {.15f, 0, .1f}}, {{}, {.15f, 0, -.2f}});
  assert(hit.hit && hit.pose.p.z > -.02f);
  for (int n = 1; n < argc; n++) {
    f = fopen(argv[n], "rb");
    assert(f);
    TargetAsset asset;
    assert(readTarget(f, unsigned(n), asset));
    fclose(f);
    Pose end = asset.Motion(asset.duration);
    printf("asset %u parts %zu duration %.3f angle %.3f\n", asset.kind,
           asset.parts.size(), asset.duration, 2 * acos(end.q.w) * 180 / pi);
  }
  puts("Target path lookup, bounded assets, animation gating and body/keyway "
       "collision passed");
}
