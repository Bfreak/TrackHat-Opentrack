TrackHat opentrack 2026.1 **Beta 2**: a test build of TrackHat opentrack's first major update in many years.
Thank you for testing it.

## Download
- **trackhat-opentrack-2026.1-beta2-win64-setup.exe**: the installer (recommended).
- **trackhat-opentrack-2026.1-beta2-win64-portable.zip**: the same program without installing; unzip it anywhere
  and run `opentrack.exe`.

Windows may say "Windows protected your PC", because the installer isn't signed yet: click **More info**, then
**Run anyway**. Installing over Beta 1 is fine; your profiles are kept.

## New in Beta 2
- **First-time setup**: on first start, a short guided setup walks you through:
  1. placing the TrackHat sensor
  2. calibrating your head center
  3. choosing center and pause hotkeys (keyboard, mouse or controller)
  4. how far the view turns when you turn your head (1:1, Mild, Medium, Strong), tried live in a test window
  5. how far the view moves when you lean (same presets)
  6. the deadzone: how gently the view starts to follow your head near the center (None, Small, Medium, Large)

  You can skip it, or any step. Run it again any time from **Options > Setup wizard**.
- In every preset, looking up and down (pitch) is 60% as sensitive as turning left and right (yaw).
- **"Starting..."** is shown from when you press Start until tracking is running (about 5-7 seconds), and the
  window no longer freezes while the sensor starts.
- New defaults: left/right movement (X) and roll were reversed with the TrackHat sensor and are now the right way
  round; smoother default filtering.
- Fixed: the crash when pressing Start in the first Beta 1 download.

## Also in this update (since TrackHat opentrack 3.2)
- A completely new look, with a 3D head preview that shows when tracking is running, paused or stopped.
- All game outputs (freetrack/TrackIR, FlightGear, SimConnect, UDP, vJoy, OSC) run at the same time, so most games
  work without choosing one. Choose which are on in **Options > Protocols**.
- **Test head tracking**: look around a cockpit, a race car or a soldier's position to try your settings.
- A newer head tracking model, and lower CPU use.

## Please try
- The setup on a clean start, especially the head center calibration and the presets.
- Your usual games: does the view move the right way on every axis, and is it smooth?
- Start / Stop, pausing and centering with your hotkeys.

## Known issues
- Starting tracking takes about 5-7 seconds while the sensor sets its exposure; "Starting..." shows meanwhile.
- If you used Beta 1 and left/right or roll still moves the wrong way, your profile kept the old setting: open
  **Options > Output** and tick or untick the first "Invert" box for that axis.
- The program installs into `Program Files (x86)` even though it's 64-bit.

## Reporting problems
Tell us what you did, what you expected and what happened, which game, and your Windows version. A screenshot of
the main window and of **Options** helps.
