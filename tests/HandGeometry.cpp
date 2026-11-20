// SPDX-License-Identifier: GPL-3.0-only
#include "HandGeometry.h"
#include <cassert>

using namespace locks;

int main() {
  HandMesh mesh;
  mesh.vertices = {
      {{.02f, 0, 0}, {.03f, .01f, .01f}, .1f, .2f,
       {0, 1, 0}, {0, 0, 1}, {.02f, .02f, 0}, {1, 0, 0}},
      {{0, .03f, 0}, {.01f, .02f, .01f}, .3f, .4f,
       {1, 0, 0}, {0, 1, 0}, {.01f, .01f, .02f}, {0, 0, 1}},
      {{0, 0, .04f}, {.01f, 0, .03f}, .5f, .6f,
       {0, 0, 1}, {1, 0, 0}, {0, .02f, .02f}, {0, 1, 0}}};
  mesh.triangles = {{2, 0, 1}};
  auto pending = mesh.nativeVertices(0, false);
  for (bool pinch : {false, true}) {
    for (float curl : {0.f, .23f, .2301f, 1.f}) {
      auto vertices = mesh.nativeVertices(curl, pinch);
      assert(vertices == mesh.nativeVertices(curl, pinch));
      for (float angle : {0.f, .45f, -1.1f}) {
        Pose pose{{0, std::sin(angle), 0, std::cos(angle)}, {-.2f, 1.3f, -.45f}};
        for (size_t i = 0; i < 3; ++i) {
          const auto &v = mesh.vertices[mesh.triangles[0][i]];
          const auto &localVertex = (*vertices)[i];
          Vec p = v.open * (1 - curl) + (pinch ? v.pinched : v.closed) * curl;
          Vec n = v.normalOpen * (1 - curl) + (pinch ? v.normalPinched : v.normalClosed) * curl;
          Vec worldNormal = normal(rotate(pose.q, n));
          float oldShade = .55f + .45f * std::abs(dot(worldNormal, normal({-.3f, .6f, 1})));
          assert(length(world(pose, localVertex.p) - world(pose, p)) < 1e-6f);
          assert(length(rotate(pose.q, localVertex.n) - worldNormal) < 1e-6f);
          assert(std::abs(handShade(localVertex.n, handLocalLight(pose.q)) - oldShade) < 1e-6f);
          assert(localVertex.uv.x == v.u && localVertex.uv.y == v.v && localVertex.uv.z == 1);
        }
      }
    }
  }
  assert(length(pending->front().p - mesh.vertices[2].open) == 0);
}
