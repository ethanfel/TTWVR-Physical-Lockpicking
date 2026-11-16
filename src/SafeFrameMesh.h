// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LockStyle.h"
#include "Props.h"
#include "ToolGeometry.h"
namespace locks {
struct SafeFrameMesh {
  PropMesh body;
  void tri(PropVertex a, PropVertex b, PropVertex c) {
    uint32_t n = uint32_t(body.vertices.size());
    body.vertices.insert(body.vertices.end(), {a, b, c});
    body.triangles.push_back({n, n + 1, n + 2});
  }
  static PropVertex vertex(Vec p, Vec n) {
    // Worn painted steel from the original safe's front panel; no new texture.
    return {p, n, .255f + .17f * p.x / safeHalf,
            .255f - .17f * p.y / safeHalf};
  }
  static Vec outward(Vec p) {
    constexpr float c = safeHalf - safeCorner;
    return normal({p.x - std::clamp(p.x, -c, c),
                   p.y - std::clamp(p.y, -c, c), 0});
  }
  void band(Vec a, Vec b, Vec c, Vec d, Vec na, Vec nb,
            bool depthUv = false, float angleA = 0, float angleB = 0) {
    auto v = [&](Vec p, Vec n, float angle) {
      PropVertex result = vertex(p, n);
      if (depthUv) {
        // Front projection collapses on vertical walls and destabilises the
        // normal-map tangent frame. Unwrap these surfaces around their depth.
        result.u = .085f + .34f * angle / (2 * pi);
        result.v = .085f + .34f * (p.z - safeBack) / (safeFront - safeBack);
      }
      return result;
    };
    tri(v(a, na, angleA), v(b, nb, angleB), v(c, na, angleA));
    tri(v(b, nb, angleB), v(d, nb, angleB), v(c, na, angleA));
  }
  SafeFrameMesh() {
    constexpr int sides = 96;
    for (int j = 0; j < sides; j++) {
      float a = j * 2 * pi / sides, b = (j + 1) * 2 * pi / sides;
      Vec A = safeRim(a, safeFront, safeBevel),
          B = safeRim(b, safeFront, safeBevel),
          C{lockPlugRadius * std::cos(a), lockPlugRadius * std::sin(a), safeFront},
          D{lockPlugRadius * std::cos(b), lockPlugRadius * std::sin(b), safeFront},
          E = safeRim(a, safeFront - safeBevel),
          F = safeRim(b, safeFront - safeBevel),
          G = safeRim(a, safeBack), H = safeRim(b, safeBack);
      band(A, B, C, D, {0, 0, 1}, {0, 0, 1});
      band(E, F, A, B, normal(outward(A) + Vec{0, 0, 1}),
           normal(outward(B) + Vec{0, 0, 1}));
      band(G, H, E, F, outward(G), outward(H), true, a, b);
      Vec I = C, J = D;
      I.z = J.z = safeBack;
      band(H, G, J, I, {0, 0, -1}, {0, 0, -1});
      band(C, D, I, J, normal({-C.x, -C.y, 0}), normal({-D.x, -D.y, 0}), true, a, b);
    }
  }
};
} // namespace locks
