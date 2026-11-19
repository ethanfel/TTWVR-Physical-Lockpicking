// SPDX-License-Identifier: GPL-3.0-only
#pragma once
namespace locks {
// Interaction dimensions from the approved pre-texture demo (3904966).
// prepare_props.py reads these literals: appearance must fit this geometry.
constexpr float pinShaftLength = .075f; // tip to bend centre
constexpr float pinHandleLength = .081f;
constexpr float pinBendRadius = .004f;
constexpr float pinWireRadius = .0014f;
constexpr float pinWaveAmplitude = .0015f;
constexpr float driverBladeEnd = .028f;
constexpr float driverShaftStart = .042f;
constexpr float driverHandleStart = .112f;
constexpr float driverLength = .182f;
constexpr float driverInsertionDepth = .006f;
// TTWProp5 files store the original broad face. Fit it once at load time.
constexpr float propBladeTipHalfWidth = .0045f;
constexpr float propBladeHalfWidth = .0065f;
constexpr float propBladeNeckHalfWidth = .00168f;
constexpr float driverBladeTipHalfWidth = .004f;
constexpr float driverBladeHalfWidth = .0042f;
constexpr float driverBladeHalfThickness = .0006f;
constexpr float driverShaftRadius = .004f;
constexpr float driverHandleRadius = .012f;
// Verified continuous pin-metal island in the original lock texture atlas.
// prepare_props.py isolates this region; the complete wire uses 0..1 UVs.
constexpr float pinUvU0 = .08f;
constexpr float pinUvU1 = .42f;
constexpr float pinUvV0 = .94f;
constexpr float pinUvV1 = .955f;
constexpr float plugFaceRecessDepth = .001f;
constexpr float plugKeywayDepth = .036f; // front face to the bottom of the textured slot
constexpr float plugSlotTop = .0135f; // upper lip of the working opening in all three mounted plugs
constexpr float demoKeywayHalfWidth = .005f;
constexpr float demoKeywayHalfHeight = .018f;
} // namespace locks
