// SPDX-License-Identifier: GPL-3.0-only
#include "WorldLock.h"
#include "PhysicalKey.h"
#include "WorldPin.h"
#include "NativeScene.h"
#include "MountedInteraction.h"
#include "LockRules.h"
#include "Target.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>
namespace locks {
namespace {
using Ptr = uintptr_t;
template <class T> T read(Ptr p) {
  return Readable((void *)p, sizeof(T)) ? *reinterpret_cast<const T *>(p) : T{};
}
struct Element {
  union {
    double number;
    void *form;
  } data{};
  unsigned char type = 0;
};
struct Scripts {
  bool(__cdecl *call)(void *, void *, void *, Element *, uint8_t, ...);
  void *params, *extract, *format, *alt;
  void *(__cdecl *compile)(const char *);
};
Scripts *api = nullptr;
// Public xNVSE command table ABI. GetLocked's condition handler can be called
// directly on a reference, including during controller updates.
using Condition = bool(__cdecl *)(void *, void *, void *, double *);
struct ConditionCommand {
  const char *name, *alias;
  unsigned opcode;
  const char *help;
  uint16_t needsParent, paramCount;
  void *params, *execute, *parse;
  Condition eval;
};
struct CommandTable {
  unsigned version;
  void *start, *end, *byOpcode;
  const ConditionCommand *(__cdecl *byName)(const char *);
};
static_assert(offsetof(ConditionCommand, eval) == 0x20);
CommandTable *commands = nullptr;
// Prefix of the xNVSE array interface, through GetElements.
struct Arrays {
  void *create, *createStringMap, *createMap, *assign, *set, *append;
  unsigned (__cdecl *size)(void *);
  void *lookup, *get;
  bool (__cdecl *elements)(void *, Element *, Element *);
};
Arrays *arrays = nullptr;
void *nearbyQuery = nullptr;
void *action = nullptr, *attachments[4][targetKinds]{};
struct DriverFrame {
  Mat r;
  Vec tip;
};
struct Mount {
  unsigned style, reverse;
  float scale;
  Mat basis;
  Vec anchors[2];
};
static_assert(sizeof(Mount) == 72 && sizeof(DriverFrame) == 48);
Mount mounts[targetKinds]{};
MountSurfaceGrid panels[targetKinds][2];
bool panelsReady = false;
DriverFrame drivers[2]{};
bool ready = false;
struct Selection {
  Ptr ref = 0, root = 0, hardware = 0, cell = 0;
  unsigned id = 0, kind = 0, side = 0;
  bool attached = false;
  int level = 0;
  bool door = false;
  ULONGLONG mountDeadline = 0;
  Ptr playerCell = 0;
};
Selection active;
struct VisibleLock {
  Selection target;
  bool nearby = false;
};
std::map<unsigned, VisibleLock> visible;
// The visibility query already enumerates doors and containers around the
// player. Keep identities rather than scene nodes across game-thread ticks.
struct NearbyRef { Ptr ref; unsigned id; };
std::vector<NearbyRef> nearbyCellRefs;
Ptr nearbyCell = 0;
Vec nearbyOrigin{};
ULONGLONG nearbyUntil = 0, nextVisibility = 0;
constexpr float visibilityRadius = 900, toolOriginRadius = 360;
Ptr previousCloseup = 0;
MountedInteraction model;
LockRules rules;
LiveLockPuzzle puzzle;
LockTurn turn;
Ergonomics settings;
bool entered = false;
struct ControllerGrip {
  Transform fit;
  int owner=-1;
  LiveTool sample(const LiveTool &animated,int k,int h,float opening,float twist) {
    Transform controller;
    auto input=animated;
    auto rotation=matrix(mountedToolPose(animated,k).q);
    Vec pivot=k ? handleCenter(1) : presentedPinContact(opening,twist);
    Transform grip{rotation,animated.tip+rotation.apply(pivot)*animated.scale,animated.scale};
    if (!KitControllerWorld(h,controller)) return input;
    if (owner!=h) {
      fit=relativeTransform(grip,controller);
      owner=h;
    }
    auto r=mul(controller.rotation,fit.rotation);
    Vec gripPoint=controller.position+controller.rotation.apply(fit.position)*controller.scale;
    input.tip=gripPoint-r.apply(pivot)*animated.scale;
    input.grip=gripPoint;
    input.across=r.apply({1,0,0});
    input.up=r.apply({0,1,0});
    input.shaft=r.apply(k ? Vec{0,0,1} : normal(pinHinge(opening,twist)));
    return input;
  }
};
ControllerGrip anchors[2];
struct SoundHandle { unsigned key=0xffffffff, flags=0, state=0; };
SoundHandle tension;
Ptr address(Ptr va) { return Ptr(GetModuleHandleW(nullptr)) + (va-0x400000); }
void stopTension(unsigned ms) {
  if (tension.key==0xffffffff) return;
  reinterpret_cast<void(__thiscall *)(SoundHandle*,unsigned)>(address(0xad8da0))(&tension,ms);
  tension={};
}
bool playSound(const char *name, unsigned flags, SoundHandle &h, bool loop) {
  reinterpret_cast<void(__thiscall *)(Ptr,SoundHandle*,const char*,unsigned)>(
      address(0xae5680))(address(0x11f6ef0),&h,name,flags);
  if (h.key!=0xffffffff)
    reinterpret_cast<void(__thiscall *)(SoundHandle*,bool)>(address(0xad8830))(&h,loop);
  return h.key != 0xffffffff;
}
void sound(Ptr id,unsigned flags,bool loop=false) {
  auto name = reinterpret_cast<const char *>(address(id));
  if (!Readable(name,24)) return;
  SoundHandle shot;
  bool played = playSound(name,flags,loop ? tension : shot,loop);
  Log("live_lock_audio cue=%s handle_valid=%d",name,played);
}
void playEvents(unsigned events) {
  if (events&Turn) sound(0x10745c4,0x121);
  if (events&SqueakA) sound(0x10745a4,0x101);
  if (events&SqueakB) sound(0x1074584,0x101);
  if (events&Stop) sound(0x1074554,0x101);
  if (events&Tension) sound(0x1074538,1,true);
  if (events&ReleaseTension) stopTension(events&Break ? 100 : 500);
  if (events&Break) sound(0x107450c,0x101);
}
bool beginRules(Ptr pc,int level) {
  int rank=std::clamp(reinterpret_cast<int(__cdecl *)(int)>(address(0x430b40))(level),0,4);
  int skill=reinterpret_cast<int(__thiscall *)(Ptr,int)>(address(0x66ef20))(pc+0xa4,0x24);
  auto threshold=[&](int n){return read<int>(address(0x11da28c)+4*n);};
  float fraction=std::clamp(float(skill-threshold(rank)) /
                        std::max(1,2*(threshold(rank+1)-threshold(rank))),0.f,1.f);
  auto width=[&](Ptr va){ return read<int>(read<Ptr>(address(va)+4*rank)+4); };
  auto lerp=[&](Ptr a,Ptr b) {return int(width(a)+(width(b)-width(a))*fraction);};
  rules.pickPixels=lerp(0x11a0328,0x11a033c);
  rules.driverPixels=lerp(0x11a0350,0x11a0364);
  Ptr root=read<Ptr>(read<Ptr>(address(0x11d8a80))+0x9c);
  if (!root) return false;
  auto trait=reinterpret_cast<float(__thiscall *)(Ptr,unsigned)>(address(0xa011b0));
  rules.width=reinterpret_cast<float(__cdecl *)()>(address(0x715d40))()-4*trait(root,0xfc0);
  int margin=rules.driverPixels+rules.pickPixels/2;
  if (!(rules.width>2*margin && margin>0)) return false;
  const bool retry = puzzle.reference == active.id;
  rules.center = puzzle.centerFor(active.id, rules.width, margin, [] {
    Ptr rng = reinterpret_cast<Ptr(__cdecl *)()>(address(0x476c00))();
    return reinterpret_cast<float(__thiscall *)(Ptr)>(address(0x4dff60))(rng);
  });
  rules.breakSeconds=read<float>(address(0x11d41c8)+4);
  rules.healthBase=std::max(.01f,1+skill*read<float>(address(0x11d0de0)+4));
  Log("live_lock_rules rank=%d skill=%d track=%.1f pick_width=%d driver_width=%d break_s=%.3f health=%.3f ref=%08x reused=%d",
      rank,skill,rules.width,rules.pickPixels,rules.driverPixels,rules.breakSeconds,rules.healthBase,active.id,retry);
  return true;
}
Ptr parkedMesh = 0;
Transform parkedLocal;
float parkedAngle = 0;
bool parkedShaking = false;
void discardParkedPin() {
  ReleaseMountedChild(parkedMesh);
  parkedMesh = 0;
  parkedShaking=false;
}
Ptr finishing = 0;
unsigned finishingID = 0;
bool submitted = false, forceFinish = false;
// The stock menu resolves ownership again during the success transaction.
using OwnerOf = Ptr(__thiscall *)(Ptr);
OwnerOf ownerOf = nullptr;
using IsCrime = bool(__thiscall *)(Ptr);
IsCrime isCrime = nullptr;
using MarkCrime = void(__thiscall *)(Ptr, bool);
using ReportCrime = void(__thiscall *)(Ptr, Ptr, Ptr, unsigned, unsigned, Ptr);
MarkCrime markCrime = nullptr;
ReportCrime reportCrime = nullptr;
void *findWitness = nullptr;
unsigned reportedRef = 0, reportedOwner = 0, finishingOwner = 0;
ULONGLONG nextWitnessCheck = 0;

ULONGLONG deadline = 0, nextScan = 0, nextRankCheck = 0, lastFrame = 0;

Ptr player() { return read<Ptr>(Ptr(GetModuleHandleW(nullptr)) + 0xddea3c); }
Ptr rootOf(Ptr ref) { return read<Ptr>(read<Ptr>(ref + 0x64) + 0x14); }
bool current(const Selection &s) {
  return s.ref && read<unsigned>(s.ref + 0xc) == s.id &&
         read<Ptr>(s.ref + 0x40) == s.cell && rootOf(s.ref) == s.root;
}
Ptr __fastcall ownerOnSuccess(Ptr ref, void *) {
  Ptr owner = ownerOf(ref);
  if (submitted && finishing == ref && finishingOwner && owner &&
      read<unsigned>(ref + 0xc) == finishingID &&
      read<unsigned>(owner + 0xc) == finishingOwner) {
    Log("live_crime_already_reported ref=%08x owner=%08x", finishingID, finishingOwner);
    return 0;
  }
  return owner;
}
void checkWitness(ULONGLONG now, Ptr pc) {
  const bool forcing = model.physics.forceEvent != ForceGesture::None;
  if (!findWitness || active.level < 0 || (!model.pin && !forcing) || (!forcing && now < nextWitnessCheck))
    return;
  nextWitnessCheck = now + 400;
  Ptr owner = ownerOf(active.ref);
  if (!owner) return;
  unsigned id = read<unsigned>(owner + 0xc);
  if (!id || (reportedRef == active.id && reportedOwner == id) || !isCrime(active.ref))
    return;
  Element result;
  if (!api->call(findWitness, nullptr, nullptr, &result, 1, owner) ||
      result.type != 2 || !result.data.form) return;
  Ptr witness = Ptr(result.data.form);
  reportedRef = active.id;
  reportedOwner = id;
  Log("live_crime_witness ref=%08x owner=%08x actor=%08x", active.id, id,
      read<unsigned>(witness + 0xc));
  markCrime(pc, true);
  reportCrime(pc, witness, 0, 0, 0, owner);
}
void ensureWorldAction() {
  if (!action && api && api->compile)
    action = api->compile("ref target\nint mode\nbegin Function {target "
                          "mode}\nSetFunctionValue (Call (CompileScript "
                          "\"TTWVRLockKit/WorldAction.txt\") target mode)\nend");
}
int call(void *code, Ptr ref, unsigned mode) {
  Element out;
  if (!code || !api->call(code, nullptr, nullptr, &out, 2, ref, mode) ||
      out.type != 1 || !std::isfinite(out.data.number))
    return -1;
  return int(out.data.number);
}
int attach(const Selection &s, unsigned flags) {
  auto &code = attachments[flags][s.kind - 1];
  if (!code) {
    char txt[256];
    snprintf(txt, sizeof(txt),
             "ref lockRef\nint taskNumber\nint resultCode\nbegin Function "
             "{lockRef taskNumber}\n"
             "let resultCode := AttachModel lockRef %d "
             "\"TTWVRPhysicalLockpicking\\world-lock-%u."
             "nif\"\nSetFunctionValue resultCode\nend",
             int(flags), s.kind);
    code = api->compile(txt);
  }
  return call(code, s.ref, 0);
}
Mat basis(const Selection &s) {
  Mat b = mounts[s.kind - 1].basis;
  if (s.side) {
    for (int j = 0; j < 3; ++j) {
      b.v[3 * j] *= -1;
      b.v[3 * j + 2] *= -1;
    }
  }
  return b;
}
struct MountFrame {
  Mat r;
  Vec origin;
  float factor;
  Vec point(Vec p) const {
    return r.transpose().apply(p - origin) * factor + Vec{0, 0, -.013f};
  }
  LiveTool tool(const LiveTool &t) const {
    auto turn = r.transpose();
    return {point(t.tip),     turn.apply(t.shaft), point(t.grip),
            turn.apply(t.up), t.scale * factor,    turn.apply(t.across)};
  }
};
MountFrame frame(const Selection &s) {
  const auto &m = mounts[s.kind - 1];
  auto w = read<Transform>((s.hardware ? s.hardware : s.root) + 0x68);
  return {mul(w.rotation, basis(s)),
          w.position + w.rotation.apply(m.anchors[s.side]) * w.scale,
          m.scale / std::max(.0001f, w.scale)};
}
int twistDirection(unsigned kind, unsigned side) {
  const auto &m = mounts[kind-1];
  static int directions[targetKinds][2]{};
  auto &sign = directions[kind-1];
  if (!sign[0]) {
    sign[0] = sign[1] = 1;
    auto path = folder + L"\\TTWVRPhysicalLockpicking\\target-" + std::to_wstring(kind) + L".bin";
    if (auto *f = _wfopen(path.c_str(), L"rb")) {
      TargetAsset target;
      uint32_t parts, frames;
      if (readTargetHeader(f, kind, target, parts, frames)) {
        float reverseX = m.basis.transpose().apply(m.anchors[1] - m.anchors[0]).x * m.scale;
        sign[0] = doorTwistDirection(target.bounds, false, 0);
        sign[1] = doorTwistDirection(target.bounds, true, reverseX);
      }
      fclose(f);
    }
  }
  return sign[side];
}
void rotateHardware(const Selection &selection, const Mat &r, Vec pivot) {
  unsigned first = mounts[selection.kind-1].style == 1 ? 3 : 1;
  for (unsigned n = first; n < first + 2; ++n) {
    auto node = FindSceneNode(
        selection.hardware, "TTWVRLockHardware_" +
                             std::string(selection.side ? "reverse_" : "front_") +
                             std::to_string(n));
    if (node && Readable((void *)(node + 0x34), sizeof(Transform))) {
      auto &t = *reinterpret_cast<Transform *>(node + 0x34);
      t.rotation = r;
      t.position = pivot - r.apply(pivot);
      UpdateSceneNode(node);
    }
  }
}
void plug(float angle,bool updatePlug=true) {
  if (!current(active) || !active.hardware ||
      read<Ptr>(active.hardware + 0x18) != active.root)
    return;
  auto &m = mounts[active.kind - 1];
  auto b = basis(active);
  auto r = mul(mul(b, matrix(axisAngle({0, 0, 1}, -angle * pi / 180))),
               b.transpose());
  Vec pivot = m.anchors[active.side] + b.apply({0, 0, .013f / m.scale});
  if (parkedMesh && read<Ptr>(parkedMesh + 0x18) == active.hardware) {
    Mat delta = mul(mul(b, matrix(axisAngle({0, 0, 1}, (parkedAngle - angle) * pi / 180))),
                    b.transpose());
    auto &local = *reinterpret_cast<Transform *>(parkedMesh + 0x34);
    local.rotation = mul(delta, parkedLocal.rotation);
    local.position = pivot + delta.apply(parkedLocal.position - pivot);
    // Apply the same collision-limited visual tremor to a supported pin.
    if (model.physics.pinResisting) {
      Pose actual=model.physics.tools[0].pose, visual=model.physics.shownPin();
      Mat shake=mul(mul(b,matrix(multiply(visual.q,inverse(actual.q)))),b.transpose());
      Vec tip=m.anchors[active.side]+b.apply(actual.p+Vec{0,0,.013f})*(1/m.scale);
      Vec moved=m.anchors[active.side]+b.apply(visual.p+Vec{0,0,.013f})*(1/m.scale);
      local.position=moved+shake.apply(local.position-tip);
      local.rotation=mul(shake,local.rotation);
    }
    UpdateSceneNode(parkedMesh);
  }
  if (!updatePlug) return;
  rotateHardware(active,r,pivot);
}
void releaseContact(bool restorePlug) {
  RestoreWorldDriver();
  discardParkedPin();
  if (restorePlug && turn.cylinder > 0)
    plug(0);
  anchors[0]={}; anchors[1]={};
  stopTension(500);
  turn={}; rules={}; entered=false;
  model = {};
  model.physics.config=settings;
  lastFrame = 0;
}
void clear(bool unloading) {
  const bool sameRef = !unloading && active.ref &&
                       read<unsigned>(active.ref + 0xc) == active.id;
  releaseContact(sameRef);
  if (sameRef && active.attached)
    attach(active, 2);
  active = {};
}
bool locked(Ptr ref) {
  if (!ref || (read<unsigned>(ref + 8) & (0x20u | 0x800u))) return false;
  // Load doors can store their lock on the other end of the transition.
  // Resolve the game's own condition once, after all plugins have loaded.
  static const ConditionCommand *getLocked =
      commands && commands->byName ? commands->byName("GetLocked") : nullptr;
  double result = 0;
  return getLocked && getLocked->eval &&
         getLocked->eval(reinterpret_cast<void *>(ref), nullptr, nullptr, &result) &&
         result > 0;
}
Selection nearest(Ptr cell, const LiveTool *wire, const LiveTool *driver,
                  Vec eye) {
  const auto scanStart = std::chrono::steady_clock::now();
  unsigned examined = 0;
  // GetRefs measures from the player. Use the full-cell fallback if its last
  // sphere no longer contains the entire search sphere of either tool.
  const bool cached = cell == nearbyCell && GetTickCount64() < nearbyUntil &&
      (!wire || length(wire->tip - nearbyOrigin) < visibilityRadius - toolOriginRadius) &&
      (!driver || length(driver->tip - nearbyOrigin) < visibilityRadius - toolOriginRadius);
  Selection best;
  float distance = 30; // world units, about a forearm's reach
  auto consider = [&](Ptr ref) {
    ++examined;
    if (!ref) return;
    Vec pos = read<Vec>(ref + 0x30);
    if ((!wire || length(pos - wire->tip) > toolOriginRadius) &&
        (!driver || length(pos - driver->tip) > toolOriginRadius))
      return;
    Ptr base = read<Ptr>(ref + 0x20);
    auto type = read<unsigned char>(base + 4);
    if ((type != 0x1b && type != 0x1c) || !locked(ref))
      return;
    GameState g;
    g.target = ref;
    UpdateTarget(g);
    if (!g.targetKind)
      return;
    Selection t{ref, rootOf(ref), 0, read<Ptr>(ref + 0x40), g.targetRefId, g.targetKind};
    t.door = g.targetIsDoor;
    t.playerCell = cell;
    if (!t.root)
      return;
    int rank = 0;
    bool checkedRank = false;
    for (unsigned j = 0; j <= mounts[t.kind - 1].reverse; ++j) {
      t.side = j;
      auto f = frame(t);
      // Double-sided door hardware belongs to the side the player faces.
      // The controller can extend through the panel while still in front of it.
      if (mounts[t.kind - 1].reverse && f.point(eye).z < -.018f)
        continue;
      for (auto *tool : {wire, driver}) {
        if (!tool)
          continue;
        auto p = f.point(tool->tip);
        float d = length(p - mouth(0)) / f.factor;
        auto grip = f.point(tool->grip);
        if (tool == wire) {
          auto bend = p + f.r.transpose().apply(wire->shaft) *
                              (pinShaftLength * wire->scale * f.factor);
          d = std::min(distanceTo(mouth(0), {p, bend}),
                       distanceTo(mouth(0), {bend, grip})) /
              f.factor;
        }
        if (d >= distance ||
            (std::max(p.z, grip.z) < -.018f && f.point(eye).z <= plugFaceZ))
          continue;
        if (!checkedRank) {
          rank = call(action, ref, 0);
          checkedRank = true;
        }
        if (rank < 0)
          continue;
        t.level =
            rank - 1; // A skill- or key-gated mount still has a solid face.
        distance = d;
        best = t;
      }
    }
  };
  if (cached) {
    for (const auto &item : nearbyCellRefs)
      if (read<unsigned>(item.ref + 0xc) == item.id && read<Ptr>(item.ref + 0x40) == cell)
        consider(item.ref);
  } else {
    for (Ptr node = cell + 0xac, n = 0; node && n < 8192;
         node = read<Ptr>(node + 4), ++n)
      consider(read<Ptr>(node));
  }
  for (const auto &entry : visible) {
    const auto &s = entry.second.target;
    if (s.cell != cell && current(s)) consider(s.ref);
  }
  const double cost = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - scanStart).count();
  static const bool trace = GetEnvironmentVariableW(L"TTWVR_LOCK_KIT_TRACE", nullptr, 0) > 0;
  static ULONGLONG nextReport = 0;
  const auto now = GetTickCount64();
  if (trace || (cost >= 12 && now >= nextReport)) {
    Log("live_lock_search cached=%d refs=%u elapsed_ms=%.2f target=%08x",
        cached, examined, cost, best.id);
    nextReport = now + 1000;
  }
  return best;
}
void initWitness() {
  const Ptr game = Ptr(GetModuleHandleW(nullptr));
  ownerOf = reinterpret_cast<OwnerOf>(game + 0x167790);
  isCrime = reinterpret_cast<IsCrime>(game + 0x179690);
  markCrime = reinterpret_cast<MarkCrime>(game + 0x515d20);
  reportCrime = reinterpret_cast<ReportCrime>(game + 0x4bfa40);
  const NativeCall hook{game + 0x38f74d, Ptr(ownerOf),
                        reinterpret_cast<void *>(ownerOnSuccess)};
  if (InstallVerifiedCalls(&hook, 1)) {
    // GetDetectingActors excludes dead actors and requires positive detection.
    // A faction can have multiple members present. Check each observing member.
    findWitness = api->compile(R"(ref itemOwner
array_var observers
array_var entry
ref observer
begin Function {itemOwner}
 let observers := player.GetDetectingActors
 if eval (ar_size observers) < 1
  return
 endif
 foreach entry <- observers
  let observer := entry["value"]
  if GetType itemOwner == 8
   if eval observer.GetInFaction itemOwner
    SetFunctionValue observer
    return
   endif
  elseif eval observer.GetIsID itemOwner
   SetFunctionValue observer
   return
  endif
 loop
end)");
  }
  Log("live_witness_support enabled=%d", findWitness != nullptr);
}

struct KeySession {
  Selection target;
  Ptr mesh = 0, form = 0;
  PhysicalKey gesture;
  Vec gripOffsets[2]{};
  Transform closePlacement;
  GameState backdrop;
  bool closeup = false, switchRequested = false;
  unsigned triggerArmed = 0;
  float drawnAngle = 0;
  int direction = 1, pullHand = -1;
  ULONGLONG lastTracked = 0;
};
KeySession key;
ULONGLONG nextKeyScan = 0, nextKeyRequest = 0;
void *matchingKey = nullptr, *useKey = nullptr, *keyAttach = nullptr, *keyForget = nullptr, *keyRequest = nullptr;
bool keyScripts() {
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    matchingKey = api->compile(R"(ref rTarget
begin Function {rTarget}
 SetFunctionValue (Call (CompileScript "TTWVRLockKit/MatchingKey.txt") rTarget)
end)");
    useKey = api->compile(R"(ref rTarget
ref rKey
begin Function {rTarget rKey}
 SetFunctionValue (Call (CompileScript "TTWVRLockKit/UseKey.txt") rTarget rKey)
end)");
    auto attachment = [](int flag) {
      char txt[220];
      snprintf(txt,sizeof(txt),"ref rTarget\nbegin Function {rTarget}\nSetFunctionValue "
          "(AttachModel rTarget %d \"TTWVRPhysicalLockpicking\\physical-key.nif\")\nend",flag);
      return api->compile(txt);
    };
    keyAttach = attachment(3);
    keyForget = attachment(0);
    keyRequest = api->compile(R"(ref asked
begin Function {}
 let asked := player.AuxVarGetRef "*_TTWVRKeyCloseup"
 if asked
  player.AuxVarErase "*_TTWVRKeyCloseup"
  SetFunctionValue asked
 endif
end)");
  }
  return matchingKey && useKey && keyAttach && keyForget && keyRequest;
}
Ptr ownedKey(Ptr ref) {
  Element e;
  return api->call(matchingKey,nullptr,nullptr,&e,1,ref) && e.type == 2
         ? Ptr(e.data.form) : 0;
}
void keyPlug(const Selection &t, float angle) {
  if (!current(t) || !t.hardware || read<Ptr>(t.hardware+0x18) != t.root) return;
  const auto b = basis(t);
  const auto &m = mounts[t.kind-1];
  const Mat rotation = mul(mul(b,matrix(axisAngle({0,0,1},-angle*pi/180))),b.transpose());
  rotateHardware(t,rotation,m.anchors[t.side]+b.apply({0,0,.013f/m.scale}));
}
void resetPhysicalKey() {
  if (key.closeup) EndKeyCloseup();
  else if (key.drawnAngle != 0) keyPlug(key.target,0);
  ReleaseMountedChild(key.mesh);
  key = {};
}
struct KeyOffer { Selection selection; Ptr form = 0; };
KeyOffer nearbyKey(Vec eye) {
  std::vector<std::pair<float,Selection>> candidates;
  for (const auto &entry : visible) {
    auto t = entry.second.target;
    if (!entry.second.nearby || !current(t) || read<Ptr>(t.hardware+0x18) != t.root) continue;
    for (unsigned side=0; side<=mounts[t.kind-1].reverse; ++side) {
      t.side = side;
      const auto mount = frame(t);
      float distance = length(eye-mount.origin);
      if (distance > 125 || mount.point(eye).z < -.012f) continue;
      candidates.emplace_back(distance,t);
    }
  }
  std::sort(candidates.begin(),candidates.end(),[](const auto &a,const auto &b){return a.first < b.first;});
  for (const auto &candidate : candidates)
    if (Ptr form = ownedKey(candidate.second.ref)) return {candidate.second,form};
  return {};
}
bool offerPhysicalKey(KeyOffer offer, ULONGLONG now) {
  auto &t = offer.selection;
  Element e;
  if (!api->call(keyAttach,nullptr,nullptr,&e,1,t.ref) || e.type!=1 || e.data.number<=0)
    return false;
  Ptr mesh = FindSceneNode(t.root,"TTWVRPhysicalKey");
  api->call(keyForget,nullptr,nullptr,&e,1,t.ref);
  if (!mesh || read<Ptr>(mesh+0x18)!=t.root) return false;
  RetainSceneNode(mesh);
  key.target = t;
  key.mesh = mesh;
  *reinterpret_cast<uint32_t *>(mesh+0x30) |= 1u; // Keep the parked live key inside the kit.
  key.form = offer.form;
  key.direction = read<uint8_t>(read<Ptr>(t.ref+0x20)+4)==0x1c ? twistDirection(t.kind,t.side) : 1;
  key.lastTracked = now;
  Log("physical_key_offered ref=%08x key=%08x side=%u",t.id,read<unsigned>(key.form+0xc),t.side);
  return true;
}
bool useMatchingKey(Ptr ref, Ptr form) {
  Element result;
  return api->call(useKey,nullptr,nullptr,&result,2,ref,form) && result.type==1 && result.data.number==1;
}
} // namespace
void PlayForceSeatSound() {
  // Play the stock mechanical detent once, through the lock menu's 2D audio path.
  static constexpr char cue[] = "WPN357RevolverReloadClick";
  SoundHandle shot;
  bool played = playSound(cue, 0x101, shot, false);
  Log("force_lock_seat_audio cue=%s handle_valid=%d", cue, played);
}
int HoverLockLevel(unsigned id) {
  // Resolve a fresh form on the game thread; the render thread only passes an ID.
  auto ref = reinterpret_cast<Ptr(__cdecl *)(unsigned)>(address(0x4839c0))(id);
  if (!ref || read<unsigned>(ref + 0xc) != id || read<unsigned char>(ref + 4) != 0x3a) return -1;
  auto base = read<Ptr>(ref + 0x20);
  auto type = read<unsigned char>(base + 4);
  if (!base || (type != 0x1b && type != 0x1c)) return -1;
  ensureWorldAction();
  return call(action,ref,3) - 1;
}
bool HasPhysicalKeyTarget() { return key.mesh != 0; }
int PhysicalKeyHand() { return key.gesture.owner; }
bool PullPhysicalKey(int hand, bool grip) {
  if (!key.mesh || !current(key.target) || !locked(key.target.ref) ||
      ownedKey(key.target.ref) != key.form) return false;
  if (grip && key.gesture.owner < 0 && !key.gesture.seated && key.pullHand < 0)
    key.pullHand = hand;
  return true;
}
void TickPhysicalKey(GameState &g) {
  if (!ready || !NativeReady() || !g.kitGameplay || g.menu || g.kitDrawn || g.holsterEditing || finishing || HasParkedPin()) {
    if (key.mesh) resetPhysicalKey();
    return;
  }
  Vec eye;
  if (!WorldEye(eye) || !keyScripts()) return;
  auto now = GetTickCount64();
  const auto mountEntry=visible.find(key.target.id);
  if (key.mesh && (mountEntry==visible.end() || mountEntry->second.target.hardware!=key.target.hardware ||
      !current(key.target) || read<Ptr>(key.mesh+0x18) != key.target.root ||
      read<Ptr>(key.target.hardware+0x18) != key.target.root || !locked(key.target.ref) ||
      length(eye-frame(key.target).origin)>145)) resetPhysicalKey();
  if (now >= nextKeyScan) {
    nextKeyScan = now + 350;
    if (key.mesh && (key.closeup || key.gesture.owner>=0 || key.gesture.seated)) {
      if (ownedKey(key.target.ref)!=key.form) resetPhysicalKey();
    } else {
      const auto offer = nearbyKey(eye);
      if (offer.selection.ref!=key.target.ref || offer.form!=key.form || offer.selection.side!=key.target.side) {
        resetPhysicalKey();
        if (offer.form) offerPhysicalKey(offer,now);
      }
    }
  }
  if (!key.mesh) return;
  g.keyAvailable = true;
  auto mount = frame(key.target);
  Transform placement{mount.r,mount.origin+mount.r.apply({0,0,.013f/mount.factor}),1/mount.factor};
  bool switchView = key.switchRequested;
  key.switchRequested = false;
  if (now >= nextKeyRequest) {
    nextKeyRequest = now+65;
    Element request;
    if (api->call(keyRequest,nullptr,nullptr,&request,0) && request.type==2 && Ptr(request.data.form)==key.target.ref) switchView = true;
  }
  if (switchView) {
    if (key.closeup) {
      resetPhysicalKey();
      nextKeyScan = now+350;
      return;
    }
    key.backdrop.target = key.target.ref;
    UpdateTarget(key.backdrop);
    key.backdrop.closeupHiddenParts = CloseupHiddenParts(key.backdrop);
    key.backdrop.doorTurn = CloseupDoorTurn(key.backdrop);
    if (!KeyCloseupPlacement(settings,key.closePlacement) ||
        !PaintKeyCloseup(key.backdrop,settings,key.closePlacement,0)) {
      Log("physical_key_closeup_unavailable ref=%08x",key.target.id);
      return; // The live key remains usable. Only a completed turn may activate it.
    }
    if (key.drawnAngle!=0) keyPlug(key.target,0);
    key.closeup = true;
    key.gesture = {};
    key.pullHand = -1;
    key.drawnAngle = 0;
    Log("physical_key_closeup ref=%08x",key.target.id);
  }
  if (key.closeup) placement = key.closePlacement;
  Transform hands[2]; unsigned tracked=0,grips=0,triggers=0;
  for (int h=0;h<2;++h) {
    bool held=false, trigger=false; Transform world;
    if (!KitControllerWorld(h,world,false,&held,&trigger)) continue;
    if (trigger) triggers |= 1u<<h;
    tracked |= 1u<<h;
    if (held) grips |= 1u<<h;
    // Fit the bow to the fingertips at pickup. Once held, keep this rigid
    // offset in the controller frame even when the avatar arm reaches its limit.
    if (key.gesture.owner != h) {
      Vec fingers;
      key.gripOffsets[h] = KeyFingerContact(h,fingers)
          ? bounded(world.rotation.transpose().apply(fingers-world.position) *
                    (1 / g.worldUnitsPerMeter), .095f) : Vec{};
    }
    world.position = world.position + world.rotation.apply(key.gripOffsets[h])*g.worldUnitsPerMeter;
    hands[h]=relativeTransform(world,placement);
  }
  if (tracked) key.lastTracked=now;
  else if (now-key.lastTracked>1000) {
    resetPhysicalKey();
    nextKeyScan=now+550;
    return;
  }
  const bool wasSeated=key.gesture.seated;
  const int pull = key.pullHand;
  key.pullHand = -1;
  if (pull >= 0 && (tracked & grips & (1u<<pull))) {
    key.gesture.draw(pull,hands[pull]);
    Log("physical_key_draw source=holster hand=%d ref=%08x",pull,key.target.id);
  }
  Transform rest;
  if (key.closeup) {
    // Present the bow at the player's configured pin-box position.
    rest.rotation = matrix(axisAngle({0,1,0},-pi/3));
    rest.position = holderPoint(settings,0) - rest.rotation.apply({0,0,PhysicalKey::bowZ});
  }
  key.gesture.step(hands,tracked,grips,key.direction,settings.fullTurnWrist,rest,key.closeup);
  g.keyGripMask=key.gesture.inReach | (key.gesture.owner>=0 ? 1u<<key.gesture.owner : 0u);
  // Open or leave the close-up at the lock even when the live key is stowed.
  // Reserve only trigger near the keyway; grip still belongs to the kit.
  for (int h=0;h<2;++h)
    if ((tracked & (1u<<h)) && length(hands[h].position-Vec{0,0,.065f}) < .105f)
      g.keyTriggerMask |= 1u<<h;
  g.keyTriggerMask |= g.keyGripMask;
  if (key.gesture.owner < 0 && (triggers & key.triggerArmed & g.keyTriggerMask & ~grips))
    key.switchRequested = true;
  key.triggerArmed = tracked & ~triggers;
  static const bool keyTrace = GetEnvironmentVariableW(L"TTWVR_LOCK_KIT_TRACE",nullptr,0)>0;
  static ULONGLONG traceAt = 0;
  if (keyTrace && now>=traceAt) {
    traceAt=now+350;
    Vec bow, axis, palm[2]{};
    const auto toTracking=[&](Vec p,Vec &out) {
      return WorldToTracking(placement.position+placement.rotation.apply(p)*placement.scale,out);
    };
    Vec resting = key.closeup ? holderPoint(settings,0) : Vec{0,0,.065f};
    if (toTracking(resting,bow) && toTracking(resting+Vec{0,0,.05f},axis)) {
      for (int h=0;h<2;++h) if (tracked&(1u<<h)) toTracking(hands[h].position,palm[h]);
      axis=axis-bow;
      Log("physical_key_sample mode=%s bow=%.4f,%.4f,%.4f travel=%.4f,%.4f,%.4f left=%.4f,%.4f,%.4f right=%.4f,%.4f,%.4f grips=%u tracked=%u hand=%d direction=%d turn=%.1f",
          key.closeup?"closeup":"live",bow.x,bow.y,bow.z,axis.x,axis.y,axis.z,
          palm[0].x,palm[0].y,palm[0].z,palm[1].x,palm[1].y,palm[1].z,
          grips,tracked,key.gesture.owner,key.direction,key.gesture.angle);
    }
  }
  if (!wasSeated && key.gesture.seated) {
    sound(0x10745e0,0x121);
    Log("physical_key_seated ref=%08x mode=%s hand=%d",key.target.id,key.closeup?"closeup":"live",key.gesture.owner);
  }
  const Transform parent=read<Transform>(key.target.root+0x68);
  if (!(parent.scale>.0001f)) { resetPhysicalKey(); return; }
  auto &flags=*reinterpret_cast<uint32_t *>(key.mesh+0x30);
  const bool visibleKey=key.closeup || key.gesture.owner>=0 || key.gesture.seated;
  if (visibleKey) flags &= ~1u; else flags |= 1u;
  auto local=relativeTransform(childWorld(placement,key.gesture.pose),parent);
  auto &previous=*reinterpret_cast<Transform*>(key.mesh+0x34);
  if (memcmp(&previous,&local,sizeof(local))) {
    previous=local;
    UpdateSceneNode(key.mesh);
  }
  if (key.drawnAngle!=key.gesture.angle) {
    float angle=key.gesture.angle*key.direction;
    if (key.closeup) {
      if (!PaintKeyCloseup(key.backdrop,settings,placement,angle)) { resetPhysicalKey(); return; }
    } else keyPlug(key.target,angle);
    key.drawnAngle=key.gesture.angle;
  }
  if (key.gesture.complete) {
    const auto t=key.target;
    const auto form=key.form;
    resetPhysicalKey(); // Remove our pre-activation marker before invoking Fallout.
    const bool called=useMatchingKey(t.ref,form);
    Log("physical_key_turned ref=%08x activated=%d",t.id,called);
    nextKeyScan=now+500;
    g.keyAvailable=false;
    g.keyGripMask=0;
  }
}
void InitWorldLocks(void *scripts, void *arrayInterface, void *commandTable) {
  commands = static_cast<CommandTable *>(commandTable);
  arrays = static_cast<Arrays *>(arrayInterface);
  settings = LoadPlayerSettings(folder);
  model.physics.config=settings;
  api = (Scripts *)scripts;
  if (!api || !api->compile || !api->call)
    return;
  auto f = _wfopen(
      (folder + L"\\TTWVRPhysicalLockpicking\\kit-world.bin").c_str(), L"rb");
  if (!f)
    return;
  char magic[8];
  unsigned count = 0;
  ready = fread(magic, 1, 8, f) == 8 && !memcmp(magic, "TTWLive1", 8) &&
          fread(drivers, sizeof(drivers), 1, f) == 1 &&
          fread(&count, 4, 1, f) == 1 && count == targetKinds &&
          fread(mounts, sizeof(mounts), 1, f) == 1;
  fclose(f);
  for (auto &m : mounts)
    ready &= m.style <= 2 && m.reverse <= 1 && m.scale > 0 && m.scale <= .1f;
  if (!ready)
    return;
  auto maps = _wfopen((folder + L"\\TTWVRPhysicalLockpicking\\kit-surfaces.bin").c_str(), L"rb");
  if (maps) {
    unsigned gridSide = 0;
    float spacing = 0;
    panelsReady = fread(magic, 1, 8, maps) == 8 && !memcmp(magic, "TTWFace1", 8) &&
        fread(&count, sizeof(count), 1, maps) == 1 && count == targetKinds &&
        fread(&gridSide, sizeof(gridSide), 1, maps) == 1 && gridSide == MountSurfaceGrid::side &&
        fread(&spacing, sizeof(spacing), 1, maps) == 1 && spacing == MountSurfaceGrid::spacing;
    for (unsigned k = 0; panelsReady && k < targetKinds; ++k)
      for (unsigned side = 0; panelsReady && side <= mounts[k].reverse; ++side) {
        panelsReady = fread(panels[k][side].depths.data(), sizeof(int16_t),
                            gridSide * gridSide, maps) == gridSide * gridSide;
        if (panelsReady) panels[k][side].measureHeight();
      }
    panelsReady = panelsReady && fgetc(maps) == EOF;
    fclose(maps);
  }
  Log("live_mount_surfaces loaded=%d kinds=%u", panelsReady, targetKinds);

}
// Visible hardware has scene lifetime only. JIP flag 3 attaches it immediately;
// flag 0 forgets its reload registration while leaving the current node in place.
// A retained node lets us remove exactly our own hardware on unlock or cell unload.
void TickVisibleLocks(Ptr closeupTarget) {
  if (!ready || !arrays || !NativeReady()) return;
  auto now = GetTickCount64();
  if (now < nextVisibility && previousCloseup == closeupTarget) return;
  nextVisibility = now + 500;
  previousCloseup = closeupTarget;
  nearbyCell = 0;
  const Ptr pc = player(), cell = read<Ptr>(pc + 0x40);
  if (!cell) return;
  const Vec origin = read<Vec>(pc + 0x30);
  if (!nearbyQuery)
    nearbyQuery = api->compile("int formType\nbegin Function {formType}\n"
        "SetFunctionValue (player.GetRefs formType 1 1 900)\nend");
  if (!nearbyQuery) return;

  std::vector<Element> nearby;
  for (unsigned type : {0x1bu, 0x1cu}) { // containers and doors, including adjacent exterior cells
    Element result;
    if (!api->call(nearbyQuery, nullptr, nullptr, &result, 1, type) ||
        result.type != 4 || !result.data.form) return;
    unsigned count = arrays->size(result.data.form);
    if (count > 65535) return;
    auto start = nearby.size();
    nearby.resize(start + count);
    if (count && !arrays->elements(result.data.form, nearby.data() + start, nullptr)) return;
  }
  nearbyCellRefs.clear();
  nearbyCell = cell;
  nearbyOrigin = origin;
  nearbyUntil = now + 750;
  for (auto &entry : visible) entry.second.nearby = false;
  unsigned added = 0;
  for (const auto &item : nearby) {
    Ptr ref = Ptr(item.data.form);
    if (item.type != 2 || !ref) continue;
    unsigned id = read<unsigned>(ref + 0xc);
    if (id && read<Ptr>(ref + 0x40) == cell) nearbyCellRefs.push_back({ref, id});
    if (ref == closeupTarget || ref == finishing || !locked(ref)) continue;
    Ptr root = rootOf(ref);
    if (!root || !id) continue;
    auto found = visible.find(id);
    if (found != visible.end()) {
      auto &old = found->second;
      old.nearby = old.target.ref == ref && old.target.root == root &&
                   read<Ptr>(old.target.hardware + 0x18) == root;
      continue;
    }
    if (added >= 4 || ref == active.ref) continue;
    GameState state;
    state.target = ref;
    UpdateTarget(state);
    if (!state.targetKind) continue;
    Selection s{ref,root,0,read<Ptr>(ref + 0x40),id,state.targetKind};
    auto name = "TTWVRWorldLock_" + std::to_string(s.kind);
    if (FindSceneNode(root, name) || attach(s, 3) <= 0) continue;
    s.hardware = FindSceneNode(root, name);
    attach(s, 0);
    if (!s.hardware || read<Ptr>(s.hardware + 0x18) != root) continue;
    RetainSceneNode(s.hardware);
    visible.emplace(id, VisibleLock{s,true});
    ++added;
    Log("visible_lock_added ref=%08x kind=%u", id, s.kind);
  }
  for (auto it = visible.begin(); it != visible.end();) {
    const auto &v = it->second;
    if (v.nearby || (active.hardware && v.target.hardware == active.hardware)) { ++it; continue; }
    ReleaseMountedChild(v.target.hardware);
    Log("visible_lock_removed ref=%08x", it->first);
    it = visible.erase(it);
  }
}
DoorTurn CloseupDoorTurn(const GameState &g) {
  DoorTurn turn;
  if (!ready || !g.targetIsDoor || !g.targetKind) return turn;
  Selection s{g.target, rootOf(g.target), 0, 0, g.targetRefId, g.targetKind};
  Vec eye;
  if (!WorldEye(eye)) eye = read<Vec>(player() + 0x30);
  const auto &m = mounts[s.kind-1];
  if (s.root && m.reverse && frame(s).point(eye).z < -.013f) {
    s.side = 1;
    turn.reverseFace = reverseDoorPose(m.basis.transpose().apply(m.anchors[1]-m.anchors[0]) * m.scale);
  }
  turn.sign = twistDirection(s.kind, s.side);
  return turn;
}
uintptr_t WorldDriverMount() { return active.hardware; }
bool HasParkedPin() { return parkedMesh != 0; }
bool ParkLivePin(uintptr_t mesh) {
  if (!mesh || parkedMesh || !model.pin || !current(active) || !locked(active.ref) ||
      read<Ptr>(player() + 0x40) != active.playerCell ||
      read<Ptr>(active.hardware + 0x18) != active.root ||
      !MountSceneChild(mesh, active.hardware)) return false;
  parkedMesh = mesh;
  parkedLocal = read<Transform>(mesh + 0x34);
  parkedAngle = model.physics.signedCylinder(turn.cylinder);
  Log("live_pin_parked ref=%08x pick=%.1f", active.id, model.pick);
  return true;
}
bool ParkedPinPose(Transform &world) {
  if (!parkedMesh || !current(active) || read<Ptr>(player() + 0x40) != active.playerCell ||
      read<Ptr>(parkedMesh + 0x18) != active.hardware ||
      read<Ptr>(active.hardware + 0x18) != active.root) return false;
  world = read<Transform>(parkedMesh + 0x68);
  return world.scale > 0;
}
void TakeLivePin() {
  Transform world;
  if (ParkedPinPose(world)) {
    auto &contact = model.pinContact;
    contact.previous = contact.shown = frame(active).point(world.position);
    contact.offset = {};
    contact.entryDebt = 0;
  }
  discardParkedPin();
}
void ResetWorldLock(bool unloading) {
  if (unloading) resetPhysicalKey();
  clear(unloading);
  reportedRef = reportedOwner = 0;
  nextWitnessCheck = 0;
  nextScan = 0;
  if (unloading) {
    for (const auto &entry : visible) ReleaseMountedChild(entry.second.target.hardware);
    visible.clear();
    nearbyCellRefs.clear();
    nearbyCell = 0;
    nextVisibility = 0;
    previousCloseup = 0;
    puzzle = {};
    finishing = 0;
  }
}
bool WorldLockFrame(GameState &g, const LiveTool *wire, const Transform *hand,
                    int pinHand, float opening, float twist, float &pinHealth, MountedCorrection (&corrections)[2]) {
  if (!ready || finishing || (!wire && !hand && !parkedMesh) || pinHand < 0 || pinHand > 1) {
    if (active.ref)
      clear(false);
    return false;
  }
  static bool witnessInitialized = false;
  if (!witnessInitialized) {
    witnessInitialized = true;
    initWitness(); // xNVSE registers script commands after plugin load.
  }
  ensureWorldAction();
  auto now = GetTickCount64();
  auto pc = player(), cell = read<Ptr>(pc + 0x40);
  if (!cell || !action) {
    if (active.ref) clear(false);
    return false;
  }
  Vec eye = read<Vec>(pc + 0x30);
  WorldEye(eye);
  LiveTool d;
  if (hand) {
    auto &fit = drivers[1 - pinHand];
    auto r = mul(hand->rotation, fit.r);
    Vec z = r.apply({0, 0, 1});
    Vec tip = hand->position + hand->rotation.apply(fit.tip) * hand->scale;
    d = {tip, z, tip + z * (80.f * hand->scale * handleCenter(1).z),
         r.apply({0, 1, 0}), 80.f * hand->scale};
  }
  bool crossedDoor = false;
  if (active.ref) {
    if (!current(active) || cell != active.playerCell || !locked(active.ref) ||
        (active.hardware && read<Ptr>(active.hardware + 0x18) != active.root))
      clear(false);
    else {
      auto f = frame(active);
      if (parkedMesh ? (read<Ptr>(parkedMesh + 0x18) != active.hardware ||
                        length(eye - f.origin) > 2.5f * g.worldUnitsPerMeter)
                     : ((!wire || length(f.point(wire->tip)) > f.factor * 38) &&
                        (!hand || length(f.point(d.tip)) > f.factor * 38))) {
        clear(false);
      } else if (!parkedMesh && mounts[active.kind - 1].reverse && f.point(eye).z < -.018f) {
        auto opposite = active;
        opposite.side ^= 1;
        // Crossing both seating planes avoids flipping contact inside the panel.
        crossedDoor = frame(opposite).point(eye).z > -.008f;
      }
    }
  }
  const bool inAir = !model.pin && model.pinContact.surface == LiveSurface::Air &&
                     model.bladeContact.surface == LiveSurface::Air;
  if (crossedDoor || (inAir && now >= nextScan)) {
    auto candidate = nearest(cell, wire, hand ? &d : nullptr, eye);
    nextScan = GetTickCount64() + 250;
    if (candidate.ref && candidate.ref == active.ref) {
      active.level = candidate.level;
      if (candidate.side != active.side) {
        releaseContact(true);
        active.side = candidate.side;
        Log("live_lock_target ref=%08x kind=%u side=%u level=%d", active.id,
            active.kind, active.side, active.level);
      }
    } else if (candidate.ref != active.ref) {
      clear(false);
      active = candidate;
      if (active.ref) {
        auto name = "TTWVRWorldLock_" + std::to_string(active.kind);
        active.hardware = FindSceneNode(active.root, name);
        if (!active.hardware) {
          int result = attach(active, 3);
          active.attached = result > 0;
          active.mountDeadline = now + 600;
          Log("live_lock_mount ref=%08x kind=%u registered=%d", active.id,
              active.kind, result);
        }
        Log("live_lock_target ref=%08x kind=%u side=%u level=%d", active.id,
            active.kind, active.side, active.level);
      }
    }
    nextRankCheck = now + 500;
  } else if (active.ref && now >= nextRankCheck) {
    // Inventory, skill, and scripted lock difficulty can change mid-contact.
    active.level = std::max(0, call(action, active.ref, 0)) - 1;
    nextRankCheck = now + 500;
  }
  if (!active.ref)
    return false;
  if (!active.hardware) {
    active.hardware = FindSceneNode(
        active.root, "TTWVRWorldLock_" + std::to_string(active.kind));
    if (!active.hardware) {
      if (now >= active.mountDeadline) {
        clear(false);
        nextScan = now + 500;
      }
      return false;
    }
    Log("live_lock_mount_ready ref=%08x", active.id);
  }
  // Attachment can be removed by the gallery or a replaced scene graph.
  if (!current(active) || read<Ptr>(active.hardware + 0x18) != active.root) {
    clear(false);
    return false;
  }
  auto f = frame(active);
  // The game changes its animated wrist when contact or the grip button changes.
  // Once a tool approaches a mount, take motion from its tracked controller.
  // Keep the animated transform separately to fit the visible hand to the result.
  LiveTool art[2]{wire ? f.tool(*wire) : LiveTool{}, hand ? f.tool(d) : LiveTool{}};
  auto w=art[0];
  if (wire) w=f.tool(anchors[0].sample(*wire,0,pinHand,opening,twist));
  else anchors[0]={};
  if (hand) d=f.tool(anchors[1].sample(d,1,1-pinHand,opening,twist));
  else anchors[1]={};
  bool priorPin = model.pin, priorBlade = model.blade;
  const LiveSurface previousSurface[2]{model.pinContact.surface,
                                       model.bladeContact.surface};
  float previous = turn.cylinder;
  float dt = lastFrame ? std::min(.05f, float(now - lastFrame) * .001f) : 0;
  lastFrame = now;
  static const bool trace =
      GetEnvironmentVariableW(L"TTWVR_LOCK_KIT_TRACE", nullptr, 0) > 0;
  static ULONGLONG nextTrace = 0;
  model.physics.doorTurnSign = active.door ? twistDirection(active.kind, active.side) : 1;
  model.physics.targetBounds.style=LockStyle(mounts[active.kind-1].style);
  model.physics.targetBounds.panel=panelsReady ? &panels[active.kind-1][active.side] : nullptr;
  ForcePalms palms;
  palms.metresPerUnit = 1.f/(g.worldUnitsPerMeter*f.factor);
  Transform offhand;
  if (hand && !wire && active.level >= 0 && KitControllerWorld(pinHand, offhand, true)) {
    palms.point[pinHand] = f.point(offhand.position);
    palms.open = 1u << pinHand;
  }
  model.step(wire ? &w : nullptr,hand ? &d : nullptr,opening,twist,parkedMesh!=0,pinHand,
             turn.cylinder,turn.stage,pinHealth,dt,1.f/(g.worldUnitsPerMeter*f.factor),
             f.point(eye).z>plugFaceZ, &palms);
  static uint64_t lastKnock = 0;
  if (model.physics.forceEvent == ForceGesture::Knock) {
    lastKnock = (now << 1) | unsigned(1-pinHand);
    PlayForceSeatSound();
    Log("force_lock_tap mode=live ref=%08x driver=%d",active.id,1-pinHand);
  }
  g.worldKnock = lastKnock;
  if (model.pin && active.level>=0 && !entered && beginRules(pc,active.level)) {
    entered=true; sound(0x10745e0,0x121);
  }
  if (entered)
    playEvents(turn.step(rules,nativePinSearch(model.pick,settings.pinSearchDegrees),
                         active.level>=0 ? model.demand : 0.f,dt,pinHealth));
  float goal=rules.width>0 ? rules.center*180/rules.width : -1;
  if (!parkedMesh || model.blade) checkWitness(now, pc);
  const LiveContact *contacts[]{&model.pinContact, &model.bladeContact};
  for (int k = 0; k < 2; ++k) {
    if (k == 0 && parkedMesh) continue;
    const auto &contact = *contacts[k];
    int h = k ? 1 - pinHand : pinHand;
    if (k ? hand!=nullptr : wire!=nullptr) {
      const auto &raw=art[k];
      Pose shown=model.shown[k];
      if (!k) shown=model.physics.shownPin();
      auto delta=matrix(multiply(shown.q,inverse(mountedToolPose(raw,k).q)));
      Vec tip=f.origin+f.r.apply(raw.tip+Vec{0,0,.013f})*(1/f.factor);
      corrections[h]={tip,tip+f.r.apply(shown.p-raw.tip)*(1/f.factor),
                      mul(mul(f.r,delta),f.r.transpose()),true};
    }
    g.worldSurface[h] = unsigned(contact.surface);
    if (previousSurface[k] != contact.surface)
      Log("live_tool_surface ref=%08x hand=%d tool=%d surface=%u "
          "requested_z=%.5f shown_z=%.5f offset=%.5f",
          active.id, h, k, unsigned(contact.surface), contact.previous.z,
          contact.shown.z, length(contact.offset));
  }
  if (now >= nextTrace && (trace || (wire && length(w.tip) < .23f))) {
    Log("live_lock_sample ref=%08x wire=%.4f,%.4f,%.4f shaft_z=%.3f "
        "blade=%.4f,%.4f,%.4f driver_z=%.3f bend=%.2f seated=%d,%d pick=%.1f "
        "goal=%.1f turn=%.2f demand=%.2f",
        active.id, w.tip.x, w.tip.y, w.tip.z, w.shaft.z, d.tip.x, d.tip.y,
        d.tip.z, d.shaft.z, opening, model.pin, model.blade, model.pick, goal,
        turn.cylinder, model.demand);
    nextTrace = now + (trace ? 180 : 300);
  }
  bool shake=parkedMesh && model.physics.pinResisting;
  if (turn.cylinder != previous || shake || parkedShaking)
    plug(model.physics.signedCylinder(turn.cylinder),turn.cylinder != previous);
  parkedShaking=shake;
  if (priorPin != model.pin || priorBlade != model.blade)
    Log("live_lock_contact ref=%08x pin=%d blade=%d pick=%.1f cylinder=%.1f",
        active.id, model.pin, model.blade, model.pick, turn.cylinder);
  g.worldPick = pinHand;
  g.worldStage = turn.stage;
  g.worldHealth = pinHealth;
  g.worldCylinder = turn.cylinder;
  g.worldDemand = model.demand;
  g.worldSeated = (model.pin && !parkedMesh ? (1u << pinHand) : 0) |
                  (model.blade ? (1u << (1 - pinHand)) : 0);
  if (turn.stage==5 && pinHealth <= 0) {
    bool broke = call(action, active.ref, 2) > 0;
    Log("live_lock_break ref=%08x committed=%d", active.id, broke);
    if (broke) {
      clear(false);
    } else {
      active.level = -1;
      nextRankCheck = now + 500;
    }
    return broke;
  }
  const bool force = model.physics.forceEvent == ForceGesture::Attempt;
  if ((force || (turn.stage==3 && turn.cylinder>=90)) && active.level>=0 && pinHealth>0) {
    auto selected = active;
    clear(false);
    finishing = selected.ref;
    finishingID = selected.id;
    finishingOwner = reportedRef == selected.id ? reportedOwner : 0;
    submitted = false;
    forceFinish = force;
    deadline = now + 3200;
    if (call(action, selected.ref, 1) <= 0)
      finishing = 0;
    Log("live_lock_%s ref=%08x native_activation=%d", force ? "force" : "solved", selected.id,
        finishing != 0);
  }
  return false;
}
bool IsWorldUnlockPending(uintptr_t ref) {
  return submitted && ref && ref == finishing && read<unsigned>(ref + 0xc) == finishingID;
}
bool CompleteWorldLock(const GameState &g) {
  if (!finishing)
    return false;
  if (read<unsigned>(finishing + 0xc) != finishingID) {
    finishing = 0;
    return false;
  }
  if (!g.menu) {
    const bool unlocked = !locked(finishing);
    if (unlocked || (submitted && forceFinish) || GetTickCount64() > deadline) {
      if (unlocked) puzzle = {};
      Log("live_lock_result ref=%08x unlocked=%d", finishingID, unlocked);
      finishing = 0;
    }
    return false;
  }
  if (GetTickCount64() > deadline || (g.target && g.target != finishing)) {
    Log("live_lock_native_cancel ref=%08x stage=%u", finishingID, g.stage);
    finishing = 0;
    return false;
  }
  if (!g.target)
    return true;
  // The physical turn is complete. Let New Vegas perform its own success
  // transaction (XP, statistics, crime and unlock). Doors wait for the player.
  // This singleton is never published as an interactive VR workspace.
  if (forceFinish) {
    // Submit the stock Force button only when the hidden menu accepts input.
    if (!submitted && g.stage == 2 && g.health > 0) {
      submitted = true;
      if (!NativeForceLock(g)) finishing = 0;
    }
    return true;
  }
  if (!submitted && (g.stage == 0 || g.stage == 2 || g.stage == 3) && g.health > 0) {
    submitted = true;
    Log("live_lock_native_commit ref=%08x stage=%u", finishingID, g.stage);
    auto p = (unsigned char *)g.menu;
    *reinterpret_cast<float *>(p + 0x8c) = 90;
    *reinterpret_cast<float *>(p + 0x90) = 90;
    p[0x98] = 1;
    *reinterpret_cast<unsigned *>(p + 0x28) = 3;
  }
  return true;
}
} // namespace locks
