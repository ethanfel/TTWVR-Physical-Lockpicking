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
  frame.trigger[0]=0;
  pulse(backend,true,.5f,.02f);
  ++polls;
}
}
int wmain(int argc,wchar_t **argv) {
  using namespace locks;
  if (argc!=3 || (wcscmp(argv[1],L"116") && wcscmp(argv[1],L"117"))) return 2;
  bool newer=wcscmp(argv[1],L"117")==0;
  HMODULE host=LoadLibraryW(argv[2]);
  if (!host) { std::printf("LoadLibrary failed %lu\n",GetLastError()); return 3; }
  void *systemTable[40]{}, *compositorTable[3]{};
  systemTable[2]=(void*)projection; systemTable[5]=(void*)eye; systemTable[18]=(void*)role;
  systemTable[37]=(void*)controller; systemTable[39]=(void*)vibrate; compositorTable[2]=(void*)poses;
  void **system=systemTable, **compositor=compositorTable;
  void *backend[256]{};
  const auto base=reinterpret_cast<uintptr_t>(host);
  auto **table=reinterpret_cast<void **>(base+(newer ? 0xaf36d0 : 0xad7e40));
  backend[0]=table; backend[7]=&system; backend[8]=&compositor;
  InstallHostInput(base,newer);
  using Poll=bool (__thiscall *)(void*,HostFrame*);
  auto poll=reinterpret_cast<Poll>(table[6]);
  for(int n=0;n<3;++n) {
    HostFrame frame{};
    if (!poll(backend,&frame) || frame.trigger[0]!=0) return 4;
  }
  if (polls!=3 || pulses!=3) return 5;
  std::puts("PASS real TTWVR OpenVR backend: stereo poses, both hands, analog inputs, filtering and haptic dispatch");
  return 0;
}
