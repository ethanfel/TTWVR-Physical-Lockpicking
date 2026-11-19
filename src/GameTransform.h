// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Math.h"
namespace locks {
struct Mat {
  float v[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
  Vec apply(Vec x) const {
    return {v[0] * x.x + v[1] * x.y + v[2] * x.z,
            v[3] * x.x + v[4] * x.y + v[5] * x.z,
            v[6] * x.x + v[7] * x.y + v[8] * x.z};
  }
  Mat transpose() const {
    Mat a;
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        a.v[i * 3 + j] = v[j * 3 + i];
    return a;
  }
};
inline Mat mul(Mat a, Mat b) {
  Mat c;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      c.v[i * 3 + j] = 0;
      for (int k = 0; k < 3; k++)
        c.v[i * 3 + j] += a.v[i * 3 + k] * b.v[k * 3 + j];
    }
  return c;
}
inline Mat matrix(Quat q) {
  Mat m;
  Vec cols[]{rotate(q, {1, 0, 0}), rotate(q, {0, 1, 0}), rotate(q, {0, 0, 1})};
  for (int n = 0; n < 3; n++) {
    m.v[n] = cols[n].x;
    m.v[3 + n] = cols[n].y;
    m.v[6 + n] = cols[n].z;
  }
  return m;
}
struct Transform {
  Mat rotation;
  Vec position;
  float scale = 1;
};
inline Transform childWorld(const Transform &parent, const Transform &child) {
  return {mul(parent.rotation, child.rotation),
          parent.position + parent.rotation.apply(child.position * parent.scale),
          parent.scale * child.scale};
}
inline Transform relativeTransform(const Transform &world, const Transform &parent) {
  const Mat inverse = parent.rotation.transpose();
  return {mul(inverse, world.rotation),
          inverse.apply(world.position - parent.position) * (1.f / parent.scale),
          world.scale / parent.scale};
}
static_assert(sizeof(Transform) == 52);
}
