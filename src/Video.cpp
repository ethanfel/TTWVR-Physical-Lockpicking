// SPDX-License-Identifier: GPL-3.0-only
#include "Video.h"
#include "Shared.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
namespace locks {
void VideoCapture::Stop() {
  if (header) {
    InterlockedIncrement(&header->sequence);
    header->active = 0;
    InterlockedIncrement(&header->sequence);
    UnmapViewOfFile(header);
    header = nullptr;
    Log("video_stop frames=%llu dropped=%llu", frames, dropped);
  }
  if (mapping) {
    CloseHandle(mapping);
    mapping = nullptr;
  }
  if (file != INVALID_HANDLE_VALUE) {
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
  }
  for (auto &slot : slots) {
    for (auto &image : slot.images)
      if (image) {
        image->Release();
        image = nullptr;
      }
    slot = {};
  }
  writing = -1;
  token.clear();
}
bool VideoCapture::Start(const std::string &id, unsigned fps) {
  Stop();
  auto path = folder + L"\\lockpick-video.bin";
  file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                     nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  DWORD bytes = sizeof(VideoHeader) + eyeSize * eyeSize * 8;
  mapping =
      CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, bytes, nullptr);
  if (!mapping) {
    Stop();
    return false;
  }
  header = static_cast<VideoHeader *>(
      MapViewOfFile(mapping, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, bytes));
  if (!header) {
    Stop();
    return false;
  }
  memset(header, 0, bytes);
  header->width = eyeSize * 2;
  header->height = eyeSize;
  header->format = 1;
  header->fps = fps;
  header->active = 1;
  memcpy(header->token, id.data(), 32);
  memcpy(header->magic, "TTWVID1", 8);
  token = id;
  rate = fps;
  nextFrame = 0;
  frames = dropped = 0;
  Log("video_start stereo=%ux%u fps=%u async_slots=3", eyeSize * 2, eyeSize,
      rate);
  return true;
}
void VideoCapture::Tick() {
  uint64_t now = GetTickCount64();
  if (now - lastPoll >= 250) {
    lastPoll = now;
    auto path = folder + L"\\lockpick-video.request";
    WIN32_FILE_ATTRIBUTE_DATA data{};
    FILETIME time;
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER current{}, modified{};
    current.LowPart = time.dwLowDateTime;
    current.HighPart = time.dwHighDateTime;
    bool exists =
        GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data);
    modified.LowPart = data.ftLastWriteTime.dwLowDateTime;
    modified.HighPart = data.ftLastWriteTime.dwHighDateTime;
    if (!exists || (current.QuadPart > modified.QuadPart &&
                    current.QuadPart - modified.QuadPart > 50000000ULL)) {
      if (header)
        Stop();
    } else if (auto *f = _wfopen(path.c_str(), L"rb")) {
      char id[33]{};
      unsigned fps = 0;
      bool ok = fscanf(f, "%32s %u", id, &fps) == 2;
      fclose(f);
      ok = ok && strlen(id) == 32 && fps >= 10 && fps <= 30 &&
           std::all_of(id, id + 32, [](char c) {
             return std::isxdigit(static_cast<unsigned char>(c));
           });
      if (ok && token != id && refused != id && !Start(id, fps)) {
        refused = id;
        Log("video_error start_failed=1 win32=%lu", GetLastError());
      }
    }
  }
  if (header)
    Drain();
}
void VideoCapture::Drain() {
  Slot *oldest = nullptr;
  for (auto &slot : slots)
    if (slot.pending && (!oldest || slot.stamp < oldest->stamp))
      oldest = &slot;
  if (!oldest)
    return;
  D3D11_MAPPED_SUBRESOURCE maps[2]{};
  HRESULT a = context->Map(oldest->images[0], 0, D3D11_MAP_READ,
                           D3D11_MAP_FLAG_DO_NOT_WAIT, &maps[0]);
  if (a == DXGI_ERROR_WAS_STILL_DRAWING)
    return;
  if (FAILED(a)) {
    Log("video_error map_left=%lx", a);
    refused = token;
    Stop();
    return;
  }
  HRESULT b = context->Map(oldest->images[1], 0, D3D11_MAP_READ,
                           D3D11_MAP_FLAG_DO_NOT_WAIT, &maps[1]);
  if (FAILED(b)) {
    context->Unmap(oldest->images[0], 0);
    if (b != DXGI_ERROR_WAS_STILL_DRAWING) {
      Log("video_error map_right=%lx", b);
      refused = token;
      Stop();
    }
    return;
  }
  InterlockedIncrement(&header->sequence);
  auto *pixels = reinterpret_cast<unsigned char *>(header + 1);
  for (unsigned y = 0; y < eyeSize; y++)
    for (unsigned eye = 0; eye < 2; eye++)
      memcpy(pixels + (y * eyeSize * 2 + eye * eyeSize) * 4,
             static_cast<unsigned char *>(maps[eye].pData) +
                 y * maps[eye].RowPitch,
             eyeSize * 4);
  header->timestampMs = oldest->stamp;
  header->frame = ++frames;
  header->source = oldest->source;
  InterlockedIncrement(&header->sequence);
  for (unsigned eye = 0; eye < 2; eye++)
    context->Unmap(oldest->images[eye], 0);
  oldest->pending = false;
  oldest->mask = 0;
}
void VideoCapture::Begin() {
  writing = -1;
  if (!header)
    return;
  uint64_t now = GetTickCount64();
  if (double(now) < nextFrame)
    return;
  double interval = 1000.0 / rate;
  nextFrame = std::max(nextFrame + interval, double(now));
  for (int n = 0; n < 3; n++)
    if (!slots[n].pending) {
      writing = n;
      slots[n].mask = 0;
      slots[n].stamp = now;
      return;
    }
  ++dropped; // Encoder/consumer and GPU readiness never block the render loop.
}
void VideoCapture::Copy(unsigned eye, ID3D11Texture2D *source) {
  if (writing < 0 || eye > 1)
    return;
  auto &slot = slots[writing];
  if (!slot.images[eye]) {
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (desc.Width != eyeSize || desc.Height != eyeSize ||
        desc.SampleDesc.Count != 1 ||
        (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
         desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) {
      Log("video_error unsupported_texture=1");
      refused = token;
      Stop();
      return;
    }
    desc.BindFlags = desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &slot.images[eye]))) {
      Log("video_error staging_allocation=1");
      refused = token;
      Stop();
      return;
    }
  }
  context->CopyResource(slot.images[eye], source);
  slot.mask |= 1u << eye;
}
void VideoCapture::End(bool presented, unsigned source) {
  if (writing < 0)
    return;
  auto &slot = slots[writing];
  slot.pending = presented && slot.mask == 3;
  slot.source = source;
  if (!slot.pending)
    ++dropped;
  writing = -1;
}
} // namespace locks
