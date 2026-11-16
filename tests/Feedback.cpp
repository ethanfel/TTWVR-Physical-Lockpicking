// SPDX-License-Identifier: GPL-3.0-only
#include "Feedback.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace locks;
static bool quiet(const FeedbackPlan &p) {
  return p.pulses[0].cue == Cue::None && p.pulses[1].cue == Cue::None;
}
int main() {
  {
    HapticFeedback prep;
    auto p = prep.update(1000, 2, 1, 0, 0, -1, 0, 3, 0, 3);
    assert(p.pulses[0].cue == Cue::Prepare && p.pulses[1].cue == Cue::Prepare);
    p = prep.update(1010, 2, 1, 0, 0, -1, 0, 3);
    assert(p.stop == 0 && p.pulses[1].cue == Cue::None);
    p = prep.update(1020, 2, 1, 0, 0, -1, 0, 1);
    assert(p.stop == 2);
  }

  {
    HapticFeedback insertion;
    auto p = insertion.update(1000, 2, 1, 0, 0, 1, 0, 3, 3);
    assert(p.pulses[0].cue == Cue::Insert && p.pulses[1].cue == Cue::Insert);
    p = insertion.update(1010, 2, 1, 0, 0, 1, 0, 3);
    assert(p.stop == 0 && p.pulses[0].cue == Cue::None);
    p = insertion.update(1020, 2, 1, 0, 0, -1, 0, 1);
    assert(p.stop == 2);
    p = insertion.update(1040, 4, 1, 10, 20, 1, 0, 3, 2);
    assert(p.pulses[1].cue == Cue::Contact);
  }

  for (int driver : {0, 1}) {
    int pick = 1 - driver;
    HapticFeedback h;
    assert(quiet(h.update(1000, 2, 1, 0, 0, driver, pick, 3)));
    assert(quiet(h.update(1100, 2, .1f, 0, 0, driver, pick, 3)));
    auto turn = h.update(1200, 3, 1, 0, 25, driver, pick, 3);
    assert(turn.pulses[driver].cue == Cue::Turn);
    assert(turn.pulses[driver].amplitude == .45f &&
           turn.pulses[driver].durationMs == 28);
    assert(quiet(h.update(1205, 3, 1, 2, 25, driver, pick, 3)));
    auto held = h.update(1206, 3, 1, 25, 25, driver, pick, 3);
    assert(quiet(held) && held.stop == (1u << driver));
    // The native stop itself must be felt: no extra wrist movement or damage.
    auto contact = h.update(1210, 4, 1, 25, 25, driver, pick, 3);
    assert(contact.pulses[driver].cue == Cue::Contact);
    assert(contact.pulses[driver].amplitude == 1 &&
           contact.pulses[driver].durationMs == 70);
    assert(contact.pulses[pick].cue == Cue::None);
    // Tiny overshoot and a transient native-state change cannot chop the tap.
    auto jitter = h.update(1220, 3, 1, 25, 24.8f, driver, pick, 3);
    assert(quiet(jitter) && jitter.stop == 0);
    jitter = h.update(1230, 4, 1, 25, 25.2f, driver, pick, 3);
    assert(quiet(jitter) && jitter.stop == 0);
    assert(quiet(h.update(1369, 4, 1, 25, 45, driver, pick, 3)));
    auto gentle = h.update(1370, 4, 1, 25, 25, driver, pick, 3);
    assert(gentle.pulses[driver].cue == Cue::Strain);
    assert(gentle.pulses[driver].intervalMs == 220 && gentle.pressure == 0);
    assert(quiet(h.update(1429, 4, 1, 25, 45, driver, pick, 3)));
    // With unchanged health, extra wrist pressure immediately accelerates it.
    auto hard = h.update(1430, 4, 1, 25, 45, driver, pick, 3);
    assert(hard.pulses[driver].cue == Cue::Strain && hard.pressure == 1);
    assert(hard.pulses[driver].intervalMs == 60);
    assert(hard.pulses[driver].amplitude > gentle.pulses[driver].amplitude);
    assert(hard.pulses[driver].durationMs < hard.pulses[driver].intervalMs);
    assert(quiet(h.update(1490, 4, 1, 25, 25, driver, pick, 3)));
    auto relaxed = h.update(1650, 4, 1, 25, 25, driver, pick, 3);
    assert(relaxed.pulses[driver].intervalMs == 220);
    auto release = h.update(1651, 4, 1, 25, 22, driver, pick, 3);
    assert(quiet(release) && release.stop == (1u << driver));
    auto again = h.update(1660, 4, 1, 25, 25, driver, pick, 3);
    assert(again.pulses[driver].cue == Cue::Contact);
    auto worn = h.update(1820, 4, .1f, 25, 25, driver, pick, 3);
    assert(worn.pulses[driver].cue == Cue::Strain);
    assert(worn.pulses[driver].intervalMs < 100 && worn.pressure == 0);
    // Break is still one snap per holding hand, including native ownership
    // reset.
    auto broken = h.update(1830, 5, 0, 25, 0, -1, -1, 3);
    assert(broken.pulses[driver].cue == Cue::Break &&
           broken.pulses[pick].cue == Cue::Break);
    assert(broken.pulses[driver].amplitude == 1 &&
           broken.pulses[pick].amplitude == 1);
    assert(broken.pulses[pick].durationMs > broken.pulses[driver].durationMs);
    auto next = h.update(1840, 6, 0, 25, 0, -1, -1, 3);
    assert(quiet(next) && next.stop == 0 && h.active(1840) == 3);
    assert(quiet(h.update(2000, 6, 0, 0, 0, -1, -1, 3)) && h.active(2000) == 0);
    h.update(2100, 1, 1, 0, 0, -1, -1, 3);
    h.update(2200, 4, .1f, 25, 40, driver, pick, 3);
    auto swap = h.update(2201, 4, .1f, 25, 40, pick, driver, 3);
    assert(swap.stop == (1u << driver) &&
           swap.pulses[pick].cue == Cue::Contact);
    auto letgo = h.update(2202, 4, .1f, 25, 40, -1, -1, 0);
    assert(letgo.stop == (1u << pick) && h.active(2202) == 0);
    h.reset();
    assert(quiet(h.update(2300, 5, 0, 25, 0, -1, -1, 3)));
    h.update(2400, 4, .1f, 25, 40, driver, pick, 3);
    auto one = h.update(2410, 5, 0, 25, 0, -1, -1, 1u << pick);
    assert(one.pulses[driver].cue == Cue::None &&
           one.pulses[pick].cue == Cue::Break);
    h.reset();
    h.update(2500, 4, 1, 25, 25, driver, pick, 3);
    auto invalid = h.update(2660, 4, std::numeric_limits<float>::quiet_NaN(),
                            25, 25, driver, pick, 3);
    assert(invalid.pulses[driver].intervalMs ==
           220); // Invalid health cannot fake urgency.
    h.reset();
    assert(h.active(2670) == 0);
  }
  {
    HapticFeedback h;
    auto hit = h.update(1000, 2, 1, 0, 0, 1, 0, 3, 0, 0, 1);
    assert(hit.pulses[0].cue == Cue::Housing && hit.pulses[0].durationMs == 25);
    auto next = h.update(1010, 2, 1, 0, 0, 1, 0, 3);
    assert(quiet(next) && next.stop == 0);
    auto released = h.update(1011, 2, 1, 0, 0, 1, -1, 2);
    assert(released.stop == 1);
    auto jam = h.update(1100, 4, 1, 10, 20, 1, 0, 3, 0, 0, 2);
    assert(jam.pulses[1].cue == Cue::Contact);
    auto strain = h.update(1260, 4, 1, 10, 30, 1, 0, 3, 0, 0, 2);
    assert(strain.pulses[1].cue == Cue::Strain);
  }
  puts("native-stop contact at zero overshoot, jitter latch, distinct tap/gap, "
       "pressure-scaled cadence, wear warning, break and cancellation passed");
}
