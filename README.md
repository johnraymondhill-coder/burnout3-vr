# Burnout 3 VR — community preview

Standalone Meta Quest 3 and Windows PCVR builds based on [Takedawn](https://github.com/xenophobentx/takedawn) and its xboxrecomp runtime. Menus and briefings use a floating screen; racing switches to stereo VR at GO. Head tracking and Quest controller input are supported.

**Bring your own original Xbox North American (NTSC-U) retail ISO of Burnout 3: Takedown. PS2 ISOs, PAL and modified executables are not supported by these builds.** No disc image, game assets, saves or extracted game files are included.

## Downloads and setup

Download the appropriate ZIP from this repository's **Releases** page. Extract the entire ZIP to a writable folder, then open **Burnout 3 VR Setup.exe**. A separate Python installation is not required. Choose your ISO and select **Install / validate my game**. Every extracted file is checked against the supported disc manifest.

- **PCVR:** connect your headset through Virtual Desktop with VDXR selected as your active OpenXR runtime, choose a resolution preset, then select **Launch VR**. Tested on Quest 3 with RTX 3060 Ti. Other OpenXR runtimes have not been validated. Performance is 1920×1440, Balanced 2560×1920, High 3200×2400 per eye. Balanced was tested; High is unbenchmarked. First-time shader compilation may hitch. Virtual Desktop's streaming settings also affect the resulting image. If available, 120 Hz streaming suits the game's 60 FPS target.
- **Quest 3:** enable developer mode, connect USB, and accept USB debugging in the headset. Refresh devices, select your Quest, then install your ISO. This installs the APK and transfers your game files. Launch from setup or the headset's Unknown Sources library. USB can be unplugged after setup; the game runs on the headset. The separate **Burnout VR Graphics** app controls graphics presets. Wi-Fi installation requires an already authorized wireless ADB IP and port.

Windows setup is required for the supplied installers. Allow several GB of free space on both the PC and Quest. Keep the setup folder if you want to reinstall; its Game folder contains your own extracted disc and must not be shared.

## Controls

Left stick: steering. Right trigger: accelerate. Left trigger: brake/reverse. A: confirm/boost. B: back. Y: change camera. Tap left Menu: pause. Hold left Menu: recenter. Right stick click also recenters. Wheel and force-feedback support is not implemented.

## Preview limitations

Quest image quality and frame rate are limited; shimmer remains. PCVR looks sharper and has confirmed stereo depth. This release does not promise compatibility with other headsets, GPUs, disc editions or existing third-party saves. Installer and original game builds have been checked locally; fresh-machine and fresh-headset installation remain to be tested by community preview users.

Report your platform, GPU/runtime, selected preset, behavior and relevant log excerpt in Issues. Never upload your ISO, extracted game files or saves to an issue.

## Credits and source

This is a VR adaptation of Takedawn, not an official Burnout or EA product. Credit the upstream authors sp00nz, xenophobentx and the contributors listed in the included notices. Original MIT notices and dependency licenses are retained. The runtime includes LGPL components; corresponding modified sources are included in this repository and the source release archive. See [BUILDING.md](BUILDING.md), [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `UPSTREAM.json`.
