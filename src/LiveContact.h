// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Collision.h"
#include <cstdint>
namespace locks {
struct LiveTool {
  Vec tip, shaft, grip, up;
  float scale = 1;     // authored tool metres to mounted-lock coordinates
  Vec across{1, 0, 0}; // local X of the held wire mesh
};
enum class LiveSurface : unsigned { Air, Face, Wall, Bottom };

inline float liveFront(Vec p, float radius, LockStyle style) {
  const float radial2 = p.x*p.x + p.y*p.y;
  auto within = [&](float r) { return radial2 <= (r + radius) * (r + radius); };
  if (within(plugRadius(style)))
    return plugFaceZ;
  if (style == LockStyle::Round) {
    if (within(roundShoulderRadius))
      return .020f;
    if (within(roundFrameRadius))
      return .014f;
  } else if (style == LockStyle::RoundedSafe) {
    float outline = safeOutlineDistance(p.x, p.y) - radius;
    if (outline <= 0)
      return safeFront - std::max(0.f, safeBevel + outline);
  } else {
    if (padOutlineDistance(p.x, p.y) <= radius)
      return padFront;
    // Front half of the shackle tube, including its curved crown.
    float d = shackleDistance({p.x, p.y, 0}) + shackleRadius;
    float r = shackleRadius + radius;
    float front = d < r ? std::sqrt(r*r - d*d) - radius : -1e6f;
    // The staple is a ring in the YZ plane, crossing in front of the shackle.
    const float tube = stapleWire + radius;
    if (std::abs(p.x) <= tube) {
      float outer = stapleRadius + std::sqrt(std::max(0.f, tube*tube - p.x*p.x));
      float y = p.y - stapleY;
      if (std::abs(y) <= outer)
        front = std::max(front, std::sqrt(std::max(0.f, outer*outer - y*y)) - radius);
    }
    return front;
  }
  return -1e6f;
}
// Use the same stations as the rendered wire. Test the full straight leg,
// eight segments around the bend and the waved leg back to its free end.
template <class Sample>
inline void sampleLiveWire(const LiveTool &tool, float opening, Sample sample) {
  const Vec shaft = tool.shaft * tool.scale, up = tool.up * tool.scale,
            side = tool.across * tool.scale;
  const Vec hinge = shaft * pinShaftLength;
  const Vec spread = side * (.006f * std::max(0.f, 1 - opening));
  const Vec a = hinge - shaft * pinBendRadius,
            b = hinge + spread + up * pinBendRadius;
  const Vec control =
      hinge + spread * .5f - up * (.006f * std::max(0.f, 1 - opening));
  for (int n = 0; n <= 40; ++n)
    sample(a * (float(n) / 40), true);
  for (int n = 1; n <= 8; ++n) {
    float t = float(n) / 8, u = 1 - t;
    sample(a * (u * u) + control * (2 * t * u) + b * (t * t), false);
  }
  static const auto waves = [] {
    std::array<float, 33> wave{};
    for (int i = 0; i <= 32; ++i)
      wave[i] = pinWaveAmplitude * std::sin(i * pi / 4);
    return wave;
  }();
  Vec previous = b;
  for (int n = 1; n <= 32; ++n) {
    Vec p = b + side * waves[n] + up * (pinHandleLength * n / 32.f);
    sample((previous + p) * .5f, false);
    sample(p, false);
    previous = p;
  }
}

// Keep the measured wrist orientation. A rigid translation of the complete
// hand/tool pair accounts for pressure against the metal. All distances use
// the mounted mesh frame, including the front and back sides of doors.
struct LiveContact {
  LiveSurface surface = LiveSurface::Air;
  Vec offset{}, previous{}, shown{};
  bool sampled = false, inside = false;
  float entryDebt = 0;
  bool wireInFront = false;

  void update(const LiveTool &tool, bool driver, float opening, float angle,
              LockStyle style, bool facing = false,
              const MountSurfaceGrid *panel = nullptr) {
    const auto turn = axisAngle({0, 0, 1}, angle * pi / 180);
    const Vec raw = rotate(turn, tool.tip), shaft = rotate(turn, tool.shaft);
    const Vec edge = rotate(turn, tool.up);
    const Vec thin = cross(edge, shaft);
    const float radius = driver ? driverBladeHalfThickness * tool.scale
                                : pinWireRadius * tool.scale;
    float rx = radius, ry = radius;
    if (driver) {
      rx = tool.scale * (driverBladeHalfWidth * std::abs(edge.x) +
                         driverBladeHalfThickness * std::abs(thin.x));
      ry = tool.scale * (driverBladeHalfWidth * std::abs(edge.y) +
                         driverBladeHalfThickness * std::abs(thin.y));
    }
    const float x = keywayWidth(style) - rx, y = keywayHeight(style) - ry;
    const float top = driver ? y : plugSlotTop - ry;
    Vec result = raw;
    const float face = plugFaceZ + radius;
    auto frontAt = [&](Vec point, float r) {
      float z = liveFront(point, r, style);
      return panel ? std::max(z, panel->front(point)) : z;
    };
    const bool wasInside = inside;
    const auto oldSurface = surface;
    surface = LiveSurface::Air;
    inside = false;
    // A newly selected mount cannot pull a tool through from the rear.
    const float hingeZ =
        tool.tip.z + tool.shaft.z * pinShaftLength * tool.scale;
    const float endZ =
        hingeZ + tool.up.z * (pinBendRadius + pinHandleLength) * tool.scale;
    const bool reachesFront =
        !driver && std::max({tool.tip.z, hingeZ, endZ}) >= plugFaceZ;
    const bool front = facing || reachesFront ||
                       (sampled ? (wireInFront || shown.z >= -.014f ||
                                   wasInside || oldSurface != LiveSurface::Air)
                                : raw.z >= -.012f);
    wireInFront = reachesFront;
    float outer = frontAt(tool.tip, std::max(rx, ry));
    if (front && raw.z < std::max(face, outer + radius)) {
      const bool oriented =
          shaft.z > (driver ? .20f : .34f) && (driver || pinCanEngage(opening));
      Vec crossing = raw;
      if (sampled && previous.z > face && previous.z > raw.z) {
        const Vec from = rotate(turn, previous);
        crossing = from + (raw - from) * ((from.z - face) / (from.z - raw.z));
      }
      const bool fits = x > 0 && -y < top;
      const bool atOpening =
          std::abs(crossing.x) <= x && crossing.y >= -y && crossing.y <= top;

      const bool approached =
          !facing ||
          (sampled ? shown.z >= face - .0002f || oldSurface == LiveSurface::Face
                   : raw.z >= plugFaceZ);
      // A shallow insertion can ride back out over the lip as the hand moves
      // sideways. Never hold the wrist at the slot after it has moved past
      // the housing. Deeper insertion allows a little more wall pressure.
      float beyond = std::hypot(std::max(0.f, std::abs(raw.x) - x),
                                std::max({0.f, raw.y - top, -y - raw.y}));
      float depthBefore = std::max(0.f, face - shown.z);
      float releaseTravel = .005f + std::min(.012f, depthBefore);
      bool retained = wasInside && beyond < releaseTravel;
      if (raw.z < face && oriented && fits &&
          (retained || (atOpening && approached))) {
        // Sliding into the opening while pressing on its rim spends only
        // fresh inward motion. Crossing the rim cannot snap the tip to depth.
        if (!wasInside && oldSurface == LiveSurface::Face)
          entryDebt = std::max(0.f, face - std::max(previous.z, raw.z));
        // A rim offset defines the mouth for this insertion. Withdrawing
        // from inside must immediately move the visible tip back with the hand.
        entryDebt = std::min(entryDebt, face - raw.z);
        float depth = std::max(0.f, face - raw.z - entryDebt);
        const Vec slope = shaft * (1 / shaft.z);
        const float maxDepth =
            driver ? driverBearingDepth
                   : std::min(plugKeywayDepth, pinMaxDepth(opening) * tool.scale * shaft.z);
        float limit = maxDepth;
        if (std::abs(slope.x) > 1e-5f)
          limit = std::min(limit, 2 * x / std::abs(slope.x));
        if (std::abs(slope.y) > 1e-5f)
          limit = std::min(limit, (top + y) / std::abs(slope.y));
        if (depth > limit) {
          surface = limit < maxDepth ? LiveSurface::Wall : LiveSurface::Bottom;
          depth = limit;
        }
        if (wasInside && beyond > .003f) {
          float load = (beyond - .003f) / (releaseTravel - .003f);
          depth = std::min(depth, maxDepth * std::max(0.f, 1.f - load));
        }
        result.z = face - depth;
        // Both ends of the straight segment inside the slot must clear its
        // walls. Keep the player's entry point anywhere in that interval.
        auto fit = [](float p, float low, float high, float drift) {
          const float lo = low - std::min(0.f, drift),
                      hi = high - std::max(0.f, drift);
          return lo <= hi ? std::clamp(p, lo, hi) : (lo + hi) * .5f;
        };
        result.x = fit(raw.x, -x, x, depth * slope.x);
        result.y = fit(raw.y, -y, top, depth * slope.y);
        if (surface == LiveSurface::Air &&
            (std::abs(result.x - raw.x) > .00003f ||
             std::abs(result.y - raw.y) > .00003f))
          surface = LiveSurface::Wall;
        inside = true;
      } else if (raw.z < outer + radius) {
        result.z = outer + radius;
        surface = LiveSurface::Face;
      }
    }
    if (!driver && front) {
      Vec base = rotate(inverse(turn), result);
      // A bound on the entire wire avoids visiting its vertices in clear air.
      float reach = (pinShaftLength + pinHandleLength + pinBendRadius + .01f) *
                    tool.scale;
      float extent = panel ? MountSurfaceGrid::spacing * (MountSurfaceGrid::side / 2)
                           : stapleY + stapleRadius;
      // Bound the lowest wire point along the lock normal. The curve stays
      // in its control hull and the waved leg stays within its wire amplitude.
      float spread = .006f * std::max(0.f, 1.f - opening);
      float lowZ = base.z + tool.scale * (
          std::min(0.f, tool.shaft.z) * pinShaftLength +
          std::min(0.f, tool.up.z) * (pinBendRadius + pinHandleLength) -
          std::max(0.f, tool.up.z) * spread -
          std::abs(tool.across.z) * (spread + pinWaveAmplitude));
      float highest = panel ? std::max(plugFaceZ, panel->highest) : plugFaceZ;
      if (lowZ < highest + radius &&
          std::max(std::abs(base.x), std::abs(base.y)) <= reach + extent) {
        float lift = 0;
        sampleLiveWire(tool, opening, [&](Vec station, bool workingLeg) {
          Vec p = base + station;
          if (p.z >= highest + radius - lift)
            return;
          if (workingLeg && inside) {
            Vec slot = rotate(turn, p);
            if (std::abs(slot.x) <= x + .00001f && slot.y >= -y - .00001f && slot.y <= top + .00001f)
              return;
          }
          float support = frontAt(p, radius);
          lift = std::max(lift, support + radius - p.z);
        });
        if (lift > .00001f) {
          result.z += lift;
          surface = LiveSurface::Face;
          if (result.z >= face - .00005f)
            inside = false;
        }
      }
    }
    if (!inside && surface != LiveSurface::Face)
      entryDebt = 0;
    offset = rotate(inverse(turn), result - raw);
    previous = tool.tip;
    shown = tool.tip + offset;
    sampled = true;
  }
};
} // namespace locks
