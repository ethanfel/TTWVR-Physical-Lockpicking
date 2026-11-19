# Help and removal

| What you see | What to check |
| --- | --- |
| The game starts with ordinary lockpicking | Confirm the mod and `TTWVRLockKit.esp` are enabled and that TTWVR is 1.1.6 or 1.1.7. For a manual install, check that the ESP is directly in `Data`. |
| I can’t assign the pin | Open the **large floating Pip-Boy**, highlight a bobby pin under **Items → Misc**, hold Hotkey, select a wheel direction, then release. |
| A particular object has no usable world lock | Stow the tools and activate it for the close-up interaction. Live placement covers 101 model paths, including standard safes, ammo boxes, toolboxes and many doors. Replacement meshes can move the latch. |

## Check the log

Look for `TTWVRPhysicalLockpicking.log` under `NVSE/Plugins` in the installed addon or your manager's **Overwrite** folder. Check the entries from your latest launch.

- `plugin_loaded` with `release="Beta 0.1"` identifies the running addon.
- `host_input_active backend=OpenVR` or `host_input_active backend=OpenXR` identifies the backend selected by TTWVR. The addon uses the same one.
- `live_mount_surfaces loaded=1 kinds=101` confirms the live lock placements loaded.
- `plugin_bypass disabled=1` means an environment override disabled the addon. Clear `DISABLE_TTWVR_PHYSICAL_LOCKPICKING` and any `TTWVR_PHYSICAL_LOCKPICKING=0` setting.

## Remove the addon

Close Fallout and disable the mod and `TTWVRLockKit.esp` in the profile. For a manual install, remove the files listed in the ZIP and restore any files backed up before installation. Its optional shader cache in `Data/LPVR` (or your manager’s Overwrite folder) can also be removed.

[Windows install](install-windows.md) · [Linux install](install-linux.md)
