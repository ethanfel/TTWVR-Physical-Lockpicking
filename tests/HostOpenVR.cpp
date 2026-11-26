// SPDX-License-Identifier: GPL-3.0-only
// Run under 32-bit Windows/Wine with the stock TTWVR DLL as an argument.
// The host's real OpenVR poll and haptic methods run against test devices.
#include "HostInput.h"
#include "Shared.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace {
unsigned polls = 0, pulses = 0;
using Poll = bool (__thiscall *)(void*, locks::HostFrame*);
static Poll earlier[2]{};
unsigned forwarded = 0;
template<unsigned index>
bool __fastcall forwardPoll(void *self, void *, locks::HostFrame *frame) {
  ++forwarded;
  return earlier[index](self, frame);
}

struct Matrix { float m[3][4]; };
struct DevicePose {
  Matrix pose;
  float velocity[6];
  int tracking;
  bool valid, connected;
};
static_assert(sizeof(DevicePose) == 80);
void setPose(Matrix &pose, float x) {
  pose = {};
  for (int i=0; i<3; ++i) pose.m[i][i] = 1.f;
  pose.m[0][3] = x;
}
int __fastcall poses(void*,void*,DevicePose *devices,unsigned count,void*,unsigned) {
  assert(count >= 3);
  for (unsigned n=0; n<3; ++n) {
    devices[n] = {};
    setPose(devices[n].pose, n==1 ? -.3f : n==2 ? .4f : 0.f);
    devices[n].valid = devices[n].connected = true;
    devices[n].tracking = 200;
  }
  return 0;
}
Matrix *__fastcall eye(void*,void*,Matrix *out,int side) {
  setPose(*out,side ? .032f : -.032f); return out;
}
void __fastcall projection(void*,void*,int,float *left,float *right,float *top,float *bottom) {
  *left = *top = -1; *right = *bottom = 1;
}
unsigned __fastcall role(void*,void*,unsigned hand) { return hand; }
bool __fastcall controller(void*,void*,unsigned index,char *out,unsigned size) {
  assert(size >= 60 && (index==1 || index==2));
  // SteamVR's application-menu bit on the right hand feeds TTWVR's B action.
  const uint64_t pressed = index == (polls == 1 ? 1u : 2u) ? 1ull << 1 : 0;
  std::memcpy(out + 8, &pressed, sizeof(pressed));
  float axes[10]{};
  axes[2] = index==1 ? .75f : .125f;
  axes[4] = index==2 ? .875f : .25f;
  // TTWVR requests the SteamVR Windows 64-byte controller-state layout.
  std::memcpy(out+24,axes,sizeof(axes));
  return true;
}
void __fastcall vibrate(void*,void*,unsigned index,unsigned axis,unsigned short length) {
  assert(index==2 && axis==0 && length>=1900 && length<=2100);
  ++pulses;
}
}
namespace locks {
void Log(const char *fmt,...) {
  va_list args; va_start(args,fmt); std::vprintf(fmt,args); va_end(args); std::puts("");
}
void ProcessHostInput(void *backend,HostPulse pulse,HostFrame &frame,bool valid,bool openVR) {
  assert(openVR && valid && frame.valid && frame.head.usable());
  assert(frame.hands[0].grip.tracked && frame.hands[1].grip.tracked);
  std::printf("L %.3f  R %.3f  trigger %.3f %.3f grip %.3f %.3f  eyes %.3f %.3f\n",frame.hands[0].grip.position.x,frame.hands[1].grip.position.x,frame.trigger[0],frame.trigger[1],frame.squeeze[0],frame.squeeze[1],frame.eyes[0].pose.position.x,frame.eyes[1].pose.position.x);
  assert(std::abs(frame.hands[0].grip.position.x+.3f)<1e-5f && std::abs(frame.hands[1].grip.position.x-.4f)<1e-5f);
  assert(frame.eyes[0].pose.usable() && frame.eyes[1].pose.usable());
  assert(frame.trigger[0]==.75f && frame.trigger[1]==.125f);
  assert(frame.squeeze[0]==.25f && frame.squeeze[1]==.875f);
  assert(bool(frame.rightB()) == (polls != 1));
  frame.trigger[0]=0;
  pulse(backend,true,.5f,.02f);
  ++polls;
}
}
int wmain(int argc,wchar_t **argv) {
  using namespace locks;
  if ((argc != 3 && argc != 4) || (wcscmp(argv[1],L"116") && wcscmp(argv[1],L"117") && wcscmp(argv[1],L"118"))) return 2;
  const bool direct = argc == 4 && wcscmp(argv[3], L"direct") == 0;
  const bool wrapped = argc == 4 && wcscmp(argv[3], L"wrapped") == 0;
  const bool mixed = argc == 4 && wcscmp(argv[3], L"mixed") == 0;
  if (argc == 4 && !direct && !wrapped && !mixed) return 2;
  const auto &build = !wcscmp(argv[1],L"118") ? host118 : !wcscmp(argv[1],L"117") ? host117 : host116;
  HMODULE host=LoadLibraryW(argv[2]);
  if (!host) { std::printf("LoadLibrary failed %lu\n",GetLastError()); return 3; }
  void *systemTable[40]{}, *compositorTable[3]{};
  systemTable[2]=(void*)projection; systemTable[5]=(void*)eye; systemTable[18]=(void*)role;
  systemTable[37]=(void*)controller; systemTable[39]=(void*)vibrate; compositorTable[2]=(void*)poses;
  void **system=systemTable, **compositor=compositorTable;
  void *backend[256]{};
  const auto base=reinterpret_cast<uintptr_t>(host);
  // Check the actual host instructions for all live and close-up integrations,
  // including the ones a controller-only test cannot exercise in a game scene.
  auto checkCall = [base](HostLayout::Call call) {
    auto *instruction = reinterpret_cast<const unsigned char *>(base + call.site);
    assert(instruction[0] == 0xe8);
    int32_t rel;
    std::memcpy(&rel, instruction + 1, sizeof(rel));
    assert(call.site + 5 + rel == call.target);
  };
  for (auto call : {build.endFrame, build.center, build.restoreHand, build.sleeve,
                    build.autoWrist, build.hover, build.inventory.commit}) checkCall(call);
  for (unsigned site : build.lootSites) checkCall({site, build.lootPreference});
  const auto *gate = reinterpret_cast<const unsigned char *>(base + build.inventory.gate);
  uintptr_t tab;
  std::memcpy(&tab, gate + 6, sizeof(tab));
  assert(std::memcmp(gate, "\x85\xf6\x74\x32\x83\x3d", 6) == 0 && tab == base + build.inventory.tab);
  assert(std::memcmp(gate + 10, "\x03\x7c\x29", 3) == 0);
  auto **table=reinterpret_cast<void **>(base+build.openVR.table);
  backend[0]=table; backend[7]=&system; backend[8]=&compositor;
  auto **xrTable = reinterpret_cast<void **>(base + build.openXR.table);
  if (direct) {
    for (auto **methods : {table, xrTable}) {
      DWORD old = 0, ignored = 0;
      assert(VirtualProtect(methods, 29*sizeof(void *), PAGE_READWRITE, &old));
      for (unsigned slot : {6u, 28u}) {
        auto *entry = static_cast<unsigned char *>(methods[slot]);
        assert(entry[0] == 0xe9);
        int32_t displacement;
        std::memcpy(&displacement, entry + 1, sizeof(displacement));
        methods[slot] = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(entry) + 5 + displacement);
      }
      assert(VirtualProtect(methods, 29*sizeof(void *), old, &ignored));
    }
  }
  if (wrapped || mixed) {
    unsigned i = 0;
    for (auto **methods : {table, xrTable}) {
      if (mixed && i == 0) { ++i; continue; }
      DWORD old = 0, ignored = 0;
      assert(VirtualProtect(methods + 6, sizeof(void *), PAGE_READWRITE, &old));
      earlier[i] = reinterpret_cast<Poll>(methods[6]);
      methods[6] = i == 0 ? reinterpret_cast<void *>(forwardPoll<0>) :
                            reinterpret_cast<void *>(forwardPoll<1>);
      assert(VirtualProtect(methods + 6, sizeof(void *), old, &ignored));
      ++i;
    }
  }
  auto *xrOriginal = xrTable[6];
  assert(InstallHostInput(base,build));
  assert(InstallHostInput(base,build));  // Retrying must not deliver a frame twice.
  assert((xrTable[6] == xrOriginal) == (wrapped || mixed));
  auto poll=reinterpret_cast<Poll>(table[6]);
  if (wrapped || mixed) {
    if (wrapped) assert(table[6] == reinterpret_cast<void *>(forwardPoll<0>));
    auto *call = reinterpret_cast<unsigned char *>(base + build.dispatch + 3);
    assert(call[0] == 0xe8);
    int32_t displacement;
    std::memcpy(&displacement, call + 1, sizeof(displacement));
    poll = reinterpret_cast<Poll>(reinterpret_cast<uintptr_t>(call) + 5 + displacement);
  }
  for(int n=0;n<3;++n) {
    HostFrame frame{};
    if (!poll(backend,&frame) || frame.trigger[0]!=0) return 4;
  }
  if (polls!=3 || pulses!=3 || forwarded!=(wrapped ? 3u : 0u)) return 5;
  std::puts("PASS real TTWVR OpenVR backend: stereo poses, both hands, analog inputs, filtering and haptic dispatch");
  return 0;
}
