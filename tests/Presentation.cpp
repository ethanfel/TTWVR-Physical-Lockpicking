// SPDX-License-Identifier: GPL-3.0-only
#include "ToolGeometry.h"
#include <cassert>
using namespace locks;
int main() {
  for (float side : {-1.f, 1.f}) {
    Vec anchor{side * .03872f, -.03996f, -.02932f};
    Pose tracked{axisAngle({0, 1, 0}, .8f), {.1f, .2f, .3f}};
    Vec contact = world(tracked, anchor + Vec{0, .010f, 0});
    auto shown = presentPinHand(tracked, contact, anchor, .035f, 20);
    assert(length(world(shown, anchor) - contact) < .00001f);
    assert(length(shown.p - tracked.p) <= .03501f);
    // A visual correction is limited and cannot change its input pose.
    assert(tracked.p.x == .1f && tracked.p.y == .2f && tracked.p.z == .3f);
    auto limited =
        presentPinHand(tracked, contact + Vec{1, 2, 3}, anchor, .01f, 90);
    assert(length(limited.p - tracked.p) <= .01001f);
    Quat delta = unit(multiply(inverse(tracked.q), limited.q));
    assert(std::abs(2 * std::acos(delta.w) * 180 / pi - 20) < .001f);
    auto disabled = presentPinHand(tracked, contact, anchor, 0, 0);
    assert(length(disabled.p - tracked.p) == 0);
    assert(length(rotate(disabled.q, {0, 0, 1}) -
                  rotate(tracked.q, {0, 0, 1})) < .00001f);
  }
}
