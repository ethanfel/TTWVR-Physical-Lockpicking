// SPDX-License-Identifier: GPL-3.0-only
#include "Props.h"
#include <cassert>
#include <limits>
using namespace locks;
static std::vector<unsigned char> good() {
  std::vector<unsigned char> bytes;
  auto add = [&](const void *p, size_t n) {
    const auto *b = static_cast<const unsigned char *>(p);
    bytes.insert(bytes.end(), b, b + n);
  };
  add("TTWProp5", 8);
  uint32_t count = PropCount;
  add(&count, 4);
  for (unsigned i = 0; i < PropCount; i++) {
    uint32_t header[]{3, 1, i % 3};
    add(header, sizeof(header));
    PropVertex v[]{{{0, 0, 0}, {0, 0, 1}, 0, 0},
                   {{.1f, 0, 0}, {0, 0, 1}, 1, 0},
                   {{0, .1f, 0}, {0, 0, 1}, 0, 1}};
    uint32_t t[]{0, 1, 2};
    add(v, sizeof(v));
    add(t, sizeof(t));
  }
  return bytes;
}
static bool parse(const std::vector<unsigned char> &bytes, Props &out) {
  FILE *f = tmpfile();
  assert(f);
  assert(fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
  rewind(f);
  bool ok = readProps(f, out);
  fclose(f);
  return ok;
}
int main() {
  auto bytes = good();
  Props out;
  assert(parse(bytes, out));
  assert(out[DriverCan].vertices.size() == 3);
  // A failed load must leave the last valid asset intact.
  auto reject = [&](std::vector<unsigned char> b) {
    assert(!parse(b, out));
    assert(out[DriverCan].vertices.size() == 3);
    assert(out[Frame].vertices[1].p.x == .1f);
  };
  for (size_t n : {size_t(0), size_t(8), size_t(23), bytes.size() - 1})
    reject({bytes.begin(), bytes.begin() + n});
  auto b = bytes;
  b[7] = '3';
  reject(b);
  b = bytes;
  b[0] = 'X';
  reject(b);
  b = bytes;
  b[8] = 6;
  reject(b);
  b = bytes;
  b[20] = 3;
  reject(b); // invalid material
  b = bytes;
  b[12] = 255;
  b[13] = 255;
  reject(b); // oversized count
  b = bytes;
  b[24 + 96] = 3;
  reject(b); // triangle index beyond vertex array
  b = bytes;
  float bad = std::numeric_limits<float>::quiet_NaN();
  memcpy(b.data() + 24, &bad, 4);
  reject(b);
  b = bytes;
  bad = 2;
  memcpy(b.data() + 24, &bad, 4);
  reject(b); // absurd geometry
  b = bytes;
  bad = 0;
  memcpy(b.data() + 24 + 20, &bad, 4);
  reject(b); // zero normal
  b = bytes;
  b.push_back(0);
  reject(b);
  assert(!readProps(nullptr, out));
}
