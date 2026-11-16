# Validation — 2026-11-16

Tested in the isolated Tale of Two Realities 1.1.6 / TTW profile through Proton and the Monado simulated headset, using the existing production physical-lockpicking DLL. This validates the game and GPU path; headset reach, readability and comfort still need the user's WiVRn test.

- All 111 model rows / 555 lock references validated against the generated catalogue; every reference is new, with no original-record overrides.
- All 14 galleries loaded through the terminal's navigation controls.
- Native terminal activation and button callbacks exercised, including repeated submenu use.
- Skill presets 0, 25, 50, 75, 100 and original (42 on the test save) verified. At skill 0, activating the Very Hard test safe did not enter the native lockpick menu.
- Five safe columns entered the actual native minigame, reporting difficulty indices 0–4 and skill 100.
- Infiltrator toggled on/off, restored from the perk submenu and restored on leaving.
- Bobblehead effect toggled between 0.01 and 0.015. Native starting pin health at skill 100 changed from 2.0 to 2.5.
- A deliberately unlocked Very Hard stage safe was reset to locked, level 100.
- Full-object selection verified for safe, ammo box and both supported doors; wall safe and toolbox selected the standalone fallback. These checks do not establish that every possible model variant has a fitted lock or working opening animation.
- Automatic entry consumed its INI flag once. Deferred teleportation allowed the return marker to be placed before entering.
- Returning restored skill 42, no Infiltrator and the original 0.01 game setting. Loading the outside save while the test bobblehead effect was active restored 0.01 too.
- Sign NIFs round-tripped with the correct little-endian header, native shader type and existing DDS texture paths. Labels and both terminal screens were visually inspected.
- Eight protected regular-install files remained byte-for-byte unchanged.

The addon DLL was not changed for this stage. Controller haptics, grip interaction and stereo comfort were not requalified by these scene/terminal tests.
