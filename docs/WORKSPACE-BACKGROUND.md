# How the lockpicking workspace is rendered

The addon presents a reachable stereo workspace while Fallout's native lockpicking menu handles lock difficulty, resistance, damage and success. A separate room or a teleport is not needed to display it.

## Original dark workspace

1. The addon creates two D3D11 OpenXR swapchains, one for each eye.
2. It acquires and waits for its own images, then clears them to a near-black colour with alpha 1.
3. It draws the lock, tools, containers and hands from the corresponding eye pose and field of view.
4. It releases those images and appends its stereo projection layer after the application's submitted layers.

The opaque workspace covers the underlying view. The player stays in the same game location, with pausing controlled by the original menu. The “void” is the clear colour around the rendered objects, rather than a scene containing a black room.

## Current game background

While the workspace is active, the addon makes private GPU copies of suitable application colour images before their release to OpenXR. It uses each submitted eye's image rectangle, array slice, field of view and orientation, and rejects stale or mismatched stereo sources. Two small separable blur passes prepare the dimmed background. The foreground lock and tools remain sharp.

The game copies are read-only inputs for the addon. Deferred rendering and state-preserving execution keep the application's D3D11 state intact. Normal background rendering has no CPU image transfers. Diagnostic screenshots and videos deliberately add GPU readback.

The supported safe, ammo-box and door models are placed around the working lock within this workspace. Their presentation does not move the actual world object or the player. Native success can start the copied model's opening animation; the real reference retains the game's normal interaction logic.

## Settings

Edit `TTWVRPhysicalLockpicking.ini` beside the DLL, then open a new lock attempt.

| Setting | Default | Effect |
| --- | --- | --- |
| `UseGameBackground` | `1` | Show the game view; `0` restores the dark workspace |
| `BackgroundBrightness` | `0.35` | Brightness in linear light, from 0 to 1 |
| `BackgroundBlurPixels` | `12` | Blur amount in source-image pixels, from 0 to 32 |
| `UseTargetObject` | `1` | Show a supported target model around the working lock |

If a suitable game image is missing, stale, protected or unsupported, the workspace falls back to its dark background. The native minigame can be restored for the current attempt with F8; reopening starts a fresh addon attempt.

## Captures

Version 24's capture tools automatically switch between the game's submitted projection/quad layers and the physical lockpicking workspace. A recording can therefore cover approaching a lock, manipulating it and returning to gameplay. The desktop mirror does not include the separate addon workspace. See [capture usage](REFERENCE.md#video-recording).

## Relevant source

- [Layer.cpp](../src/Layer.cpp): swapchain ownership, background selection and final OpenXR layer submission.
- [Background.cpp](../src/Background.cpp): private copies, stereo source validation, blur and background composition.
- [Render.cpp](../src/Render.cpp): clear colour and foreground objects.
- [GameCapture.cpp](../src/GameCapture.cpp): private gameplay capture composition.

The [validation record](VALIDATION.md) separates simulated-runtime checks from physical-headset checks and records the current limitations.
