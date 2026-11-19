// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "GameTransform.h"
#include "ToolGeometry.h"
namespace locks {
// The native weapon rig centers ForeTwist at the elbow and divides the
// forearm-to-hand rotation evenly. Choose the half turn closest to the
// animated sleeve, including when the wrist crosses a half revolution.
inline Mat weaponSleeve(Mat fore, Mat wrist, Mat animated) {
  const Mat relative = mul(fore.transpose(), wrist);
  const Vec z = relative.apply({0,0,1}), y = relative.apply({0,1,0});
  const Quat swing = shortestArc({0,0,1}, z);
  const Vec up = rotate(swing, {0,1,0});
  const float roll = std::atan2(dot(z,cross(up,y)),dot(up,y));
  Quat q = unit(multiply(axisAngle(z,roll),swing));
  if (q.w < 0) q = {-q.x,-q.y,-q.z,-q.w};
  const Quat half = unit({q.x,q.y,q.z,1.f+q.w});
  const Mat shortMid = mul(fore,matrix(half));
  const Vec axis{q.x,q.y,q.z};
  if (length(axis)<.0001f) return shortMid;
  const Vec n = normal(axis);
  const Mat longMid = mul(shortMid,matrix({n.x,n.y,n.z,0}));
  float agreement=0;
  for (int i=0;i<9;++i)
    agreement += animated.v[i]*(shortMid.v[i]-longMid.v[i]);
  return agreement>=0 ? shortMid : longMid;
}
// The wrist bends at the end of the sleeve. Applying that bend to the
// sleeve's full length pulls the skinned seam away from the hand.
inline Mat fitSleeveToWrist(Mat roll, Vec toWrist) {
  if (!finite(toWrist) || length(toWrist)<.001f) return roll;
  return mul(matrix(shortestArc(roll.apply({1,0,0}),toWrist)),roll);
}
// Solve in world space using the animated elbow as the bend-plane guide.
// Only rotations are returned to the skeleton. Both bone lengths stay authored.
inline bool contactArm(const Transform &parent, Transform &upper, Transform &fore,
                       Transform &hand, const Transform &goal, Transform &result) {
  Transform shoulder = childWorld(parent, upper);
  Transform elbow = childWorld(shoulder, fore);
  Transform wrist = childWorld(elbow, hand);
  Vec a = elbow.position - shoulder.position, b = wrist.position - elbow.position;
  float l = length(a), r = length(b);
  if (!(l > .001f && r > .001f && parent.scale > .0001f)) return false;
  Vec reach = goal.position - shoulder.position;
  Vec axis = normal(reach);
  if (length(axis) < .5f) axis = normal(a);
  float distance = std::clamp(length(reach), std::max(std::abs(l-r), .0001f), l+r);
  Vec guide = a - axis * dot(a,axis);
  if (length(guide) < .001f) {
    // Parallel transport the animated bend plane as the reach changes.
    Vec old = normal(a+b);
    Vec oldGuide = a - old * dot(old,a);
    guide = rotate(shortestArc(old,axis),oldGuide);
    if (length(guide) < .001f) {
      Vec basis = shoulder.rotation.apply({0,1,0});
      guide = basis - axis * dot(basis,axis);
      if (length(guide) < .001f) guide = cross(axis,shoulder.rotation.apply({0,0,1}));
    }
  }
  float along = std::clamp((l*l-r*r+distance*distance)/(2*distance), -l, l);
  Vec arm = axis*along + normal(guide)*std::sqrt(std::max(0.f,l*l-along*along));
  Mat swing = matrix(shortestArc(a,arm));
  Mat shoulderRotation = mul(swing,shoulder.rotation);
  Mat elbowRotation = mul(matrix(shortestArc(swing.apply(b),axis*distance-arm)),
                          mul(swing,elbow.rotation));
  upper.rotation = mul(parent.rotation.transpose(),shoulderRotation);
  fore.rotation = mul(shoulderRotation.transpose(),elbowRotation);
  hand.rotation = mul(elbowRotation.transpose(),goal.rotation);
  result = childWorld(childWorld(childWorld(parent,upper),fore),hand);
  return true;
}
} // namespace locks
