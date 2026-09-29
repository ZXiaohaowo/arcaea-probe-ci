# Standard-toolchain build (GitHub Actions)

Purpose: build the practice frameworks with the STANDARD Apple toolchain (Xcode/clang on a
macOS runner). This pipeline replaced the zig-on-Windows path after the S1 blocker was
resolved: every zig-built variant was rejected by iOS 26.6, while the standard-toolchain
probe loaded and executed on device (see `docs/s1_breakthrough_and_next_plan.md`).

## What it builds
	- `practice_probe_ci.framework` - minimal probe (probe.c).
	- `practice_bootstrap.framework` - the real bootstrap module, stamped with the build id
	  (commit short hash) so every build is identifiable in the device log.
	- `practice_clock_probe.framework` - S2a sampling framework (practice_clock_probe.c):
	  session header + 1 Hz monotonic samples with buffered writes; v2 (v9) adds a READ-ONLY
	  pointer-chain probe of the recorded AudioManager/provider chain - no game function
	  calls, guarded reads, change-driven chain log (see docs/s2a_audio_position_static_findings.md).

## Repository layout (the contents of this `ci/` folder = repository root)
	- `probe.c`, `practice_bootstrap.c`, `practice_clock_probe.c` - sources. `practice_bootstrap.c`
	  and `practice_clock_probe.c` are synced from the authoritative `src/` copies in the project
	  (check with `python tools/check_ci_sync.py` before pushing).
	- `Info.plist`, `bootstrap-Info.plist`, `clock_probe-Info.plist` - the framework Info.plists.
	- `build.sh` - the build script (toolchain info + build + verify + package). Runs on any
	  macOS host with Xcode command line tools; the workflow only calls it.
	- `.github/workflows/build-probe-framework.yml` - CI workflow. Actions are pinned to
	  full commit SHAs; repository permission is contents: read.

## Operator steps
	1. Update the repository (only what changed):
	   - ADD / refresh `build.sh`
	   - REPLACE `.github/workflows/build-probe-framework.yml`
	   - If sources changed: refresh `practice_bootstrap.c` and/or `bootstrap-Info.plist`
	   - S2a (2026-09-29): ADD `practice_clock_probe.c` and `clock_probe-Info.plist`
	     (also refresh `build.sh` and the workflow, which now include the clock probe)
	2. Actions -> "build-probe-framework" -> Run workflow.
	3. When done, open the run: the log shows Xcode / clang / SDK versions and the SHA-256 of
	   each binary. Download the artifact `frameworks` (contains all three
	   `*.framework.zip` files).
	4. Unpack and hand the framework folder(s) to the Windows side for assembly.

## Notes
	- Nothing secret is uploaded: no IPA, no account credentials, no signing keys.
	- Keep a local copy of every verified artifact together with its run log; hosted
	  artifacts have a limited retention period.
	- The workflow pins `actions/checkout` and `actions/upload-artifact` to verified commit
	  SHAs (2026-09-29); if these actions are ever updated on purpose, update the SHAs
	  deliberately and re-verify.
