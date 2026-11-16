# Linux and macOS builds: viability and plan (2026-11-16)

Assessed against `trackhat-2026.1` at `8d9f8a2` (3.3.1 + README).

## Verdict

| | Linux | macOS |
|---|---|---|
| Builds from the current branch? | **Yes, verified** (Ubuntu 24.04, distro Qt 6.4.2, ORT 1.17.3, 2 min, no errors) | Not tried: needs a Mac or a macOS CI runner |
| Trackhat UI, themes, Protocols tab, test window | **Run, verified** under Xvfb with `OPENTRACK_UI_SNAPSHOT` (OpenGL 4.5 core via Mesa) | Should work: Qt and OpenGL 3.3 core are available, Info.plist already asks for camera access |
| Sensor capture (640x480@60 MJPEG) | Expected to work (V4L2 honours size, fps and MJPEG) | **Uncertain**: OpenCV's AVFoundation backend has little control over format and fps |
| Exposure presets | **Wrong today**: values are DirectShow units; needs a V4L2 branch (small) | **Not possible through OpenCV**: needs new UVC-control code |
| Useful outputs | UDP, FlightGear, OSC, libevdev joystick, **Wine/Proton (freetrack/NPClient)**, X-Plane plugin | UDP, FlightGear, OSC, X-Plane plugin only |
| Distribution | AppImage, unsigned is normal | Signed + notarized `.app` in a `.dmg` (Apple Developer Program, USD 99/yr) |
| Effort to a first release | **Small-medium**: ~1 week of Claude work + one evening of sensor tests by the owner | **Medium-large**: ~2-3 weeks + a Mac with the sensor for testing |
| Recommendation | **Do it now** | **Decide first** whether X-Plane/FlightGear on Mac justifies the cost |

Nothing here has been run with a real camera or the real sensor on either platform.

## Recommendation on "forks"

Don't make long-lived Linux and macOS forks. The code difference is a handful of `#ifdef`s in files that already have
them (`video-opencv/impl-camera.cpp`, `compat/camera-names.cpp`); three diverging branches would triple every future
merge and every upstream update. Instead:

- one release branch, `trackhat-2026.1`, and one version tag per release for all three platforms;
- short-lived work branches **`trackhat-linux`** and **`trackhat-macos`**, merged back when each milestone below is green;
- one CI workflow per platform: `trackhat-linux.yml`, `trackhat-macos.yml` (next to `trackhat-windows.yml`).

## What the check found

### Common to both
- **`-march=native`**: the default GCC/Clang release flags (`CMakeLists.txt:53`, `cmake/unix.cmake`) build for the
  build machine's CPU. A release binary built that way can crash with "illegal instruction" on users' older CPUs.
  Release builds must pass `-D__otr_compile_flags_set=TRUE` and portable flags (`-O2 -march=x86-64-v2` on Linux,
  `-mcpu=apple-m1` on Apple silicon). The Linux test build in this check used `-march=native` (fine for a smoke test only).
- **Update check**: trackhat.org has one line, `SOFTWARE-UPDATE-V3: <version>`, and the dialog links to
  `trackhat.org/trackhat-opentrack`. That works for every platform if all three are released together and the download
  page lists all three. If releases may differ per platform, add `SOFTWARE-UPDATE-V3-LINUX:` / `-MACOS:` lines
  (small change in `opentrack/software-update-dialog.cpp`). Owner decision.
- **Exposure presets** (`video-opencv/impl-camera.cpp`): Near/Medium/Far send `CAP_PROP_EXPOSURE` -6/-5/-4
  (DirectShow log2-seconds) and `CAP_PROP_AUTO_EXPOSURE` 0 (manual on DirectShow, invalid on V4L2, where manual is 1).
  Only "Auto" already has a non-Windows branch.
- **Sensor name**: the app looks for a camera named exactly `TrackHat sensor`. On Linux the name is the USB product
  string as V4L2 reports it; on macOS it is the AVFoundation name. Both probably match, but this needs one look at the
  real sensor (`v4l2-ctl --list-devices` / `system_profiler SPCameraDataType`).
- **Default protocols**: "everything except mouse" means different sets per platform. On Linux that turns on the
  libevdev virtual joystick, which some games pick up as a real controller; consider making it off by default on Linux.

### Linux specifics
- **Camera list duplicates**: `compat/camera-names.cpp` lists `/dev/video0`-`15` and keeps every node that answers
  `VIDIOC_QUERYCAP`. UVC cameras create a second "metadata" node with the same name, so "TrackHat sensor" can appear
  twice. Fix: keep only nodes whose `device_caps` include `V4L2_CAP_VIDEO_CAPTURE`.
- **Camera settings** button (Custom preset) opens `qv4l2` if it is installed; otherwise nothing happens. Document it,
  or say so in the UI.
- **Wine/Proton output** (`proto-wine`) is the reason Linux gamers want this: it gives freetrack/NPClient (TrackIR) to
  Windows games running under Wine or Steam Proton. It only builds with `-DSDK_WINE=ON` and `wineg++` installed, and
  defaults to a 32-bit wrapper (`-m32`); 64-bit-only Proton prefixes may need `OPENTRACK_WINE_ARCH=-m64`. Not built in
  this check.
- **X-Plane plugin** needs the free X-Plane SDK (`-DSDK_XPLANE=<dir>`). Not built in this check.
- **libevdev joystick** needs write access to `/dev/uinput` (a udev rule; ship it and document it).
- **Global shortcuts** use X11 (`qxt-mini`). On Wayland desktops they only work for X11 apps; the AppImage should start
  with `QT_QPA_PLATFORM=xcb` as the default.
- **Settings**: `~/.config/trackhat-v3`, as documented.

### macOS specifics
- **Toolchain**: Apple Clang has no OpenMP, which `tracker-neuralnet` requires (`find_package(OpenMP)`): use Homebrew
  `libomp` and bundle `libomp.dylib`. ONNX Runtime 1.17.3 has `osx-arm64`, `osx-x86_64` and `osx-universal2` packages
  (checked: all downloadable). OpenCV from Homebrew drags in many libraries; a minimal OpenCV built in CI is smaller.
- **Camera selection by index**: names come from Qt (`QMediaDevices`), but OpenCV opens AVFoundation devices by its own
  index. The orders are not guaranteed to match, so choosing "TrackHat sensor" could open the built-in FaceTime camera.
  Needs a fix (open by AVFoundation unique ID, or capture through Qt Multimedia on macOS).
- **Format, fps and exposure**: OpenCV's AVFoundation backend does not reliably pick 640x480@60 MJPEG and cannot set
  exposure on USB cameras. To get presets, Trackhat needs native code: an AVFoundation capture that selects the 60 fps
  format, and UVC exposure control through IOKit USB requests (as tools like `uvc-util` do). This is the largest item.
- **Signing**: macOS blocks unsigned downloaded apps; since macOS 15 the right-click "Open" shortcut is gone and users must allow the app in System Settings > Privacy & Security. Camera
  access also needs a signed app with the `com.apple.security.device.camera` entitlement under the hardened runtime.
  Requires the owner to join the Apple Developer Program and put the Developer ID certificate and notarization
  credentials in GitHub secrets.
- **Branding**: `macosx/Info.plist` still says `com.github.opentrack` / `opentrack`, and `make-app-bundle.sh` makes
  the icon from `gui/images/opentrack.png`. Change to a Trackhat bundle ID, name and icon.
- **Settings folder**: macOS uses `~/Documents/trackhat-v3`, which triggers a "wants to access your Documents folder"
  prompt. Keep it (consistent with Windows) or move to `~/Library/Application Support`; owner decision.
- **Outputs**: no freetrack/NPClient, no mouse, no vJoy. `proto-iokit-foohid` needs a kernel extension that no longer
  loads on current macOS; leave it out. Real use: X-Plane 11/12 (plugin), FlightGear, apps that read UDP.
- **Test window**: asks for OpenGL 3.3 core; macOS offers 4.1 core, so it should run (OpenGL is deprecated on macOS
  but still works, also on Apple silicon). Not verified.
- **Architecture**: ship Apple silicon (arm64) first. Intel Macs are being phased out, and a universal build needs
  universal builds of OpenCV and libomp too.

## Plan: `trackhat-linux`

Milestone L1 - CI build (Claude, ~1 day)
1. `.github/workflows/trackhat-linux.yml` on `ubuntu-22.04` (older glibc, so the binary runs on more distros): Qt 6.8.3
   via `install-qt-action` (same Qt as Windows), OpenCV 4.x built minimal, ORT 1.17.3 linux-x64, X-Plane SDK,
   `wine64-tools` + `wineg++`, `-D__otr_compile_flags_set=TRUE` with `-O2 -march=x86-64-v2`.
2. Run the existing UI snapshot self-test under Xvfb in CI and upload the PNGs.

Milestone L2 - camera fixes (Claude, 1-2 days)
3. `compat/camera-names.cpp`: skip non-capture nodes.
4. `video-opencv/impl-camera.cpp`: V4L2 branch for Near/Medium/Far: `CAP_PROP_AUTO_EXPOSURE` 1 and
   `CAP_PROP_EXPOSURE` in 100 microsecond units (starting points 156 / 312 / 625, the DirectShow equivalents; calibrate
   on the sensor).
5. libevdev off by default on Linux (if the owner agrees).

Milestone L3 - package (Claude, 1-2 days)
6. AppImage (`linuxdeploy` + Qt plugin): bundles Qt, OpenCV, ONNX Runtime, models, test-window assets, Wine wrapper,
   X-Plane plugin; defaults `QT_QPA_PLATFORM=xcb`. Also a plain `.tar.gz`. Flatpak is not recommended: its sandbox
   stops the Wine output from reaching the user's Wine/Proton prefixes.
7. `docs/trackhat/LINUX-USER-GUIDE.md`: udev rule for `/dev/uinput`, Proton/Wine setup, X-Plane plugin install.

Milestone L4 - owner tests with the sensor (owner, one evening)
8. On a Linux PC (or a live USB): sensor is listed once and first, tracking at ~60 fps, every exposure preset changes
   the image, one Proton game via freetrack/NPClient, X-Plane if available. Claude adjusts exposure values from the results.

Then merge into `trackhat-2026.1` and release with the next tag.

## Plan: `trackhat-macos`

Milestone M0 - owner decisions before any work
- Is a Mac version worth it for X-Plane/FlightGear users? Is there a Mac with the sensor for testing?
- Join the Apple Developer Program (needed to ship; not needed for M1-M2).

Milestone M1 - CI build (Claude, 1-2 days)
1. `.github/workflows/trackhat-macos.yml` on `macos-14` (Apple silicon): Qt 6.8.3, Homebrew `libomp`, OpenCV, ORT
   1.17.3 osx-arm64, X-Plane SDK, portable flags. Produce an unsigned `.app` zip and the UI snapshots (macOS runners
   have a display).
2. Trackhat `Info.plist` (bundle ID, name, icon, version from the tag); drop `proto-iokit-foohid` from the build.

Milestone M2 - camera (Claude, ~1 week, the risky part)
3. Correct device selection: open the camera the user picked, not "camera number N".
4. Native AVFoundation capture in `video-opencv` (or a new `video-avfoundation` module) that selects 640x480@60 and
   delivers frames in the same form as today.
5. UVC exposure control over IOKit for the presets; if the sensor's UVC controls can't be reached, ship macOS with
   Auto only and hide the preset box.

Milestone M3 - ship (Claude + owner, 2-3 days once the certificate exists)
6. Hardened runtime, camera entitlement, `codesign`, `notarytool`, stapled `.dmg` in CI.
7. `docs/trackhat/MACOS-USER-GUIDE.md`: camera permission prompt, X-Plane plugin install.

Milestone M4 - owner tests on a Mac with the sensor
8. Right camera opens, ~60 fps, presets, X-Plane head movement, test window, app opens without Gatekeeper warnings.

## Owner decisions needed
1. Branch model: work branches merged into one release branch (recommended), or permanent per-platform forks?
2. Same version/release day for all platforms (one update line), or per-platform update lines?
3. Linux: libevdev joystick off by default?
4. macOS: go / no-go, Apple Developer Program, who tests on a Mac, settings folder location.
