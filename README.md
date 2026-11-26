# TTWVR Physical Lockpicking

Pick a door, safe or ammo box with both hands in VR. Draw the bobby pin and screwdriver from one holster, bend the pin, then feel the lock resist as you twist the screwdriver.

**[Download the ready-to-play 1.1.8-r2 ZIP](https://github.com/ethanfel/TTWVR-Physical-Lockpicking/releases/download/1.1.8-r2/TTWVR-Physical-Lockpicking-1.1.8-r2-Ready-to-Play.zip)**

## Install

Start with **Tale of Two Realities (TTWVR) 1.1.8** already working with Tale of Two Wastelands. The release number after `-r` counts addon updates for that TTWVR version. This build also recognizes the previously supported 1.1.6 and 1.1.7 hosts.

1. Install the ZIP as a mod in **Mod Organizer 2** on Windows or **Fluorine** on Linux.
2. Enable the mod and **TTWVRLockKit.esp** in your VR profile.
3. Launch TTWVR as usual.

When updating, keep a copy of your customized `NVSE/Plugins/TTWVRPhysicalLockpicking.ini`, replace the previous addon in the same profile, then restore your INI.

For a manual installation, back up any matching files in `Fallout New Vegas/Data`, then copy the ZIP contents directly into that folder and enable the ESP. `TTWVRLockKit.esp` should sit alongside `FalloutNV.esm`.

The addon takes controller input and haptics from the backend TTWVR selects, including OpenVR and OpenXR. The same ZIP works with stock textures and optional texture packs such as NMC. Install the same ZIP on Windows or Linux. Python, asset conversion, OpenXR registration and extra launch options are unnecessary.

## Your first lock

1. Highlight **Items → Misc → Bobby Pin**. On the **WristBoy**, click the right stick, tilt it toward a slot, then let it return to center. On the floating **PauseBoy**, hold **Hotkey**, choose a wheel direction, then release Hotkey. Both menus bind the same holster. Place it within reach of both hands.
2. Squeeze the **grip** inside the holster sphere and pull your hand out while holding it to draw a pin. Bend the pin with both hands. Use the same grip-and-pull gesture with your free hand for the screwdriver. To move the holster, hold **grip** on the sphere for three seconds, carry it, and release. Grip with the other hand while carrying it to resize.
3. Seat the pin in the keyhole. Insert the screwdriver alongside the pin and twist. On a door, turn clockwise if the lock is on your left and counterclockwise if it is on your right as you face the door. Centered locks and containers turn clockwise. If you feel resistance, ease off and adjust the pin. A replacement pin keeps the same sweet spot on that lock.

**Choose which tool to draw.** Set `DrawToolsByButton=1` under `[Ergonomics]` in `NVSE/Plugins/TTWVRPhysicalLockpicking.ini` and restart Fallout. Hold **trigger** in the shared holster and pull out for a pin, or **grip** and pull out for the screwdriver. Either hand can draw either tool first. You can park the pin in the lock, release it, and use that same hand to draw the screwdriver. For a live Force Lock attempt, draw just the screwdriver and leave the pin in your inventory. Set the option to `0` for the original pin-first draw.

**To force a lock**, seat the screwdriver in twist mode and ease off the torque. Leave the pin parked or stowed. With your free hand open, tap the butt of the handle from behind and draw your hand back. A short metallic click and a pulse in both controllers mark the screwdriver seating another 12 mm into the lock. A fresh 15° twist in the opening direction makes one Fallout **Force Lock** attempt. Pull the screwdriver back to cancel before twisting. A failed attempt can jam the lock.

The hovering difficulty label shows **Force Lock** and the starting success percentage when you can attempt it and the chance is above zero. Under `[Ergonomics]`, `EnableForceLock=0` turns off the gesture and its odds display. For testing, set `ForceLockChanceOverride=100` for success or `0` for failure. An explicit zero also appears on the label. Restore `-1` for Fallout’s normal odds and restart after each change.

A 45° wrist twist requests a full turn. Hold your wrist still to hold your progress, ease it back to reduce pressure, or pull the blade out to release it. To adjust sensitivity, edit `FullTurnWristDegrees` under `[Ergonomics]` in `NVSE/Plugins/TTWVRPhysicalLockpicking.ini`. For the previous sideways push in the lower slot, set `ScrewdriverMode=0`. Restart Fallout after changing the INI. To restore the defaults, close Fallout and delete the addon INI. The addon recreates it on the next launch.

A picked door stays closed until you open it. Containers open on success. For the close-up interaction with the pin box and screwdriver tin, stow both tools and activate the lock. Switch on your Pip-Boy light before opening the close-up to light the lock and tools. The optional minimum brightness for the close-up is off by default (`MinimumPuzzleLight=0`). Raise it in the addon INI if you want extra visibility in dark rooms. Press **B** on the right Touch controller to leave the close-up.

[Help and removal](docs/help.md) · [Manual setup and building from source](docs/advanced.md) · [GPL-3.0-only](LICENSE)
