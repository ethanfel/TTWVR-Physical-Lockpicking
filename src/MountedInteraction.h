// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "LiveContact.h"
#include "Physical.h"
namespace locks {
// Convert the visible tool axes to the same grip frame used by heldTool.
// Pin shaft direction depends on its bend, so use the fixed leg axes for it.
inline Pose mountedToolPose(const LiveTool &tool, int k) {
  Vec y = normal(tool.up);
  Vec z = k ? normal(tool.shaft) : normal(cross(tool.across, y));
  Quat forward = shortestArc({0, 0, 1}, z);
  Vec up = rotate(forward, {0, 1, 0});
  float roll = std::atan2(dot(z, cross(up, y)), dot(up, y));
  return {unit(multiply(axisAngle(z, roll), forward)), tool.tip};
}

struct MountedInteraction {
  PhysicalInteraction physics;
  LiveContact pinContact, bladeContact;
  bool pin = false, blade = false;
  float demand = 0, pick = 90;
  Pose shown[2]{};
  void step(const LiveTool *wire, const LiveTool *driver, float opening,
            float twist, bool parked, int pinHand, float cylinder,
            unsigned stage, float health, float dt, float travelScale,
            bool facing) {
    MountedTools kit{{wire ? pinHand : -1, driver ? 1 - pinHand : -1},
                     opening,
                     twist,
                     travelScale,
                     parked};
    Pose hands[2]{};
    LiveContact *contacts[]{&pinContact, &bladeContact};
    const LiveTool *raw[]{wire, driver};
    for (int k = 0; k < 2; ++k) {
      if (!raw[k])
        continue;
      physics.targetBounds.toolScale[k] = raw[k]->scale;
      auto &c = *contacts[k];
      Pose pose = mountedToolPose(*raw[k], k);
      // If tracking first appears behind the face, recover to the front.
      // The common swept solver owns all subsequent motion and contact.
      if (!c.sampled && facing) {
        c.update(*raw[k], k != 0, opening, cylinder, physics.targetBounds.style,
                 true, physics.targetBounds.panel);
        pose.p = c.shown;
      }
      hands[kit.owners[k]] = {
          pose.q, world(pose, handleCenter(k, opening, twist) * raw[k]->scale)};
    }
    physics.update({}, hands[0], hands[1], 0, 0, cylinder, dt, stage, health,
                   hands, &kit);
    pin = physics.tools[0].inserted;
    blade = physics.tools[1].inserted;
    demand = physics.target;
    pick = physics.pick;
    for (int k = 0; k < 2; ++k) {
      auto &c = *contacts[k];
      auto &tool = physics.tools[k];
      shown[k] = tool.pose;
      if (!raw[k]) {
        if (k || !parked)
          c = {};
        continue;
      }
      c.previous = raw[k]->tip;
      c.shown = tool.pose.p;
      c.offset = c.shown - c.previous;
      c.sampled = true;
      c.inside = tool.inserted;
      c.surface = !tool.blocked    ? LiveSurface::Air
                  : !tool.inserted ? LiveSurface::Face
                  : std::abs(c.offset.z) > std::hypot(c.offset.x, c.offset.y)
                      ? LiveSurface::Bottom
                      : LiveSurface::Wall;
    }
  }
};
} // namespace locks
