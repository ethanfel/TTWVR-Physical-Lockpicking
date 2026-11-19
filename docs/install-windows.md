# Install on Windows

Start with a working **TTW VR profile in Mod Organizer 2**, including xNVSE and the TTWVR utilities (JIP LN, JohnnyGuitar and ShowOff). Beta 0.1 supports the original TTWVR 1.1.6 and 1.1.7 DLLs. The Windows asset exporter has been checked under Proton. Native Windows headset testing is still pending.

## 1. Download and extract

Download **TTWVR-Physical-Lockpicking-Beta-0.1.zip** and extract it to a folder you can keep, such as `Documents\TTWVR-Lockpicking`. Install **64-bit Python 3.12** with the Python launcher if needed. Python 3.11 also works. Allow about **12 GB** of temporary space on the mods drive for an NMC-sized texture setup. [Download the addon](https://github.com/ethanfel/TTWVR-Physical-Lockpicking/releases/download/beta-0.1/TTWVR-Physical-Lockpicking-Beta-0.1.zip) · [Python for Windows](https://www.python.org/downloads/windows/)

## 2. Prepare the mod

Close Fallout and MO2. In File Explorer, open the extracted folder, type `cmd` in the address bar and press Enter. Run the installer from that prompt.

~~~bat
py -3.12 install.py
~~~

Use `py -3.11 install.py` for Python 3.11. Paste the paths the installer asks for.

- **Game folder** — the folder MO2 launches `FalloutNV.exe` from.
- **VR profile** — the folder containing its `modlist.txt`, usually `<MO2 instance>\profiles\<VR profile>`.
- **ModOrganizer.exe** — accept the suggested location if correct.

Wait for **Prepared**. The installer creates a mod directory, two registry files and `Play-TTWVR.cmd`.

## 3. Register and enable

Double-click **register-ttwvr-lockpicking.reg** in the extracted folder and accept the Registry Editor prompt. Start MO2 with **Play-TTWVR.cmd**. It sets the launch variable for that session.

In your VR profile, enable **TTWVR Physical Lockpicking - Beta 0.1** in the left pane and **TTWVRLockKit.esp** in the Plugins tab. Give the addon priority over conflicting copies of its files.

## 4. Play

Launch your usual TTWVR executable from MO2. Use **Play-TTWVR.cmd** whenever you start MO2 for this game. [Assign the holster and pick your first lock](../README.md#your-first-lock) · [Troubleshooting](help.md)
