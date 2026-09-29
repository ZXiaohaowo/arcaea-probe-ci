# Standard-toolchain probe (GitHub Actions)

Purpose: build the same minimal module with the STANDARD Apple toolchain (Xcode/clang
on a macOS runner) as a control for the iOS 26.6 "code signature invalid" blocker.
See `docs/s1_signature_blocker_brief.md` for why this control matters: five earlier
attempts all shared the same zig-built module, so a stock-toolchain reference is the
highest-value next comparison.

## What you need
	- A GitHub account. A private repository is fine (macOS runner minutes are limited on
	  free plans but a single build takes ~1-2 minutes; a public repo is unlimited).

## Steps
	1. Create a new repository (any name, e.g. `arcaea-probe-ci`).
	2. Upload the CONTENTS of this `ci/` directory to the repository root, preserving the
	   folder structure:
	   - `probe.c`
	   - `Info.plist`
	   - `.github/workflows/build-probe-framework.yml`
	   (GitHub web upload: drag the files; if the web UI refuses to create the
	   `.github/workflows` path, use `git push` from a local clone instead.)
	3. Open the repository's "Actions" tab, select "build-probe-framework", click
	   "Run workflow" (it is a manual-dispatch workflow).
	4. After ~1-2 minutes the run completes; open it and download the artifact
	   `practice_probe_ci_framework` (a zip containing `practice_probe_ci.framework.zip`).
	5. Unzip down to the `practice_probe_ci.framework` folder and tell us its local path
	   (or place it on the Desktop). We will then build the injection IPA from it and run
	   the same Sideloadly install test.

## Notes
	- The build proves its own freshness: the workflow prints `lipo -info` and a SHA-256
	  of the produced binary in the run log.
	- Nothing secret is uploaded: no IPA, no account credentials, no signing keys.
	- Expected artifact: `practice_probe_ci.framework` containing a native arm64 iOS
	  dynamic library named `practice_probe_ci` plus its `Info.plist`.
