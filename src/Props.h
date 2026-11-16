// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Math.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
namespace locks {
enum PropPart {
  Frame,
  Cylinder,
  Driver,
  PinShaft,
  PinHandle,
  PinBoxProp,
  DriverCan,
  PropCount
};
struct PropVertex {
  Vec p, n;
  float u, v;
};
struct PropMesh {
  uint32_t material = 0;
  std::vector<PropVertex> vertices;
  std::vector<std::array<uint32_t, 3>> triangles;
};
using Props = std::array<PropMesh, PropCount>;
// Validate the entire private asset before changing the live geometry.
inline bool readProps(FILE *file, Props &out) {
  if (!file)
    return false;
  static_assert(sizeof(PropVertex) == 32);
  char magic[8];
  uint32_t count;
  if (fread(magic, 1, 8, file) != 8 || memcmp(magic, "TTWProp2", 8) ||
      fread(&count, 4, 1, file) != 1 || count != PropCount)
    return false;
  Props loaded;
  size_t triangles = 0;
  for (auto &m : loaded) {
    uint32_t n, t;
    if (fread(&n, 4, 1, file) != 1 || fread(&t, 4, 1, file) != 1 ||
        fread(&m.material, 4, 1, file) != 1 || !n || n > 10000 || !t ||
        t > 10000 || m.material > 2)
      return false;
    triangles += t;
    if (triangles > 12000)
      return false;
    m.vertices.resize(n);
    m.triangles.resize(t);
    if (fread(m.vertices.data(), sizeof(PropVertex), n, file) != n ||
        fread(m.triangles.data(), 12, t, file) != t)
      return false;
    for (const auto &v : m.vertices)
      if (!finite(v.p) || length(v.p) > 1 || !finite(v.n) ||
          length(v.n) < .5f || length(v.n) > 1.5f || !std::isfinite(v.u) ||
          !std::isfinite(v.v) || std::abs(v.u) > 16 || std::abs(v.v) > 16)
        return false;
    for (auto tri : m.triangles)
      for (auto i : tri)
        if (i >= n)
          return false;
  }
  if (fgetc(file) != EOF || ferror(file))
    return false;
  out = std::move(loaded);
  return true;
}
} // namespace locks
