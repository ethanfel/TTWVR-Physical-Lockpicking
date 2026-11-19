// SPDX-License-Identifier: GPL-3.0-only
#include "Collision.h"
#include "LockMesh.h"
#include "ToolGeometry.h"
#include <cassert>
using namespace locks;
int main() {
  // Measured from the pre-texture demo, not source-mesh bounding boxes.
  assert(pinShaftLength == .075f && pinHandleLength == .081f);
  assert(pinWireRadius == .0014f && driverHandleStart == .112f);
  assert(driverLength == .182f && driverInsertionDepth == .006f);
  // The prepared mesh and the collision blade have the same finished widths.
  for (auto [z, half, expected] : {std::array<float,3>{0,propBladeTipHalfWidth,.004f},
                                   {driverBladeEnd,propBladeHalfWidth,.0042f}}) {
    Vec p{half,0,z}, n{1,0,0};
    fitPreparedDriverBlade(p,n);
    assert(std::abs(p.x-expected)<1e-7f && p.z==z);
    assert(std::abs(p.x-(z==0 ? driverBladeTipHalfWidth : driverBladeHalfWidth))<1e-7f);
  }
  // Keep the wrist roll fixed while the lower slot rotates under the blade.
  // This insertion depth used to pinch the flat against the walls from 57°.
  for (auto style : {LockStyle::Round, LockStyle::RoundedSafe, LockStyle::Padlock}) {
    TargetBounds bounds;
    bounds.style = style;
    ToolShape blade(1,1,0,bounds);
    for (int angle = 0; angle <= 90; ++angle) {
      Pose seated{};
      seated.p = turnWithCylinder({{}, {0,-.010f,.014f}},float(angle)).p;
      assert(blade.clearance(seated,float(angle)) > .0002f);
    }
  }
  assert(keywayWidth(LockStyle::Round) == .005f);
  assert(keywayHeight(LockStyle::Round) == .018f);
  auto edge = fitLockVertex({{assetPlugRadius, 0, .024f}, {1, 1, 1}, .2f, .7f},
                            LockMeshSize::RoundPlug);
  assert(std::abs(edge.p.x - .032f) < 1e-6f && edge.p.z == .024f);
  assert(std::abs(length(edge.n) - 1) < 1e-6f && edge.u == .2f &&
         edge.v == .7f);
  auto slot = fitLockVertex({{.0071f, .0294f, .024f}, {0, 0, 1}, 0, 0},
                            LockMeshSize::RoundPlug);
  assert(std::abs(slot.p.x - keywayWidth(LockStyle::Round)) < 1e-7f);
  assert(std::abs(slot.p.y - keywayHeight(LockStyle::Round)) < 1e-7f);
  assert(std::abs(housingDistance({slot.p.x, 0, .023f})) < 1e-6f);
  auto frame = roundLockVertex({{.075f, 0, 0}, {1, 0, 0}, 0, 0});
  assert(std::abs(housingDistance(frame.p)) < 1e-6f);
  PropVertex tool{{.01f, .02f, .182f}, {1, 0, 0}, .1f, .3f};
  auto unchanged = fitLockVertex(tool, LockMeshSize::Unchanged);
  assert(length(unchanged.p - tool.p) == 0 &&
         length(unchanged.n - tool.n) == 0);

  // The imported UI face has no sidewall. Duplicate vertices model UV seams;
  // welding them must still find one complete outer ring for the new skirt.
  PropMesh openFace;
  for(int i=0;i<24;i++) {
    float a=i*2*pi/24,b=(i+1)*2*pi/24;
    unsigned first=unsigned(openFace.vertices.size());
    openFace.vertices.push_back({{0,0,.024f},{0,0,1},.5f,.5f});
    openFace.vertices.push_back({{assetPlugRadius*std::cos(a),assetPlugRadius*std::sin(a),.024f},{0,0,1},.2f,.2f});
    openFace.vertices.push_back({{assetPlugRadius*std::cos(b),assetPlugRadius*std::sin(b),.024f},{0,0,1},.8f,.2f});
    openFace.triangles.push_back({first,first+1,first+2});
  }
  for(bool pad:{false,true}) {
    auto skirt=makePlugSkirt(openFace,pad);
    assert(skirt.triangles.size()>=192);
    unsigned back=0,front=0;
    for(const auto &v:skirt.vertices) {
      assert(finite(v.p)&&std::abs(length(v.n)-1)<1e-5f);
      if(std::abs(v.p.z-.0195f)<1e-7f) {
        ++back;assert(std::hypot(v.p.x,v.p.y)>(pad?padPlugRadius:lockPlugRadius)+.0004f);
      } else {++front;assert(std::abs(v.p.z-.024f)<1e-7f);}
    }
    assert(back&&front);
    // Sidewalls need a two-dimensional metal mapping, not stretched boundary
    // texels with a singular tangent frame.
    for(auto face:skirt.triangles) {
      auto a=skirt.vertices[face[0]],b=skirt.vertices[face[1]],c=skirt.vertices[face[2]];
      assert(std::abs((b.u-a.u)*(c.v-a.v)-(c.u-a.u)*(b.v-a.v))>1e-8f);
      for(auto v:{a,b,c})assert(v.u>.65f&&v.u<.87f&&v.v>=.14f&&v.v<=.22f);
    }
  }

  for (float side : {-1.f, 1.f}) {
    Vec anchor{side * .03872f, -.03996f, -.02932f};
    Pose tracked{axisAngle({0, 1, 0}, .8f), {.1f, .2f, .3f}};
    auto grip = pinGripPose(tracked, anchor, side < 0 ? 1 : 0);
    assert(length(grip.p-world(tracked,anchor)) < 1e-6f);
    Pose held = heldTool(grip,0,1);
    assert(length(world(held,handleCenter(0))-grip.p) < 1e-6f);
    Vec localDirection=rotate(inverse(tracked.q),rotate(grip.q,{0,1,0}));
    assert(localDirection.x*side < -.5f && localDirection.y < -.8f);
    // The working leg must point out of the palm, not just hang downward.
    // The baked left/right palms face opposite grip-local Z directions.
    Vec working = rotate(inverse(tracked.q), held.p-world(held,pinHinge(1)));
    assert(working.z*side < -.07f);
    if (side>0) {
      Quat previousLeft=axisAngle({0,0,1},145*pi/180);
      assert(length(rotate(pinGripOrientation(0),{1,2,3})-
                    rotate(previousLeft,{1,2,3}))<1e-6f);
    }
    // With the hand raised in the user's pinch pose, the bend and working
    // tip must hang below the contact, not project upward over the fingers.
    Pose raised{axisAngle({1, 0, 0}, pi), {0, 0, 0}};
    auto raisedGrip = pinGripPose(raised, anchor, side < 0 ? 1 : 0);
    auto raisedPin = heldTool(raisedGrip, 0, 1);
    assert(world(raisedPin, pinHinge(1)).y < raisedGrip.p.y - .05f);
    assert(raisedPin.p.y < raisedGrip.p.y - .05f);
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
