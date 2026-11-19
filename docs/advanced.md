# Manual setup and source builds

The guided installer covers a standard managed profile. These commands are for custom layouts, texture overrides or development.

## Prepare into a different folder

Run these commands from the **extracted release ZIP** using Python 3.11 or 3.12. Create a virtual environment with `python -m venv .venv`. Activate it with `source .venv/bin/activate` on Linux or `.venv\Scripts\activate.bat` in Windows Command Prompt.

~~~text
python -m pip install -r requirements.txt
python prepare.py --data "GAME/Data" --ttwvr "MODS/Tale of Two Realities 1.1.7" --mods "MODS" --modlist "VR_PROFILE/modlist.txt" --output "MODS/TTWVR Physical Lockpicking - my profile"
python openxr.py --game "D:\Games\Fallout New Vegas"
~~~

Replace the paths with your own. `--data` supplies the original game BSAs. `--ttwvr` points to the active mod's `NVSE` and `meshes` folders. Use a **new** output directory. For `openxr.py` use the path to the managed game as seen by Windows or Wine.

- To prioritize a BSA, add `--archive "file.bsa"`, in winning-first order. To replace the lock diffuse texture, add `--lock-texture "lockinterface01.dds"`.
- For a fully deployed Data directory containing all required assets, use it for both `--ttwvr` and `--data` and omit `--mods` and `--modlist`.
- Enable the generated mod and `TTWVRLockKit.esp`, import the generated register file, and set `TTWVR_PHYSICAL_LOCKPICKING=1` for the game. For a manual Windows setup, run `set "TTWVR_PHYSICAL_LOCKPICKING=1"` in Command Prompt and launch MO2 from that prompt.

The locally generated mod includes assets derived from the player's Fallout and TTWVR installation. Share the downloadable release ZIP.

## Build the DLL

From the **repository root** on Linux, install CMake 3.24+, the i686 MinGW-w64 C++ toolchain and Khronos OpenXR headers. Set `OPENXR_INCLUDE_DIR` if `openxr.h` is outside `/usr/include/openxr`.

~~~sh
cmake -S . -B build32 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw32.cmake -DCMAKE_BUILD_TYPE=Release -DLOCKPICK_TEST_INPUT=OFF
cmake --build build32 --target lockpick
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
~~~

The release ZIP contains the same addon sources in `source/`. For a release build, compile `tools/export_hardware.cpp` as C++17 with `src/` on the include path for x86-64 Linux and 32-bit Windows. Place the DLL and the two exporters in one directory, then run `python release/package.py --bin PATH_TO_BINARIES --output TTWVR-Physical-Lockpicking-Beta-0.1.zip` from a checkout.

Original addon code is [GPL-3.0-only](../LICENSE). The project began in [ttwvr-linux](https://github.com/ethanfel/ttwvr-linux). Bethesda, TTW and TTWVR assets come from the player's installation.
