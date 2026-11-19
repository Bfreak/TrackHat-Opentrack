# GitHub situation and sandbox constraints

## Repository layout (github.com/Bfreak/TrackHat-Opentrack)
A fork of `opentrack/opentrack`, used one way only: we pull upstream releases in when we decide to; nothing
ever goes back upstream (no pushes, no PRs).

| Branch | Role |
|---|---|
| `trackhat-2026.1` (default) | The Trackhat release branch. All releases are tagged from here. |
| `master` | Untouched mirror of upstream `master`. Never commit to it; only "Sync fork" / fast-forward. |
| `trackhat-<name>-prototype` | Experiments (currently `trackhat-mediapipe-prototype`). Merge `trackhat-2026.1` in to stay current; land via PR into `trackhat-2026.1`. |

- Tags: `trackhat-opentrack-<version>` for Trackhat releases. Keep upstream's `opentrack-2026.1.0`: it is the
  base of `trackhat-2026.1` and `git describe` needs it for the version string.
- Taking a new opentrack: merge the upstream release *tag* (not upstream `master`) into the Trackhat branch.
- CI: "Trackhat Windows (MSVC)" builds pushes to `trackhat-*` branches once each (PRs from
  branches in this repo are skipped, since their push already built them; PRs from other forks still build). To attach the installer and zip to a
  release, run it manually with `release_tag`. Upstream's "CMake" workflow is disabled in the Actions UI (it lives
  unchanged on `master`, so it can't be edited there).

## Local clone conventions
- `origin` = `Bfreak/TrackHat-Opentrack` (push here); `upstream` = `opentrack/opentrack` with push URL `DISABLED`.
- `gh` default repo is the fork (`remote.origin.gh-resolved=base`), so `gh pr create` never targets upstream.
- New clone: `git clone https://github.com/Bfreak/TrackHat-Opentrack.git`, then
  `git remote add upstream https://github.com/opentrack/opentrack.git && git remote set-url --push upstream DISABLED`
  and `gh repo set-default Bfreak/TrackHat-Opentrack`.

## Registry is virtualised for Claude Code in the desktop app (owner's PC)
The Claude desktop app is a packaged (MSIX) Windows app. Every process it starts (PowerShell, `reg`, and opentrack
itself when launched from there) sees a **private copy of HKCU**: reads fall through to the real registry until the
first write, writes and deletes stay private. So `Remove-Item HKCU:\Software\trackhat-v3` from a Claude shell does
nothing to what the owner's opentrack sees, and an opentrack started from a Claude shell doesn't see the owner's
settings. Found 2026-11-19: the setup wizard "never opened" because the real `trackhat-setup-wizard-done` was still set.
- To read or change the **real** HKCU, put the `reg query` / `reg export` / `reg delete` commands in a `.bat` and
  start it through Explorer: `Start-Process explorer.exe -ArgumentList '"C:\path\script.bat"'`; have it write its
  output to a file under Documents.
- To test what the owner gets, start opentrack the same way (through Explorer, e.g. the Start menu `.lnk`). It
  then has no foreground rights, so its windows may sit behind others in screenshots.
- Files are not affected (Documents, Program Files); installers/uninstallers run elevated, outside the package.

## Cloud sandbox (only if a session runs there)
- GitHub access is a repo-scoped allowlist via the session proxy; attach `Bfreak/TrackHat-Opentrack` with `add_repo`.
- Don't try to bypass the proxy policy (curl with tokens, alternate hosts). Report the denial instead.

## Sandbox facts (cloud Linux, only relevant if you resume there)
- Network from the shell is an allowlist (package registries + GitHub via proxy); other sites are refused.
- Work lived under `/home/claude/work/`: `opentrack-upstream` (repo), `qt/6.8.3/{gcc_64,mingw_64}`, `cv-win/install`, `oscpack-win`,
  `ort-win`, `ort-dl`, `build` (Linux), `build-win`, `install-win`. None of this persists beyond the session; rebuild with the docs.
- Real camera/Windows testing is impossible there; Wine 9 is available for GUI smoke tests.
