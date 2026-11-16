# Displaying the object being lockpicked

**Implementation update (version 20, 2026-11-16):** the four initial model profiles now have a private asset converter, bounded cache, native target selection, tool/body clearance and native-success-gated Open tracks. See the [preparation and limitations](../README.md#target-objects-version-20). The study below records the broader design; texture swaps, additional models, exact animated collision and manual opening remain future work. The display ends with the native lock menu, so the complete long door animation is not held on screen.


Original feasibility study, 2026-11-16. The study itself made no runtime or installed-addon changes; implementation updates are recorded above and below.

## Findings verified from the local installation

The installed game already provides textured NIF models for doors, freestanding
safes, wall/floor safes, ammo containers, footlockers and toolboxes. These can be
reused; Blender and newly authored replacement objects are not prerequisites.
The existing local PyFFI asset pipeline successfully parsed all eight sampled
models and their geometry, UVs, material texture paths, transforms, collision
blocks and Open/Close animation sequences.

The base `FalloutNV.esm` contains 2,478 CONT and 320 DOOR records, referencing
260 distinct nonempty model paths across both categories. These are base records,
not counts of placed objects or locked instances, and do not include the full
winning TTW/mod load order. Many records share the same mesh: for example the
standard safe path is used by 49 container base records, and the common ammo
box by 115. Placement profiles keyed by normalized model path therefore cover
many objects without per-reference configuration.

Samples loaded from the installed TTW mesh archives:

| Model | Triangles | Base records sharing path | Animations present |
| --- | ---: | ---: | --- |
| `genericsafewall01.nif` | 344 | 47 | Open, Close |
| `genericsafefloor01.nif` | 344 | 29 | Open, Close |
| `genericsafe01.nif` | 360 | 49 | Open, Close |
| `toolbox01.nif` | 340 | 30 | Open, Close |
| `footlocker01.nif` | 210 | 158 | Open, Close |
| `ammobox01.nif` | 172 | 115 | Open, Close |
| `offdoorsml01.nif` | 850 | 3 | Open, Close |
| `schdoorsmr01.nif` | 854 | 1 | Open, Close |

The raw geometry is small, suggesting modest mesh cost. No performance benchmark
of object rendering has been performed. Close-up texture quality, shaders,
alpha materials and resource loading still need visual/performance validation.

The safe models have named door nodes (`SafeDoor01/02/03`), the footlocker has
`Top`, and the ammo/tool boxes have generic `Object01`/`Low_AmmoBox01` nodes.
None of the eight samples exposes an explicitly named keyhole/lock-placement
node. A hinge node is useful for opening animation but does not identify the
keyhole. We should not infer universal lock placement from the model origin or
bounds: wall-safe thickness is along X, floor-safe thickness along Z, and door
roots/hinges differ. A profile must specify the lock's local position, outward
normal/orientation and size, then be checked visually.

## Agreed placement approach

Use the model's visible lock area even when there is no named marker. Inspect
its textured appearance, then record a local anchor once per model. An existing
keyhole, lock plate or latch guides the position. Where no suitable area is
visible, add the working lock in a plausible place: by a door handle, on a safe
front, or on a container's closing latch. Absence of an authored marker does not
block model support.

This is visual authoring of a small placement profile, not a requirement for
runtime image recognition or a newly modelled object. Store position,
orientation/outward normal, mounting size and the local surface-clearance rule.
A front-mounted lock should sit against the surface, with a working insertion
space behind it; the target hull must not stop the pin inside the keyway. Reuse
our functional cylinder and preserve the original gameplay rules. A decorative
safe dial or latch is not automatically a functional pin-tumbler lock; placing
the addon cylinder there is an intentional presentation adaptation.

A calibrated profile applies to every reference using that mesh. Mirrored or
substantially changed replacement meshes need a separate/overridden profile.
For unknown meshes, any automatic plausible placement remains provisional;
keep the standalone fallback if we cannot avoid floating/clipping geometry.

The original collision blocks exist (convex hulls or packed Havok meshes).
They are not automatically usable by the addon's local tool physics, and are
not proof that a small keyhole is physically open. Keep precise keyway collision
separate. Use a simple object hull or static triangle contact outside that region.

## Identifying the actual target

`Plugin.cpp` already snapshots the native menu's target reference, and
`Inventory.cpp` uses it for pin-drop placement. The public
[JIP LockPickMenu layout](https://github.com/jazzisparis/JIP-LN-NVSE/blob/main/nvse/GameUI.h)
identifies `targetRef` at 0x6C. There is no need to guess from whichever object
happens to be under the crosshair after the menu opens.

The [xNVSE reference layout](https://github.com/xNVSE/NVSE/blob/master/nvse/nvse/GameObjects.h)
provides the base form, position, rotation, scale and loaded scene node.
Its [door/container definitions](https://github.com/xNVSE/NVSE/blob/master/nvse/nvse/GameForms.h)
include a model component with a NIF path and texture swaps. These match the
corresponding local JIP headers inspected for this study. Any implementation
must retain runtime-version checks and validated pointer reads.

Collect an immutable target description on the game thread: form/reference IDs,
model path, type, effective textures and any needed transform. Do not keep raw
engine scene-node pointers on the OpenXR rendering thread across menu closes,
loads or unloads. The practice menu with no target must retain the current
standalone lock. Native unlocking, inventory, ownership, crime, scripts and XP
must continue to affect the original reference, not a spawned duplicate.

## Presentation choices

1. **Matching object displayed in the existing reachable workspace (recommended
   first).** Import the selected object's mesh as a visual prop. Position its
   configured keyhole at the existing comfortable interaction point, and draw
   the current functional lock on its front. Preserve the blurred world behind
   it. Safe bodies, ammo boxes and doors then have the correct appearance while
   the current hand/tool/minigame path stays intact. This is a display copy, not
   another game reference. Prefer natural dimensions and a sensible view of a
   large door; do not shrink every door into a tabletop miniature. Floor-safe
   ergonomics and seated reach should be explicit placement policies.

2. **Lock attached to the actual object in the world.** More immersive, but it
   additionally needs calibrated game-world-to-OpenXR transforms, player scale,
   room-scale offsets, recenter handling and correct tool/object occlusion.
   The current full-frame blur would also blur that object: it needs a sharp
   object pass or a depth-aware mask. A 2D rectangle around it is insufficient
   for stereo silhouettes/occlusion. This was not tested in the current study.

3. **Engine-rendered object clone in a separate pass.** Potentially preserves
   arbitrary mod materials and animation automatically, but requires additional
   engine rendering hooks and resource/state/lifetime handling. The current
   addon cannot directly hand a Gamebryo NiNode to its D3D11 renderer. This is
   broader than extending the existing prepared-asset route.

## Smallest useful implementation

Start with `clutter/security/genericsafe01.nif` (360 triangles), followed by
`clutter/ammo/ammobox01.nif` (172), then a common office/school door (850/854).

- Add target metadata capture and diagnostic logging without changing outcomes.
- Generalize the private preparation/cache tool to import a model's geometry
  and materials, keeping original transforms, normals, UVs and closed pose.
- Resolve assets through the active mod setup's winning loose/BSA content and
  texture swaps. The current preparer's alphabetical BSA search is insufficient
  as a general mod-priority resolver. Never load model files from an arbitrary
  unvalidated external path.
- Add per-model placement profiles and a bounded target-object asset format.
  `TTWProp2` currently requires exactly seven parts, only three material groups
  and vertices within one metre; it cannot directly hold a full-size door.
  Keep existing tool assets compatible and add an independently validated
  target mesh/cache rather than weakening those checks without bounds.
- Load/cache the selected model outside the per-eye hot path, publish it once
  ready, then render it around the interactive cylinder. Use the current lock
  alone while unavailable/unsupported. Do not parse NIFs or read files each frame.
- Add object/tool collision separately, preserving an opening for the pin and
  screwdriver. Avoid doubling decorative lock faces with our working cylinder.
- Initially keep the displayed object closed; let the real game handle the
  successful unlock. Opening the display copy afterwards can use the existing
  animation tracks in a later step, with no early automatic opening or activation.

Validation should use real locked references through normal activation for
both door and container cases. The old artificially target-linked practice
fixture is not sufficient. Check both eyes, natural size, hand reach, insertion
clearance, model/texture overrides, left/right door variants, wall/floor safes,
missing-model fallback, cancellation/load transitions, native unlock behavior
and actual application frame time. Simulated-headset captures can verify
rendering; physical reach requires the headset.

## Local evidence

`local/lockpick-dev/revisions/target-model-study/audit.py` reads game records and
NIFs without changing them. `asset-audit.json` records sample model sources,
triangle counts, node names, bounds, textures, animation/collision types and
base-record model-path counts. No game meshes/textures were extracted into the
repository or distributed. No game was launched for the initial study. Subsequent version-20 runtime evidence is recorded in [VALIDATION.md](VALIDATION.md#target-objects--version-20-2026-11-16).


## Padlock style (version 21)

The ammo-box placement profile selects a padlock style; other current profiles
keep the round housing. The profile moves the box 7 cm up around the unchanged
working keyhole so the padlock hangs beneath its latch. The shared style header
defines the body, shackle and attachment loop dimensions for both geometry and
collision. The original cylinder, insertion clearance and gameplay are retained.

The local search scanned 18,248 NIF contents in the two installed combined
Fallout/TTW mesh archives without finding a named padlock. The padlock message
icon exists, but is a 2D UI texture. Inspection of ammo-box, toolbox and jail-door
node trees did not reveal a separate reusable padlock part. This does not rule
out an unnamed detail embedded in some other model. The implementation uses
original body/shackle geometry and the locally extracted lock metal atlas and
cylinder, with no additional proprietary assets in the release.

`TTWTarg2` adds a bounded style field; the loader continues to accept `TTWTarg1`
as a round lock. Unknown styles and invalid model/style pairings fall back.
Native success lifts and turns the shackle around its retained leg. The
attachment loop follows the lid track while the body/tools stay in place.
Closed-pose collision is used during manipulation; success animation does not
introduce a second gameplay or inventory path.


## Compact padlock revision (version 22)

The first padlock was oversized and its cool lock-atlas material did not match
the ammo box. Its casing is now 76 × 64 mm, with a 48 mm wide shackle and smaller
attachment loop. The placement profile lowers the box 76 mm from the initial
version around the unchanged tool contact point so the shorter shackle still
meets the latch. Regenerate target caches when updating.

The casing UVs sample the original ammo-box latch, and the shackle samples its
handle steel from `50CalAmmo.dds` and the matching normal map. The original lock
cylinder is compacted while preserving slot width and the tool contact/depth
plane. Its slot height and outer diameter shrink; geometry normals, collision
and pin-capture bounds follow the resized shape. Only this cylinder is given a
subdued, warm steel treatment. Other materials retain their existing shading.
No new texture file or game-asset redistribution is needed.


## Door keyhole placement correction (2026-11-16)

For the office-left and school-right models, the visible upper round keyhole
is centred at approximately pixel (917.5, 101.5) in the installed 1024²
`OffDoorSm01.dds` atlas. Barycentric mapping through the front-facing triangles
places it at the following original model coordinates:

| Profile | X (depth) | Y | Z |
| --- | ---: | ---: | ---: |
| Office left | 4.031213 | −51.232950 | 82.425599 |
| School right | 4.031189 | 51.233114 | 82.425599 |

The former anchors used X=9.78, Y=−46/+46 and Z=86. The corrected anchors align
the working axis with the visible keyhole and bring its surface to the lock's
mounting plane. Correcting only Y/Z left a visible offset under head movement,
because the lock was approximately 6.9 cm too far forward from the door.
Round lock size/material, interaction frame and native opening tracks are
retained. The production DLL is unchanged. Safe/ammo-box geometry and all
prepared textures are byte-identical to the previous revision.

The optional `collision_front` profile value places the simplified closed-body
hull at the door face (−0.013 m). The door knob remains visibly in front of it,
without expanding a single bounding box over empty space around the keyhole.
This remains approximate body collision: it does not add a separate exact knob
collision mesh. The preparer validates the override against the mesh depth and
existing negative-front clearance, and records both hull and mesh bounds.


## Safe frame style (version 23)

The standard freestanding-safe profile selects style 2: a rounded-square outer
frame, 146 mm across, with 14 mm corners and a 4 mm bevel. Mesh construction and
collision share dimensions. The existing full-size cylinder/keyway remains;
its finish is darkened for this safe only. The casing samples worn painted
metal from the original safe's texture and follows the existing Open track.
The side walls use depth UVs to avoid degenerate tangent frames in normal-map
lighting. The safe model's geometry, anchor and original animation are unchanged.
The ammo-box and aligned door assets and their materials are retained.
