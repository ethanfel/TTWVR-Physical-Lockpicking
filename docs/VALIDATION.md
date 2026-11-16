# Prototype validation — 2026-11-15

Tested in an independent Fluorine game copy and Proton prefix with TTWVR 1.1.6, xNVSE 6.4.90, an RTX 5090 and the local Monado simulated headset. The normal install was protected by read-only mounts.

## Verified

- 32-bit MinGW build with warnings treated as errors.
- Native mathematical tests for coordinate transforms, clockwise wrist rotation, stereo projection and depth.
- All three native call targets (pick input, torque input, cylinder limit) match the supported executable before patching.
- The addon attaches its action set and receives active controller poses through Wine OpenXR.
- Both eye swapchains render and are accepted on consecutive frames. Matching stereo captures show the same lock with appropriate disparity.
- Developer-only synthetic hand movement changes the native pick position. Wrist torque changes the native cylinder angle.
- In the native practice menu, an unsuitable angle reaches the jam and break states, with pick health reaching zero. Releasing returns the cylinder to zero and does not keep applying torque.
- An angle sweep using only ordinary input and observable feedback reaches cylinder 90 degrees with health 1.42 and native completion stage 6. The hidden solution is never read.
- File-requested fallback removes the workspace and restores the original visible minigame. The code path is shared with F8.

The state-machine evidence came from a separate `LOCKPICK_TEST_INPUT=ON` build. The headset binary is built with that option OFF. Earlier development log lines had a misaligned formatting field; success/failure evidence is also recorded independently from read-only native stage, cylinder and health observations. The format is corrected and compiler-checked in the final source.

The initial prototype’s non-synthetic headset DLL also passed a fresh startup/load and consecutive stereo-frame smoke test. Its log reports matching `pick=90.00 native_pick=90.00`, cylinder zero and torque zero at rest. The synthetic input filename is absent from the binary. A Wine INI-cache issue in the external test harness was resolved by changing command-file size; it did not require an addon change.

## Contact and wrist-position revision

The first headset trial reported pick movement without touching its handle and a screwdriver that kept turning by itself. The original 38 cm grab sphere and wrist-to-continuous-torque mapping caused those behaviors.

The revised interaction uses the rendered handles for contact tests, a fresh trigger press to acquire each tool, a grab offset to prevent pick snapping, and an upper bound on native cylinder rotation derived from wrist angle. Moving away or losing controller tracking drops the grip and requires another trigger release/press. The original native resistance remains the upper limit even when the requested wrist angle is larger.

Regression evidence:

- Both native test suites pass. Interaction tests include far presses, entering range with a held trigger, stationary pick, no snap, release/re-grab, rotated lock space, and 10,000 stationary-wrist updates without angle accumulation.
- In the running native practice menu, pressing both triggers away from the handles and moving into range with them held left the cylinder at zero.
- Grabbing the screwdriver without twisting left the cylinder at zero. A 25-degree wrist turn reached 25 degrees and held there for the remainder of a five-second trial, with health unchanged at 1.42. Reversing to 12 degrees reached 12; releasing returned it to zero.
- The final developer build reaches native jam/break states with health zero at unsuitable angles, and reaches native completion stage 6 at exactly 90 degrees with health 1.42 at a suitable angle. This was found by an ordinary input sweep, without reading the hidden solution.
- Full-quarter-turn input is rounded within 0.01 degree of 90 to avoid quaternion rounding leaving it just below the native completion comparison. This does not override native resistance.
- The simulated headset supplies valid head position without a position-tracked flag. Head/eye placement therefore uses valid flags; controller manipulation requires both valid and tracked position/orientation.

The revised contact tolerances and controller feel still need headset confirmation. Synthetic poses are used only in the developer DLL; they are absent from the installed headset build.

## Either-hand tools and axial screwdriver revision

The next headset report identified fixed hand assignments and the screwdriver handle orbiting the lock center during a wrist twist. Both are corrected:

- Either controller can acquire either tool. Each tool has one owner; a held trigger cannot steal a tool or automatically grab after moving into range. Releasing before a fresh press permits handoff.
- The screwdriver is a straight shaft and cylindrical handle. Its centerline remains fixed in the keyhole while grip ridges spin around that axis. It no longer follows an arc around the cylinder face. Translation away from the handle still drops the tool.
- Input extracts relative quaternion twist around the actual shaft. A tilted starting controller orientation or a rotated lock does not change which rotation drives it. Resistance haptics target the current screwdriver hand.

Validation of this revision:

- Both native test suites pass. Contact, motion and stationary-angle tests run with both hand assignments. Additional checks cover competing grabs, handoffs, tilt versus twist, rotated spaces and the fixed handle centerline.
- In the native practice menu, the right hand held the screwdriver at 25 degrees with left-hand pick ownership; after release and swapping, the left hand reached 25 degrees with right-hand pick ownership. Native health stayed at 1.42.
- The left wrist reversed the native cylinder to 12 degrees. Releasing returned it to zero, and the right hand then moved the native pick to approximately 105 degrees.
- Matching stereo captures at neutral and 25 degrees show the handle center staying in place while the grip ridges rotate. See [neutral stereo](stereo-prototype.png) and [left-hand twist](stereo-axial-twist.png).
- The native resistance/outcome hooks are unchanged from the preceding revision. Full success and breakage were validated there; this revision specifically retests input ownership, wrist control and stereo geometry.
- The installed headset DLL has synthetic input disabled. Handset feel, physical haptics and comfortable reach remain to be confirmed in the headset.

## Original-input conflict revision

The subsequent headset trial reported that attempts to interact ran the original lock instead of moving the tools. Its log showed no tool owner while the native cylinder nevertheless advanced. The earlier automated tests injected the addon’s final hold values and did not exercise competing runtime actions.

This revision adds grip support, claims the grab inputs, neutralizes application input queries while the workspace is presented (preserving Back/menu bindings), and routes the lock menu’s keyboard/mouse and gamepad modes through the same verified axis hooks. Merely holding the screwdriver no longer freezes the pick. The native cylinder also refuses positive rotation when no wrist turn is requested.

Verified in the private runtime:

- Thirteen native call signatures match. The original game action set has priority zero.
- Real OpenXR trigger input with same-priority competing actions reports `capture=1 raw=1.0 filtered=0.0 own=1.0 raw_boolean=1 filtered_boolean=0` for both hands. Far presses and moving held buttons into range leave the cylinder at zero.
- Fresh grabs at both handles do not turn the lock or reduce pick health. The pick moves with the screwdriver held at rest; a requested wrist turn advances the native cylinder and release returns it to zero.
- The native input-mode flag was reset by the running game, so an INI/flag change did not establish keyboard-mode coverage. A separate private-process test temporarily made the original mode predicate return false, verified `original_gamepad=0 physical_axes=1` and a five-degree turn/release, then restored the original runtime bytes. No installed game code was changed.
- The simulated WMR side-grip states were pressed through the private runtime. The addon reports both triggers zero, both grips one and distinct tool owners; grip-only twist reaches five degrees and release returns to zero. Those runtime states were then released.
- Fallback reports `capture=0 raw=1.0 filtered=1.0 own=0.0 raw_boolean=1 filtered_boolean=1`, proving original float and boolean input restoration.
- Native resistance still limited a deliberately unsuitable pick angle to about 14.55 degrees and broke the pick when tension continued. A scripted expectation of 25 degrees was corrected to respect that native resistance.
- Both C++ test suites pass, including the new both-tools-held-without-tension regression. The installed headset build excludes synthetic input and competing-action probes.

Real Quest/WiVRn grip feel and the preserved controller Back/menu bindings still need headset confirmation. The diagnostics now log button/ownership transitions, rather than relying only on periodic samples.

## Controller vibration replaces the condition bar

The input-routing revision was confirmed working in the headset. The next change removes the green pick-condition strip and uses controller vibration for feedback.

- Turning produces a light 12 ms pulse on the screwdriver hand. At rest, or while holding tools without tension, there is no periodic feedback.
- Native jam damage increases the screwdriver-hand pulse amplitude from 0.20 toward 0.65, lengthens pulses from 18 toward 35 ms and shortens the interval from 180 toward 60 ms. These are normalized OpenXR amplitudes, not measured physical force.
- When the pick breaks, the pick hand receives one 75 ms pulse at 0.72 and the screwdriver hand one 55 ms pulse at 0.60, provided those hands still hold their grab buttons. Repeated break-animation frames do not repeat the snap.
- Releasing pressure, release/handoff, menu changes, fallback, focus/tracking loss, recentering and rendering failure cancel pending addon feedback. Application haptic start/stop calls are masked while the physical menu owns input, preserving the addon cues; normal application rumble resumes outside it.

All three C++ test suites pass, covering both hand assignments, cadence and amplitude progression, stationary silence, one-shot break events, release/handoff and lifecycle reset. In the native practice lock, an unsuitable pick angle reached the game's 30-degree resistance limit and broke normally. The runtime accepted three light turning pulses, progressively stronger strain pulses from 0.20 through 0.65, and exactly one break pulse per holding hand. Releasing then produced no further haptic events.

The [current stereo capture](stereo-haptic-preview.png) shows that the bar is absent in both eyes. Earlier captures in this document predate that removal. Simulated-runtime acceptance verifies scheduling and OpenXR routing; physical vibration strength and comfort still require the headset check.

## Strong vibration tuning

The previous vibration implementation was confirmed working in the headset, but the user requested substantially stronger feedback. The same scheduling and cancellation logic now uses these OpenXR amplitudes and durations:

| Cue | Amplitude | Pulse length |
| --- | --- | --- |
| Turning | 0.45 | 28 ms |
| Jam / damage warning | 0.70 rising to 1.00 | 35 rising to 50 ms |
| Break, screwdriver hand | 1.00 | 100 ms |
| Break, pick hand | 1.00 | 140 ms |

The warning interval still decreases from 180 to 60 ms as the pick takes damage, leaving gaps between pulses. Release/focus/tracking/fallback cancellation remains active, including during the longer break pulse. These are normalized amplitude requests, not a linear measure of perceived strength. All three regression suites pass with the longer durations; this parameter-only revision does not repeat the previous native runtime trial. The new strength still needs headset confirmation.

## Distinct contact and pressure-paced resistance

The headset report confirmed that normal rotation felt acceptable, but the start of resistance was difficult to distinguish from it. The preceding warning depended mainly on accumulated damage and required angular overshoot before starting. This revision preserves normal rotation and break cues while changing resistance feedback:

- The native jam state immediately produces one 70 ms pulse at amplitude 1.00 on the screwdriver hand, including at zero overshoot. It waits at least 160 ms from contact before starting the strain rhythm, leaving at least 90 ms after the tap.
- Extra wrist twist beyond the blocked cylinder scales the warning interval from 220 to 60 ms over 20 degrees. Damage can also increase urgency. Strain pulses last 40 ms at amplitude 0.85–1.00. This is an angular pressure proxy, not a force measurement; the runtime still chooses the motor carrier frequency.
- Small wrist/native-stage fluctuations do not truncate or retrigger the tap. Backing off clearly, releasing or changing the screwdriver hand rearms it. Existing cancellation paths remain active.

All three C++ suites pass. The feedback suite tests both hand assignments, contact at zero overshoot with full pick health, jitter tolerance, the quiet gap, faster warning cadence at constant health, reduced cadence after backing off, wear-based warnings, one-shot break feedback and cancellation. Both production and developer DLLs build successfully.

A fresh private native practice-menu trial reached resistance at 4.39 degrees with a requested 90-degree wrist turn. OpenXR accepted one 70 ms contact pulse at full pick health, followed 181 ms later by the first 40 ms strain pulse. All 19 strain events requested amplitude 1.00 and a 60 ms interval because the angular pressure proxy was saturated. The first five measured inter-pulse intervals were 62, 69, 67, 67 and 66 ms. The unchanged turning cue was 0.45 for 28 ms. Native breakage produced exactly one full-amplitude pulse per holding hand (100 ms screwdriver, 140 ms pick), and release remained quiet. These observations validate native-state integration and routing, not physical tactile strength. Gentle-to-hard pressure scaling at constant health is covered by the C++ tests rather than this saturated-pressure runtime trial.

The installed headset DLL has synthetic input disabled; private test automation is disabled and the private game/compositor have been stopped. All eight protected regular-install files match their recorded hashes. This revision does not alter geometry or native outcome hooks; the earlier stereo and success checks still describe those unchanged paths. The new contact/strain distinction needs a physical controller check.

## Loose tools, insertion, gravity and local hand meshes

The preceding contact/pressure feedback was confirmed much clearer in the headset. This revision preserves that haptic planner and replaces fixed tools with loose physical objects.

`PhysicalInteraction` owns each tool's pose, exclusive hand owner, insertion constraint, velocity and angular velocity. Fresh grip/trigger presses acquire handles without snapping away the grab offset. Tips approach the keyhole from the front; aligned insertion constrains them to the native lock. Both tools must remain inserted and held for native torque. Withdrawal keeps ownership; release detaches the tool and gravity takes over. The horizontal tray has bounded, substepped collision and damping. Out-of-reach tools return to the tray without changing native pick health or inventory. Only native breakage requests a replacement pick.

Four C++ suites pass, including both hand assignments, missed/backwards insertion, hand-relative movement, insertion/withdrawal, stationary wrist control, immediate torque cancellation after a pick drop, world-down gravity under a tilted lock, exclusive ownership/handoff, native replacement, tray settlement at several frame rates, and out-of-bounds recovery. An extended native test found a screwdriver overhanging the tray could keep rotating against its underside. The support constraint now lets an overhanging centre of mass fall clear; the exact case has a regression test and passed a fresh native drop/recovery/re-grab trial.

The final native practice-menu trial verifies:

- Both loose tools are grabbed and manually inserted; neither applies torque while free.
- A requested five-degree wrist turn reaches and holds five degrees with native pick health unchanged at 1.42.
- Releasing the pick leaves the screwdriver held but returns the native cylinder to zero.
- Pulling the screwdriver out preserves ownership; releasing it lets it fall. The overhanging drop reports recovery, returns it to the tray, and permits another fresh grab.
- Reinserted tools at an unsuitable pick angle reach native resistance at 60 degrees and breakage at health zero. The runtime accepts the unchanged full-strength contact tap, pressure-paced strain pulses and one break pulse for each holding hand. After the native replacement animation, the new pick is loose on the tray while the screwdriver remains held. The harness waits for the periodic diagnostic snapshot when checking replacement visibility.

The locally installed hand meshes each contain 780 vertices and 1,320 triangles. The preparation tool reads their real skin weights and installed hand texture, bakes open/curled poses, and records provenance locally. The renderer validates these inputs and displays textured, smoothly shaded hands driven by controller pose and grip value. Matching [tray](stereo-physical-tray.png) and [inserted-tool](stereo-physical-inserted.png) captures show both hands and the new workspace in both eyes. This is mesh reuse with addon-driven finger animation, not a hook into the game's live outfit/Pip-Boy animation. The source and distributable package contain no derived hand mesh or texture assets.

The final production build disables synthetic controller input, private automation is disabled, and the private game/compositor are stopped. Six protected files match the older regular-install baseline; the regular mod list and saved VR state differ and were left intact. This revision does not edit or restore those regular-profile files. Native success was established in earlier revisions; this revision retests manipulation and breakage, not a real door's inventory/XP/crime outcomes. Hand alignment, size, insertion comfort, perceived physics and application frame-time impact remain headset checks. Collision is limited to the tray and insertion constraint; this is not game-world Havok integration.

## Standing placement and relaxed tool handling

The loose-tool headset test exposed uncomfortable tray-derived grips, a bent pick handle, face-level placement, and rigid inserted tools. This revision supersedes the preceding release-to-drop and fixed-wrist behavior:

- The lock uses the opening head pose's horizontal facing only, 42 cm forward and 42 cm below the eyes. The tray is another 18 cm lower. Looking down or rolling the head does not tilt the panel. Placement stays fixed during the attempt.
- Both tools use straight, collinear shafts and handles. A loose held tool centres on the grip position and points along the OpenXR aim orientation; rendering retains the actual grip pose for the hands. Missing aim tracking falls back to the grip orientation.
- Tip proximity starts a guide, followed by 18 mm of actual hand advance and a 35 ms seating pulse at amplitude 0.65. No head direction or prescribed wrist roll gates insertion.
- Inserted tools remain supported when released. Regripping captures a fresh neutral wrist angle without changing the pick angle or applying torque. Pulling away by 5.5 cm extracts the tool; releasing it then drops it. The screwdriver must be held to apply torque, but a seated pick can be left supported.
- Inserted shafts aim toward the hands instead of forcing a fixed handle pose. The default full cylinder request uses 40° physical wrist travel. Pick search uses ±45° from neutral. Native resistance still limits actual travel.
- The accompanying INI reloads on a new menu and provides bounded reach, wrist, insertion and extraction settings. The earlier contact/pressure feedback values are preserved.

All four C++ suites pass. New regressions exercise both hand assignments, upright placement across head pitch/roll, configuration bounds, partial/full insertion, arbitrarily tilted starting wrists, stationary control, relaxed regrip, supported release versus loose gravity, extraction, and seating-cue priority/cancellation. Both production and developer-input 32-bit DLLs build with warnings treated as errors.

The isolated native game trial reports tracked aim poses for both hands (`aim_mask=3`) and the configured upright placement. Synthetic controller inputs verify partial insertion applies no torque, full insertion produces one seating cue per hand, and a 2° wrist turn requests/reaches 4.5° cylinder rotation with health unchanged at 1.42. Releasing both hands leaves both tools inserted while the native cylinder returns to zero. Regripping at different wrist rolls keeps the cylinder at zero, then another 2° turn reaches 4.5°. Extraction retains hand ownership, cancels torque, and release restores gravity/recovery.

A separate native resistance trial searches with the pick wrist, requests full travel using 40° screwdriver rotation, and reaches the game's resistance at approximately 11.22°. Native damage reaches zero and triggers the break animation, one break pulse per holding hand, and a replacement pick loose on the tray while the screwdriver remains held. The full-strength contact tap and pressure-paced strain events are accepted by OpenXR. The trial does not read the hidden solution or force success.

The [standing tray](stereo-standing-tray.png) and [inserted tools](stereo-standing-inserted.png) captures use a simulated head looking down 45° after placement. They verify both-eye rendering and the upright workspace. The synthetic poses use identical aim/grip orientations and therefore do not establish real-controller hand alignment. This revision still needs a physical headset check for wrist comfort, insertion feel, reach and visual hand/tool fit.

Design reference: [SkyrimVR Physical Lockpicking 1.1.2, commit 3080624](https://github.com/Stanislavsonder/SkyrimVR-PhysicalLockpicking/tree/3080624819aa3d6ff6cae232039462d29da52e72). Only the interaction approach was used; no Skyrim assets, hooks, HIGGS coordinate offsets or hidden-solution logic were imported.

The headset DLL has synthetic input disabled. Test automation is disabled and the private game/compositor are stopped. The eight regular-install files match hashes captured at the start of this revision. Local hand assets remain private and excluded from the package. Native success/inventory/XP checks were not repeated; the earlier validation boundaries still apply.

## Prepare a bobby pin, insert, then manipulate the lock

The standing revision improved reach, but the headset report still found the pin grip awkward. The user supplied folded/opened bobby-pin references and requested a distinct preparation stage. This revision changes only the pin interaction, its hand pose and preparation feedback; the screwdriver retains its established input mapping and geometry.

The pin now has a rounded fold, straight insertion leg and corrugated held leg. Either hand is the primary owner. A fresh grip by the other hand near the free end starts preparation; increasing inter-hand separation by 8 cm opens it to an L. This is a guided gesture/animation, not simulated metal deformation or a rigid attachment of the free tip to the assisting controller. The generous assisting-hand contact zone accommodates controller shells. Partial progress remains on release and recovery. A folded or partly prepared pin cannot enter the insertion constraint. Completion sends one 45 ms pulse at amplitude 0.55 to each participating hand. Native breakage supplies a fresh folded pin.

The installed hand assets now contain a fitted thumb/index pinch as well as the existing open/curled poses. The bone fit works from the installed meshes, keeps joint angles bounded, and produces an estimated pad gap of 4.06 mm on each hand. Its midpoint is exported and used by both contact detection and the held pin. The preparation helper uses the same pose. The screwdriver continues to use the palm-centred curled grip. The binary loader validates the new pose/anchor data and retains compatibility with old two-pose files. Generated game assets remain private and excluded from the addon package.

Pin angle now follows horizontal movement of the pinch point in the fixed lock frame, with 6.5 cm from neutral to either search limit. Wrist roll alone no longer changes the requested pin angle. Regripping rebases position without a jump; applied screwdriver tension holds the current pin angle. Insertion, supported release, extraction and gravity continue after preparation.

Four C++ suites pass, including both primary-hand assignments, partial preparation and release/regrip, no opening from translating both hands together, folded-insertion rejection, fingertip versus palm contact, one-shot preparation feedback and release cancellation, supported regripping, sideways search, preparation preservation after recovery, folded replacement, native resistance bounds and the existing driver/physics regressions. Both production and developer 32-bit DLL builds pass.

The private native trial loaded the new pinch assets (`hand_pinch available=1`) and verified:

- A folded pin passing through the insertion gate remains unseated and applies no torque.
- A 4 cm separation increase produces half-open geometry; releasing/regripping retains that progress; a further pull reaches ready state with one preparation cue per hand.
- Prepared pin and screwdriver seat after the existing short push; both render in stereo with the new hand pose.
- The unchanged 2° screwdriver wrist movement reaches 4.5° native cylinder rotation with health 1.42. Supported release returns the cylinder to zero; regripping remains neutral.
- Sideways pin movement changes the native requested angle from 90° to 20°. A full driver request reaches native resistance at about 4.84°, then native damage reaches zero. Contact/strain cues and one break pulse per holding hand are accepted. The driver remains held while a folded replacement appears on the tray.

Stereo evidence: [folded pin grip](stereo-pin-folded.png), [prepared pin](stereo-pin-prepared.png), [inserted tools](stereo-pin-inserted.png). These use synthetic poses and a simulated headset; they demonstrate geometry, ownership, native integration and rendering, not physical comfort or optical finger tracking. The earlier all-input routing and real-door validation limits remain in effect.

The production headset DLL excludes synthetic input. Private test automation is disabled and the test game/compositor are stopped. All eight regular-install files match this revision's fresh baseline. The release archive contains source, DLL, configuration and diagnostics, with no derived hand meshes or game textures.

## Direct video capture of the VR workspace

Plugin version 11 adds opt-in video capture of the separate OpenXR layer, which is absent from the game's desktop mirror. Three reusable staging pairs copy owned eye images before release. Completed pairs are published only after successful stereo presentation, using non-waiting GPU readback and a sequence-protected shared buffer. The external Python recorder produces silent H.264 MP4 via FFmpeg, with optional single-eye cropping, delayed start, fixed duration and Ctrl+C finalization. A token and heartbeat isolate sessions; an abandoned request stops capture after approximately five seconds. Recording has readback/encoding overhead and is not an application performance benchmark.

Five test suites pass. The new stream tests reject stale session tokens, incomplete/oversized buffers, stopped streams and odd/in-progress publications, and verify the two-eye payload remains a single complete pair. Both 32-bit DLL variants build with warnings treated as errors.

Private native recording checks:

- A software-encoded stereo clip contains 300 frames at 2048×1024 and 30 fps, exactly 10 seconds. A decoded frame visibly contains the addon lock, tray and hands.
- After correcting the NVENC probe to use a supported 512×512 input, a GPU-encoded stereo clip contains 240 frames and lasts 8 seconds. All 16 decoded samples at 2 fps differ during controlled hand motion.
- A single-eye NVENC clip interrupted by Ctrl+C is valid 1024×1024 H.264, with 94 frames and 3.133 seconds. The request and recorder lock are removed.
- The final external-recorder timing change briefly waits for a just-late producer before repeating a frame. Its four-second single-eye NVENC check contains 120 encoded frames, 114 fresh and six repeated. Repetition remains explicitly counted; a 30 fps container does not imply 30 distinct application frames per second.
- A request without a heartbeat started capture and then stopped automatically after about 4.42 seconds of observation (filesystem timestamp granularity affects the observed interval). Native log counters reported no staging-ring overflow in these trials.

Normal game interaction, pick preparation and haptics were not changed or revalidated in the headset by this revision. The earlier native interaction evidence remains applicable. Capture frames are readback diagnostics; WiVRn latency, physical comfort and recording's gameplay frame-time cost still need headset measurement. Windows recording remains untested.

The production DLL has developer input disabled, test automation is disabled, and the private game/compositor have stopped. Eight protected regular-install files match the fresh baseline. The package includes source and recorder tools, not derived game hand assets or external encoding binaries.

## Full interaction pass — 2026-11-16, plugin version 12

The prepared-pin insertion and holder pass was validated first. The user then expanded this work to include free bending and dropped-world-item recovery.

Five suites pass, including both-hand pickup/return, free bending and correction, shape-dependent usability, full pin insertion, bounded driver tilt, angular searching, supported regripping, native break replacement, gravity, exactly one drop request, rollback recovery and an empty holder at zero stock. Geometry checks sweep both the pin and cylinder ranges. Valid bent shapes keep the exposed handle in front of the lock face. Production and developer 32-bit builds compile with warnings treated as errors.

Native simulated-headset evidence:

- The fitted v4 assets load both hands. The screwdriver uses its derived curled-palm frame; the pin uses the fitted pinch point. Missing older assets retain a fallback. Raw assets remain private.
- Both eyes render on consecutive frames. Stereo captures show the central seated pin, driver and full search arc; the main pin shaft is buried and its bend stays at the face. Release/regrip leaves the native cylinder at zero until a deliberate twist.
- Two hands produced 45°, 95°, 150°, 110°, fully refolded 0° and corrected 90° shapes in sequence. The shape remains continuous and can be corrected without spawning a new pin. A tighter helper contact region and bounded visual fingertip correction reduce the gap seen in the initial capture.
- Native resistance and break feedback remain operative. An unsuitable search angle reaches the native jam, health loss and break states; a replacement appears folded with the default preparation setting.
- A real inventory transaction reduced stock from 16 to 15 and returned world reference `FF002D98`. The saved reference survived reload; ordinary activation returned stock from 15 to 16. Measured pickup distance after reload was 11.39 game units from the player.
- A pin damaged through native resistance fell from condition 1.420 to 1.363. Withdrawing and dropping it reduced stock from 16 to 15 and restored the replacement to 1.420. The test harness was changed to publish synthetic input atomically after a partial file write released a test grip; the normal controller path was unaffected.
- Dropping the last pin reduced stock from 1 to 0 and created reference `FF002D83`. The workspace remained visible with an empty pin holder and did not grant a replacement.

For inventory testing, the native practice menu was linked to an existing door reference in the private copied save. Only the target pointer was assigned by the test harness; the hidden solution was never read or written. This establishes the actual native drop, inventory count, world reference, pickup and save-persistence path. It does **not** establish the ordinary door-activation path, successful real-lock completion, XP or crime behavior. Practice resistance tests ran without that target assignment, because practice menus do not initialize the real-target pin-count UI gate.

Drops run on the game thread through xNVSE's public scripting interface. No inventory item is removed before the pickup exists. The count is checked before acknowledging a replacement; failure removes the new pickup. A successful replacement resets only the current pin's condition to the native initial value captured when the attempt opened. Native difficulty, resistance and completion remain responsible for the lock outcome.

Bending is a hinge model with a variable plane; it does not simulate deformable wire along its whole length. The ordinary inventory stack does not preserve per-pin bend shapes. Holders are configurable workspace pockets fixed from the initial head pose, not modifications to the mod's body holsters. Tool/body/world-mesh collision and real headset comfort remain outside headless validation.

## Housing, insertion and repeated drops — 2026-11-16, plugin version 13

The headset report identified an unwanted tray, missing later floor pickups, an insertion guide that appeared to require a vertical pin, and tools passing through the housing.

- Removed the rendered tray and its support collision. Lost reusable tools return to their holders; hiding pocket meshes no longer selects an invisible tray layout.
- The guide accepts inclined shafts independently of wrist roll. Once the tip catches, hand travel along the captured shaft direction advances insertion. The previous raw-tip check could cancel the guide when wrist rotation moved the unconstrained tip away. Backing off or moving the hand sideways still cancels it; reversed shafts remain rejected.
- Held and loose tools now sweep against the backplate, outer cylinder and face rings, with an open central keyway. Sampled tool volumes cover the shaft and handle, including intermediate rotation and fast motion. Contact permits sliding and a short haptic tap; native resistance/strain cues keep priority. These are local housing collisions, not game-world, body or tool-to-tool collision.
- The user’s v12 log already showed twelve committed drop transactions with inventory decrements. Reference creation alone did not establish visibility or accessibility. Pickups now spawn at separated final positions through JIP’s `PlaceAtCell`, rather than stacking at one point after `PlaceAtMe` and three immediate `SetPos` calls. The earlier placement/initialization issue is a suspected cause, not a conclusively isolated engine defect.

Six regression suites pass. New coverage includes both hands at 0°, ±35° and ±60° shaft tilts with ±90° rolls, wrist relaxation after contact, repeated drop requests through zero stock, front/side/back and swept-rotation collisions, free space, an open keyway, surface sliding, gravity at 20/90/180 Hz, and haptic priority/cancellation. Both 32-bit builds compile with warnings treated as errors.

Native simulated-headset testing verifies both tools stop against the housing and pull away, a 45° inclined/90° rolled pin stays caught through a further 60° wrist relaxation and seats on a push, and supported release/regripping remains neutral. Four consecutive drops returned four unique references and reduced inventory 16 → 12. After leaving the workspace, all four references had loaded 3D and were enabled at distinct nearby positions (13–66 game units away). Activating each returned exactly one item, restoring inventory 12 → 16. This uses a copied save and a practice menu linked to a real door, not an ordinary gameplay activation.

A diagnostic capture revealed that checking for a screenshot request separately in each eye could combine an old left image with a new right image. The request is now sampled once before either eye and acknowledged only after both copies and presentation succeed. This affected exported still images; the live stereo scene is computed once and shared between eyes. The final native build repeated the collision/insertion checks and exported four consecutive matching pairs. See [housing contact](stereo-v13-housing-contact.png) and [inserted tools](stereo-v13-inclined-inserted.png).

The user’s preceding v12 gameplay log records two real-target attempts reaching native stage 7 with cylinder 90° and remaining pin health, followed by menu closure. That provides real-lock completion evidence; XP/crime effects were not recorded.

A local optimized CPU microbenchmark measured about 25 µs per free-space sweep and 0.48–0.58 ms for aggressive contact/rotation sweeps. These are isolated routine timings, not headset application frame times. Physical contact feel and insertion comfort require a headset check.

## Soft pin engagement and physical stock — 2026-11-16, plugin version 14

The headset log showed a 65° pin held against the housing without engagement. The old 80° usability gate rejected that bend. Only a still-folded pin now requires further preparation (minimum approximately 36°); other bends engage when the straight tip reaches the keyhole. There is no required pin insertion stroke. Poor bend, entry alignment and shallow depth increase search sensitivity without changing the hidden solution. Optional depth changes do not themselves move the requested pin angle. Depth is bounded to leave the held leg outside the face. A deliberate pull into clear space also recovers a pin wedged by housing contact without resetting the menu.

Native resistance now adds visible pin trembling. The animation is sampled once for both eyes and applied only to the copied render pose; interaction geometry, native angle and difficulty remain unchanged. The strong contact/pause/strain haptics remain in place.

The left holder now contains up to 18 individual physical pins, representing available stock without a text counter. Folded pins lie lengthwise. Simplified wire contacts, gravity, damping and sleep settle the pile. Grabbing selects an actual nearby pin; a loose pin dropped back into the fixed box rejoins it after settling. Drawing and returning do not spend inventory. A drop outside retains the verified native pickup transaction. The box is fixed, its stored contents are contained, and only one pin is actively handled at a time. Moving/tipping the box, controller-driven stirring and unrestricted pile spilling are not implemented. Bend shapes survive storage during an attempt but not inventory save/reload.

Eight regression suites pass. New coverage includes either hand at 40°, 65°, 90°, 120°, 150° and 162° bends; immediate engagement, depth adjustment, regripping, withdrawal/reuse, held-leg clearance, the reported stuck-contact fixture, and render-only tremor cancellation. Full 18-pin piles settle and sleep within five seconds at 20, 90 and 180 Hz. Both-hand repeated box returns leave stock unchanged; outside drops request exactly one transaction and zero inventory empties the box. Production and developer 32-bit DLL builds pass with warnings treated as errors.

Private native simulated-headset checks:

- A two-handed 65° bend engaged immediately at 6 mm depth, then pushed to 26 mm. It could be released, regripped, withdrawn and reused without resetting the menu.
- Normal native resistance and breakage still occurred. Recorded render tremor ranged from −2.168° to +1.570° in the sampled jam frames and returned to zero on release. This is animation amplitude, not controller haptic force.
- Inventory 22 displayed the capped 18-pin pile. All 18 eventually reported sleeping. Drawing and gravity-returning a pin left inventory at 22.
- Inventory three showed three pins. Dropping one outside committed a real transaction from three to two and created pickup reference `FF002D83`. Zero inventory showed an empty box. These checks use a copied save and a practice menu linked to an existing door, as in the earlier inventory tests; they do not establish XP/crime behavior.
- Full-box testing exposed the renderer's old 32,768-vertex capacity. Increasing it to 65,536 accommodates the pile and both local hand meshes. The final full/partial/empty-stock session had no `render_bypass frame_failed=1` events.
- Matching stereo stills show the [full physical box](stereo-v14-pin-box-full.png), [three-pin stock](stereo-v14-pin-box-three.png), and [shallow 65° pin engagement](stereo-v14-soft-pin.png). A private stereo video of resistance/release contains 90 frames at 2048×1024 over three seconds: 88 fresh and two repeated producer frames. This diagnostic capture does not measure headset performance or comfort.

The production build has developer input disabled. Installation remains isolated from the regular Steam profile. Headset ergonomics, perceived shaking and extended pile stability require a play test; Windows runtime behavior remains untested.

## Entry continuity and rummaging — 2026-11-16, plugin version 15

The v14 headset report clarified that the pin still jumped into position on insertion. Immediate engagement had removed the insertion stroke, but retained a broad 28 mm capture zone and replaced the approach pose with a central, aligned pose. This revision removes those two causes. Engagement requires the collision-resolved tip to reach the actual slot. It preserves the incoming position and orientation, including lateral offset, tilt and roll. The supported pin swivels about its real entry contact and can slide deeper; housing contacts limit movement. Release/regrip retains that supported pose. The screwdriver controls and native solution/difficulty logic are unchanged.

Either empty hand can now disturb the physical pin stock. Three swept finger/palm proxy spheres transfer bounded movement and angular impulses to the wire bodies, waking the pile. Contact is limited to the box interior, excludes hands holding tools or assisting a bend, ignores large tracking jumps, and does not change inventory. Pins remain contained in the fixed box and settle/sleep after the hand leaves. These are approximate controller-driven contacts, not articulated finger collision, force feedback or a movable/spillable box.

Nine suites pass. The new continuity tests exercise both hands, 65°, 90° and 135° bends, ±35° approach tilt, and a nonzero lateral entry/roll. Entry and the following frame retain position within 0.15 mm with no canonical orientation jump. Regripping and withdrawal remain valid. A 20 mm near miss no longer engages. At 20/90/180 Hz, rummaging moves the settled pile, keeps all 18 items, and lets them sleep again; stationary/far-away hands and tracking jumps leave it alone. Both-hand integration tests preserve inventory and do not generate drop requests. The previous stuck-pin recovery, box returns, outside drops and haptic regressions also pass. Both 32-bit builds compile with warnings treated as errors.

Native simulated-headset validation in the copied-save lab confirms:

- An approach at x=1 mm, y=7 mm, z=26 mm with 20° tilt and 25° roll remains at that actual position/orientation after engagement. The measured position matches within 0.2 mm. See [the native entry pose](stereo-v15-entry.png).
- Supported release, regrip at a different wrist orientation, withdrawal and reuse work. A 20 mm near miss stays outside; a central entry permits voluntary depth and searching.
- The initial chosen search angle turned freely. Moving to the other end of the visible search arc reached native resistance at 5.65°, accepted contact/strain haptics, and broke the pin normally. No hidden solution was read or modified.
- Both hands logged `pin_box_contact` while sweeping through the pile. Stock stayed at 22 with the display capped at 18. Selecting a pin afterwards and returning it by gravity also left stock at 22. See [the pile before](stereo-v15-box-before.png) and [after both hands](stereo-v15-box-after.png).
- Both eyes rendered and the final native session had no frame-failure bypass events.

These checks establish native rendering and interaction, not headset comfort or application frame-time cost. The production DLL excludes synthetic input; test automation is disabled after deployment. The regular installation remains unchanged. The v14 entry/depth figures above are historical and are superseded by this revision's pose-preserving contact.

## Pin recovery and cylinder-relative control — 2026-11-16, plugin version 16

The headset report identified a bad insertion that could not be removed, an off-centre pin that did not turn with the keyway, and apparent success from multiple visible positions. The log captured an over-bent pin (116.1°, bend plane −0.449 radians) at position (0.00299, −0.01496, −0.00196) metres that remained blocked after leaving the inserted state.

A deliberate clear-space pull now recovers either an engaged pin or a free pin trapped by the housing sweep, retaining ownership and shape. Re-engagement is blocked until the raw tip clears the face. Capture also requires actual inward raw-tip motion near its constrained position; collision correction alone cannot pull a distant hand back into the slot.

The supported pin, contact point and manipulation references now follow cylinder rotation. Under screwdriver tension the search pose stays fixed relative to the cylinder, so moving the hand cannot visibly search while the native angle remains frozen. Unloading rebases control without a jump. The native angle is derived from the constrained visible held leg in the cylinder frame, independent of the contact offset. The previous unconstrained-hand sensitivity multiplier is removed; quality remains diagnostic. Native difficulty, resistance, damage and completion code are unchanged.

Ten suites pass. New coverage includes the exact headset wedge for both hands in engaged/free states, withdrawal and reuse, off-centre contact rotation through 0–90° and back, loaded hand motion, unloading, independent contact offsets, and agreement between the constrained pin and native request. Existing no-snap entry, geometry, box, haptic, inventory and capture regressions pass. Both 32-bit builds compile with warnings treated as errors.

Native copied-save validation:

- An off-centre entry at (1, 7, 26) mm followed a 22.4° cylinder turn. Logged contact positions matched the rotated entry within 0.04 mm; visible/requested angles agreed within 0.005°. Moving the hand under tension did not change that angle, and unloading did not jump it. See [seated](stereo-v16-offset.png) and [turning](stereo-v16-turning.png).
- In one practice attempt, visible angles 22.595°, 90.952° and 156.636° produced maximum cylinder rotations of 5.47°, 26.88° and 26.88° under the same limited torque request. The first reached native jam/damage; the others reached the requested rotation without damage. This verifies different resistance without reading or changing the hidden solution; it does not prove every possible real-lock outcome.
- The headset's bend and approach orientation were recreated. A bad fit contacted the housing at roughly 29 mm depth, then a deliberate forward pull released it. The same pin stayed held, retained its shape and re-engaged in the same attempt without a reset. See [recovered pin](stereo-v16-recovered.png).
- No frame-failure render bypass occurred. The private file-input harness was stabilized with fixed-size in-place updates after stale pose samples and virtual-filesystem rename delivery disrupted preliminary setup. The production build has no file-input route.

Actual headset comfort, extended gameplay and application frame time remain unverified for this revision.

## Fallout prop visuals — 2026-11-16, plugin version 17

Seven locally converted mesh parts replace the simple lock, tools and holders when `UseGameProps=1`. Source geometry comes from the installed Fallout BSAs; the local lock/tool diffuse texture comes from the user's NV Lockpick archive. The Bobby Pins carton has an opened lid, visible original label and an added inner shell matching the existing pile bounds. The rusty can holds the screwdriver. Original pin mesh sections follow the current bend and twist with a short curved connector. Meshes and textures remain private dependencies and are excluded from releases.

All eleven Linux CTest suites pass, including malformed/truncated asset rejection, bounds/finite/index checks and atomic preservation of the previous mesh on a failed load. Production and developer 32-bit builds compile. The actual converted file contains seven parts and 1,252 triangles before repetition of stored pins. Normal-map and diffuse textures have mipmaps and anisotropic filtering. The renderer retains its own swapchain ownership and deferred-context state preservation.

Private Proton/Monado checks:

- Both eyes rendered the corrected prop set over consecutive frames. Source textures, open lid, hollow can, tool materials and cylinder rotation were inspected in stereo captures.
- A target-linked practice menu showed **16 individual pins for 16 inventory pins**, all settled. Drawing the folded pin removed it from the visible stock without an inventory decrement. See [the box](stereo-v17-stock.png) and [held tools](stereo-v17-tools.png). The initial capture harness assumed an older 18+ inventory count; its assertion was corrected to use this save's actual count.
- The pin engaged at an off-centre entry and both tools seated. Releasing the tools left them supported. A 10° wrist request produced a native 22.4° cylinder turn with the pin following it. Unloading returned the cylinder. See [supported tools](stereo-v17-inserted.png) and [turning](stereo-v17-turning.png).
- `UseGameProps=0` restored the complete simple geometry at the next attempt. No rendering failure bypass was logged.
- The final production DLL also rendered textured stereo over consecutive frames with no synthetic-input code. Its installed hash matched the release build. A head-only close-view capture confirmed the fitted prop geometry.
- The pin-recovery mechanics from version 16 remain in place, with their regression tests passing. This visual pass does not change difficulty, inventory/drop transactions, haptic parameters or native angle hooks.

The production build is installed with preparation enabled. Test automation is disabled, test control/request files are removed, and the private game/compositor are stopped. All eight protected regular-install files still match the previous fresh baseline.

The actual door or safe is not shown. The working tool axes align with the original lock, but collision still uses the bounded housing and rectangular bore rather than every decorative recess. The opened lid is not articulated or a new collision target. Real-headset clarity, comfort and application frame time are not validated by these simulated captures. Native Windows remains untested.

## Lock proportions and pin readability — 2026-11-16, plugin version 18

The original lock frame and plug now use the same lateral scale. The plug radius is 50.84 mm for a nominal 75 mm outer frame, restoring the source mesh's approximately 68% ratio (previously 43%). The face collider and simple-geometry fallback use the same plug radius. The rectangular working keyway grows to 14.2 × 58.8 mm; wire-margin entry tests and collision share those dimensions. No canonical entry snap is reintroduced. Decorative recesses remain an approximation rather than mesh-accurate collision.

The native wavy pin section had been scaled to one-third of its longitudinal scale in its wave direction. Its lateral proportion is restored, with restrained metallic highlights to approximate the original pin's environment reflection. Keyhole depth shading is attached to the plug's local depth and remains consistent across both eyes and cylinder rotation. The mounting plate is dimmer so the lock and tools stand out.

The primary pin hand uses a bounded visual precision grip: up to 20° backward palm tilt and a pad contact 10 mm farther along the held leg. Input still uses the original tracked pinch. This correction stays within the existing hand-translation/rotation limits; the assisting hand keeps its previous contact behavior. Zero correction settings disable it. No new hand assets are required.

Twelve CTest suites pass. New checks cover the restored plug face, entry just inside/outside the resized keyway, rejection of old prop dimensions, contact preservation for both pinch anchors, and translation/rotation bounds. The existing insertion continuity, housing collision, recovery, inventory pile and haptic suites remain passing. Both 32-bit builds compile. Prop assets use `TTWProp2`, so an old local mesh falls back safely until regenerated.

Private Proton/Monado evidence:

- The target-linked practice menu displayed 16 pins for the save's 16 inventory pins. Folded and prepared pins, the broader centre plug and dark recess rendered in both eyes. See [overview](stereo-v18-stock.png), [folded pin](stereo-v18-folded.png) and [prepared pin profile](stereo-v18-pin.png).
- Both tools drew and seated, remained supported when released, and followed a native 22.4° cylinder turn. Releasing tension returned the cylinder. [Supported tools](stereo-v18-inserted.png).
- The previously reported 116.1° awkward bend and approach quaternion were recreated. A deliberate pull released the pin, and the same bent pin reinserted in the same attempt without resetting.
- The simple-geometry setting fallback rendered successfully. No frame-failure bypass was logged in the final native trials.
- The production DLL rendered consecutive stereo frames without synthetic-input code. Its installed hash matches the release build. Test automation and control files were cleared, the game and private compositor stopped, and all eight protected regular-install files remained unchanged.

Headset feel, physical grip alignment and actual application frame time still require a real session.

## Still requires a headset/gameplay check

- Reach, wrist direction, comfort and visible hand/tool alignment on Quest/WiVRn.
- Physical haptic feedback and controller-button coexistence.
- Native keyboard F8 and controller B/back behavior while wearing the headset.
- XP and crime consequences on real doors/containers. The user’s v12 log records real-target native completion; the automated inventory test uses a target-linked practice menu.
- Tracking loss and recenter behavior during active manipulation.
- Measured application frame-time impact and extended gameplay stability.
- Windows installation and other TTWVR versions.

## Current scope

The prototype is a reachable stereo workspace over a dimmed, blurred game view, with locally prepared Fallout lock/tool meshes, a Bobby Pins box, a tin-can screwdriver holder and TTWVR hands. Four target model profiles surround the working lock; the ammo box uses a padlock and the safe uses a dark rounded-square frame. Exterior collision, free pin bending and inventory-backed drops remain. Unsupported assets use simpler geometry or the standalone lock. It does not place the workspace on the actual world object or provide manual door opening. Existing game audio and native lock difficulty remain in use.

## Version 19 — round lock, loose screwdriver and game background

The rectangular mounting plate and four bolts have been removed from the visual
mesh and collision distance field. Tools collide with the circular housing;
space behind/outside that housing is open. Existing insertion, collision,
withdrawal and wedge-recovery regressions still pass.

The screwdriver is a loose rigid body in the open tin can. Its tip rests on the
bottom and its shaft/handle can lean against the rim. Contact impulses include
angular inertia and friction, with finer substeps for fast motion near the can.
Release near the holder no longer teleports it to a stored pose. Lost-tool
recovery remains. The new can suite verifies an open top, solid sides/bottom,
settling, stability and dropping into the container at 20, 90 and 180 Hz. It
settles around 16.2 degrees from vertical in the default setup. All **13 suites**
pass.

The background uses current-frame application projection images copied before
`xrReleaseSwapchainImage`, while the successfully waited images are still owned
by the application. Only addon-owned copies are sampled afterwards. Both eyes
must be current; rectangle, array slice, field of view and relative orientation
are respected. Unsupported/missing/incompatible sources retain the dark
workspace. The implementation follows the OpenXR
[image-release contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrReleaseSwapchainImage.html).
Two 512-pixel separable blur passes and a dimmed composition run on the GPU,
before the sharp lock, tools and hands. No CPU image readback is used except for
explicit diagnostic captures/video. The D3D11 command list restores app state.

Private Proton/Monado tests:

- Live world imagery appeared in both eyes behind the lock, without a duplicate
  original minigame. The tested sources are separate 2944×2208 BGRA sRGB images.
- Either hand picked up the leaning screwdriver, lifted it and released it back
  into the can. It settled again without the old proximity snap.
- Both tools inserted and remained supported on release. In a fresh native
  practice menu, a wrist turn reached 14.52 degrees and native resistance;
  releasing tension returned the cylinder to zero. This is a resistance check,
  not a new completion/inventory/XP validation.
- The initial artificially target-linked inventory fixture stopped advancing
  the native lock while the visual interaction continued. Reopening the normal
  practice menu restored native input. That fixture is kept separately in the
  local evidence; real door/safe activation still needs headset confirmation.
- Background off/on was exercised in separate attempts and captured for both
  eyes. Consecutive frames used the background without a render-failure bypass.

The production DLL rendered consecutive stereo frames with the background and
16 visible pins, without synthetic-controller code. Its installed hash matches
the release build. Test automation/control files were removed and the private
game and compositor stopped. All eight protected regular-install files match
their previous hashes. See the [workspace](stereo-v19-workspace.png),
[production close view](stereo-v19-close.png), [inserted tools](stereo-v19-inserted.png)
and [background disabled](stereo-v19-off.png).
Headset comfort, physical can handling, WiVRn latency and GPU frame-time cost
remain unmeasured in a real headset. Array images, multisample resolves and
unsupported-source fallback paths are implemented but not exercised by this
runtime's source layout.


## Target objects — version 20 (2026-11-16)

The four prepared targets are the standard freestanding safe, ammo box, left office door and right school door. All 14 regression suites pass, including bounded target-asset validation, path normalization, animation interpolation/gating and object/keyway collision. The production 32-bit build passes warnings-as-errors.

A developer build in the isolated Proton/Monado installation selected all four models from newly created, normally activated locked references. No menu target pointer was injected. The physical controls drew and inserted both tools into the safe, reached native cylinder 90 with pin health 1.42, and triggered real-target completion state **7**. `GetLocked` then returned zero. This distinguishes real-target completion from the earlier targetless practice-menu state 6. The opening trigger now explicitly uses state 7; jam, break and other states cannot trigger it. The test did not read the hidden solution or call Unlock.

Stereo captures show the original diffuse/normal textures and authored geometry. All four Open tracks were additionally exercised through a developer-only preview override. This does not substitute for native success: only the safe's success-to-animation transition has been exercised end to end. The safe lock was aligned with its original keyhole using its UV mapping. Door and ammo-box placements need physical headset review.

The safe and ammo box's original tracks turn only about 5 degrees. Office/school tracks turn 105 degrees. The display follows native menu lifetime, so a long door track can be cut short when returning to gameplay. No additional reference activation, menu delay, inventory change or forced unlock was added. Collision is the closed object's bounding box with working-keyway clearance, not its animated triangle mesh. Form-specific texture swaps and unsupported models use the standalone fallback.

The final production DLL (synthetic inputs and preview override compiled out) selected all four targets through normal activation and rendered consecutive frames in both eyes with no failure bypass. An unsupported toolbox and `UseTargetObject=0` rendered the standalone fallback. See [safe](stereo-v20-safe.png), [ammo box](stereo-v20-ammo.png), [office door](stereo-v20-office-door.png), [school door](stereo-v20-school-door.png), [disabled](stereo-v20-setting-off.png), and the developer build's [native successful unlock](stereo-v20-success.png).

The production DLL is installed in the isolated headset profile. Preparation and target presentation are enabled, synthetic files removed, automation disabled, and the private game/compositor stopped. Eight protected regular-install files still match their baseline hashes. Private game-derived target assets are excluded from the source and release ZIP. Headset reach/comfort, actual application frame time, WiVRn latency and a native Windows run remain unvalidated.


## Ammo-box padlock — version 21 (2026-11-16)

The ammo-box profile selects a procedural padlock with its own rounded body,
shackle and attachment loop. It retains the existing Fallout cylinder, metal
atlas, working keyhole and controls. Geometry and collision share dimensions.
Old target caches retain the round lock; updated caches explicitly select the
style, with invalid combinations rejected.

All **15 regression suites** passed after the style/collision changes. Target
and padlock suites passed again after the final rim-UV and shackle-cap refinement.
Coverage includes both hands, multiple pin bends, no-snap insertion, supported
regripping and withdrawal, swept contact with the body/shackle/attachment loop,
keyway clearance at multiple angles, cache compatibility and success-only
shackle release. All four locally prepared target files passed the asset tests.

The isolated developer build activated a real Very Easy ammo-box reference in
the test stage. Synthetic controller poses drew a folded pin, bent it to 90
degrees, inserted both tools, searched the visible angle and turned against
native resistance. Native success reached state **7**, cylinder **90 degrees**,
pin health **1.931**, followed by `GetLocked=0`. The addon recorded
`target_opening kind=2 native_stage=7` and submitted stereo frames showing the
release. The test did not read the hidden solution or call Unlock. The native
[success capture](stereo-v21-success.png) precedes the final cosmetic UV/cap
refinement; no interaction or outcome code changed afterwards.

The final production DLL has synthetic-controller input and animation preview
compiled out. Runtime checks select all four supported targets, retaining round
locks for the safe and doors. Native cancellation leaves each tested reference
locked and never triggers the opening animation. The unsupported toolbox,
`UseTargetObject=0` and `UseGameProps=0` fallbacks have separate stereo captures.
Consecutive stereo frames render without a failure bypass. See the final
[padlock](stereo-v21-padlock.png) and [untextured fallback](stereo-v21-untextured.png).

The shackle lifts and turns around its retained leg only on native success;
the attachment loop follows the lid track while the body and inserted tools
stay at the working position. The native menu lifetime still limits the display.
Manipulation uses closed-pose collision. This does not implement a loose falling
padlock or manual lid opening.

The final DLL and updated private assets are installed only in the isolated
headset profile. Test automation is disabled, temporary input/preview controls
are removed, and the private game/compositor are stopped. All eight protected
regular-install files match their baseline hashes. No extracted game models or
textures are included in the source/release. Physical reach, WiVRn comfort,
application frame time and native Windows behavior still need user testing.


## Compact padlock — version 22 (2026-11-16)

The casing is reduced from 146 × 130 mm to 76 × 64 mm, with a smaller shackle,
attachment loop and 20 mm release travel. The body now samples the ammo-box
latch material and the shackle samples its handle steel. The smaller original
cylinder receives a subdued warm steel finish; other surfaces are unchanged.
The placement profile keeps the shorter shackle attached to the box's latch.

The working contact/depth plane and slot width are preserved. Slot height is
48% of the original, with matching mesh, collision and pin-capture limits.
The original cylinder is warped around its slot, preserving the working width
while reducing the outer diameter to 50 mm. Normals follow that deformation.
All **15 suites** pass, including padlock body/shackle contact, both-hand
insertion and withdrawal, varied bends and keyway clearance through rotation.
All four regenerated private target caches pass asset validation.

A real Very Easy ammo-box reference was activated in the private gallery.
Synthetic hand poses drew and bent a folded pin, inserted both tools, released
and regripped them, found resistance at 90 degrees of pick search, then unlocked
at 120 degrees. Native success reached stage **7**, cylinder **90 degrees**,
health **1.975**, with `GetLocked=0` and the padlock-release animation submitted
in both eyes. No hidden solution was read and Unlock was not called. The first
automated reach-to-bend attempt used a stale throttled pose log; waiting for
fresh telemetry resolved that harness timing issue without a gameplay change.

See the [compact padlock](stereo-v22-padlock.png) and
[native successful unlock](stereo-v22-success.png). The final production build
renders all four supported targets and the unsupported/disabled/untextured
fallbacks in both eyes without a failure bypass. Native cancellation leaves
all tested references locked and does not trigger the release animation.
The headset profile contains the production DLL and updated private assets;
test automation is disabled and the private game/compositor are stopped.
All eight protected regular-install files match their baseline hashes.

Headset reach, comfort and the visual fit still need the user's review. The
simulated runtime validates rendering and native logic, not physical controller
comfort or WiVRn latency. The smaller opening may require more precise vertical
placement; no controller sensitivity or native skill/difficulty value changed.


## Door keyhole alignment (version 22 asset correction, 2026-11-16)

The office-left and school-right anchors now use the upper round keyhole's
UV-derived model position, including depth. Updating only the horizontal and
vertical coordinates initially left visible parallax because the working lock
floated in front of the door. The final prepared geometry places each keyhole
on the working axis at the mounting plane. A calibrated front face keeps the
simple body collision behind the lock despite the projecting knob; detailed
knob collision is not implemented.

All four prepared target caches pass the existing target validation executable.
Safe/ammo geometry and every prepared texture are byte-identical to the previous
revision. The production DLL is unchanged. Native activation selects both door
variants, renders them in both eyes without a failure bypass, and cancellation
leaves the tested references locked. The corrected assets are installed in the
private headset profile; test automation is disabled and the private game and
compositor are stopped. All eight protected regular-install files are unchanged.

See the [office door](stereo-door-aligned-office-door.png) and
[school door](stereo-door-aligned-school-door.png). The visual correction still
needs headset review; no new native-success or physical-controller test was
performed for this asset-only revision.


## Rounded-square safe frame — version 23 (2026-11-16)

The safe housing is now 146 mm square with 14 mm corner radii and a 4 mm bevel.
It samples the safe's existing painted metal; the original round cylinder
receives a darker, less reflective finish. Mesh and exterior collision share
these dimensions. The working cylinder, keyhole, insertion plane and native
controls are unchanged. Padlock geometry and the aligned door profiles are
unchanged. Regenerated private assets differ only in the safe's style field;
all target geometry and textures are byte-identical to the preceding revision.

All **16 suites** pass, covering rounded corners, the bevel, swept contact,
clearance through cylinder rotation, either-hand insertion and withdrawal,
and rejection of invalid style/model combinations. Both 32-bit DLL variants
build with warnings treated as errors. A stereo preview exposed degenerate
front-projected UVs on the frame's vertical walls; those walls now unwrap
around their depth. The affected safe-frame suite and both builds passed again.

In the private gallery, ordinary synthetic hand inputs drew and bent a pin,
inserted both tools, regripped and unlocked the Very Easy safe. Native success
reached stage **7**, cylinder **90 degrees**, health **1.965**, and `GetLocked=0`.
The safe-opening track was submitted in both eyes. No hidden solution was read
and Unlock was not called. See the [safe frame](stereo-v23-safe.png) and
[successful unlock](stereo-v23-success.png).

The final production DLL renders the safe, ammo box, both aligned doors and
unsupported-model fallback on consecutive stereo frames without a rendering
failure. Native cancellation leaves each tested reference locked and does not
trigger opening. Disabling the target returns the standalone round lock;
disabling game props preserves the [untextured safe frame](stereo-v23-untextured.png).
Synthetic input is absent from the installed production DLL. Private automation
is disabled and the test game/compositor are stopped. All eight protected
regular-install files match their recorded hashes.

The simulated runtime verifies rendering and native behavior. Appearance and
reach still need headset review; WiVRn latency, application frame-time impact
and native Windows runtime behavior were not measured for this revision.


## Gameplay and lockpicking capture — version 24 (2026-11-16)

The PNG and MP4 tools now follow the active scene. Outside physical lockpicking,
the addon composes submitted stereo projection and quad layers into private
1024 × 1024 eye images. Inside the workspace it captures the actual addon layer.
Game images are copied before release; capture never reads runtime-owned images
afterward. Projection subrects, array slices, layer order/alpha, quad visibility
and spaces are preserved. Game capture does not use the workspace blur/downsample.
No capture image is added to the headset's layer list, and controls/outcomes are
unchanged. Request checks are limited to four per second when idle.

All **16 regression suites** pass, including atomic source metadata during
partial frame publication and game → lockpicking → game transitions. The
production 32-bit DLL builds with warnings treated as errors. It has synthetic
controller injection disabled. A private Monado session started capture during
gameplay before opening any lock, verifying that capture initializes independently
of the minigame. PNGs show [gameplay](stereo-v24-capture-game.png),
[physical lockpicking](stereo-v24-capture-lockpicking.png), and the
[original menu fallback](stereo-v24-capture-menu.png) in both eyes.

A 38-second H.264/NVENC clip contains 1,140 output frames: **1,046 fresh and 94
repeated**, with 758 fresh gameplay frames and 288 physical-lockpicking frames.
It switches from gameplay to lockpicking, back to gameplay, then through another
attempt to the original menu fallback. Logs confirm capture of the projection
plus a quad layer. Screenshot requests also succeed while recording. These
figures describe capture throughput under the simulated runtime, not headset
application frame time. There is no audio track.

The five-second recorder lease was tested without heartbeat renewal during
gameplay. Frame publication stopped and private capture resources were released.
Private automation is disabled after testing; the game/compositor are stopped
and all eight protected regular-install files match their recorded hashes.
The final production DLL and refreshed source package are installed only in the
private test setup.

Unavailable/static images without a copy of their latest release and unsupported
layer types cannot be captured; video repeats the last complete frame during
such gaps. Main application projection and quad layers are supported; compositor
reprojection, system overlays and streaming are outside this capture path.
Native Windows and physical-headset recording remain unvalidated.
