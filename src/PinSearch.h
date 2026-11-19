// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cmath>
namespace locks {
inline float pinSearchSpan(float span) {
  return std::isfinite(span) ? std::clamp(span, 180.f, 270.f) : 270.f;
}
inline float pinSearchMin(float span) { return 90 - pinSearchSpan(span) * .5f; }
inline float pinSearchMax(float span) { return 90 + pinSearchSpan(span) * .5f; }
inline float boundPinSearch(float angle, float span, float previous = 90) {
  if (!std::isfinite(angle))
    return previous;
  // Unwrap beside the current angle so a fast movement across the excluded
  // lower sector hits its near edge instead of teleporting to the other edge.
  float unwrapped = previous + std::remainder(angle - previous, 360.f);
  return std::clamp(unwrapped, pinSearchMin(span), pinSearchMax(span));
}
inline float nativePinSearch(float physical, float span) {
  return std::clamp((physical - pinSearchMin(span)) * 180 / pinSearchSpan(span),
                    0.f, 180.f);
}
inline float physicalPinSearch(float native, float span) {
  return pinSearchMin(span) +
         std::clamp(native, 0.f, 180.f) * pinSearchSpan(span) / 180;
}
} // namespace locks
