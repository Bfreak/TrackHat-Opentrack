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
4. **Rotation preset** and 5. **Position preset**: a slider over 1:1, Mild, Medium, Strong. Moving it writes the
   curves straight into the live mapping (the test window opens beside the wizard), Next/Finish saves them, closing the
   wizard puts them back. Untouched sliders write nothing, so custom curves survive a re-run.

## Presets
The owner's curves (2026-11-18), read off his Mapping screenshots (input -> output):

| Preset | Points |
|---|---|
| 1:1 | straight line to the end of the axis range |
| Mild | (1.5, 1.5) (25, 31.5) (100, 180) |
| Medium | (3, 4.5) (21, 35) (63.5, 180) |
| Strong | (1.25, 2.75) (15.5, 39.5) (40.5, 180) |

`setup_wizard::fit_preset()` fits them to each axis without changing the outputs below the limits: points past the
axis' max input or max output are replaced by the point where the curve (straight-line estimate) reaches the limit.
- Yaw, roll (180 in / 180 out): as listed.
- Pitch (90 / 90): same curve, ending at 90 deg out (Mild (54.5, 90), Medium (37.1, 90), Strong (24.5, 90)).
- X, Y, Z (30 cm in / 75 cm out): the same numbers read as cm (owner's choice), ending at 30 cm in or 75 cm out.
  1:1 is a real 1:1 (30 -> 30 cm); opentrack's empty curve would be 30 -> 75.

## Not verified
- The calibration with a real person in front of the sensor (needs someone to move their head).
- Hotkey capture from the wizard on real keys/buttons (same code path as Options).
- Screen layouts other than this PC's; very small screens (the wizard is 740x640).
