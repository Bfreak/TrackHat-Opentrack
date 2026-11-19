# First-run setup wizard (2026.1)

Code: `opentrack/setup-wizard.{hpp,cpp}` (wizard), `gui/key-capture.{hpp,cpp}` (hotkey capture shared with the wizard),
images `gui/images/setup-sensor-{top,bottom}.png`, styles at the end of `gui/themes/midnight.qss`.

## When it shows
- On start-up when the global setting `trackhat-setup-wizard-done` is not set (registry `HKCU\Software\trackhat-v3`, or
  `globals.ini` in a portable install). Closing the wizard in any way sets it, so it shows once.
- Again from **Options > Setup wizard...** (bottom left).
- Never in the development/test modes (`OPENTRACK_UI_SNAPSHOT`, `OPENTRACK_HEAD_TEST_SNAPSHOT`, `OPENTRACK_VIDEO_FILE`).
- `OPENTRACK_SETUP_WIZARD_SNAPSHOT=<dir>` saves every page (and the calibration states, and each preset) as PNGs and quits.
  It doesn't start tracking or change settings.

## Pages
0. **Welcome**: Start setup / Skip setup.
1. **Sensor**: is "TrackHat sensor" in the camera list (refreshed every 2.5 s); place it above or below the monitor,
   whichever is nearer eye height (the owner's two pictures).
2. **Head center offset calibration**. Starts tracking if needed, sets the tracker's offset to 0, and records the raw
   pose (before centering and mapping) while the user turns left/right (50 deg span) and nods (30 deg span); finishes
   by itself. Then solves `[R  -I] [o; J] = -f` by least squares: `f` is the face position the tracker reports, `J`
   the fixed neck pivot, `o` the offset (forward, up, right, mm, head frame). `R = Ry(-yaw) Rz(pitch) Rx(-roll)` and
   `f = (-TZ, TY, -TX) * 10` invert `NeuralNetTracker::data()`; checked numerically (angles round-trip exactly; a known
   offset is recovered to ~1 mm with 2 mm noise). Results outside fwd -250..100, up -150..100, |right| < 60 mm or with
   residual RMS >= 25 mm are rejected and the previous offset is restored. "Use the default offset" = -70 / -25 / 0.
   Upstream's own calibration (Tracker tab) has a "FIXME: does not work" comment; this one doesn't use it.
3. **Hotkeys**: Center (`center`) and Pause/resume (`toggle`), first slot of each; keyboard, mouse, joystick or gamepad
   (same capture dialog as Options > Shortcuts). Saved immediately; the main window re-registers shortcuts on save.
4. **Rotation preset** and 5. **Position preset**: the owner's axis pictures, and a slider over 1:1, Mild, Medium,
   Strong. Moving it writes the curves straight into the live mapping (the test window opens beside the wizard),
   Next saves them, closing the wizard puts them back. A profile without curves starts on 1:1 and gets it written
   (opentrack's empty X/Y/Z curve is 30 -> 75 cm, not 1:1). A profile with its own curves shows "Your own curves"
   and is left alone unless the slider is moved.
6. **Deadzones**: two sliders, turning and moving: None, Small, Medium, Large. In the owner's sense (2026-11-19):
   not a dead spot but how gently the curve starts. It adds one point under the straight preset line; opentrack's
   spline rounds it off into an ease-in (see "Deadzone" below). Starts on Small. Finish saves.

Window: fixed 900x800, 11pt text (22pt title). Owned by the main window (always in front of it) and placed once,
right beside the main window on its screen; after that it stays where the user puts it. (Placed at the far left of
the screen until 2026-11-19, it went unseen on the owner's 5120 px ultrawide. The "done" flag is only set when the
wizard itself is closed, so quitting the app with it unseen shows it again next start.)

The flag survives uninstall/reinstall (the installer doesn't touch HKCU), like every opentrack setting, so an
update doesn't show the wizard again. To see it again, delete `trackhat-setup-wizard-done` or use Options > Setup
wizard. When testing from Claude Code in the desktop app, see "Registry is virtualised" in `GITHUB-AND-ENVIRONMENT.md`:
the 2026-11-19 "wizard doesn't open after a clean install" reports were the real registry still having the flag.

## Presets
Straight lines from 0 to the end point of the owner's curves (2026-11-18, read off his Mapping screenshots,
input -> output):

| Preset | Points |
|---|---|
| 1:1 | straight line to the end of the axis range |
| Mild | (100, 180) |
| Medium | (63.5, 180) |
| Strong | (40.5, 180) |

History: the screenshots had two more points under the line ((1.5, 1.5) / (25, 31.5) for Mild, (3, 4.5) / (21, 35)
Medium, (1.25, 2.75) / (15.5, 39.5) Strong). The first went on 2026-11-19 (first test), the second after the owner's
second test the same day: "the presets should be linear, the deadzone step chooses how smooth the start is".

## Deadzone (gentle start)
One point at `x * end input`, with `y *` the straight line's output there (`setup_wizard::deadzones()`):

| Level | x | y |
|---|---|---|
| None | - | - (straight line) |
| Small | 0.15 | 0.75 |
| Medium | 0.25 | 0.55 |
| Large | 0.35 | 0.40 |

Small and Medium match the owner's example curves on a line to (75, 180): (13, 24) and (20.5, 27). It scales with the
preset (Strong's ease-in is short, 1:1's long) and with each axis' fitted end. A profile's curves are recognised as
"preset + level" when re-running the wizard.

`setup_wizard::fit_curve()` fits the preset to each axis without changing the outputs below the limits: a point past
the axis' max input or max output is replaced by the point where the line reaches the limit. Then the deadzone point
is added relative to that end.
- Yaw, roll (180 in / 180 out): as listed.
- Pitch (90 / 90): 60% as sensitive as yaw in every preset (owner, 2026-11-20: `axis_sensitivity()` scales the
  outputs by 0.6 before fitting), ending at 90 deg out; 1:1 becomes 90 -> 54. Its deadzone point is placed
  relative to its own fitted end.
- The snapshot mode also writes `curves.txt`: every preset and deadzone as fitted to each axis.
- X, Y, Z (30 cm in / 75 cm out): the same numbers read as cm (owner's choice), ending at 30 cm in or 75 cm out.
  1:1 is a real 1:1 (30 -> 30 cm); opentrack's empty curve would be 30 -> 75.

## Not verified
- The calibration with a real person in front of the sensor (needs someone to move their head).
- Hotkey capture from the wizard on real keys/buttons (same code path as Options).
- Screen layouts other than this PC's; very small screens (the wizard is a fixed 900x800).
