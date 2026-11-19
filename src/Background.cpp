// SPDX-License-Identifier: GPL-3.0-only
#include "Background.h"
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
} // namespace
Background::~Background() {
  drop(vs);
  drop(blur);
  drop(compose);
  drop(constants);
  drop(sampler);
  for (int n = 0; n < 2; n++) {
    drop(target[n]);
    drop(filtered[n]);
    drop(temporary[n]);
  }
}
void ReleasedImage::Clear() {
  frame = 0;
  for (auto *v : slices) v->Release();
  for (auto *t : textures) t->Release();
  slices.clear();
  textures.clear();
}
bool Background::Init(ID3D11Device *d) {
  const char *shader = R"(
Texture2D image:register(t0); SamplerState linearClamp:register(s0);
cbuffer Params:register(b0) {
 float4 rect, sourceTan, viewTan, rightRay, upRay, backRay, options, direction;
};
struct V { float4 p:SV_Position;float2 uv:TEXCOORD0; };
V vs(uint id:SV_VertexID) {
 V v;v.uv=float2((id<<1)&2,id&2);v.p=float4(v.uv*float2(2,-2)+float2(-1,1),0,1);return v;
}
float4 blur(V v):SV_Target {
 float3 color=0;float total=0;
 for(int n=-4;n<=4;n++) {
  float w=exp(-.5*n*n/(1.8*1.8));
  float2 uv=rect.xy+clamp(v.uv+float(n)*direction.xy/rect.zw,
                         options.xy/rect.zw*.5,1-options.xy/rect.zw*.5)*rect.zw;
  color+=image.Sample(linearClamp,uv).rgb*w;total+=w;
 }
 return float4(color/total,1);
}
float4 compose(V v):SV_Target {
 float3 ray=float3(lerp(viewTan.x,viewTan.y,v.uv.x),lerp(viewTan.w,viewTan.z,v.uv.y),-1);
 ray=rightRay.xyz*ray.x+upRay.xyz*ray.y+backRay.xyz*ray.z;
 float2 p=ray.xy/max(-ray.z,.0001);
 float2 uv=float2((p.x-sourceTan.x)/(sourceTan.y-sourceTan.x),
                 (sourceTan.w-p.y)/(sourceTan.w-sourceTan.z));
 float valid=ray.z<0 && all(uv>=0) && all(uv<=1);
 return float4(image.Sample(linearClamp,uv).rgb*options.w*valid,1);
}
)";
  auto compile = [&](const char *entry, const char *profile, ID3DBlob **b) {
    ID3DBlob *error = nullptr;
    auto h = D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr,
                        entry, profile, 0, 0, b, &error);
    if (FAILED(h))
      Log("background_shader entry=%s error=%s", entry,
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
  if (FAILED(h))
    return false;
  if (!compile("blur", "ps_5_0", &b))
    return false;
  h = d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr,
                           &blur);
  drop(b);
  if (FAILED(h))
    return false;
  if (!compile("compose", "ps_5_0", &b))
    return false;
  h = d->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr,
                           &compose);
  drop(b);
  if (FAILED(h))
    return false;
  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = 8 * 16;
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
  D3D11_TEXTURE2D_DESC td{};
  td.Width = td.Height = 512;
  td.ArraySize = td.MipLevels = td.SampleDesc.Count = 1;
  td.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  for (int n = 0; n < 2; n++)
    if (FAILED(d->CreateTexture2D(&td, nullptr, &temporary[n])) ||
        FAILED(d->CreateRenderTargetView(temporary[n], nullptr, &target[n])) ||
        FAILED(
            d->CreateShaderResourceView(temporary[n], nullptr, &filtered[n])))
      return false;
  return true;
}

bool ReleasedImage::Save(ID3D11Device *device, ID3D11Texture2D *image, unsigned stamp) {
  frame = 0;
  if (!device || !image)
    return false;
  D3D11_TEXTURE2D_DESC d{};
  image->GetDesc(&d);
  if (d.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
      d.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
      d.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
      d.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
      d.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
    return false;
  if (textures.empty()) {
    auto owned = d;
    owned.SampleDesc = {1, 0};
    owned.ArraySize = owned.MipLevels = 1;
    owned.Usage = D3D11_USAGE_DEFAULT;
    owned.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    owned.CPUAccessFlags = owned.MiscFlags = 0;
    for (unsigned n = 0; n < d.ArraySize; n++) {
      ID3D11Texture2D *texture = nullptr;
      ID3D11ShaderResourceView *srv = nullptr;
      if (FAILED(device->CreateTexture2D(&owned, nullptr, &texture))) {
        Clear();
        return false;
      }
      textures.push_back(texture);
      if (FAILED(device->CreateShaderResourceView(texture, nullptr, &srv))) {
        Clear();
        return false;
      }
      slices.push_back(srv);
    }
  }
  ID3D11DeviceContext *immediate = nullptr;
  device->GetImmediateContext(&immediate);
  for (unsigned n = 0; n < d.ArraySize; n++) {
    unsigned sub = D3D11CalcSubresource(0, n, d.MipLevels);
    if (d.SampleDesc.Count > 1)
      immediate->ResolveSubresource(textures[n], 0, image, sub, d.Format);
    else
      immediate->CopySubresourceRegion(textures[n], 0, 0, 0, 0, image, sub,
                                       nullptr);
  }
  immediate->Release();
  frame = stamp;
  return true;
}
BackgroundView ReleasedImage::View(unsigned slice, unsigned stamp) const {
  BackgroundView v;
  if (!stamp || frame != stamp || slice >= slices.size())
    return v;
  D3D11_TEXTURE2D_DESC d{};
  textures[slice]->GetDesc(&d);
  v.image = slices[slice];
  v.width = d.Width;
  v.height = d.Height;
  return v;
}
bool Background::Draw(ID3D11DeviceContext *ctx, ID3D11RenderTargetView *rtv,
                      const BackgroundView &s, XrFovf f, unsigned width, unsigned height,
                      float radius, float brightness) {
  if (!filtered[1] || !s.image || !s.width || !s.height)
    return false;
  float data[8][4]{};
  data[0][0] = float(s.rect.offset.x) / s.width;
  data[0][1] = float(s.rect.offset.y) / s.height;
  data[0][2] = float(s.rect.extent.width) / s.width;
  data[0][3] = float(s.rect.extent.height) / s.height;
  auto tangent = [](float *p, XrFovf f) {
    p[0] = tan(f.angleLeft);
    p[1] = tan(f.angleRight);
    p[2] = tan(f.angleDown);
    p[3] = tan(f.angleUp);
  };
  tangent(data[1], s.fov);
  tangent(data[2], f);
  Vec axes[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (int n = 0; n < 3; n++) {
    Vec v = rotate(s.rotation, axes[n]);
    data[n + 3][0] = v.x;
    data[n + 3][1] = v.y;
    data[n + 3][2] = v.z;
  }
  data[6][0] = 1.f / s.width;
  data[6][1] = 1.f / s.height;
  data[6][2] = radius;
  data[6][3] = brightness;
  data[7][0] = radius * .5f / s.width;
  auto upload = [&]() {
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
      return false;
    memcpy(map.pData, data, sizeof(data));
    ctx->Unmap(constants, 0);
    return true;
  };
  if (!upload())
    return false;
  ctx->IASetInputLayout(nullptr);
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->VSSetShader(vs, nullptr, 0);
  ctx->PSSetConstantBuffers(0, 1, &constants);
  ctx->PSSetSamplers(0, 1, &sampler);
  ctx->OMSetRenderTargets(1, &target[0], nullptr);
  D3D11_VIEWPORT viewport{0, 0, 512, 512, 0, 1};
  ctx->RSSetViewports(1, &viewport);
  ctx->PSSetShader(blur, nullptr, 0);
  ctx->PSSetShaderResources(0, 1, &s.image);
  ctx->Draw(3, 0);
  // Second separable pass at the small working resolution; no sparse 2D grid.
  data[0][0] = data[0][1] = 0;
  data[0][2] = data[0][3] = 1;
  data[6][0] = data[6][1] = 1.f / 512;
  data[7][0] = 0;
  data[7][1] = radius * .5f / s.rect.extent.height;
  if (!upload())
    return false;
  ctx->OMSetRenderTargets(1, &target[1], nullptr);
  ctx->PSSetShaderResources(0, 1, &filtered[0]);
  ctx->Draw(3, 0);
  ctx->OMSetRenderTargets(1, &rtv, nullptr);
  viewport.Width = float(width); viewport.Height = float(height);
  ctx->RSSetViewports(1, &viewport);
  ctx->PSSetShader(compose, nullptr, 0);
  ctx->PSSetShaderResources(0, 1, &filtered[1]);
  ctx->Draw(3, 0);
  ID3D11ShaderResourceView *empty = nullptr;
  ctx->PSSetShaderResources(0, 1, &empty);
  return true;
}
} // namespace locks
