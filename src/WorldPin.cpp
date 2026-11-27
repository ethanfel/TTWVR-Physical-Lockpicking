// SPDX-License-Identifier: GPL-3.0-only
#include "WorldPin.h"
#include "ArmContact.h"
#include "PhysicalKey.h"
#include "ToolGeometry.h"
#include "WorldLock.h"
#include <chrono>
#include <cstdio>
#include <cstring>

namespace locks {
namespace {
template <class T> T *field(uintptr_t node, unsigned offset) {
  auto *p = reinterpret_cast<T *>(node + offset);
  return node && Readable(p, sizeof(T)) ? p : nullptr;
}
enum class Grip { None = -1, Pin, Driver, Key };
constexpr int jointCount = 15;
struct HandRig {
  uintptr_t hand = 0, bones[jointCount]{};
  Mat saved[jointCount]{};
  Grip pose = Grip::None;
  uintptr_t upper=0, fore=0, sleeve=0, tool=0;
  Vec contactOffset{}, keyPinch{}, keyAxis{};
  bool shifted=false, keyReach=false;
  Vec shoulderRaw{}, shoulderShown{};
  Transform goal, toolFit, presentedFore;
  Vec presentedWrist;
  Mat raw[3], shown[3];
  Transform shownTool;

};
HandRig rigs[2];
Mat fitted[2][3][jointCount];
int fittedCount[3]{};
int driverOwner = -1;
bool loaded = false, tried = false;
int owner = -1, helper = -1;
float opening = 0, twist = 0;
Vec initialHelper, initialTip;
uintptr_t pinRoot = 0, parts[3]{};
float meshOpening = -1, meshTwist = 0;
constexpr float units = 70.f;
using RestoreHand = void(__cdecl *)(uintptr_t, const char *, void *, int);
RestoreHand restoreHand = nullptr;
Transform renderedPin;
bool pinPoseReady = false, regripped = false;
Transform regripFit, controllerFit;
bool controllerGrip = false;
DWORD gameThread = 0;

// During collision the controller may travel past a solid surface. Reach with
// the whole arm. Preserve the same admitted rigid tool in both animation passes.
void restoreArm(HandRig &r) {
  if (!r.shifted) return;
  uintptr_t nodes[]{r.upper,r.fore,r.hand};
  for (int i=0;i<3;++i)
    if (auto *rotation=field<Mat>(nodes[i],0x34))
      if (std::memcmp(rotation,&r.shown[i],sizeof(Mat))==0) *rotation=r.raw[i];
  if (r.keyReach)
    if (auto *upper=field<Transform>(r.upper,0x34))
      if (std::memcmp(&upper->position,&r.shoulderShown,sizeof(Vec))==0) upper->position=r.shoulderRaw;
  if (auto *tool=field<Transform>(r.tool,0x34))
    if (std::memcmp(tool,&r.shownTool,sizeof(Transform))==0) *tool=r.toolFit;
}
void removeContact(HandRig &r) {
  restoreArm(r);
  r.shifted=false;
  r.keyReach=false;
  r.contactOffset={};
  r.tool=0;
}
void applyContact(HandRig &r) {
  auto *base=field<uintptr_t>(r.upper,0x18);
  auto *parent=base ? field<Transform>(*base,0x68) : nullptr;
  auto *upper=field<Transform>(r.upper,0x34), *fore=field<Transform>(r.fore,0x34);
  auto *hand=field<Transform>(r.hand,0x34);
  if (!parent || !upper || !fore || !hand) return;
  restoreArm(r);
  Transform reached;
  r.raw[0]=upper->rotation; r.raw[1]=fore->rotation; r.raw[2]=hand->rotation;
  r.shoulderRaw=upper->position;
  if (r.keyReach && parent->scale>.0001f) {
    const Vec toWrist=parent->rotation.transpose().apply(r.goal.position-parent->position)*(1.f/parent->scale);
    const float arm=(length(fore->position)+length(hand->position)*fore->scale)*upper->scale;
    upper->position=reachJoint(upper->position,toWrist,arm);
  }
  r.shoulderShown=upper->position;
  if (!contactArm(*parent,*upper,*fore,*hand,r.goal,reached)) {
    upper->position=r.shoulderRaw;
    return;
  }
  r.presentedWrist=reached.position;
  r.presentedFore=childWorld(childWorld(*parent,*upper),*fore);
  r.shown[0]=upper->rotation; r.shown[1]=fore->rotation; r.shown[2]=hand->rotation;
  if (auto *local=field<Transform>(r.tool,0x34)) {
    // At the reach limit leave the blade against the surface. Re-express the
    // same world transform in the reachable wrist without changing its scale.
    r.shownTool=r.toolFit;
    if (reached.scale>.0001f)
      r.shownTool.position=r.toolFit.position+
          reached.rotation.transpose().apply(r.goal.position-reached.position)*(1.f/reached.scale);
    *local=r.shownTool;
  }
  r.shifted=true;
}
void setContact(int h,Vec offset,Mat turn,uintptr_t tool,bool keyReach=false) {
  auto &r=rigs[h];
  auto *w=field<Transform>(r.hand,0x68);
  static const Mat identity;
  if (!w || !r.upper || (length(offset)<.0001f &&
      std::memcmp(&turn,&identity,sizeof(Mat))==0)) return;
  r.keyReach=keyReach;
  r.contactOffset=offset;
  r.goal={mul(turn,w->rotation),w->position+offset,w->scale};
  r.tool=tool;
  if (auto *local=field<Transform>(tool,0x34)) r.toolFit=*local;
  applyContact(r);
  if (r.shifted) UpdateSceneNode(r.upper);
}

// The sleeve is a sibling of the wrist. TTWVR caches its world pose
// independently, after the v73 contact pose has moved the arm.
using CommitBone = void(__cdecl *)(uintptr_t,const Vec *,const Mat *,float,unsigned);
CommitBone commitBone = nullptr;
void __cdecl commitToolSleeve(uintptr_t node,const Vec *position,const Mat *rotation,
                              float scale,unsigned flags) {
  if (node && rotation && GetCurrentThreadId()==gameThread) {
    for (const auto &r : rigs) {
      if (r.sleeve!=node || r.pose==Grip::None) continue;
      const auto *fore=field<Transform>(r.fore,0x68);
      const auto *handLocal=field<Transform>(r.hand,0x34);
      if (!fore || !handLocal) break;
      // The host is still writing the raw arm here. Contact already solved
      // its visible endpoints, including the nearest physically reachable wrist.
      Vec elbow=fore->position;
      Vec toWrist=fore->rotation.apply(handLocal->position)*fore->scale;
      Mat roll=*rotation;
      if (r.shifted) {
        elbow=r.presentedFore.position;
        toWrist=r.presentedWrist-elbow;
        roll=weaponSleeve(r.presentedFore.rotation,r.goal.rotation,*rotation);
      }
      const Mat sleeve=fitSleeveToWrist(roll,toWrist);
      commitBone(node,&elbow,&sleeve,scale,flags);
      return;
    }
  }
  commitBone(node,position,rotation,scale,flags);
}

// TTWVR restores a snapshot of the hand tree in its animation pass. Add the
// live pinch to that pose before the engine propagates it to the skinned mesh.
void __cdecl restoreWithPin(uintptr_t hand, const char *name, void *snapshot,
                            int depth) {
  restoreHand(hand, name, snapshot, depth);
  if (GetCurrentThreadId() != gameThread)
    return;
  for (int h = 0; h < 2; ++h) {
    auto &r = rigs[h];
    if (r.hand != hand || r.pose == Grip::None)
      continue;
    const int pose = static_cast<int>(r.pose);
    for (int j = 0; j < fittedCount[pose]; ++j)
      if (auto *rotation = field<Mat>(r.bones[j], 0x34))
        *rotation = fitted[h][pose][j];
    if (h == owner && pinPoseReady && pinRoot) {
      const auto *parent = field<uintptr_t>(pinRoot, 0x18);
      if (parent && *parent == hand)
        if (auto *t = field<Transform>(pinRoot, 0x34))
          *t = renderedPin;
    }
    if (r.shifted) {
      applyContact(r);
      // This callback already runs inside TTWVR's skeleton traversal. Publish
      // the two parent frames directly; the caller is about to skin the hand.
      // Starting another UpperArm traversal here re-enters this callback.
      auto *base=field<uintptr_t>(r.upper,0x18);
      auto *clavicle=base ? field<Transform>(*base,0x68) : nullptr;
      auto *u=field<Transform>(r.upper,0x68), *f=field<Transform>(r.fore,0x68);
      if (clavicle && u && f) {
        *u=childWorld(*clavicle,*field<Transform>(r.upper,0x34));
        *f=childWorld(*u,*field<Transform>(r.fore,0x34));
      }
    }
  }
}

void loadFit() {
  if (tried)
    return;
  tried = true;
  auto f = _wfopen(
      (folder + L"\\TTWVRPhysicalLockpicking\\kit-pinch.bin").c_str(), L"rb");
  if (!f) {
    Log("world_pin_grip_asset missing=1");
    return;
  }
  char magic[8];
  if (fread(magic, 1, 8, f) == 8) {
    if (!memcmp(magic, "TTWGrip2", 8)) {
      loaded = true;
      for (int h=0; h<2; ++h)
        loaded &= fread(fitted[h], sizeof(Mat), 2*jointCount, f) == 2*jointCount;
      char tail[8];
      if (loaded && fread(tail,1,8,f)==8 && !memcmp(tail,"TTWKeyG1",8)) {
        bool complete=true;
        for (int h=0; h<2; ++h)
          complete &= fread(fitted[h][2],sizeof(Mat),jointCount,f)==jointCount;
        if (complete) fittedCount[2]=jointCount;
      }
      fittedCount[0] = fittedCount[1] = jointCount;
    } else if (!memcmp(magic, "TTWGrip1", 8)) {
      loaded = true;
      for (int h = 0; h < 2; ++h)
        loaded &= fread(fitted[h][0], sizeof(Mat), 6, f) == 6;
      fittedCount[0] = 6;
    }
  }
  fclose(f);
  Log("world_pin_grip_asset loaded=%d", loaded);
}
void bindRig(int h, uintptr_t hand) {
  auto &r = rigs[h];
  if (hand == r.hand)
    return;
  r = {};
  r.hand = hand;
  if (!hand)
    return;
  auto parent=[](uintptr_t n){auto *p=field<uintptr_t>(n,0x18); return p ? *p : uintptr_t{};};
  auto named=[](uintptr_t n,const char *name){
    auto *p=field<const char *>(n,8);
    return p && *p && Readable(*p,std::strlen(name)+1) && !std::strcmp(*p,name);
  };
  r.fore=parent(hand);
  r.upper=parent(r.fore);
  if (!named(r.fore,h ? "Bip01 R Forearm" : "Bip01 L Forearm") ||
      !named(r.upper,h ? "Bip01 R UpperArm" : "Bip01 L UpperArm")) r.fore=r.upper=0;
  const char *digits[]{"Thumb1", "Finger1", "Finger2", "Finger3", "Finger4"};
  const char *ends[]{"", "1", "2"};
  const std::string prefix = h ? "Bip01 R " : "Bip01 L ";
  if (r.fore) {
    r.sleeve=FindSceneNode(r.fore,prefix+"ForeTwist");
    if (parent(r.sleeve)!=r.fore) r.sleeve=0;
  }
  for (int j = 0; j < jointCount; ++j)
    r.bones[j] = FindSceneNode(hand, prefix + digits[j / 3] + ends[j % 3]);
  if (!fittedCount[2]) return;
  Vec contact[2];
  for (int digit=0; digit<2; ++digit) {
    Transform endpoint;
    for (int i=0; i<3; ++i) {
      const int j=3*digit+i;
      const auto *joint=field<Transform>(r.bones[j],0x34);
      if (!joint) return;
      auto local=*joint;
      local.rotation=fitted[h][2][j];
      endpoint=childWorld(endpoint,local);
    }
    // Thumb pulp and the opposing side of the curled index, in bone units.
    Vec surface=digit ? Vec{1.13f,-.60f,h ? .18f : -.18f} : Vec{1.45f,.57f,0};
    contact[digit]=endpoint.position+endpoint.rotation.apply(surface)*endpoint.scale;
  }
  r.keyPinch=(contact[0]+contact[1])*.5f;
  r.keyAxis=normal(contact[0]-contact[1]);
}
void poseRig(int h, Grip wanted) {
  auto &r = rigs[h];
  if (!r.hand || !loaded || (r.pose == Grip::None && wanted == Grip::None))
    return;
  const int old = static_cast<int>(r.pose), next = static_cast<int>(wanted);
  for (int j = 0; j < jointCount; ++j) {
    auto *rotation = field<Mat>(r.bones[j], 0x34);
    if (!rotation)
      continue;
    bool ours = r.pose != Grip::None && j < fittedCount[old] &&
                std::memcmp(rotation, &fitted[h][old][j], sizeof(Mat)) == 0;
    if (wanted != Grip::None && j < fittedCount[next]) {
      if (!ours)
        r.saved[j] = *rotation;
      *rotation = fitted[h][next][j];
    } else if (ours)
      *rotation = r.saved[j];
  }
  r.pose = wanted;
  UpdateSceneNode(r.hand);
}
bool pads(int h, Vec &point) {
  auto *index = field<Transform>(rigs[h].bones[5], 0x68);
  auto *thumb = field<Transform>(rigs[h].bones[2], 0x68);
  if (!index || !thumb)
    return false;
  Vec a = index->position + index->rotation.apply({1.25f * index->scale, 0, 0});
  Vec b = thumb->position + thumb->rotation.apply({1.1f * thumb->scale, 0, 0});
  point = (a + b) * .5f;
  return finite(point);
}
bool bindPin(int h) {
  auto &r = rigs[h];
  auto parent = field<uintptr_t>(pinRoot, 0x18);
  if (pinRoot && parent && *parent == r.hand && parts[0] && parts[1] && parts[2])
    return true;
  pinRoot = FindSceneNode(r.hand, h ? "TTWVRKit_1_0" : "TTWVRKit_0_0");
  meshOpening = -1;
  if (!pinRoot)
    return false;
  for (int k = 0; k < 3; ++k)
    parts[k] = FindSceneNode(pinRoot, "KitPart" + std::to_string(3 + k));
  return parts[0] && parts[1] && parts[2];
}
void updateWire() {
  if (meshOpening == opening && meshTwist == twist)
    return;
  auto *shaft = field<Transform>(parts[0], 0x34);
  auto *leg = field<Transform>(parts[1], 0x34);
  auto *geometry = field<uintptr_t>(parts[2], 0xb8);
  if (!shaft || !leg || !geometry)
    return;
  auto data = *geometry;
  auto *count = field<uint16_t>(data, 8);
  auto *pp = field<Vec *>(data, 0x20), *nn = field<Vec *>(data, 0x24);
  if (!count || *count != 72 || !pp || !nn ||
      !Readable(*pp, 72 * sizeof(Vec)) || !Readable(*nn, 72 * sizeof(Vec)))
    return;
  Vec hinge = pinHinge(opening, twist);
  shaft->rotation = matrix(shortestArc({0, 0, 1}, hinge));
  leg->position =
      (hinge + Vec{.006f * std::max(0.f, 1 - opening), 0, 0}) * units;
  Vec axis = rotate(axisAngle({0, 1, 0}, twist), {1, 0, 0});
  for (int i = 0; i < 9; ++i) {
    float t = i / 8.f;
    Vec p = pinBendPoint(opening, t, twist);
    Vec tangent = normal(pinBendPoint(opening, std::min(1.f, t + .01f), twist) -
                         pinBendPoint(opening, std::max(0.f, t - .01f), twist));
    Vec side = normal(cross(tangent, axis));
    if (length(side) < .5f)
      side = normal(cross(tangent, {0, 1, 0}));
    Vec second = cross(tangent, side);
    for (int j = 0; j < 8; ++j) {
      float a = pi * j / 4;
      Vec n = side * std::cos(a) + second * std::sin(a);
      (*pp)[i * 8 + j] = (p + n * pinWireRadius) * units;
      (*nn)[i * 8 + j] = n;
    }
  }
  *field<Vec>(data, 0x10) = hinge * units;
  *field<float>(data, 0x1c) = .02f * units;
  *field<uint16_t>(data, 0xe) |= 0xfff;
  meshOpening = opening;
  meshTwist = twist;
}
} // namespace
bool InstallWorldPinPose(uintptr_t site, uintptr_t target, uintptr_t sleeveSite,
                         uintptr_t sleeveTarget) {
  restoreHand = reinterpret_cast<RestoreHand>(target);
  commitBone = reinterpret_cast<CommitBone>(sleeveTarget);
  const NativeCall hooks[]{{site,target,reinterpret_cast<void *>(restoreWithPin)},
                           {sleeveSite,sleeveTarget,reinterpret_cast<void *>(commitToolSleeve)}};
  gameThread = GetCurrentThreadId();
  return InstallVerifiedCalls(hooks, 2);
}
bool ParkWorldPin(int hand) {
  return hand == owner && pinPoseReady && ParkLivePin(pinRoot);
}
bool NearParkedPin(int hand, float unitsPerMeter) {
  Transform wire;
  Vec fingertips;
  if (!ParkedPinPose(wire) || !pads(hand, fingertips)) return false;
  Vec grip = wire.position +
      wire.rotation.apply(presentedPinContact(opening, twist) * (units * wire.scale));
  return length(fingertips - grip) <= .065f * unitsPerMeter;
}
bool RegrabWorldPin(int hand) {
  Transform wire;
  auto *wrist = field<Transform>(rigs[hand].hand, 0x68);
  if (!wrist || !(wrist->scale > 0) || !ParkedPinPose(wire) || !bindPin(hand))
    return false;
  TakeLivePin();
  owner = hand;
  helper = -1;
  regripFit = relativeTransform(wire, *wrist);
  Transform controller;
  controllerGrip = KitControllerWorld(hand, controller);
  if (controllerGrip) {
    // Keep the point on the exposed wire that the fingers actually hold.
    wire.position = wire.position + wire.rotation.apply(
        presentedPinContact(opening, twist) * (units * wire.scale));
    controllerFit = relativeTransform(wire, controller);
  }
  regripped = true;
  meshOpening = -1;
  Log("live_pin_regrip hand=%d", hand);
  return true;
}
void BeforeWorldToolRemoval(int h, int tool) {
  if (tool==1) RestoreWorldDriver();
  if (h >= 0 && h < 2 && rigs[h].shifted) {
    removeContact(rigs[h]);
    UpdateSceneNode(rigs[h].upper);
  }
  if (tool == 0 && h == owner) {
    regripped = false;
    pinPoseReady = false;
    pinRoot = 0;
  }
}
void ResetWorldPin() {
  RestoreWorldDriver();
  ResetWorldLock(true);
  regripped = false;
  pinPoseReady = false;
  for (int h = 0; h < 2; ++h)
    rigs[h] = {}; // a save load can replace the entire avatar
  owner = helper = driverOwner = -1;
  pinRoot = 0;
  opening = twist = 0;
  meshOpening = -1;
}
// The fitted contact is wrist-local. Read the animated arm before posing it,
// so the rendered correction can never feed back into controller tracking.
static Mat alignKeyFaces(const HandRig &r,const Transform &wrist,const Mat &key) {
  const Vec axis=wrist.rotation.apply(r.keyAxis);
  Vec face=key.apply({1,0,0});
  if (dot(axis,face)<0) face=face*-1.f;
  return matrix(shortestArc(axis,face));
}
bool KeyFingerContact(int h, Vec &world) {
  const auto &r=rigs[h];
  const auto *w=field<Transform>(r.hand,0x68);
  if (!w || length(r.keyAxis)<.5f) return pads(h,world);
  world=w->position+w->rotation.apply(r.keyPinch)*w->scale;
  return finite(world);
}
static bool keyArmReach(const HandRig &r,Vec &origin,float &reach) {
  const auto *base=field<uintptr_t>(r.upper,0x18);
  const auto *collar=base ? field<Transform>(*base,0x68) : nullptr;
  const auto *shoulder=field<Transform>(r.upper,0x68), *elbow=field<Transform>(r.fore,0x68);
  const auto *w=field<Transform>(r.hand,0x68);
  if (!collar || !shoulder || !elbow || !w) return false;
  origin=collar->position;
  reach=length(shoulder->position-origin)+length(elbow->position-shoulder->position)
      +length(w->position-elbow->position);
  return reach>.01f;
}
void LimitKeyReach(int h,Transform &grip) {
  const auto &r=rigs[h];
  const auto *w=field<Transform>(r.hand,0x68);
  Vec origin; float reach;
  if (!w || length(r.keyAxis)<.5f || !keyArmReach(r,origin,reach)) return;
  grip.position=origin+bounded(grip.position-origin,reach+length(r.keyPinch)*w->scale);
}
void PresentKeyGrip(int h, const Transform &keyWorld) {
  auto &r=rigs[h];
  if (length(r.keyAxis)<.5f) return;
  if (r.pose!=Grip::Key) poseRig(h,Grip::Key);
  const auto *w=field<Transform>(r.hand,0x68);
  if (!w || !(keyWorld.scale>0)) return;
  const Vec bow=keyWorld.position+keyWorld.rotation.apply({0,0,PhysicalKey::bowZ})*keyWorld.scale;
  Mat turn=alignKeyFaces(r,*w,keyWorld.rotation);
  Vec wristToBow=turn.apply(w->rotation.apply(r.keyPinch)*w->scale);
  Vec origin; float reach;
  if (keyArmReach(r,origin,reach) && length(bow-origin-wristToBow)>reach) {
    // When turning a seated key at full extension the wrist can swivel
    // around the pinch. Keep the fingertips on the head throughout the turn.
    const Vec admitted=reachJoint(wristToBow,bow-origin,reach);
    turn=mul(matrix(shortestArc(wristToBow,admitted)),turn);
    wristToBow=admitted;
  }
  setContact(h,bow-(w->position+wristToBow),turn,0,true);
  static const bool trace=GetEnvironmentVariableW(L"TTWVR_LOCK_KIT_TRACE",nullptr,0)>0;
  static ULONGLONG report=0;
  if (trace && GetTickCount64()>=report) {
    Vec actual;
    if (KeyFingerContact(h,actual))
      Log("key_grip hand=%d center_error_mm=%.1f face_dot=%.3f",h,
          length(actual-bow)*1000.f/keyWorld.scale,
          std::abs(dot(w->rotation.apply(r.keyAxis),keyWorld.rotation.apply({1,0,0}))));
    report=GetTickCount64()+350;
  }
}
void WorldPinFrame(GameState &g, const uintptr_t hands[2], int pinHand,
                   unsigned down, unsigned pressed, unsigned valid,
                   unsigned nearKit, int driverHand, float &pinHealth) {
  if (!g.kitSlot && !HasPhysicalKeyTarget() && owner < 0 && helper < 0 && driverOwner < 0 &&
      rigs[0].pose == Grip::None && rigs[1].pose == Grip::None)
    return;
  if (pinHand < 0 && driverHand < 0)
    ResetWorldLock(false);
  driverOwner = driverHand;
  const bool driverDrawn = driverHand >= 0;
  using Clock = std::chrono::steady_clock;
  using Ms = std::chrono::duration<double, std::milli>;
  const auto frameStart = driverDrawn ? Clock::now() : Clock::time_point{};
  loadFit();
  for (int h = 0; h < 2; ++h) {
    bindRig(h, hands[h]);
  }
  const bool parked = HasParkedPin();
  if (owner != pinHand) {
    regripped = false;
    pinPoseReady = false;
    owner = pinHand;
    helper = -1;
    if (!parked) opening = twist = 0;
    pinRoot = 0;
    meshOpening = -1;
  }
  if (parked) helper = -1;
  if (helper >= 0 && (!(down & valid & (1u << helper)) || driverDrawn))
    helper = -1;
  // Sample the animated wrist before applying this frame's common solver result.
  // TTWVR can restore either component independently between game updates.
  for (int h=0;h<2;++h) {
    if (rigs[h].shifted) {
      removeContact(rigs[h]);
      UpdateSceneNode(rigs[h].upper);
    }
    poseRig(h, h == PhysicalKeyHand() && length(rigs[h].keyAxis)>.5f ? Grip::Key
               : (!parked && h == owner) || h == helper || h == PhysicalKeyHand() ? Grip::Pin
               : h == driverHand         ? Grip::Driver
                                         : Grip::None);
    if (parked && h != driverHand && NearParkedPin(h, g.worldUnitsPerMeter))
      g.kitRegrabNear |= 1u << h;
    Vec fingerContact;
    if (pads(h, fingerContact) &&
        WorldToTracking(fingerContact, g.kitFingerPads[h]))
      g.kitFingerPadsValid |= 1u << h;
    auto *w = field<Transform>(hands[h], 0x68);
    if (w && WorldToTracking(
                 w->position +
                     w->rotation.apply({5.5f * w->scale, 1.4f * w->scale, 0}),
                 g.kitPalms[h]))
      g.kitPalmsValid |= 1u << h;
  }
  Vec gripShift{},expectedTip{};
  auto resolve = [&](const LiveTool *wire) {
    auto *driver =
        driverDrawn && (valid & (1u << driverHand))
            ? field<Transform>(hands[driverHand], 0x68) : nullptr;
    MountedCorrection corrections[2]{};
    Transform trackedDriver;
    if (driver) {
      trackedDriver = *driver;
    }
    const auto started = driver ? Clock::now() : Clock::time_point{};
    g.worldPinBroken =
        WorldLockFrame(g, wire, driver ? &trackedDriver : nullptr,
                       owner >= 0 ? owner : 1 - driverHand, opening, twist, pinHealth, corrections);
    const auto solved = driver ? Clock::now() : Clock::time_point{};
    bool mounted = false;
    double meshMs = 0;
    for (int h=0;h<2;++h) {
      auto &c=corrections[h];
      Vec shift=h==owner && wire ? gripShift : Vec{};
      if (wire && h==owner) expectedTip=c.valid ? c.shownTip : wire->tip;
      auto *wrist=field<Transform>(hands[h],0x68);
      if (c.valid && wrist)
        shift=c.shownTip+c.turn.apply(wrist->position+shift-c.rawTip)-wrist->position;
      auto tool=h==owner && wire ? pinRoot : uintptr_t{};
      if (h==driverHand) {
        auto mount=c.valid ? WorldDriverMount() : uintptr_t{};
        const auto meshStart = mount ? Clock::now() : Clock::time_point{};
        if (mount && wrist)
          mounted = PresentWorldDriver(hands[h], h, mount,
              {mul(c.turn, wrist->rotation), wrist->position + shift, wrist->scale});
        else RestoreWorldDriver();
        if (mount) meshMs = Ms(Clock::now() - meshStart).count();
      }
      setContact(h,shift,c.turn,tool);
    }
    if (driver) {
      const double prepMs = Ms(started - frameStart).count();
      const double solverMs = Ms(solved - started).count();
      const double armsMs = Ms(Clock::now() - solved).count() - meshMs;
      static const bool trace = GetEnvironmentVariableW(L"TTWVR_LOCK_KIT_TRACE", nullptr, 0) > 0;
      static ULONGLONG nextReport = 0;
      if ((trace || prepMs + solverMs + meshMs + armsMs >= 15) && GetTickCount64() >= nextReport) {
        Log("live_driver_frame mounted=%d parked=%d prep_ms=%.3f lock_ms=%.3f mesh_ms=%.3f arms_ms=%.3f",
            mounted, parked, prepMs, solverMs, meshMs, armsMs);
        nextReport = GetTickCount64() + 1000;
      }
    }
  };
  g.kitOpening = opening;
  g.kitTwist = twist;
  if (parked || owner < 0 || !(valid & (1u << owner)) || !hands[owner] || !bindPin(owner)) {
    helper = -1;
    resolve(nullptr);
    return;
  }
  auto *w = field<Transform>(hands[owner], 0x68);
  auto *local = field<Transform>(pinRoot, 0x34);
  Vec contact;
  if (!w || !local || w->scale <= 0 || !pads(owner, contact)) {
    resolve(nullptr);
    return;
  }
  const float sign = owner ? -1.f : 1.f;
  Mat handBasis{{0, -1, 0, sign, 0, 0, 0, 0, sign}};
  // Express the scene's palm-back presentation in the tool frame so the
  // live wrist can continue to track the controller.
  local->rotation =
      mul(handBasis, matrix(multiply(axisAngle({1, 0, 0}, 20.f * pi / 180.f),
                                     pinGripOrientation(owner))));
  Transform controller;
  const bool trackedGrip = regripped && controllerGrip && KitControllerWorld(owner, controller);
  if (regripped) *local = regripFit;
  if (trackedGrip) {
    // TTWVR can move the animated wrist by several centimetres when the grip
    // closes. Keep the wire rigid to the measured controller through pickup.
    local->rotation = mul(w->rotation.transpose(), mul(controller.rotation, controllerFit.rotation));
    local->scale = controllerFit.scale / w->scale;
    Vec desiredGrip = controller.position + controller.rotation.apply(controllerFit.position);
    gripShift = desiredGrip - (contact);
  }
  auto wireContact = [&]() { return presentedPinContact(opening, twist); };
  const Mat worldRotation = mul(w->rotation, local->rotation);
  const float size = units * w->scale;
  auto helperPosition = [&](Vec p) {
    return worldRotation.transpose().apply(p -
                                           (contact)) *
           (1 / size);
  };
  int other = 1 - owner;
  Vec second;
  if ((valid & (1u << other)) && !driverDrawn && pads(other, second)) {
    Vec offset = helperPosition(second);
    float gap = length(offset + wireContact());
    bool atWire = gap < .060f;
    if (atWire && !(nearKit & (1u << other)))
      g.kitBendNear = 1u << other;
    if (helper < 0 && atWire && (pressed & g.kitBendNear)) {
      helper = other;
      poseRig(helper, Grip::Pin);
      pads(other, second);
      initialHelper = helperPosition(second);
      initialTip = pinHinge(opening, twist) * -1;
      Log("world_pin_bend owner=%d helper=%d contact_m=%.4f", owner, helper,
          gap);
    }
    if (helper == other) {
      bool wasUsable=pinUsable(opening);
      bendPin(initialTip, helperPosition(second) - initialHelper, opening,
              twist);
      if (!wasUsable && pinUsable(opening)) g.worldPrepared=(1u<<owner)|(1u<<helper);
      g.kitHands |= 1u << helper;
    }
  }
  if (!regripped || trackedGrip)
    local->position =
      w->rotation.transpose().apply(contact - w->position) * (1 / w->scale) -
      local->rotation.apply(wireContact() * (units * local->scale));
  if (trackedGrip) {
    regripFit = *local;
    regripFit.position = regripFit.position +
        w->rotation.transpose().apply(gripShift) * (1.f / w->scale);
  }
  renderedPin = *local;
  pinPoseReady = true;
  updateWire();
  UpdateSceneNode(pinRoot);
  const auto *shaft = field<Transform>(parts[0], 0x68);
  const auto *leg = field<Transform>(parts[1], 0x68);
  if (!shaft || !leg) {
    resolve(nullptr);
    return;
  }
  LiveTool wire{shaft->position + gripShift,
                shaft->rotation.apply({0, 0, 1}),
                contact + gripShift,
                leg->rotation.apply({0, 1, 0}), units * shaft->scale,
                leg->rotation.apply({1, 0, 0})};
  resolve(&wire);
  if (g.worldStage && (g.worldSurface[owner] || g.worldSeated)) {
    static ULONGLONG nextCheck = 0;
    auto now = GetTickCount64();
    if (now >= nextCheck) {
      auto *shown = field<Transform>(parts[0], 0x68);
      Vec actualPads;
      if (shown && pads(owner, actualPads)) {
        auto *handle=field<Transform>(pinRoot,0x68);
        if (!handle) return;
        Vec pinch=handle->position+handle->rotation.apply(presentedPinContact(opening,twist)*units*handle->scale);
        float mm = 1000.f / g.worldUnitsPerMeter;
        Log("live_pin_render hand=%d tip_error_mm=%.1f grip_error_mm=%.1f hand_shift_mm=%.1f",
            owner, length(shown->position - expectedTip) * mm,
            length(actualPads-pinch) * mm,
            length(rigs[owner].contactOffset) * mm);
      }
      nextCheck = now + 300;
    }
  }
  g.kitOpening = opening;
  g.kitTwist = twist;
  static int lastDegree = -1;
  int angle = int(opening * 90);
  if (std::abs(angle - lastDegree) >= 5 || (pressed && angle == 0)) {
    Log("world_pin_pose owner=%d helper=%d bend_deg=%d "
        "contact_world=%.3f,%.3f,%.3f",
        owner, helper, angle, contact.x, contact.y, contact.z);
    lastDegree = angle;
  }
}
} // namespace locks
