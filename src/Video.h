// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <d3d11.h>
#include <string>
namespace locks {
// Shared file protocol consumed by the native Python recorder. All fields are
// little-endian; sequence is odd during publication, even when readable.
struct alignas(8) VideoHeader {
  char magic[8];
  uint32_t width, height, format, fps;
  volatile LONG sequence;
  uint32_t active;
  uint64_t timestampMs, frame;
  char token[32];
  uint32_t source; // 0: old producer, 1: game, 2: physical lockpicking
  char reserved[44];
};
static_assert(sizeof(VideoHeader) == 128);
class VideoCapture {
  struct Slot {
    ID3D11Texture2D *images[2]{};
    uint64_t stamp = 0;
    unsigned mask = 0;
    bool pending = false;
    unsigned source = 0;
  } slots[3];
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
  VideoHeader *header = nullptr;
  std::string token, refused;
  uint64_t lastPoll = 0, frames = 0, dropped = 0;
  double nextFrame = 0;
  unsigned eyeSize = 0, rate = 30;
  int writing = -1;
  bool Start(const std::string &id, unsigned fps);
  void Drain();

public:
  void Init(ID3D11Device *d, ID3D11DeviceContext *c, unsigned size) {
    device = d;
    context = c;
    eyeSize = size;
  }
  void Tick();
  void Begin();
  void Copy(unsigned eye, ID3D11Texture2D *source);
  bool Active() const { return header != nullptr; }
  bool WantsFrame() const { return writing >= 0; }
  void End(bool presented, unsigned source = 2);
  void Stop();
};
} // namespace locks
