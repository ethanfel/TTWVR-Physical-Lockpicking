// SPDX-License-Identifier: GPL-3.0-only
#include "PadlockMesh.h"
#include "Physical.h"
#include "Target.h"
#include <cassert>
#include <cstdio>
using namespace locks;
Pose grip(Pose p, float opening = 1) {
  return {p.q, world(p, handleCenter(0, opening))};
}
int main() {
  TargetBounds pad;
  pad.style = LockStyle::Padlock;
  assert(housingDistance({.034f, .028f, 0}, 0, pad) < 0); // rectangular corner
  assert(housingDistance({.05f, .04f, 0}, 0, pad) > 0);
  assert(housingDistance({.05f, .04f, 0}) > 0);
  assert(housingDistance({0, -.072f, 0}, 0, pad) >
         0); // no old round phantom rim
  assert(housingDistance({0, -.072f, 0}) > 0);
  assert(housingDistance({shackleHalfWidth, .058f, 0}, 0, pad) < 0);
  assert(housingDistance({0, .05f, 0}, 0, pad) >
         0); // open space inside shackle
  assert(housingDistance({0, stapleY, 0}, 0, pad) <
         0); // shackle passes through staple
  assert(housingDistance({0, stapleY + stapleRadius, 0}, 0, pad) < 0);
  // Decorative cylinder diameter differs; the working slot and blade bearing
  // are identical to the round/safe lock.
  // Its normals remain valid after the piecewise warp.
  auto centre = padlockPlugVertex({{.0071f, .0294f, .024f}, {0, 0, 1}, 0, 0});
  assert(std::abs(centre.p.x - keywayWidth(pad.style)) < 1e-6f && centre.p.z == .024f);
  assert(std::abs(centre.p.y - keywayHeight(pad.style)) < 1e-6f);
  auto edge = padlockPlugVertex({{assetPlugRadius, 0, .024f}, {1, 1, 1}, 0, 0});
  assert(std::abs(edge.p.x - padPlugRadius) < 1e-6f);
  assert(std::abs(length(edge.n) - 1) < 1e-6f);
  PadlockMesh mesh;
  for (auto m : {&mesh.body, &mesh.shackle, &mesh.staple}) {
    for (auto v : m->vertices) {
      assert(finite(v.p) && finite(v.n));
      assert(length(v.n) > .99f && length(v.n) < 1.01f);
      assert(housingDistance(v.p, 0, pad) <
             .0012f); // visible surfaces agree with collision
    }
    for (auto t : m->triangles)
      for (auto i : t)
        assert(i < m->vertices.size());
  }
  for (float angle : {0.f, 45.f, 90.f})
    for (float pick : {0.f, 45.f, 90.f, 135.f, 180.f}) {
      ToolShape pin(0, 1, 0, pad), driver(1, 1, 0, pad);
      assert(pin.clearance(seatedPose(0, pick, angle), angle) > 0);
      assert(driver.clearance(turnWithCylinder(aimedTool({{}, {.07f, -.035f, .15f}}, mouth(1, pad.style), driverInsertionDepth, pad.style), angle), angle) > 0);
    }
  for (int k = 0; k < 2; k++) {
    ToolShape tool(k, 1, 0, pad);
    for (Vec spot :
         {Vec{.034f, .028f, 0}, Vec{.024f, .058f, 0}, Vec{0, .074f, 0}}) {
      auto c = moveAgainstHousing(tool, {{}, spot + Vec{0, 0, .3f}},
                                  {{}, spot - Vec{0, 0, .3f}});
      assert(c.hit && tool.clearance(c.pose, 0) >= -.00015f);
      Pose away = c.pose;
      away.p.z += .12f;
      auto d = moveAgainstHousing(tool, c.pose, away);
      assert(length(d.pose.p - away.p) < .0002f);
    }
  }
  for (int h = 0; h < 2; h++)
    for (float opening : {65.f / 90, 1.f, 1.5f}) {
      PhysicalInteraction t;
      t.targetBounds = pad;
      t.config.useHolders = false;
      Pose hand[2];
      hand[1 - h].p = {2, 2, 2};
      auto step = [&](float held) {
        t.update({}, hand[0], hand[1], h == 0 ? held : 0, h == 1 ? held : 0, 0);
      };
      step(0);
      auto &pin = t.tools[0];
      pin.opening = opening;
      pin.pose = holderTool(t.config, 0, opening, 0);
      hand[h] = grip(pin.pose, opening);
      step(1);
      Pose entry{pinAlignment(opening, 0), {.001f, .007f, .026f}};
      Pose outside = entry;
      outside.p.z += .06f;
      hand[h] = grip(outside, opening);
      step(1);
      hand[h] = grip(entry, opening);
      step(1);
      assert(pin.inserted && length(pin.pose.p - entry.p) < .00015f);
      step(0);
      hand[h] = grip(pin.pose, opening);
      step(1);
      hand[h].p = hand[h].p + pin.entryAxis * .10f;
      step(1);
      assert(!pin.inserted && t.pickHand == h);
    }
  assert(keywayWidth(pad.style)==keywayWidth(LockStyle::Round));
  assert(keywayHeight(pad.style)==keywayHeight(LockStyle::Round));
  assert(length(mouth(1,pad.style)-mouth(1,LockStyle::Round))==0);
  for(int owner:{0,1}) for(float depth:{.015f,.04f,.067f})
    for(Vec entry:{Vec{0,.0027f,.026f},Vec{-.002f,.007f,.026f},Vec{.002f,.012f,.026f}}) {
      PhysicalInteraction state;state.targetBounds=pad;state.config.useHolders=false;
      Pose hand[2]; hand[0].p=hand[1].p={2,2,2};float hold[2]{};
      auto step=[&](float cylinder=0.f){state.update({},hand[0],hand[1],hold[0],hold[1],cylinder);};
      step();auto &pin=state.tools[0];auto &driver=state.tools[1];pin.opening=1;
      hand[owner]=grip(pin.pose);hold[owner]=1;step();assert(pin.owner==owner);
      for(float dz:{.10f,0.f,-depth}) {hand[owner]=grip({{},entry+Vec{0,0,dz}});step();}
      assert(pin.inserted);
      auto driverHand=[&](Pose tool){return Pose{tool.q,world(tool,handleCenter(1))};};
      hand[1-owner]=driverHand(driver.pose);hold[1-owner]=1;step();assert(driver.owner==1-owner);
      Pose blade=aimedTool({{}, {owner?-.07f:.07f,-.035f,.15f}},mouth(1,pad.style));
      Pose approach=blade;approach.p.z+=.08f;hand[1-owner]=driverHand(approach);step();
      hand[1-owner]=driverHand(blade);step();
      assert(pin.inserted&&driver.inserted);
      for(float desired:{-35.f,215.f,90.f}) {
        hold[owner]=0;step();hand[owner]=grip(pin.pose);hold[owner]=1;step();
        Pose start=pin.pose;float initial=state.pick;
        for(int n=1;n<=30;n++) {
          Quat q=axisAngle({0,0,1},-(desired-initial)*float(n)/30*pi/180);
          hand[owner]=grip({multiply(q,start.q),pin.pinContact+rotate(q,start.p-pin.pinContact)});step();
        }
        assert(pin.inserted&&driver.inserted);
        assert(std::abs(state.pick-desired)<2.f);
      }
      // A small push against a stopped cylinder applies pressure while the
      // actual blade remains in contact. The driver is not attached to it.
      hold[1-owner]=0;step();hand[1-owner]=driverHand(driver.pose);hold[1-owner]=1;step();
      hand[1-owner].p.x+=.006f;step();
      assert(state.target>6&&state.seated());
      hold[1-owner]=0;step();assert(state.target==0);
      hold[owner]=0;step();hand[owner]=grip(pin.pose);hold[owner]=1;step();
      hand[owner].p=hand[owner].p+pin.entryAxis*.12f;step();assert(!pin.inserted&&pin.owner==owner);
    }
  TargetOpening win;
  for (auto args :
       {std::array<float, 3>{7, 1, 0}, std::array<float, 3>{4, 1, 60},
        std::array<float, 3>{7, 0, 90}}) {
    win.Step(1, 2, unsigned(args[0]), args[1], args[2], .1f);
    assert(!win.opened && !win.time);
  }
  win.Step(1, 2, 7, 1, 90, .1f);
  assert(win.opened);
  Pose closed = padlockShackleMotion(0), open = padlockShackleMotion(.3f);
  assert(length(closed.p) < 1e-6f && closed.q.w == 1);
  assert(world(open, {-shackleHalfWidth, shackleBottom, 0}).y - shackleRadius >
         padHalfY);
  assert(world(open, {shackleHalfWidth, shackleRightBottom, 0}).y < padHalfY);
  assert(world(open, {-shackleHalfWidth, shackleBottom, 0}).z > .025f);
  win.Step(0, 0, 0, 0, 0, .1f);
  assert(!win.opened && win.time == 0);
  printf("Padlock: %zu triangles, matching collision, both-hand "
         "insertion/withdrawal, two-tool search/torque at three depths and off-centre entries, clear keyway and success-only release passed\n",
         mesh.body.triangles.size() + mesh.shackle.triangles.size() +
             mesh.staple.triangles.size());
}
