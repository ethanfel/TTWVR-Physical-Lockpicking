// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Collision.h"
#include "Kit.h"
#include "ToolContact.h"
#include "DriverCan.h"
#include "DriverContact.h"
#include "PinBox.h"
#include "ToolGeometry.h"
namespace locks {
inline Pose dropOriginPose(Pose lock) {
  Vec forward = rotate(lock.q, {0, 0, 1});
  float yaw = std::atan2(forward.x, forward.z);
  Pose origin{axisAngle({0, 1, 0}, yaw), lock.p};
  origin.p = origin.p + Vec{0, -.18f, 0} + rotate(origin.q, {0, 0, -.03f});
  return relative(lock, origin);
}
struct PhysicalTool {
  Pose pose;
  Quat wristReference, heldRotation;
  Pose seatPose, driverTracked;
  bool driverTracking = false;
  float driverDemand = 0, driverTurnStep = 0, driverPullback = 0;
  Vec pinContact, lastRawTip;
  bool captureBlocked = false;
  Vec entryHand, entryAxis{0, 0, 1}, lastHand, seatHand;
  float searchAngle = 0, leverStartCylinder = 0;
  Vec leverStartHand;
  float opening = 0, bendYaw = 0;
  float referenceAngle = 90, seatedRadius = 0, insertProgress = 0;
  Vec velocity, angular;
  int owner = -1;
  bool inserted = false, inserting = false, visible = true, holstered = false;
  bool blocked = false;
  float lostTime = 0, blockedTime = 0, boxRest = 0;
  bool housingLoaded = false;
  float pinDepth = 0, seatDepth = 0, alignment = 1;
};
// Live inventory owns pickup/drop. Both presentations run the held-tool solver below.
struct MountedTools {
  int owners[2]{-1,-1};
  float opening = 0, twist = 0, trackingScale = 1;
  bool parked = false;
};
class PhysicalInteraction {
  bool initialized = false, broken = false;
  float tremorTime = 0, strain = 0;
  Segment previousHands[2];
  bool handsInitialized = false;
  bool pinLoaded = false;

  void syncOwners() {
    pickHand = tools[0].owner;
    screwdriverHand = tools[1].owner;
  }
  void respawn(int k, bool preserveOpening = false) {
    float opening = tools[k].opening, twist = tools[k].bendYaw;
    tools[k] = {};
    tools[k].bendYaw = preserveOpening ? twist : 0;
    tools[k].opening =
        preserveOpening ? opening : (config.requirePreparation ? 0.f : 1.f);
    // Even with the pocket meshes hidden, tools start at reachable anchors.
    // There is no tray or invisible support surface.
    tools[k].holstered = true;
    if(sharedKit) tools[k].visible=false;
    tools[k].pose = holderTool(config, k, tools[k].opening, tools[k].bendYaw);
    if (k == 1 && config.useHolders) {
      tools[k].holstered = false;
      tools[k].pose = looseDriverStart(config);
    }
  }

  void release(int k, bool detach = false) {
    auto &t = tools[k];
    if (k == 1 && config.driverMode == DriverMode::AxialTwist && t.inserted) {
      // Park it at the actual depth on release, including a partial pull.
      t.seatPose.p = turnWithCylinder({{}, t.pose.p}, -cylinderAngle).p;
      if (plugFaceZ - t.pose.p.z < .003f) detach = true;
    }
    t.owner = -1;
    t.driverTracking = false;
    t.inserting = false;
    t.insertProgress = 0;
    if (detach || (k == 1 && config.driverMode == DriverMode::SidePush))
      t.inserted = false;
    if (t.inserted)
      t.velocity = t.angular = {};
  }
  // The contact pose defines the physical shaft. Only the admitted cylinder
  // angle turns the visible metal, including while the lock resists.
  void updateAxialDriver(const Pose (&hands)[2], Pose previous, float cylinder,
                         unsigned acquired, bool manipulating, bool wasBlocked) {
    auto &tool = tools[1];
    const auto &pin = tools[0];
    constexpr float seatDepth = .014f; // measured behind the visible plug face
    constexpr float stopDepth = .018f;
    Pose hand = tool.owner >= 0 ? hands[tool.owner] : Pose{};
    if (tool.inserted) {
      Vec axis = rotate(tool.seatPose.q, {0, 0, 1});
      // A released screwdriver remains supported. Regripping establishes a
      // fresh wrist reference at the current cylinder angle.
      float pull = tool.owner >= 0 ? std::max(0.f, dot(hand.p - tool.seatHand, axis)) : 0;
      tool.pose = {unit(multiply(tool.seatPose.q, axisAngle({0, 0, 1}, -cylinder * pi / 180))),
                   turnWithCylinder({{}, tool.seatPose.p}, cylinder).p + axis * pull};
      if (tool.owner < 0) return;
      float depth = plugFaceZ - tool.pose.p.z;
      tool.insertProgress = std::clamp(depth / seatDepth, 0.f, 1.f);
      Vec delta = hand.p - tool.seatHand;
      bool inReach = length(delta - axis * dot(delta, axis)) < .055f &&
                     dot(delta, axis) > -.065f;
      if (depth > 0 && inReach) {
        if (!pin.inserted || !manipulating || (insertedEvents & 1u) || pull > .003f) {
          tool.wristReference = hand.q;
          tool.leverStartCylinder = cylinder;
        } else {
          float angle = shaftTwist(tool.seatPose, tool.wristReference, hand.q);
          target = std::clamp(tool.leverStartCylinder +
                              angle * (90.f / config.fullTurnWrist), 0.f, 90.f);
          if (target < 1.f) target = 0;
          if (target >= 89.99f) target = 90;
        }
        return;
      }
      tool.inserted = false;
      withdrawnEvents |= 2u;
      previous = tool.pose;
    }
    if (tool.owner < 0) return;
    ToolShape shape(1, 1, 0, targetBounds);
    shape.driverStopZ = plugFaceZ - stopDepth;
    Pose raw{hand.q, hand.p - rotate(hand.q, handleCenter(1) * targetBounds.toolScale[1])};
    auto contact = moveAgainstHousing(shape, previous, raw, cylinder);
    tool.pose = contact.pose;
    tool.blocked = contact.hit;
    if (pin.visible && !pin.holstered) {
      auto between = sweepToolContact(ToolShape(0, pin.opening, pin.bendYaw, targetBounds),
                                      pin.pose, pin.pose, previous, tool.pose);
      tool.pose = between.driver;
      tool.blocked |= between.hit;
    }
    if (tool.blocked && !wasBlocked) housingHands |= 1u << tool.owner;
    float depth = plugFaceZ - tool.pose.p.z;
    Vec axis = rotate(tool.pose.q, {0, 0, 1});
    bool insideSlot = false, centered = false;
    if (depth > 0 && axis.z > .85f) {
      Vec entry = tool.pose.p + axis * (depth / axis.z);
      Vec slot = turnWithCylinder({{}, entry}, -cylinder).p;
      insideSlot = std::abs(slot.x) < keywayWidth(targetBounds.style) &&
                   std::abs(slot.y) < keywayHeight(targetBounds.style);
      centered = insideSlot && std::abs(slot.y) <= keywayHeight(targetBounds.style) * .45f;
    }
    const bool enteredFromFront = tool.inserting || previous.p.z >= plugFaceZ;
    tool.inserting = manipulating && insideSlot && enteredFromFront && !(acquired & 2u);
    tool.insertProgress = tool.inserting ? std::clamp(depth / seatDepth, 0.f, 1.f) : 0;
    if (!centered || tool.insertProgress < 1) return;
    tool.inserted = true;
    tool.inserting = false;
    tool.seatPose = {unit(multiply(tool.pose.q, axisAngle({0, 0, 1}, cylinder * pi / 180))),
                     turnWithCylinder({{}, tool.pose.p}, -cylinder).p};
    tool.wristReference = hand.q;
    tool.seatHand = hand.p;
    tool.leverStartCylinder = cylinder;
    insertedEvents |= 2u;
  }

  void looseStep(int k, Pose lock, float dt) {
    auto &t = tools[k];
    Vec gravity = rotate(inverse(lock.q), {0, -9.81f, 0});
    int steps = std::max(1, int(std::ceil(dt * 180)));
    if (k == 1 && config.useHolders &&
        length(t.pose.p - holderPoint(config, 1)) < .4f)
      steps = std::max(
          steps, std::min(256, int(std::ceil(dt *
                                             (length(t.velocity) + 9.81f * dt +
                                              length(t.angular) * .2f) /
                                             .002f))));
    t.blocked = false;
    float h = dt / steps;
    ToolShape shape(k, t.opening, t.bendYaw, targetBounds);
    for (int s = 0; s < steps; s++) {
      Pose desired = t.pose;
      Vec center = world(t.pose, toolCenter(k, t.opening, t.bendYaw));
      t.velocity = t.velocity + gravity * h;
      center = center + t.velocity * h;
      float speed = length(t.angular);
      if (speed > 1e-5f)
        desired.q = unit(multiply(axisAngle(t.angular, speed * h), t.pose.q));
      desired.p =
          center - rotate(desired.q, toolCenter(k, t.opening, t.bendYaw));
      auto contact = moveAgainstHousing(shape, t.pose, desired, cylinderAngle);
      t.pose = contact.pose;
      if (k == 0 && config.useHolders) {
        StoredPin body{t.pose, t.velocity, t.angular, t.opening, t.bendYaw};
        box.contactTool(body, config);
        t.pose = body.pose;
        t.velocity = body.velocity;
        t.angular = body.angular;
      }
      if (k == 1 && config.useHolders)
        contactCan(t.pose, t.velocity, t.angular, config, shape);
      t.blocked |= contact.hit;
      if (contact.hit) {
        float vn = dot(t.velocity, contact.normal);
        if (vn < 0)
          t.velocity = t.velocity - contact.normal * (1.08f * vn);
        t.velocity = t.velocity * std::exp(-8 * h);
        t.angular = t.angular * std::exp(-18 * h);
      } else
        t.angular = t.angular * std::exp(-.2f * h);
    }
    if (k == 0 && config.useHolders) {
      StoredPin body{t.pose, t.velocity, t.angular, t.opening, t.bendYaw};
      box.contactTool(body, config);
      t.pose = body.pose;
      t.velocity = body.velocity;
      t.angular = body.angular;
      bool resting = box.inside(t.pose, t.opening, t.bendYaw, config) &&
                     length(t.velocity) < .10f && length(t.angular) < 1.f;
      t.boxRest = resting ? t.boxRest + dt : 0;
      if (t.boxRest > .20f) {
        box.put(body);
        respawn(0);
        storedEvents |= 1;
        return;
      }
    }
    Vec p = local(dropOrigin, t.pose.p);
    if (k == 0 && worldDrops && (p.y < -.16f || length(p) > .9f)) {
      t.visible = false;
      t.velocity = t.angular = {};
      dropPending = true;
      droppedEvents = 1;
      return;
    }
    bool lost = p.y < -.45f || length(p) > 1.2f || !finite(p);
    t.lostTime = lost ? t.lostTime + dt : 0;
    if (t.lostTime > .6f) {
      respawn(k, true);
      if(sharedKit) kit.put(k);
      recovered |= 1u << k;
    }
  }

public:
  bool sharedKit = false;
  unsigned kitNear = 0, kitHandoff = 0;
  KitRoles kit;
  TargetBounds targetBounds;
  PhysicalTool tools[2];
  PinBox box;
  int inventory = -1;
  bool pinResisting = false;
  Grab grabs[2];
  Pose dropOrigin;
  float cylinderAngle = 0;
  int pickHand = -1, screwdriverHand = -1, bendHand = -1;
  Vec bendStartHelper, bendStartTip;
  unsigned preparedHands = 0;
  float pick = 90, target = 0, pinTremor = 0;
  bool pickNear = false, screwdriverNear = false;
  unsigned insertedEvents = 0, withdrawnEvents = 0, recovered = 0,
           storedEvents = 0, housingHands = 0, rummagedHands = 0;
  Ergonomics config;
  bool worldDrops = false, dropPending = false;
  unsigned droppedEvents = 0;
  void resolveDrop(bool success, int remaining) {
    dropPending = false;
    respawn(0, !success);
    if(sharedKit) kit.put(0);
    if (success && remaining == 0)
      tools[0].visible = false;
  }
  void syncInventory(bool available, int count) {
    inventory = available ? count : -1;
    if (dropPending) {
      tools[0].visible = false;
      release(0, true);
    } else if (available && count == 0) {
      if(sharedKit && !tools[0].holstered) {kit.put(0);respawn(0);}
      tools[0].visible = false;
      release(0, true);
    }
  }
  void reset(float angle = 90) {
    auto settings = config;
    *this = {};
    config = settings;
    pick = physicalPinSearch(angle, config.pinSearchDegrees);
  }
  Pose shownPin() const {
    const auto &p=tools[0];
    Pose visible=tremblingPinPose(p.pose,pinTremor);
    if (seated() && std::abs(pinTremor)>.00001f)
      return sweepToolContact(ToolShape(0,p.opening,p.bendYaw,targetBounds),
                              p.pose,visible,tools[1].pose,tools[1].pose).pin;
    return visible;
  }
  bool seated() const { return tools[0].inserted && tools[1].inserted; }
  void update(Pose lock, Pose left, Pose right, float lt, float rt,
              float cylinder, float dt = 1.f / 90, unsigned stage = 2,
              float health = 1, const Pose *pinches = nullptr, const MountedTools *mounted = nullptr) {
    dt = std::isfinite(dt) ? std::clamp(dt, 0.f, .05f) : 0;
    insertedEvents = withdrawnEvents = recovered = preparedHands =
        storedEvents = droppedEvents = housingHands = rummagedHands = 0;
    float cylinderDelta = cylinder - cylinderAngle;
    cylinderAngle = cylinder;
    if (initialized && tools[0].inserted && std::abs(cylinderDelta) > 1e-6f) {
      auto &p = tools[0];
      Quat q = axisAngle({0, 0, 1}, -cylinderDelta * pi / 180);
      p.pose = turnWithCylinder(p.pose, cylinderDelta);
      p.seatPose = turnWithCylinder(p.seatPose, cylinderDelta);
      p.pinContact = rotate(q, p.pinContact);
      p.entryAxis = rotate(q, p.entryAxis);
      p.seatHand = rotate(q, p.seatHand);
    }
    // Both tips belong to the rotating keyway. Keep their separation while
    // the cylinder turns, including when either supported tool is released.
    if (config.driverMode == DriverMode::SeatedLever && initialized && tools[1].inserted && std::abs(cylinderDelta) > 1e-6f)
      tools[1].pose = turnWithCylinder(tools[1].pose, cylinderDelta);
    bool wasBlocked[2]{tools[0].blocked, tools[1].blocked};
    Pose previous[2]{tools[0].pose, tools[1].pose};
    for (auto &tool : tools)
      tool.blocked = false;
    if (!initialized) {
      dropOrigin = dropOriginPose(lock);
      respawn(0);
      respawn(1);
      initialized = true;
    }
    bool manipulating = stage == 2 || stage == 3 || stage == 4;
    if (!broken && (stage == 5 || (stage == 6 && health <= 0))) {
      if (tools[0].owner >= 0)
        grabs[tools[0].owner].reset();
      release(0, true);
      tools[0].visible = false;
      broken = true;
    } else if (broken && (stage == 1 || stage == 2) && health > 0) {
      respawn(0);
      if(sharedKit) kit.put(0);
      pick = 90;
      broken = false;
    }
    if (!mounted && config.useHolders) {
      box.reconcile(pinsInBox(inventory, dropPending || (tools[0].visible &&
                                                         !tools[0].holstered)),
                    config);
      box.update(dt, lock, config);
      if (tools[0].holstered && box.count) {
        tools[0].pose = box.pins[0].pose;
        tools[0].opening = box.pins[0].opening;
        tools[0].bendYaw = box.pins[0].twist;
      }
    }
    Pose hands[2]{relative(lock, left), relative(lock, right)};
    Pose pinHands[2]{relative(lock, pinches ? pinches[0] : left),
                     relative(lock, pinches ? pinches[1] : right)};
    auto handFor = [&](int k, int h) {
      Pose hand = k == 0 ? pinHands[h] : hands[h];
      if (k == 0 && tools[0].owner == h)
        hand.q = unit(multiply(hand.q, tools[0].heldRotation));
      return hand;
    };
    float triggers[2]{lt, rt};
    auto gripCenter=[&](int k,float opening=1,float twist=0) {
      return handleCenter(k,opening,twist)*targetBounds.toolScale[k];
    };
    auto held=[&](Pose h,int k,float opening=1,float twist=0) {
      return Pose{h.q,h.p-rotate(h.q,gripCenter(k,opening,twist))};
    };
    bool nearby[2]{};
    unsigned acquired = 0;
    auto takeTool = [&](int best, int h) {
      auto &t = tools[best];
      t.owner = h;
      t.holstered = false;
      if (best == 0)
        t.heldRotation = t.inserted ? unit(multiply(inverse(pinHands[h].q), t.pose.q))
                                   : Quat{};
      Pose hand = handFor(best, h);
      t.wristReference = hand.q;
      t.searchAngle = pinBearing(hand.p);
      t.referenceAngle = pick;
      t.velocity = t.angular = {};
      t.inserting = false;
      t.lastHand = hand.p;
      t.seatedRadius = length(hand.p - turnWithCylinder({{}, mouth(best, targetBounds.style)}, cylinder).p);
      t.seatHand = hand.p;
      t.seatDepth = t.pinDepth;
      if (!(best == 1 && t.inserted && config.driverMode == DriverMode::AxialTwist))
        t.seatPose = best == 1 && t.inserted ? turnWithCylinder(t.pose, -cylinder) : t.pose;
      t.lastRawTip = t.pose.p;
      if (best == 0 && t.inserted)
        t.entryAxis = normal(rotate(t.pose.q, pinHinge(t.opening, t.bendYaw)));
      if (best == 1 && t.inserted) {
        t.leverStartHand = hand.p;
        t.leverStartCylinder = cylinder;
      }
      if (!t.inserted)
        t.pose = held(hand, best, t.opening, t.bendYaw);
      acquired |= 1u << best;
    };
    if (mounted) {
      tools[0].opening = mounted->opening;
      tools[0].bendYaw = mounted->twist;
      for (int k = 0; k < 2; ++k) {
        auto &t = tools[k];
        int h = mounted->owners[k];
        if (t.owner != h || (h < 0 && t.inserted && (k || !mounted->parked))) {
          release(k, k == 1 || (h < 0 && !mounted->parked));
          if (h >= 0) {
            takeTool(k, h);
            previous[k] = t.pose;
            t.lastRawTip = t.pose.p;
          }
        }
        t.visible = h >= 0 || (k == 0 && mounted->parked);
        t.holstered = !t.visible;
      }
    } else {
      for (int h = 0; h < 2; h++) {
        if (triggers[h] <= .2f) {
          grabs[h].update(triggers[h], false);
          for (int k = 0; k < 2; k++)
            if (tools[k].owner == h) {
              bool store =
                  !config.useHolders && !tools[k].inserted &&
                  (sharedKit ? (kitNear & (1u<<h)) != 0 :
                   length(handFor(k, h).p - holderPoint(config, k)) < .075f);
              release(k);
              if (store) {
                respawn(k, true);
                if(sharedKit) kit.put(k);
                storedEvents |= 1u << k;
              }
            }
        }
      }
      if (bendHand >= 0 && (triggers[bendHand] <= .2f || tools[0].owner < 0 ||
                            !tools[0].visible || !manipulating)) {
        grabs[bendHand].reset();
        bendHand = -1;
      }
      for (int h = 0; h < 2; h++) {
        int best = -1, stockPin = -1;
        float closest = 1e9f;
        for (int k = 0; k < 2; k++) {
          auto &t = tools[k];
          auto handle = toolHandle(k, t.opening, t.bendYaw);
          float d = distanceTo(handFor(k, h).p, {world(t.pose, handle.a),
                                                 world(t.pose, handle.b)});
          bool close = t.visible && d <= (k == 0 ? .04f : .045f);
          if (k == 0 && t.holstered && config.useHolders) {
            stockPin = box.nearest(handFor(0, h).p);
            close = t.visible && stockPin >= 0;
            if (close)
              d = 0; // Selecting an actual wire in the box takes priority.
          }
          nearby[k] |= close;
          if (close && t.owner < 0 && d < closest) {
            best = k;
            closest = d;
          }
        }
        if (grabs[h].held)
          continue;
        // A second hand can take the free end. The generous contact zone keeps
        // physical controller shells from needing to occupy the same point.
        auto &pin = tools[0];
        bool canBend = pin.owner >= 0 && pin.owner != h && pin.visible &&
                       !pin.inserted && bendHand < 0 &&
                       length(pinHands[h].p - pin.pose.p) < .05f;
        bool handoff=sharedKit && (kitHandoff & (1u<<h));
        KitRoles proposed=kit;
        int draw=-1;
        if(sharedKit && manipulating && (handoff || (kitNear & (1u<<h)))) {
          draw=handoff ? (kit.pinHand==h ? 0 : 1) : proposed.draw(h,inventory>0);
          if(draw>=0 && tools[draw].holstered && (draw==1 || inventory>0)) best=draw;
          else draw=-1;
        }
        if (canBend && best < 0)
          best = 2;
        if(handoff && draw>=0 && triggers[h]>.2f) grabs[h].armed=true;
        if(handoff && manipulating) {
          kitHandoff &= ~(1u<<h);
          if(triggers[h]<=.2f || draw<0) kit.put(kit.pinHand==h ? 0 : 1);
        }
        if (!grabs[h].update(triggers[h], manipulating && best >= 0))
          continue;
        if (best == 2) {
          bendHand = h;
          bendStartHelper = local(handFor(0, pin.owner), pinHands[h].p);
          bendStartTip = pinHinge(pin.opening, pin.bendYaw) * -1;
          continue;
        }
        if(draw>=0 && best==draw) {
          kit=proposed;tools[draw].visible=true;
        }
        auto &t = tools[best];
        if (best == 0 && t.holstered && config.useHolders && stockPin >= 0) {
          auto stored = box.take(stockPin);
          t.pose = stored.pose;
          t.opening = stored.opening;
          t.bendYaw = stored.twist;
        }
        takeTool(best, h);
      }
      if (bendHand >= 0 && tools[0].owner >= 0) {
        auto &pin = tools[0];
        Vec delta =
            local(handFor(0, pin.owner), pinHands[bendHand].p) - bendStartHelper;
        bool wasUsable = pinUsable(pin.opening);
        bendPin(bendStartTip,delta,pin.opening,pin.bendYaw);
        if (!wasUsable && pinUsable(pin.opening))
          preparedHands = (1u << pin.owner) | (1u << bendHand);
      }
    }
    pickNear = nearby[0];
    screwdriverNear = nearby[1];
    // The pin and the older seated lever use this insertion path.
    for (int k = 0; k < 2; k++) {
      auto &t = tools[k];
      if (t.owner < 0 || t.inserted || (k == 1 && config.driverMode != DriverMode::SeatedLever))
        continue;
      Pose hand = handFor(k, t.owner),
           raw = held(hand, k, t.opening, t.bendYaw);
      Vec previousRaw = t.lastRawTip;
      t.lastRawTip = raw.p;
      if (k == 0 && raw.p.z > mouth(0).z + .020f)
        t.captureBlocked = false;
      if (k == 0 && (!pinCanEngage(t.opening) || bendHand >= 0)) {
        t.inserting = false;
        t.insertProgress = 0;
        t.pose = raw;
        t.lastHand = hand.p;
        continue;
      }
      Pose unconstrained = raw;
      if (k == 0) {
        auto contact =
            moveAgainstHousing(ToolShape(0, t.opening, t.bendYaw, targetBounds),
                               previous[0], raw, cylinder);
        raw = contact.pose;
        t.blocked = contact.hit;
        if (contact.hit && !wasBlocked[0])
          housingHands |= 1u << t.owner;
      }
      Vec spot = mouth(k, targetBounds.style), tip = raw.p;
      float side = std::hypot(tip.x - spot.x, tip.y - spot.y);
      Vec shaft = normal(rotate(raw.q, k == 0 ? pinHinge(t.opening, t.bendYaw)
                                              : Vec{0, 0, 1}));
      // Accept an inclined shaft, independent of the held leg's roll. Once
      // caught, use hand travel along this entry axis, not the drifting raw
      // tip (which moves when the user relaxes the wrist).
      bool inFront = shaft.z >= std::cos(70 * pi / 180);
      bool crossed =
          tip.z <= spot.z + .004f && previous[k].p.z > spot.z + .004f;
      bool approaching =
          k == 0 ? (unconstrained.p.z < previousRaw.z - .0001f &&
                    length(unconstrained.p - tip) < .025f && !t.captureBlocked)
                 : tip.z < previous[k].p.z - .0001f;
      if (!t.inserting && !(acquired & (1u << k)) && manipulating && inFront &&
          side <= config.insertRadius && tip.z >= spot.z - .10f &&
          tip.z <= spot.z + .004f && approaching) {
        if (k == 0) {
          t.inserted = true;
          t.inserting = false;
          insertedEvents |= 1;
          Vec aperture = rotate(axisAngle({0, 0, 1}, cylinder * pi / 180), tip);
          // Catch only the real slot. The old 28 mm magnetic capture zone
          // visibly teleported an off-centre pin onto the central axis.
          if (std::abs(aperture.x) > keywayWidth(targetBounds.style) - pinEntryMargin ||
              std::abs(aperture.y) > keywayHeight(targetBounds.style) - pinEntryMargin) {
            t.inserted = false;
            insertedEvents &= ~1u;
            t.pose = raw;
            t.lastHand = hand.p;
            continue;
          }
          t.entryAxis = shaft;
          t.pinDepth = t.seatDepth = std::max(0.f, (spot.z - tip.z) / shaft.z);
          t.pinContact = tip + shaft * t.pinDepth;
          t.seatPose = raw;
          t.seatHand = hand.p;
          t.alignment = std::clamp(1 - .4f * side / config.insertRadius -
                                       .45f * (1 - shaft.z),
                                   .25f, 1.f);
          pick = boundPinSearch(pinPoseAngle(raw, cylinder), config.pinSearchDegrees, pick);
          t.referenceAngle = pick;
          t.searchAngle = pinBearing(hand.p);
          t.lastHand = hand.p;
          t.pose = raw; // Preserve the exact approach pose; no seating snap.
          continue;
        }
        t.inserting = true;
        t.entryHand = hand.p;
        t.entryAxis = shaft;
        if (crossed) {
          float alpha = std::clamp((previous[k].p.z - spot.z - .004f) /
                                       (previous[k].p.z - tip.z),
                                   0.f, 1.f);
          t.entryHand = t.lastHand + (hand.p - t.lastHand) * alpha;
        }
      }
      t.pose = raw;
      t.insertProgress = 0;
      if (t.inserting) {
        Vec travel = t.entryHand - hand.p;
        float advance = dot(travel, t.entryAxis);
        float sideways = length(travel - t.entryAxis * advance);
        if (sideways > config.insertRadius + .045f || advance < -.025f) {
          t.inserting = false;
        } else {
          t.insertProgress =
              std::clamp(advance / config.insertionTravel, 0.f, 1.f);
          t.pose = aimedTool(hand, spot, driverInsertionDepth * t.insertProgress, targetBounds.style);
          if (t.insertProgress >= 1) {
            t.inserted = true;
            t.inserting = false;
            insertedEvents |= 1u << k;
            t.wristReference = hand.q;
            t.referenceAngle = pick;
            t.searchAngle = pinBearing(hand.p);
            t.seatedRadius = length(hand.p - spot);
            t.seatHand = hand.p;
            t.seatPose = turnWithCylinder(t.pose, -cylinder);
            t.leverStartHand = hand.p;
            t.leverStartCylinder = cylinder;
          }
        }
      }
      t.lastHand = hand.p;
    }
    target = 0;
    auto &pin = tools[0];
    auto &driver = tools[1];
    Pose pinBeforeDriver = pin.pose;
    if (driver.owner<0 || (acquired & 2u)) driver.housingLoaded=false;
    if (config.driverMode == DriverMode::SidePush && driver.owner >= 0) {
      bool touching = driver.inserted;
      Pose tracked = hands[driver.owner];
      Pose wanted = held(tracked, 1);
      ToolShape shape(1, 1, 0, targetBounds);
      if (touching && std::abs(cylinderDelta) > 1e-6f && pin.inserted) {
        // The native cylinder moves the pin before this controller update.
        // Resolve its contact with the driver before admitting fresh motion.
        previous[1] = recoverDriverContact(ToolShape(0,pin.opening,pin.bendYaw,targetBounds),
                                           pin.pose,shape,previous[1],cylinder);
      }
      if (driver.driverTracking && !(acquired & 2u)) {
        // Apply new controller motion at the admitted grip, discarding motion
        // blocked on the previous frame. Backing off therefore responds now,
        // not after retracing invisible translation/rotation through the lock.
        Quat delta = unit(multiply(tracked.q, inverse(driver.driverTracked.q)));
        wanted.q = unit(multiply(delta, previous[1].q));
        Vec grip = world(previous[1], gripCenter(1)) + tracked.p - driver.driverTracked.p;
        wanted.p = grip - rotate(wanted.q, gripCenter(1));
        Pose raw = held(tracked, 1);
        if (!touching && !wasBlocked[1] && shape.clearance(raw, cylinder) > .005f &&
            shape.clearance(wanted, cylinder) > .005f) {
          // Reconcile only in clear space, through the same swept collision
          // path. No snap back into the face and no permanent grip offset.
          wanted.q = rotateTowards(wanted.q, raw.q, dt * 180.f);
          grip = grip + bounded(tracked.p - grip, dt * .4f);
          wanted.p = grip - rotate(wanted.q, gripCenter(1));
        }
      }
      driver.driverTracked = tracked;
      driver.driverTracking = true;
      Vec requestedGrip = world(wanted, gripCenter(1));
      // Load the side of the lower slot. Hand bearing about the lock centre
      // used to turn a right-side grip's downward motion into clockwise input,
      // while ignoring the intended rightward push. Use only fresh handle
      // translation along the rotating keyway's loading direction instead.
      float turn = driverSideTurn((requestedGrip - world(previous[1], gripCenter(1))) *
                                  (mounted ? mounted->trackingScale : 1.f), cylinder);
      driver.driverTurnStep = turn;
      // Preserve the approved controller-to-turn mapping above; only the
      // physical blade/handle contact motion changes here.
      if (touching && !(acquired & 2u))
        wanted = driverLeverPose(previous[1], wanted, cylinder, targetBounds.style,targetBounds.toolScale[1]);
      auto collision = moveAgainstHousing(shape, (acquired & 2u) ? wanted : previous[1], wanted, cylinder);
      driver.pose = collision.pose;
      driver.blocked = collision.hit;
      if (pin.visible && !pin.holstered) {
        auto tools = sweepToolContact(ToolShape(0,pin.opening,pin.bendYaw,targetBounds),
                                      pin.pose,pin.pose,previous[1],driver.pose);
        driver.pose = tools.driver;
        driver.blocked |= tools.hit;
      }
      driver.inserting = false;
      auto contact = driverContact(driver.pose, cylinder, targetBounds.style, touching);
      driver.inserted = driver.owner>=0 && manipulating && contact.touching;
      driver.insertProgress = driver.inserted ? 1.f : 0.f;
      // The swept solver leaves a small separation after a hit. A continuous
      // push can alternate hit/clear across that gap; pulse once per contact.
      if (driver.blocked && !driver.housingLoaded) housingHands |= 1u << driver.owner;
      if (driver.blocked) driver.housingLoaded=true;
      else if (driver.housingLoaded) {
        Vec point;
        float gap=shape.clearance(driver.pose,cylinder,&point);
        Vec pressure=world(held(tracked,1),gripCenter(1))-world(driver.pose,gripCenter(1));
        driver.housingLoaded=gap<.00075f && dot(pressure,shape.normalAt(point,cylinder))<-.00002f;
      }
      if (driver.inserted && (!touching || (acquired & 2u))) {
        insertedEvents |= 2u;
        driver.leverStartHand = hands[driver.owner].p;
        driver.leverStartCylinder = cylinder;
        driver.driverDemand = cylinder;
        driver.driverPullback = 0;
      } else if (!driver.inserted && touching) {
        withdrawnEvents |= 2u;
      }
      if (!driver.inserted && driver.owner>=0) {
        driver.leverStartHand = hands[driver.owner].p;
        driver.leverStartCylinder = cylinder;
        driver.driverDemand = driver.driverPullback = 0;
      } else if (touching && !(acquired & 2u) && pin.inserted && manipulating) {
        driver.driverDemand = std::clamp(driver.driverDemand + turn, 0.f, 90.f);
        // Pulling back relieves side loading even if the rotated slot still
        // blocks the blade. Otherwise retained demand can keep the cylinder
        // loaded and break the pin while the player is trying to withdraw.
        Vec motion = requestedGrip - world(previous[1], gripCenter(1));
        driver.driverPullback = std::max(0.f, driver.driverPullback + motion.z);
        if (driver.driverPullback >= .003f) {
          driver.driverDemand = 0;
          driver.driverPullback = 0;
        }
      }
    }
    if (config.driverMode == DriverMode::AxialTwist) {
      updateAxialDriver(hands, previous[1], cylinder, acquired, manipulating, wasBlocked[1]);
    } else if (driver.owner >= 0 && driver.inserted) {
      if (!pin.inserted || !manipulating || (insertedEvents & 1u) ||
          (config.driverMode == DriverMode::SeatedLever && std::hypot(driver.leverStartHand.x, driver.leverStartHand.y) < .025f)) {
        driver.leverStartHand = hands[driver.owner].p;
        driver.leverStartCylinder = cylinder;
        driver.driverDemand = cylinder;
      } else if (config.driverMode == DriverMode::SidePush) {
        target = driver.driverDemand > 1.f ? driver.driverDemand : 0.f;
        // Match the legacy endpoint tolerance: floating-point integration
        // must not strand a valid full turn just below native completion.
        if (target >= 89.99f)
          target = 90.f;
      } else {
        float delta = leverTravel(driver.leverStartHand, hands[driver.owner].p);
        // Moving the handle clockwise about the plug applies torque. Axial
        // wrist roll and a fresh grip cannot start turning by themselves.
        if (delta > 1.f)
          target = std::clamp(driver.leverStartCylinder + delta, 0.f, 90.f);
        if (target >= 89.99f)
          target = 90;
      }
    }
    bool loadPin = seated() &&
                   (target > .5f || cylinder > .5f || stage == 3 || stage == 4);
    // The pin (and optional legacy driver) remains supported on release.
    // The free driver has no attached pose and falls as soon as it is released.
    for (int k = 0; k < 2; k++) {
      auto &t = tools[k];
      if (t.owner < 0 || !t.inserted || (k == 1 && config.driverMode != DriverMode::SeatedLever))
        continue;
      Pose hand = handFor(k, t.owner);
      if (k == 0 && loadPin != pinLoaded) {
        t.seatPose = t.pose;
        t.seatHand = hand.p;
        t.seatDepth = t.pinDepth;
        t.entryAxis = normal(rotate(t.pose.q, pinHinge(t.opening, t.bendYaw)));
      }
      Vec offset = hand.p - turnWithCylinder({{}, mouth(k, targetBounds.style)}, cylinder).p;
      float requestedDepth =
          t.seatDepth + dot(t.seatHand - hand.p, t.entryAxis);
      bool pulled =
          k == 0 ? (requestedDepth < -.015f ||
                    dot(hand.p - t.seatHand, t.entryAxis) >
                        config.withdrawTravel ||
                    std::hypot(hand.p.x - t.pose.p.x, hand.p.y - t.pose.p.y) >
                        .20f)
                 : length(offset) > t.seatedRadius + config.withdrawTravel;
      if (pulled || offset.z < -.06f || length(offset) > .35f) {
        if (k == 0) {
          // Carry the actual supported orientation into the held pose.
          // Withdrawal is not a fresh pickup and must not select a new grip.
          t.heldRotation = unit(multiply(inverse(pinHands[t.owner].q), t.pose.q));
          hand = handFor(0, t.owner);
        }
        t.inserted = false;
        t.inserting = false;
        t.insertProgress = 0;
        t.pose = held(hand, k, t.opening, t.bendYaw);
        t.lastHand = hand.p;
        if (k == 0) {
          t.captureBlocked = true;
          t.lastRawTip = t.pose.p;
        }
        withdrawnEvents |= 1u << k;
      } else if (k == 1) {
        // The seated blade and handle move with the admitted native cylinder,
        // not through a resisted stop or by twisting around the tool shaft.
        t.pose = turnWithCylinder(t.seatPose, cylinder);
      }
      else if (!loadPin) {
        float depth = std::clamp(requestedDepth, 0.f, pinShaftLength*targetBounds.toolScale[0]);
        Pose desired = t.seatPose;
        desired.p = desired.p - t.entryAxis * (depth - t.seatDepth);
        Vec grip = world(desired, gripCenter(0, t.opening, t.bendYaw));
        Vec requested =
            world(t.seatPose, gripCenter(0, t.opening, t.bendYaw)) + hand.p -
            t.seatHand + t.entryAxis * (requestedDepth - depth);
        // Swivel about the actual entry point, first around the shaft, then
        // toward the hand. The housing, rather than a canonical pose, limits
        // the available tilt and depth. Regripping retains the supported pose.
        float roll = angleDelta(pinBearing(requested, t.pinContact),
                                pinBearing(grip, t.pinContact));
        Quat swivel = axisAngle(t.entryAxis, -roll * pi / 180);
        Vec rotatedGrip = rotate(swivel, grip - t.pinContact);
        Quat fit = shortestArc(rotatedGrip, requested - t.pinContact);
        Quat change = unit(multiply(fit, swivel));
        desired = {unit(multiply(change, desired.q)),
                   t.pinContact + rotate(change, desired.p - t.pinContact)};
        desired = limitPinSearch(desired, t.pinContact, cylinder, config.pinSearchDegrees, pick);
        Vec up = rotate(desired.q, {0, 1, 0});
        if (std::hypot(up.x, up.y) < .15f)
          desired = t.pose; // Avoid an undefined search bearing end-on.
        auto contact =
            moveAgainstHousing(ToolShape(0, t.opening, t.bendYaw, targetBounds),
                               t.pose, desired, cylinder);
        t.pose = contact.pose;
        t.blocked = contact.hit;
        if (contact.hit && !wasBlocked[0])
          housingHands |= 1u << t.owner;
        t.pinDepth = std::max(
            0.f, dot(t.pinContact - t.pose.p,
                     normal(rotate(t.pose.q, pinHinge(t.opening, t.bendYaw)))));
        t.insertProgress = std::clamp(t.pinDepth / .067f, 0.f, 1.f);
      }
    }
    pinLoaded = loadPin && pin.inserted;

    if (!seated() || !manipulating)
      target = 0;
    for (int k = 0; k < 2; k++) {
      auto &t = tools[k];
      if (!t.visible)
        continue;
      if (t.owner >= 0 && !t.inserted && !t.inserting &&
          !(k == 1 && config.driverMode != DriverMode::SeatedLever)) {
        ToolShape shape(k, t.opening, t.bendYaw, targetBounds);
        Pose desired = held(handFor(k, t.owner), k, t.opening, t.bendYaw);
        auto contact =
            moveAgainstHousing(shape, previous[k], desired, cylinder);
        t.pose = contact.pose;
        t.blocked = t.blocked || contact.hit;
        if (t.blocked && !wasBlocked[k])
          housingHands |= 1u << t.owner;
      }
      if (k == 0 && t.owner >= 0) {
        Pose hand = handFor(0, t.owner);
        Pose escape = hand;
        if (t.inserted || t.blocked) escape.q = t.pose.q;
        Pose desired = held(escape, 0, t.opening, t.bendYaw);
        Vec actualGrip = world(t.pose, gripCenter(0, t.opening, t.bendYaw));
        bool outward = hand.p.z > actualGrip.z + .035f ||
                       length(hand.p - t.pinContact) >
                           length(actualGrip - t.pinContact) + .080f;
        bool clearPull = outward && length(hand.p - actualGrip) > .055f &&
                         desired.p.z > mouth(0).z + .010f &&
                         ToolShape(0, t.opening, t.bendYaw, targetBounds)
                                 .clearance(desired, cylinder) > .001f;
        t.blockedTime = clearPull ? t.blockedTime + dt : 0;
        if (t.blockedTime > .12f) {
          if (t.inserted)
            withdrawnEvents |= 1;
          t.inserted = t.inserting = t.blocked = false;
          t.captureBlocked = true;
          t.pinDepth = t.seatDepth = t.insertProgress = t.blockedTime = 0;
          t.heldRotation = unit(multiply(inverse(pinHands[t.owner].q), desired.q));
          t.pose = desired;
          t.lastHand = hand.p;
          t.lastRawTip = desired.p;
          t.velocity = t.angular = {};
          recovered |= 1;
          target = 0;
          pinLoaded = false;
        }
      }
      if (t.owner < 0 && !t.inserted && !t.holstered)
        looseStep(k, lock, dt);
      else if (t.owner >= 0 && dt > 1e-5f && !(acquired & (1u << k))) {
        Vec velocity =
            bounded((world(t.pose, toolCenter(k, t.opening, t.bendYaw)) -
                     world(previous[k], toolCenter(k, t.opening, t.bendYaw))) *
                        (1 / dt),
                    2.f);
        t.velocity = t.velocity * .4f + velocity * .6f;
        t.angular = angularVelocity(previous[k].q, t.pose.q, dt);
      }
    }
    if (pin.inserted && driver.inserted) {
      if (config.driverMode != DriverMode::SeatedLever) {
        // Solid tool contact must not silently detach either tool or erase
        // tension. Draw both hands from the final collision-resolved tools.
        auto contact = sweepToolContact(ToolShape(0,pin.opening,pin.bendYaw,targetBounds),
                                        pinBeforeDriver,pin.pose,driver.pose,driver.pose);
        pin.pose = contact.pin;
        if (contact.hit) {
          for (int k=0; k<2; ++k) {
            tools[k].blocked = true;
            if (!wasBlocked[k] && tools[k].owner >= 0)
              housingHands |= 1u << tools[k].owner;
          }
        }
      } else {
        auto contact = sweepToolContact(ToolShape(0,pin.opening,pin.bendYaw,targetBounds),
                                        previous[0],pin.pose,previous[1],driver.pose);
        pin.pose=contact.pin;driver.pose=contact.driver;
        if(contact.hit) {
          for(int k=0;k<2;k++) {
            tools[k].blocked=true;
            if(!wasBlocked[k]&&tools[k].owner>=0) housingHands|=1u<<tools[k].owner;
            if(insertedEvents&(1u<<k)) {
              tools[k].inserted=false;insertedEvents&=~(1u<<k);target=0;
            }
          }
        }
      }
    }
    // The search input follows the final visible pose after both housing and
    // tool contact. Native difficulty remains mapped monotonically across it.
    if (pin.inserted)
      pick=boundPinSearch(pinPoseAngle(pin.pose,cylinder),config.pinSearchDegrees,pick);
    for (int h = 0; h < 2; h++) {
      Segment hand{hands[h].p, pinHands[h].p};
      bool empty = tools[0].owner != h && tools[1].owner != h && bendHand != h;
      if (config.useHolders && manipulating && handsInitialized && empty &&
          box.rummage(previousHands[h], hand, dt, config))
        rummagedHands |= 1u << h;
      previousHands[h] = hand;
    }
    handsInitialized = true;
    if (config.useHolders && tools[0].holstered && box.count) {
      tools[0].pose = box.pins[0].pose;
      tools[0].opening = box.pins[0].opening;
      tools[0].bendYaw = box.pins[0].twist;
    }
    // One animation sample per frame is shared by both rendered eyes. It is
    // never fed back to native pick angle or the interaction/collision poses.
    bool resisting = seated() && stage == 4 && driver.owner >= 0 &&
                     target > .5f && target >= cylinder - .5f;
    pinResisting = resisting;
    float pressure = std::clamp((target - cylinder) / 20, 0.f, 1.f);
    strain += ((resisting ? .8f + 1.6f * pressure : 0) - strain) *
              std::min(1.f, dt * 30);
    if (!resisting && strain < .01f)
      strain = 0;
    tremorTime = std::fmod(tremorTime + dt, 10.f);
    pinTremor = strain * (std::sin(tremorTime * 2 * pi * 17) +
                          .3f * std::sin(tremorTime * 2 * pi * 29));
    if (!pin.inserted || !manipulating) {
      strain = 0;
      pinTremor = 0;
    }
    syncOwners();
  }
};
} // namespace locks
