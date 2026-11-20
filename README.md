# TTWVR Physical Lockpicking

Pick a door, safe or ammo box with both hands in VR. Draw the bobby pin and screwdriver from one holster, bend the pin, then feel the lock resist as you push the screwdriver sideways.

**[Download the ready-to-play Beta 0.1 ZIP](https://github.com/ethanfel/TTWVR-Physical-Lockpicking/releases/latest/download/TTWVR-Physical-Lockpicking-Beta-0.1-Ready-to-Play.zip)**

## Install

Start with **Tale of Two Realities (TTWVR) 1.1.6 or 1.1.7** already working with Tale of Two Wastelands.

1. Install the ZIP as a mod in **Mod Organizer 2** on Windows or **Fluorine** on Linux.
2. Enable the mod and **TTWVRLockKit.esp** in your VR profile.
3. Launch TTWVR as usual.

For a manual installation, back up any matching files in `Fallout New Vegas/Data`, then copy the ZIP contents directly into that folder and enable the ESP. `TTWVRLockKit.esp` should sit alongside `FalloutNV.esm`.

The addon takes controller input and haptics from the backend TTWVR selects, including OpenVR and OpenXR. The same ZIP works with stock textures and optional texture packs such as NMC. Install the same ZIP on Windows or Linux. Python, asset conversion, OpenXR registration and extra launch options are unnecessary.

## Your first lock

1. Highlight **Items → Misc → Bobby Pin**. On the **WristBoy**, click the right stick, tilt it toward a slot, then let it return to center. On the floating **PauseBoy**, hold **Hotkey**, choose a wheel direction, then release Hotkey. Both menus bind the same holster. Place it within reach of both hands.
2. Draw with either hand to take a pin. Bend it with both hands, then draw the screwdriver from the same holster with your free hand.
3. Seat the pin in the keyhole. Press the screwdriver blade into the lower slot and push the handle sideways. If you feel resistance, ease off and adjust the pin. A replacement pin keeps the same sweet spot on that lock.

For a wrist-twist screwdriver, set `ScrewdriverMode=1` under `[Ergonomics]` in the addon's `NVSE/Plugins/TTWVRPhysicalLockpicking.ini` and restart Fallout. Insert the blade near the center alongside the pin until it seats, then twist the handle clockwise about its shaft. Pull straight back to withdraw. This works on world locks and in the close-up. Set `ScrewdriverMode=0` for the sideways push.

A picked door stays closed until you open it. Containers open on success. For the close-up interaction with the pin box and screwdriver tin, stow both tools and activate the lock. Press **B** on the right Touch controller to leave the close-up.

[Help and removal](docs/help.md) · [Manual setup and building from source](docs/advanced.md) · [GPL-3.0-only](LICENSE)
