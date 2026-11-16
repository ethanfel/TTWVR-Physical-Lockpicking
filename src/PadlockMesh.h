// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LockStyle.h"
#include "Props.h"
#include "ToolGeometry.h"
namespace locks {
// Compact the original plug without narrowing the working slot. Preserve the
// original UVs and contact/depth plane; correct normals for the piecewise warp.
inline PropVertex padlockPlugVertex(PropVertex v) {
  constexpr float keep = .0082f;
  constexpr float slope = (padPlugRadius - keep) / (lockPlugRadius - keep);
  float ax = std::abs(v.p.x);
  float sx = ax <= keep ? 1.f : slope;
  if (ax > keep)
    v.p.x = std::copysign(keep + (ax - keep) * slope, v.p.x);
  v.p.y *= padPlugYScale;
  v.n = normal({v.n.x / sx, v.n.y / padPlugYScale, v.n.z});
  return v;
}
struct PadlockMesh {
  PropMesh body, shackle, staple;
  static void tri(PropMesh &m, PropVertex a, PropVertex b, PropVertex c) {
    uint32_t n = uint32_t(m.vertices.size());
    m.vertices.insert(m.vertices.end(), {a, b, c});
    m.triangles.push_back({n, n + 1, n + 2});
  }
  static PropVertex vertex(Vec p, Vec n, float u, float v) {
    return {p, n, u, v};
  }
  static void tube(PropMesh &m, const std::vector<Vec> &points, float radius,
                   Vec binormal, bool capped = false) {
    constexpr int sides = 16;
    for (size_t k = 1; k < points.size(); k++) {
      Vec a = points[k - 1], b = points[k];
      Vec ta = normal(points[k] - points[k > 1 ? k - 2 : k - 1]);
      Vec tb =
          normal(points[std::min(k + 1, points.size() - 1)] - points[k - 1]);
      Vec ua = normal(cross(ta, binormal)), ub = normal(cross(tb, binormal));
      for (int j = 0; j < sides; j++) {
        float q = j * 2 * pi / sides, t = (j + 1) * 2 * pi / sides;
        Vec na = binormal * std::cos(q) + ua * std::sin(q),
            nb = binormal * std::cos(t) + ua * std::sin(t);
        Vec nc = binormal * std::cos(q) + ub * std::sin(q),
            nd = binormal * std::cos(t) + ub * std::sin(t);
        float u = .733f + .044f * float(j) / sides,
              v = .53f + .19f * float(k - 1) / (points.size() - 1),
              u2 = .733f + .044f * float(j + 1) / sides,
              v2 = .53f + .19f * float(k) / (points.size() - 1);
        auto A = vertex(a + na * radius, na, u, v),
             B = vertex(a + nb * radius, nb, u2, v),
             C = vertex(b + nc * radius, nc, u, v2),
             D = vertex(b + nd * radius, nd, u2, v2);
        tri(m, A, B, C);
        tri(m, B, D, C);
      }
    }
    if (capped) {
      for (int end = 0; end < 2; end++) {
        Vec p = end ? points.back() : points.front();
        Vec n = end ? normal(p - points[points.size() - 2])
                     : normal(p - points[1]);
        Vec u = normal(cross(n, binormal));
        for (int j = 0; j < sides; j++) {
          float a = j * 2 * pi / sides, b = (j + 1) * 2 * pi / sides;
          auto rim = [&](float t) {
            return vertex(p + (binormal * std::cos(t) + u * std::sin(t)) * radius,
                          n, .755f + .020f * std::cos(t), .625f + .080f * std::sin(t));
          };
          tri(m, vertex(p, n, .755f, .625f), rim(a), rim(b));
        }
      }
    }
  }
  PadlockMesh() {
    constexpr int sides = 96;
    for (int j = 0; j < sides; j++) {
      float a = j * 2 * pi / sides, b = (j + 1) * 2 * pi / sides;
      Vec A = padRim(a, padFront), B = padRim(b, padFront),
          C{padPlugRadius * std::cos(a), padPlugRadius * std::sin(a),
            padFront},
          D{padPlugRadius * std::cos(b), padPlugRadius * std::sin(b),
            padFront};
      // The ammo-box latch's worn steel, including its original edge wear.
      // Its centre bolt lies inside the opening occupied by our working plug.
      auto face = [](Vec p) {
        return vertex(p, {0, 0, 1}, .797f + .178f * (p.x / (2 * padHalfX) + .5f),
                      .235f - .18f * (p.y / (2 * padHalfY) + .5f));
      };
      tri(body, face(A), face(B), face(C));
      tri(body, face(B), face(D), face(C));
      Vec E = A, F = B;
      E.z = F.z = padBack;
      Vec n = normal({B.y - A.y, A.x - B.x, 0});
      // Continuous steel UVs around the rim, without repeating the entire
      // atlas patch on every narrow segment.
      float u = .733f + .044f * float(j) / sides,
            u2 = .733f + .044f * float(j + 1) / sides;
      auto av = vertex(A, n, u, .53f), bv = vertex(B, n, u2, .53f),
           ev = vertex(E, n, u, .72f), fv = vertex(F, n, u2, .72f);
      tri(body, av, ev, bv);
      tri(body, bv, ev, fv);
      // Closed rear face: the physics retains the same central insertion bore.
      // Leave the plug-sized opening clear in the mesh at every depth.
      Vec G = C, H = D;
      G.z = H.z = padBack;
      auto rear = [](Vec p) {
        return vertex(p, {0, 0, -1}, .885f + p.x * 2, .145f - p.y * 2);
      };
      tri(body, rear(E), rear(G), rear(F));
      tri(body, rear(F), rear(G), rear(H));
      Vec inward = normal({-C.x, -C.y, 0});
      tri(body, vertex(C, inward, .733f, .53f), vertex(G, inward, .733f, .72f),
          vertex(D, inward, .777f, .53f));
      tri(body, vertex(D, inward, .777f, .53f), vertex(G, inward, .733f, .72f),
          vertex(H, inward, .777f, .72f));
    }
    std::vector<Vec> path{{shackleHalfWidth, shackleRightBottom, 0}};
    for (int i = 0; i <= 32; i++) {
      float a = i * pi / 32;
      path.push_back({shackleHalfWidth * std::cos(a),
                      shackleShoulder + shackleHalfWidth * std::sin(a), 0});
    }
    path.push_back({-shackleHalfWidth, shackleBottom, 0});
    tube(shackle, path, shackleRadius, {0, 0, 1}, true);
    path.clear();
    for (int i = 0; i <= 48; i++) {
      float a = i * 2 * pi / 48;
      path.push_back({0, stapleY + stapleRadius * std::cos(a),
                      stapleRadius * std::sin(a)});
    }
    tube(staple, path, stapleWire, {1, 0, 0});
  }
};
inline Pose padlockShackleMotion(float successTime) {
  float u = std::clamp(successTime / .10f, 0.f, 1.f);
  float lift = .020f * (1 - (1 - u) * (1 - u));
  float turn =
      std::clamp((successTime - .06f) / .13f, 0.f, 1.f) * 45 * pi / 180;
  Quat q = axisAngle({0, 1, 0}, turn);
  Vec hinge{shackleHalfWidth, shackleRightBottom, 0};
  return {q, hinge - rotate(q, hinge) + Vec{0, lift, 0}};
}
} // namespace locks
