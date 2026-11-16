// SPDX-License-Identifier: GPL-3.0-only
#include "GameCapture.h"
#include "Shared.h"
#include <cstring>
#include <d3dcompiler.h>
namespace locks {
namespace {
template <class T> void drop(T *&p) {
  if (p)
    p->Release();
  p = nullptr;
}
struct Params {
  float rect[4], source[4], view[4], axes[3][4], options[4], corners[4][4];
};
} // namespace
void GameCapture::Reset() {
  if (size)
    Log("capture_game_resources released=1");
  drop(vs);
  drop(ps);
  drop(constants);
  drop(sampler);
  drop(blend);
  drop(raster);
  drop(deferred);
  for (int n = 0; n < 2; n++) {
    drop(targets[n]);
    drop(images[n]);
  }
  size = 0;
}
bool GameCapture::Init(ID3D11Device *d, unsigned eyeSize) {
  Reset();
  const char *shader = R"(
Texture2D image:register(t0); SamplerState linearClamp:register(s0);
cbuffer Params:register(b0) {
 float4 rect, sourceTan, viewTan, axes[3], options, corners[4];
};
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
V vs(uint id:SV_VertexID) {
 V v;
 if(options.x>0) {
  uint index[6]={0,1,2,2,1,3}; uint n=index[id];
  v.p=corners[n];v.uv=float2(n&1,n>>1);
 } else {
  v.uv=float2((id<<1)&2,id&2);v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);
 }
 return v;
}
float4 ps(V v):SV_Target {
 float2 uv=v.uv;
 if(options.x==0) {
  float3 ray=float3(lerp(viewTan.x,viewTan.y,uv.x),lerp(viewTan.w,viewTan.z,uv.y),-1);
  ray=axes[0].xyz*ray.x+axes[1].xyz*ray.y+axes[2].xyz*ray.z;
  clip(-ray.z-.0001);
  float2 p=ray.xy/(-ray.z);
  uv=float2((p.x-sourceTan.x)/(sourceTan.y-sourceTan.x),(sourceTan.w-p.y)/(sourceTan.w-sourceTan.z));
  clip(min(uv.x,min(uv.y,min(1-uv.x,1-uv.y))));
 }
 float2 halfTexel;uint w,h;image.GetDimensions(w,h);halfTexel=.5/float2(w,h);
 float2 lo=rect.xy+halfTexel,hi=rect.xy+rect.zw-halfTexel;
 float4 c=image.Sample(linearClamp,clamp(rect.xy+uv*rect.zw,lo,hi));
 if(options.y==0)c.a=1;
 else if(options.z>0)c.rgb*=c.a;
 return c;
}
)";
  auto compile = [&](const char *entry, const char *profile, ID3DBlob **blob) {
    ID3DBlob *error = nullptr;
    auto h = D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr,
                        entry, profile, 0, 0, blob, &error);
    if (FAILED(h))
      Log("capture_shader entry=%s error=%s", entry,
          error ? (char *)error->GetBufferPointer() : "unknown");
    drop(error);
    return SUCCEEDED(h);
  };
  ID3DBlob *b = nullptr;
  if (!compile("vs", "vs_5_0", &b))
    return false;
  auto h = d->CreateVertexShader(b->GetBufferPointer(), b->GetBufferSize(),
                                 nullptr, &vs);
  drop(b);
  if (FAILED(h) || !compile("ps", "ps_5_0", &b))
    return false;
  h = d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr,
                           &ps);
  drop(b);
  if (FAILED(h) || FAILED(d->CreateDeferredContext(0, &deferred)))
    return false;
  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = sizeof(Params);
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(d->CreateBuffer(&bd, nullptr, &constants)))
    return false;
  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  if (FAILED(d->CreateSamplerState(&sd, &sampler)))
    return false;
  D3D11_BLEND_DESC blendDesc{};
  auto &r = blendDesc.RenderTarget[0];
  r.BlendEnable = TRUE;
  r.SrcBlend = r.SrcBlendAlpha = D3D11_BLEND_ONE;
  r.DestBlend = r.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
  r.BlendOp = r.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  r.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
  if (FAILED(d->CreateBlendState(&blendDesc, &blend)))
    return false;
  D3D11_RASTERIZER_DESC rd{};
  rd.FillMode = D3D11_FILL_SOLID;
  rd.CullMode = D3D11_CULL_BACK;
  rd.DepthClipEnable = TRUE;
  if (FAILED(d->CreateRasterizerState(&rd, &raster)))
    return false;
  D3D11_TEXTURE2D_DESC td{};
  td.Width = td.Height = eyeSize;
  td.ArraySize = td.MipLevels = td.SampleDesc.Count = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
  td.BindFlags = D3D11_BIND_RENDER_TARGET;
  for (int n = 0; n < 2; n++)
    if (FAILED(d->CreateTexture2D(&td, nullptr, &images[n])) ||
        FAILED(d->CreateRenderTargetView(images[n], nullptr, &targets[n])))
      return false;
  size = eyeSize;
  return true;
}
ID3D11Texture2D *GameCapture::Draw(ID3D11Device *device,
                                   ID3D11DeviceContext *immediate, unsigned eye,
                                   unsigned eyeSize, XrFovf view,
                                   const std::vector<CaptureLayer> &layers) {
  if (eye > 1 || layers.empty())
    return nullptr;
  if (size != eyeSize && !Init(device, eyeSize)) {
    Reset();
    return nullptr;
  }
  deferred->ClearState();
  float clear[4]{0, 0, 0, 1};
  deferred->ClearRenderTargetView(targets[eye], clear);
  deferred->OMSetRenderTargets(1, &targets[eye], nullptr);
  deferred->OMSetBlendState(blend, nullptr, 0xffffffff);
  deferred->RSSetState(raster);
  D3D11_VIEWPORT vp{0, 0, float(size), float(size), 0, 1};
  deferred->RSSetViewports(1, &vp);
  deferred->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  deferred->VSSetShader(vs, nullptr, 0);
  deferred->PSSetShader(ps, nullptr, 0);
  deferred->VSSetConstantBuffers(0, 1, &constants);
  deferred->PSSetConstantBuffers(0, 1, &constants);
  deferred->PSSetSamplers(0, 1, &sampler);
  auto tangent = [](float *p, XrFovf f) {
    p[0] = tan(f.angleLeft);
    p[1] = tan(f.angleRight);
    p[2] = tan(f.angleDown);
    p[3] = tan(f.angleUp);
  };
  for (const auto &layer : layers) {
    const auto &s = layer.image;
    Params data{};
    data.rect[0] = float(s.rect.offset.x) / s.width;
    data.rect[1] = float(s.rect.offset.y) / s.height;
    data.rect[2] = float(s.rect.extent.width) / s.width;
    data.rect[3] = float(s.rect.extent.height) / s.height;
    tangent(data.source, s.fov);
    tangent(data.view, view);
    Vec axes[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int n = 0; n < 3; n++) {
      auto v = rotate(s.rotation, axes[n]);
      data.axes[n][0] = v.x;
      data.axes[n][1] = v.y;
      data.axes[n][2] = v.z;
    }
    data.options[0] = layer.quad;
    data.options[1] =
        bool(layer.flags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT);
    data.options[2] =
        bool(layer.flags & XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT);
    memcpy(data.corners, layer.corners.data(), sizeof(data.corners));
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(deferred->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
      return nullptr;
    memcpy(map.pData, &data, sizeof(data));
    deferred->Unmap(constants, 0);
    deferred->PSSetShaderResources(0, 1, &s.image);
    deferred->Draw(layer.quad ? 6 : 3, 0);
  }
  ID3D11CommandList *commands = nullptr;
  if (FAILED(deferred->FinishCommandList(FALSE, &commands)))
    return nullptr;
  immediate->ExecuteCommandList(commands, TRUE);
  drop(commands);
  return images[eye];
}
} // namespace locks
