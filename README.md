# TrackHat Opentrack

Head tracking software for the [TrackHat](https://www.trackhat.org) sensor, built on
[opentrack](https://github.com/opentrack/opentrack) 2026.1.0.

It tracks your head with the TrackHat infrared sensor and sends the movement to flight sims,
racing sims and other games. No clip, hat or markers are needed.

This is TrackHat's fork of opentrack. For the general-purpose program, with its other trackers
and filters, use [upstream opentrack](https://github.com/opentrack/opentrack).

## Download

Get the Windows installer from <https://www.trackhat.org/trackhat-opentrack>.

## What is different from upstream opentrack

- **Set up for the TrackHat sensor.** The neural-net face tracker and the Accela filter are
  built in and preselected. The sensor is picked automatically when it is plugged in.
- **Exposure presets** for the sensor: Auto, Near, Medium, Far and Custom.
- **All game protocols at once.** Output goes to every enabled protocol (freetrack/TrackIR,
  FlightGear, SimConnect, UDP, vJoy, OSC), so most games work without choosing one.
  Manage them in Options > Protocols.
- **Test head tracking window.** Look around a cockpit, a race car or a soldier's position
  with your own head movement while you adjust the mapping curves.
- **Simpler interface** with the Midnight theme and a 3D head preview that shows whether
  tracking is running, paused or stopped.
- **Update check** against trackhat.org.

Settings are stored in `Documents\trackhat-v3` on Windows and `~/.config/trackhat-v3` on Linux.

## Building

| Target | How | Details |
|---|---|---|
| Windows installer and portable zip | Push to a `trackhat-*` branch; GitHub Actions builds them | `.github/workflows/trackhat-windows.yml` |
| Windows, local | `scripts\windows-local\build.bat` | `docs/trackhat/WINDOWS-LOCAL-BUILD.md` |
| Windows, cross-compiled from Linux | `scripts/windows-cross/build-windows.sh` | `docs/trackhat/WINDOWS-BUILD.md` |
| Linux | CMake and Ninja | `docs/trackhat/LINUX-BUILD.md` |

Requirements: Qt 6.8, OpenCV 4.10 and ONNX Runtime 1.17.3.

## Documentation

- `docs/trackhat/CHANGELOG.md`: what changed since TrackHat Opentrack 3.2
- `docs/trackhat/PORTING-NOTES.md`: how this fork differs from upstream, and why
- `docs/trackhat/STATUS.md`: current state of the project
- `docs/trackhat/HEAD-TEST.md`: the test head tracking window

## Branches

- `trackhat-2026.1`: the release branch
- upstream's `master` is not tracked here; fixes are cherry-picked as needed

## Credits and licence

TrackHat Opentrack is a modified version of opentrack by Stanisław Halik and contributors.
The neural-net tracker is by Michael Welter. See `AUTHORS.md`.

Most of the code is under the ISC licence. See `OPENTRACK-LICENSING.txt` for the terms and
`3rdparty-notices/` for third-party components.

For support with the TrackHat sensor, contact TrackHat through <https://www.trackhat.org>.
