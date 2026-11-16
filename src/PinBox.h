// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ToolGeometry.h"
namespace locks {
struct StoredPin {
  Pose pose;
  Vec velocity, angular;
  float opening = 0, twist = 0, quiet = 0;
  bool sleeping = false;
};
// Finite segments, including parallel/degenerate cases. Used for thin wire
// contacts; a pin is never approximated by one large sphere.
inline std::array<Vec, 2> closestSegments(Segment a, Segment b) {
  Vec u = a.b - a.a, v = b.b - b.a, w = a.a - b.a;
  float aa = dot(u, u), bb = dot(u, v), cc = dot(v, v), dd = dot(u, w),
        ee = dot(v, w);
  float s = 0, t = 0;
  if (aa < 1e-10f)
    t = cc > 1e-10f ? std::clamp(ee / cc, 0.f, 1.f) : 0;
  else if (cc < 1e-10f)
    s = std::clamp(-dd / aa, 0.f, 1.f);
  else {
    float den = aa * cc - bb * bb;
    if (den > 1e-12f)
      s = std::clamp((bb * ee - cc * dd) / den, 0.f, 1.f);
    t = (bb * s + ee) / cc;
    if (t < 0) {
      t = 0;
      s = std::clamp(-dd / aa, 0.f, 1.f);
    } else if (t > 1) {
      t = 1;
      s = std::clamp((bb - dd) / aa, 0.f, 1.f);
    }
  }
  return {a.a + u * s, b.a + v * t};
}
inline std::array<Segment, 2> pinSegments(const StoredPin &p) {
  Vec h = pinHinge(p.opening, p.twist);
  return {{{world(p.pose, {}), world(p.pose, h)},
           {world(p.pose, h + Vec{.006f * std::max(0.f, 1 - p.opening), 0, 0}),
            world(p.pose, pinHandlePoint(p.opening, 1, p.twist))}}};
}
inline Vec pinMassCenter(const StoredPin &p) {
  return world(p.pose, toolCenter(0, p.opening, p.twist));
}
inline void movePinCenter(StoredPin &p, Vec center, Quat q) {
  p.pose = {q, center - rotate(q, toolCenter(0, p.opening, p.twist))};
}
class PinBox {
  // Slightly enlarged wire collision skin keeps stacks stable without
  // changing the rendered wire. Each body has unit mass.
  static constexpr float radius = .0022f, invInertia = 1500;
  unsigned spawned = 0;
  void impulse(StoredPin &p, Vec point, Vec n, float speed, float damping) {
    Vec arm = point - pinMassCenter(p), moment = cross(arm, n);
    float vn = dot(p.velocity + cross(p.angular, arm), n);
    if (vn < speed) {
      float j = (speed - vn) / (1 + invInertia * dot(moment, moment));
      p.velocity = p.velocity + n * j;
      p.angular = bounded(p.angular + moment * (j * invInertia), 8.f);
    }
    p.velocity = p.velocity * damping;
    p.angular = p.angular * damping;
  }
  void plane(StoredPin &p, Vec point, Vec n, float penetration) {
    if (penetration <= 0)
      return;
    p.pose.p = p.pose.p + n * std::min(.008f, penetration);
    impulse(p, point, n, 0, .94f);
  }
  void walls(StoredPin &p, const Ergonomics &c) {
    Vec anchor = holderPoint(c, 0);
    Vec hinge = pinHinge(p.opening, p.twist);
    Vec localPoints[4]{{},
                       hinge,
                       hinge + Vec{.006f * std::max(0.f, 1 - p.opening), 0, 0},
                       pinHandlePoint(p.opening, 1, p.twist)};
    for (Vec localPoint : localPoints) {
      Vec point = world(p.pose, localPoint);
      Vec v = point - anchor;
      plane(p, point, {0, 1, 0}, -.098f + radius - v.y);
      // Stored pins stay inside the fixed container. Free/held tools use the
      // finite-height walls through contactTool below before becoming stored.
      plane(p, point, {1, 0, 0}, -.025f + radius - v.x);
      plane(p, point, {-1, 0, 0}, v.x - (.025f - radius));
      plane(p, point, {0, 0, 1}, -.084f + radius - v.z);
      plane(p, point, {0, 0, -1}, v.z - (.022f - radius));
    }
  }
  void pair(StoredPin &a, StoredPin &b) {
    auto as = pinSegments(a), bs = pinSegments(b);
    for (auto sa : as)
      for (auto sb : bs) {
        auto points = closestSegments(sa, sb);
        Vec diff = points[0] - points[1];
        float d = length(diff), overlap = 2 * radius - d;
        if (overlap <= 0)
          continue;
        Vec n = d > 1e-6f ? diff * (1 / d) : Vec{0, 1, 0};
        float shift = std::min(overlap * .5f, .003f);
        a.pose.p = a.pose.p + n * shift;
        b.pose.p = b.pose.p - n * shift;
        float relative = dot(a.velocity - b.velocity, n);
        if (relative < 0) {
          a.velocity = a.velocity - n * (relative * .5f);
          b.velocity = b.velocity + n * (relative * .5f);
        }
        impulse(a, points[0], n, 0, .96f);
        impulse(b, points[1], n * -1, 0, .96f);
      }
  }

public:
  std::array<StoredPin, pinBoxCapacity> pins{};
  int count = 0;
  void wake() {
    for (int n = 0; n < count; n++) {
      pins[n].sleeping = false;
      pins[n].quiet = 0;
    }
  }
  void reconcile(int wanted, const Ergonomics &c) {
    wanted = std::clamp(wanted, 0, pinBoxCapacity);
    if (wanted < count) {
      count = wanted;
      wake();
    }
    while (count < wanted) {
      StoredPin p;
      p.opening = c.requirePreparation ? 0.f : 1.f;
      Vec anchor = holderPoint(c, 0);
      float x = (count % 3 - 1) * .014f - .003f;
      // Folded pins lie lengthwise. New stock enters just above the pile.
      float y = -.090f + (count / 3) * .006f;
      p.pose = p.opening == 0 ? Pose{axisAngle({1, 0, 0}, pi / 2),
                                     anchor + Vec{x, y, .002f}}
                              : Pose{Quat{}, anchor + Vec{x, y, -.07f}};
      p.pose.q = unit(multiply(
          axisAngle({0, 1, 0}, (int(spawned++ % 3) - 1) * .025f), p.pose.q));
      pins[count++] = p;
      wake();
    }
  }
  int nearest(Vec hand) const {
    int chosen = -1;
    float distance = .045f;
    for (int n = 0; n < count; n++) {
      for (auto s : pinSegments(pins[n])) {
        float d = distanceTo(hand, s);
        if (d < distance) {
          distance = d;
          chosen = n;
        }
      }
    }
    return chosen;
  }
  StoredPin take(int n) {
    StoredPin result = pins[n];
    for (int j = n; j + 1 < count; j++)
      pins[j] = pins[j + 1];
    --count;
    wake();
    return result;
  }
  void put(StoredPin p) {
    if (count == pinBoxCapacity)
      --count; // display cap, never an inventory removal
    p.sleeping = false;
    p.quiet = 0;
    p.velocity = bounded(p.velocity, .3f);
    p.angular = bounded(p.angular, 3.f);
    pins[count++] = p;
    wake();
  }
  bool inside(Pose pose, float opening, float twist,
              const Ergonomics &c) const {
    Vec v = world(pose, toolCenter(0, opening, twist)) - holderPoint(c, 0);
    return std::abs(v.x) < .024f && v.z > -.082f && v.z < .020f &&
           v.y > -.105f && v.y < -.020f;
  }
  // Contact for a free active pin. It can miss or leave the finite box and
  // then follows the existing world-drop transaction, just like before.
  bool contactTool(StoredPin &p, const Ergonomics &c) const {
    bool hit = false;
    Vec anchor = holderPoint(c, 0);
    for (auto seg : pinSegments(p))
      for (Vec point : {seg.a, seg.b}) {
        Vec v = point - anchor;
        auto push = [&](Vec normal, float overlap) {
          if (overlap <= 0)
            return;
          p.pose.p = p.pose.p + normal * overlap;
          float vn = dot(p.velocity, normal);
          if (vn < 0)
            p.velocity = p.velocity - normal * vn;
          p.velocity = p.velocity * .75f;
          p.angular = p.angular * .7f;
          hit = true;
        };
        if (std::abs(v.x) < .03f && v.z > -.09f && v.z < .028f && v.y > -.115f)
          push({0, 1, 0}, -.098f + radius - v.y);
        if (v.y > -.105f && v.y < -.025f) {
          if (v.z > -.09f && v.z < .028f && std::abs(v.x) < .035f) {
            push({1, 0, 0}, -.025f + radius - v.x);
            push({-1, 0, 0}, v.x - .025f + radius);
          }
          if (std::abs(v.x) < .03f && v.z > -.095f && v.z < .033f) {
            push({0, 0, 1}, -.084f + radius - v.z);
            push({0, 0, -1}, v.z - .022f + radius);
          }
        }
      }
    for (int n = 0; n < count; n++)
      for (auto a : pinSegments(p))
        for (auto b : pinSegments(pins[n])) {
          auto points = closestSegments(a, b);
          Vec d = points[0] - points[1];
          float len = length(d);
          if (len < 2 * radius) {
            Vec normal = len > 1e-6f ? d * (1 / len) : Vec{0, 1, 0};
            p.pose.p = p.pose.p + normal * (2 * radius - len);
            float vn = dot(p.velocity, normal);
            if (vn < 0)
              p.velocity = p.velocity - normal * vn;
            p.velocity = p.velocity * .8f;
            p.angular = p.angular * .7f;
            hit = true;
          }
        }
    return hit;
  }
  // Swept palm/finger proxies move the stored wire bodies. Tracking jumps
  // are ignored; touching stock never changes the native inventory.
  bool rummage(Segment from, Segment to, float dt, const Ergonomics &c) {
    float travel = std::max(length(to.a - from.a), length(to.b - from.b));
    if (!count || dt <= 1e-5f || !finite(to.a) || !finite(to.b) ||
        travel < .00015f || travel > .20f)
      return false;
    Vec anchor = holderPoint(c, 0);
    // Do not stir through the exterior sides/bottom of the box.
    auto insideTop = [&](Vec v) {
      v = v - anchor;
      return std::abs(v.x) < .030f && v.z > -.090f && v.z < .028f &&
             v.y > -.098f && v.y < .025f;
    };
    bool hit = false;
    int steps = std::max(1, int(std::ceil(travel / .006f)));
    for (int step = 1; step <= steps; step++) {
      float t = float(step) / steps;
      for (int sample = 0; sample < 3; sample++) {
        float f = sample * .5f;
        Vec a = from.a + (from.b - from.a) * f,
            b = to.a + (to.b - to.a) * f,
            hand = a + (b - a) * t;
        if (!insideTop(hand))
          continue;
        float skin = .020f - .009f * f;
        Vec velocity = bounded((b - a) * (1 / dt), 1.f);
        for (int n = 0; n < count; n++) {
          auto &p = pins[n];
          for (auto segment : pinSegments(p)) {
            Vec point = closestSegments(segment, {hand, hand})[0];
            Vec delta = point - hand;
            float d = length(delta), overlap = skin + radius - d;
            if (overlap <= 0)
              continue;
            if (!hit)
              wake();
            hit = true;
            Vec normal = d > 1e-6f ? delta * (1 / d) : locks::normal(velocity);
            if (length(normal) < .5f)
              normal = {1, 0, 0};
            p.pose.p = p.pose.p + normal * std::min(overlap * .45f, .004f);
            impulse(p, point, normal,
                    std::clamp(dot(velocity, normal), .02f, .45f), .98f);
            p.velocity = bounded(p.velocity, .5f);
          }
        }
      }
      if (hit) {
        for (int iteration = 0; iteration < 2; iteration++) {
          for (int a = 0; a < count; a++)
            for (int b = a + 1; b < count; b++)
              pair(pins[a], pins[b]);
          for (int n = 0; n < count; n++)
            walls(pins[n], c);
        }
      }
    }
    return hit;
  }
  void update(float dt, Pose lock, const Ergonomics &c) {
    dt = std::clamp(dt, 0.f, .05f);
    bool awake = false;
    for (int n = 0; n < count; n++)
      awake |= !pins[n].sleeping;
    if (!awake)
      return;
    int steps = std::max(1, int(std::ceil(dt * 180)));
    float h = dt / steps;
    Vec gravity = rotate(inverse(lock.q), {0, -9.81f, 0});
    for (int step = 0; step < steps; step++) {
      for (int n = 0; n < count; n++) {
        auto &p = pins[n];
        Vec center = pinMassCenter(p);
        p.velocity = bounded(p.velocity + gravity * h, .5f);
        center = center + p.velocity * h;
        float spin = length(p.angular);
        Quat q = p.pose.q;
        if (spin > 1e-5f)
          q = unit(multiply(axisAngle(p.angular, spin * h), q));
        movePinCenter(p, center, q);
      }
      for (int iteration = 0; iteration < 4; iteration++) {
        for (int a = 0; a < count; a++)
          for (int b = a + 1; b < count; b++)
            pair(pins[a], pins[b]);
        for (int n = 0; n < count; n++)
          walls(pins[n], c);
      }
      for (int n = 0; n < count; n++) {
        auto &p = pins[n];
        p.velocity = p.velocity * std::exp(-5 * h);
        p.angular = p.angular * std::exp(-8 * h);
        bool calm = length(p.velocity) < .04f && length(p.angular) < 1.f;
        p.quiet = calm ? p.quiet + h : 0;
        p.sleeping = p.quiet > .8f;
      }
    }
  }
};
} // namespace locks
