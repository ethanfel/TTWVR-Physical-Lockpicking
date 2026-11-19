# Help and removal

| What you see | What to check |
| --- | --- |
| “Unsupported TTWVR.dll” | Select the VR profile with the original 1.1.6 or 1.1.7 DLL enabled. Check which mod wins the `NVSE/Plugins/TTWVR.dll` conflict. |
| “Output already exists” | For another install, prepare into a fresh mod folder and then switch the enabled copy. [Manual setup](advanced.md#prepare-into-a-different-folder) |
| The game starts with ordinary lockpicking | Check the addon and `TTWVRLockKit.esp` in the active profile. Confirm the registry file points to the **managed game directory**. Under Proton, import it in the game's prefix. |
| I can’t assign the pin | Open the **large floating Pip-Boy**, highlight a bobby pin under **Items → Misc**, hold Hotkey, select a wheel direction, then release. |
| A particular object has no usable world lock | Stow the tools and activate it for the close-up interaction. Live placement covers 101 model paths. Replacement meshes can move the latch. |

## Check the log

Look for `TTWVRPhysicalLockpicking.log` under `NVSE/Plugins` in the installed addon or your manager's **Overwrite** folder. Check the entries from your latest launch.

- `plugin_loaded` with `release="Beta 0.1"` identifies the running addon.
- `plugin_bypass disabled=1` means the launch environment disabled the addon. On Windows, exit MO2 completely and reopen it using `Play-TTWVR.cmd`. On Proton, check that your wrapper passes `TTWVR_PHYSICAL_LOCKPICKING=1` to the game.
- `live_mount_surfaces loaded=1 kinds=101` confirms the live lock placement data loaded. `session_created d3d11=1 spaces=1` shows the addon's OpenXR layer reached the VR session.

## Remove the addon

Close Fallout. Disable the mod and `TTWVRLockKit.esp`. Import **unregister-ttwvr-lockpicking.reg** in the same registry used at install. On Linux, remove the added Steam launch variable. On Windows, start MO2 with your usual shortcut.

[Windows install](install-windows.md) · [Linux install](install-linux.md)
