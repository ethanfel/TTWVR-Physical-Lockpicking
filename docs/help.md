# Help and removal

| What you see | What to check |
| --- | --- |
| The game starts with ordinary lockpicking | Confirm the mod and `TTWVRLockKit.esp` are enabled and that TTWVR is 1.1.6 or 1.1.7. For a manual install, check that the ESP is directly in `Data`. |
| I can’t assign the pin | Carry at least one bobby pin and highlight it under **Items → Misc**. In **WristBoy**, keep the other hand near the wrist screen, click the right stick, tilt it toward a slot, then center it to confirm. In **PauseBoy**, hold Hotkey, choose a direction, then release Hotkey. |
| A particular object has no usable world lock | Stow the tools and activate it for the close-up interaction. Live placement covers 101 model paths, including standard safes, ammo boxes, toolboxes and many doors. Replacement meshes can move the latch. |

On supported doors and containers, the physical lock stays visible even when your Lockpick skill is too low. It disappears when the object is unlocked, including when the other side of a door is unlocked.

## Check the log

Look for `TTWVRPhysicalLockpicking.log` under `NVSE/Plugins` in the installed addon or your manager's **Overwrite** folder. Check the entries from your latest launch.

- `plugin_loaded` with `release="Beta 0.1"` identifies the running addon.
- `host_input_active backend=OpenVR` or `host_input_active backend=OpenXR` identifies the backend selected by TTWVR. The addon uses the same one.
- If a world lock appears but activation opens the flat minigame, check for `host_input_active` and `native_scene_frame ... applied=1` after activating it. If either is missing, share the addon log and `TTWVR.log` from that launch. In a Virtual Desktop setup, the `runtime` line in `TTWVR.log` identifies which OpenXR runtime handled the session.
- `live_mount_surfaces loaded=1 kinds=101` confirms the live lock placements loaded.
- `plugin_bypass disabled=1` means an environment override disabled the addon. Clear `DISABLE_TTWVR_PHYSICAL_LOCKPICKING` and any `TTWVR_PHYSICAL_LOCKPICKING=0` setting.

## Remove the addon

Close Fallout and disable the mod and `TTWVRLockKit.esp` in the profile. For a manual install, remove the files listed in the ZIP and restore any files backed up before installation. Its optional shader cache in `Data/LPVR` (or your manager’s Overwrite folder) can also be removed.

[Windows install](install-windows.md) · [Linux install](install-linux.md)
