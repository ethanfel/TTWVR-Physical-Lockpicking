# Install on Linux with Proton

Start with a working **TTW VR profile in Fluorine** on x86-64 Linux, including xNVSE, JIP LN, JohnnyGuitar and ShowOff. Beta 0.1 supports the original TTWVR 1.1.6 and 1.1.7 DLLs. Use the Proton prefix and headset runtime that already launch your game. [Base TTWVR Linux setup](https://github.com/ethanfel/ttwvr-linux)

## 1. Download and extract

Download **TTWVR-Physical-Lockpicking-Beta-0.1.zip** and extract it somewhere writable. You need **Python 3.12 or 3.11** with `venv` and `pip`. Allow about **12 GB** of temporary space on the mods drive for an NMC-sized texture setup. [Download the addon](https://github.com/ethanfel/TTWVR-Physical-Lockpicking/releases/download/beta-0.1/TTWVR-Physical-Lockpicking-Beta-0.1.zip)

## 2. Prepare the mod

Close Fallout and Fluorine. Open a terminal in the extracted folder and run the installer.

~~~sh
python3.12 install.py
~~~

Use `python3.11 install.py` for Python 3.11. Paste the **managed game folder** containing `FalloutNV.exe` and the **VR profile folder** containing `modlist.txt`. Confirm the game path as Proton sees it. With the usual `Z:` mapping, `/games/Fallout New Vegas` appears as `Z:\games\Fallout New Vegas`. Wait for **Prepared**.

## 3. Register and enable

1. Use your mod manager to run `C:\windows\regedit.exe` in **Fallout's Proton prefix**. Import `register-ttwvr-lockpicking.reg` from the extracted folder. Browse through `Z:` to reach Linux files.
2. In your VR profile, enable **TTWVR Physical Lockpicking - Beta 0.1** and its **TTWVRLockKit.esp** plugin. Give the addon priority over conflicting copies of its files.
3. In Steam → Fallout: New Vegas → **Properties → Launch Options**, put `TTWVR_PHYSICAL_LOCKPICKING=1` and a space **before your existing command**. For example, a launch option containing only `%command%` becomes the following.

~~~sh
TTWVR_PHYSICAL_LOCKPICKING=1 %command%
~~~

If you use a launcher script, keep its full command and arguments after the variable.

## 4. Play

Connect your headset and launch TTWVR the usual way. [Assign the holster and pick your first lock](../README.md#your-first-lock) · [Troubleshooting](help.md)
