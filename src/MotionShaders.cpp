// SPDX-License-Identifier: GPL-3.0-only
#include "MotionShaders.h"
#include "Shared.h"
#include <array>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace locks {
namespace {
using Bytes = std::vector<unsigned char>;
using Digest = std::array<unsigned char,32>;
struct Package { unsigned id; Bytes data; Digest hash; };
bool read(const std::wstring &name, Bytes &bytes) {
  FILE *f = _wfopen(name.c_str(),L"rb");
  if (!f) return false;
  bool ok = !fseek(f,0,SEEK_END);
  long n = ftell(f);
  ok = ok && n>0 && n<=8*1024*1024 && !fseek(f,0,SEEK_SET);
  if (ok) { bytes.resize(n); ok = fread(bytes.data(),1,n,f)==size_t(n); }
  fclose(f);
  return ok;
}
bool digest(const Bytes &bytes,Digest &out) {
  BCRYPT_ALG_HANDLE alg{};
  if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
  const auto status = BCryptHash(alg,nullptr,0,const_cast<PUCHAR>(bytes.data()),
                                 ULONG(bytes.size()),out.data(),out.size());
  BCryptCloseAlgorithmProvider(alg,0);
  return status>=0;
}
unsigned word(const Bytes &bytes,size_t p) {
  unsigned n;
  memcpy(&n,bytes.data()+p,4);
  return n;
}
bool applyDelta(const Bytes &delta,Package &pkg) {
  if (delta.size()<80 || memcmp(delta.data(),"TTWVSD01",8) ||
      memcmp(delta.data()+8,pkg.hash.data(),32)) return false;
  const unsigned size=word(delta,72), count=word(delta,76);
  if (!size || size>8*1024*1024 || count>(delta.size()-80)/8) return false;
  Bytes out;
  out.reserve(size);
  size_t p=80;
  for (unsigned i=0; i<count; ++i) {
    if (delta.size()-p<8) return false;
    unsigned offset=word(delta,p), len=word(delta,p+4);
    p+=8;
    const bool literal=offset==0xffffffffu;
    const auto &source=literal ? delta : pkg.data;
    if (literal) offset=unsigned(p);
    if (offset>source.size() || len>source.size()-offset || len>size-out.size()) return false;
    out.insert(out.end(),source.begin()+offset,source.begin()+offset+len);
    if (literal) p+=len;
  }
  Digest result{};
  if (p!=delta.size() || out.size()!=size || !digest(out,result) ||
      memcmp(result.data(),delta.data()+40,32)) return false;
  pkg.data.swap(out);
  pkg.hash=result;
  return true;
}
bool cache(const std::wstring &path,const Package &pkg) {
  Bytes previous;
  Digest hash{};
  if (read(path,previous) && digest(previous,hash) && hash==pkg.hash) return true;
  auto tmp=path+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp";
  FILE *file=_wfopen(tmp.c_str(),L"wb");
  if (!file) return false;
  bool ok=fwrite(pkg.data.data(),1,pkg.data.size(),file)==pkg.data.size();
  ok=(fclose(file)==0) && ok;
  if (ok && MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) return true;
  DeleteFileW(tmp.c_str());
  return false;
}
bool mkdir(const std::wstring &p) {
  return CreateDirectoryW(p.c_str(),nullptr) || GetLastError()==ERROR_ALREADY_EXISTS;
}
}
void InitMotionShaders() {
  // FNV 1.4.0.525's shader-package filename format. Redirect only after all
  // visible packages are available, including unmodified profile overrides.
  static constexpr char original[]="Data\\Shaders\\shaderpackage%03i.sdp";
  auto format=reinterpret_cast<char *>(uintptr_t(GetModuleHandleW(nullptr))+0xcaf68c);
  if (!Readable(format,sizeof(original)) || memcmp(format,original,sizeof(original))) return;
  WIN32_FIND_DATAW entry{};
  HANDLE found=FindFirstFileW(L"Data\\Shaders\\shaderpackage???.sdp",&entry);
  if (found==INVALID_HANDLE_VALUE) return;
  std::vector<Package> files;
  unsigned edited=0;
  bool ok=true;
  do {
    std::wstring name=entry.cFileName;
    if (name.size()!=20 || name.substr(13,3).find_first_not_of(L"0123456789")!=std::wstring::npos) continue;
    Package pkg{};
    pkg.id=std::wcstoul(name.c_str()+13,nullptr,10);
    if (!read(L"Data\\Shaders\\"+name,pkg.data) || !digest(pkg.data,pkg.hash)) { ok=false; break; }
    Bytes delta;
    if (read(folder+L"\\TTWVRPhysicalLockpicking\\motion\\"+name+L".delta",delta) && applyDelta(delta,pkg)) ++edited;
    files.push_back(std::move(pkg));
  } while (FindNextFileW(found,&entry));
  ok = ok && GetLastError()==ERROR_NO_MORE_FILES;
  FindClose(found);
  if (!ok || !edited) return;
  std::sort(files.begin(),files.end(),[](const Package &a,const Package &b){return a.id<b.id;});
  Bytes keys;
  for (const auto &pkg:files) {
    keys.push_back(static_cast<unsigned char>(pkg.id));
    keys.push_back(static_cast<unsigned char>(pkg.id>>8));
    keys.insert(keys.end(),pkg.hash.begin(),pkg.hash.end());
  }
  Digest set{};
  if (!digest(keys,set)) return;
  char replacement[sizeof(original)]{};
  wchar_t dir[48]{};
  // This relative filename fits the game's buffer and remains valid under mod managers.
  snprintf(replacement,sizeof(replacement),"Data\\LPVR\\%02x%02x%02x%02x%02x%02x\\s%%03i.sdp",
           set[0],set[1],set[2],set[3],set[4],set[5]);
  swprintf(dir,48,L"Data\\LPVR\\%02x%02x%02x%02x%02x%02x",
           set[0],set[1],set[2],set[3],set[4],set[5]);
  if (!mkdir(L"Data\\LPVR") || !mkdir(dir)) return;
  for (const auto &pkg:files) {
    wchar_t leaf[16]{};
    swprintf(leaf,16,L"\\s%03u.sdp",pkg.id);
    if (!cache(std::wstring(dir)+leaf,pkg)) return;
  }
  DWORD old;
  if (!VirtualProtect(format,sizeof(original),PAGE_READWRITE,&old)) return;
  memcpy(format,replacement,sizeof(original));
  DWORD unused;
  VirtualProtect(format,sizeof(original),old,&unused);
  Log("motion_shader_cache packages=%u patched=%u path=%s",unsigned(files.size()),edited,replacement);
}
}
