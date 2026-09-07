<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Building Pippin, start to finish

Every command in this file is run **from the repository root**, and the order is the order:
each step's inputs are the step above it. `README.md` explains *why* each piece is the shape it
is and is the place to go when something behaves oddly; this file is the procedure, with no
detours.

There are **four products** and they are made by four different tools, which is the one fact
that makes the rest of it make sense:

| # | Product | Made by | Where it lands |
|---|---|---|---|
| 1 | the core — fvkit and everything under it, one static archive | CMake + clang | `build-ios/…/libpippin_core.a` (device), `build-ios-sim/…` (simulator) |
| 2 | the offline data pack | `stage_data.py` | `port/apps/Pippin/Data/` |
| 3 | the app — Swift, PippinKit, the share extension, assembled around 1 and 2 | Xcode / `xcodebuild` | `Pippin.app` |
| 4 | the installable `.ipa` | `xcodebuild archive` + `-exportArchive` | `build-xcode/ipa/Pippin.ipa` |

**Xcode never builds the core and never stages the pack.** A "Check the Peregrine core" script
phase verifies both exist, is not stale, and fails with the exact command to type. So steps 1
and 2 are yours to run, and running them is cheap when nothing changed.

---

## Step 0 — once per machine

* **Xcode 15.3 or newer**, and `xcode-select -p` pointing at it. Xcode 16+ is what the
  `Pippin.debug.dylib` beside the Debug binary comes from; it is normal.
* **CMake 3.24+** and Ninja or make. `cmake --version`.
* **Python 3.9+** for `stage_data.py`. Nothing outside the standard library.
* **The source data**, in git-ignored `testdata/`: `testdata/OSM/kiawah.mbtiles`,
  `testdata/OSM/kiawah.fvroad`, `testdata/kiawah_cycle.gpx`, and `testdata/GeoSymbol/makiPng`.
  `stage_data.py` names each missing one; `README.md`'s "The data pack" says which tool cuts
  each from what.
* **A signing team.** Automatic signing, bundle ids `org.peregrine.Pippin` and
  `org.peregrine.Pippin.Share`. The tracked project names no team — it takes one from
  `PP_DEVELOPMENT_TEAM` in the git-ignored `local/Local.xcconfig` (step 0b), and Signing &
  Capabilities or `DEVELOPMENT_TEAM=` on the `xcodebuild` line are the other two ways to give
  it one. The bundle ids have to change too, because a bundle id is unique to the account that
  signs it.
* **For a device:** plug the phone in, unlock it, answer **Trust This Computer**, and turn on
  **Settings → Privacy & Security → Developer Mode** (the phone restarts). iOS 16+ will not
  launch a sideloaded build without it and says nothing useful when it refuses.

## Step 0b — once per tree that ships: the name, the address and the team

The repository is Pippin. The app on the store is called something else, its Report a Problem
sheet mails to an address that must never appear in a commit, and it signs against a personal
Apple Developer team. All three are **build settings**, defaulted in the tracked
`Pippin.xcconfig` and overridden by a git-ignored `local/Local.xcconfig`:

```sh
./port/apps/Pippin/local/set-branding.sh "Your App Name" alias@icloud.com ABCDE12345
```

```sh
./port/apps/Pippin/local/set-branding.sh --show
```

`PP_DISPLAY_NAME` reaches the home screen, the row in Apple Maps' share sheet, the location
permission alert and every sentence `AppName.display` puts the name in. `PP_REPORT_ADDRESS`
reaches `PPReportAddress` in the app's Info.plist. `PP_DEVELOPMENT_TEAM` reaches every
target's `DEVELOPMENT_TEAM`. A clone with no local file builds an app called **Pippin** with no
report address and no team, which is a working state for the simulator and for the C++ tests:
the report sheet says so and offers the share button instead, and nothing needs signing until a
build goes to a phone.

**Xcode caches build settings.** After running the script, close and reopen the project (or
clean) before the new name appears. A fresh `xcodebuild` picks it up with no help.
`--clear` goes back to the open-source defaults.

**The script lives in `local/` and `local/` is git-ignored**, so a fresh clone has neither the
script nor the values — which is the intent, and which is why the defaults are a *working*
answer rather than a placeholder. To ship under your own name from a clone, write the file it
would have written:

```sh
printf 'PP_DISPLAY_NAME = Your Name\nPP_REPORT_ADDRESS = you@example.com\n' > port/apps/Pippin/local/Local.xcconfig
```

`Pippin.xcconfig` picks it up through a trailing `#include?` — the optional spelling, so a
checkout without the file builds silently instead of warning at everyone who clones this.

---

## Step 1 — the core

One CMake build per architecture, and **the architecture must match what you are about to
assemble**. `FVW_IOS` is *set* by the toolchain rather than asked for, so an iOS configure
cannot forget it: tests, the CLI tools and pyfvw all switch off, and this directory switches on.

```sh
cmake --preset ios-sim && cmake --build build-ios-sim -j
```

```sh
cmake --preset ios && cmake --build build-ios -j
```

The product is one archive — `build-ios[-sim]/port/apps/Pippin/libpippin_core.a`, ~66–83 MB,
22 static libraries merged by `xcrun libtool -static` — plus `pippin_core_link_flags.txt`
beside it with the arguments that are not in the archive.

**Only needed when the C++ changed**, and needed *whenever* it changed: the script phase
compares the archive against every `.cpp` and `.h` under `port/`, so a stale core is an error
and not a mystery. A mac `cmake --build build` does **not** touch these.

Optional, and still the fastest way to test the core on the phone's SDK without the app:

```sh
xcrun simctl spawn booted build-ios-sim/port/apps/Pippin/pippin_link_check "$PWD/port/apps/Pippin/Data" /tmp/kiawah.png
```

## Step 2 — the data pack

Pippin is offline by construction, so everything it reads is in the bundle. `pippin.ini` names
every piece by a bundle-relative path and the script checks that each one exists after staging —
a settings key pointing at nothing is the failure mode a phone reports as a blank map at 3 pm
on a bike.

**For development:**

```sh
python3 port/apps/Pippin/stage_data.py
```

**For anything that ships** — the store build, or any Release archive:

```sh
python3 port/apps/Pippin/stage_data.py --release
```

`--release` leaves out (and removes from an already-staged pack) the rows that measure the app
rather than ride with it. Today that is the 407 KB `kiawah_cycle.gpx` demo ride, which is
reachable only from `-PPDemoFeed`, which a Release build does not read: the flag and the probe
screens are `#if DEBUG`. The release pack is ~3.6 MB against the development pack's ~4.0 MB.

To verify a pack without copying anything:

```sh
python3 port/apps/Pippin/stage_data.py --check
```

**`kiawah.fvpoints` is not overwritten if the pack already has one.** The shipping starter
points are hand-authored — the real telephone numbers, the real links, the island's own
spellings — and `Data/` is git-ignored, so that file lives nowhere else. Staging keeps it and
says `kept`. `POINTS` in the script is the generated stand-in (reserved 555 numbers and
`.invalid` hosts) and this is how to go back to it, discarding the edited set:

```sh
python3 port/apps/Pippin/stage_data.py --regen-points
```

Keep a copy of the hand-edited `.fvpoints` somewhere outside `Data/`. Nothing else does.

## Step 3 — assembling the app

### The simulator, one button

Open `port/apps/Pippin/Pippin.xcodeproj`, pick an iPhone simulator, press Run.

### The simulator, from the terminal

```sh
xcodebuild -project port/apps/Pippin/Pippin.xcodeproj -scheme Pippin -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath build-xcode-sim build
```

```sh
xcrun simctl install booted build-xcode-sim/Build/Products/Debug-iphonesimulator/Pippin.app && xcrun simctl launch booted org.peregrine.Pippin
```

**Pass `-derivedDataPath` and mean it.** With no such flag `xcodebuild` writes to
`~/Library/Developer/Xcode/DerivedData/Pippin-*`, and a `build-xcode*/` left over from a session
that *did* pass one still holds a stale `Pippin.app` that `simctl install` will happily install
and run. BUILD SUCCEEDED followed by last week's behaviour. One derived-data path, and `ls -la`
on the binary when in doubt.

The Debug-only launch arguments, each one a screen (none of them exist in a Release build):

```sh
xcrun simctl launch booted org.peregrine.Pippin --args -PPShowStats YES
```

`-PPShowStats YES` frame cost, plus the ownship's position, stamped angle and speed ·
`-PPViewportProbe YES` P3's nine gesture checks · `-PPPixelProbe YES` P2's alpha measurement ·
`-PPGestureDemo YES` drags and pinches on a timer · `-PPDemoFeed YES` replays the bundled ride
(development pack only — see Step 2). To drive the live path instead, give CoreLocation a ride:

```sh
xcrun simctl location booted start --speed=8 32.6045,-80.0790 32.6055,-80.0740 32.6062,-80.0690
```

### A device, without an .ipa

For day-to-day device work. `-allowProvisioningUpdates` is what lets automatic signing mint or
refresh a profile without the Xcode UI; `generic/platform=iOS` builds arm64 without the phone
attached.

```sh
xcodebuild -project port/apps/Pippin/Pippin.xcodeproj -scheme Pippin -sdk iphoneos -destination 'generic/platform=iOS' -derivedDataPath build-xcode -allowProvisioningUpdates build
```

```sh
xcrun devicectl list devices
```

```sh
export IPHONE=<the Identifier column from that listing>
```

```sh
xcrun devicectl device install app --device "$IPHONE" build-xcode/Build/Products/Debug-iphoneos/Pippin.app
```

```sh
xcrun devicectl device process launch --console --device "$IPHONE" org.peregrine.Pippin -PPShowStats YES
```

Launch arguments go **after** the bundle id. `--console` keeps the process attached and prints
its stdout, which is the device equivalent of the Xcode console.

On the first launch of a build signed with a free Apple ID the phone refuses it until the
profile is approved: **Settings → General → VPN & Device Management → Developer App → Trust**.
A paid team profile skips this.

## Step 4 — the .ipa

An `.ipa` is a signed `Payload/Pippin.app` in a zip, made in two steps: **archive**, then
**export**. Both use the **Release** configuration, which is why Step 2's `--release` comes
first and why the probes are absent from what this produces.

**The five commands, in this order.** The first two are only needed when their inputs changed,
and the last three are not optional on a re-sign:

```sh
python3 port/apps/Pippin/stage_data.py --release
```

```sh
cmake --preset ios && cmake --build build-ios -j
```

```sh
rm -f ~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/*.mobileprovision
```

```sh
xcodebuild -project port/apps/Pippin/Pippin.xcodeproj -scheme Pippin -sdk iphoneos -destination 'generic/platform=iOS' -configuration Release -allowProvisioningUpdates -archivePath build-xcode/Pippin.xcarchive archive
```

```sh
xcodebuild -exportArchive -archivePath build-xcode/Pippin.xcarchive -exportOptionsPlist port/apps/Pippin/ExportOptions.plist -exportPath build-xcode/ipa
```

That writes `build-xcode/ipa/Pippin.ipa` — about 3.7 MB, most of it Kiawah — beside a
`Packaging.log` and a `DistributionSummary.plist` worth reading when something is signed wrong.

The `rm` and `-allowProvisioningUpdates` **go together**: the first makes automatic signing
issue a fresh profile instead of reusing a part-spent one (on 2026-08-27 a single archive
shipped an app profile issued on the 19th and an extension profile from the 21st, and the app
died a day early), and the second is what lets `xcodebuild` reach Apple to issue it.

Install it — `devicectl` takes the `.ipa` directly, no unzip:

```sh
xcrun devicectl device install app --device "$IPHONE" build-xcode/ipa/Pippin.ipa
```

**Install from the `.ipa` or the `.xcarchive`, NEVER from `build-xcode/Build/Products/`.** That
directory belongs to Step 3 and an archive does not rebuild it, so it keeps whatever profile it
was signed with, indefinitely — and the failure is reported against the *app*, which sends you
back to the archive you just fixed:

```
ERROR: Unable to Install "Pippin" (IXUserPresentableErrorDomain error 14)
       Failed to install embedded profile for org.peregrine.Pippin : 0xe8008011
       (This provisioning profile has expired.)
```

**And it is NOT a re-signing tool.** AltStore, Sideloadly and the rest **rewrite the bundle
identifier**, and an app under a rewritten id is a *second app*: its own `Documents/` (so the
points, route and rides are invisible to the real one) and a second claim on the `pippin` URL
scheme, which makes the share extension's handoff a coin toss. `org.peregrine.Pippin.<team-id>`
on Chris's phone is exactly that, and it cost an afternoon of P14. The answer to a dead profile
is to re-run the five commands.

### The seven-day clock, and what a paid account changes

The identity on this mac is `Apple Development` under a **free** Apple ID, and a free team's
provisioning profile expires **one week** after it is issued. When it does the app stops
launching and the phone explains nothing. No date is written down here on purpose — it would be
wrong within the week. Ask the bundle, and check **both** profiles, because the extension
carries its own:

```sh
security cms -D -i build-xcode/Pippin.xcarchive/Products/Applications/Pippin.app/embedded.mobileprovision | plutil -extract ExpirationDate raw -
```

```sh
security cms -D -i build-xcode/Pippin.xcarchive/Products/Applications/Pippin.app/PlugIns/PippinShare.appex/embedded.mobileprovision | plutil -extract ExpirationDate raw -
```

The **certificate** is a separate thing with a separate life (`security find-identity -v -p
codesigning`, good for a year), so an expiry a week out is always the profile, never the
identity.

`ExportOptions.plist` says `method = debugging` — Xcode 15.3+'s name for the old
`development` — because that is the only method a free account can sign. **A paid Developer
Program membership is what changes this step**: `method = app-store-connect` for TestFlight and
the store, `release-testing` for ad-hoc, a distribution certificate for both, and a year on the
profile instead of a week. That change is the first blocker in `pippin-store-review.md`.

---

## The whole thing, in one block

Development, simulator, after a C++ change:

```sh
python3 port/apps/Pippin/stage_data.py && cmake --preset ios-sim && cmake --build build-ios-sim -j && xcodebuild -project port/apps/Pippin/Pippin.xcodeproj -scheme Pippin -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath build-xcode-sim build && xcrun simctl install booted build-xcode-sim/Build/Products/Debug-iphonesimulator/Pippin.app && xcrun simctl launch booted org.peregrine.Pippin
```

A shipping `.ipa` from a clean tree:

```sh
python3 port/apps/Pippin/stage_data.py --release && cmake --preset ios && cmake --build build-ios -j && rm -f ~/Library/Developer/Xcode/UserData/Provisioning\ Profiles/*.mobileprovision && xcodebuild -project port/apps/Pippin/Pippin.xcodeproj -scheme Pippin -sdk iphoneos -destination 'generic/platform=iOS' -configuration Release -allowProvisioningUpdates -archivePath build-xcode/Pippin.xcarchive archive && xcodebuild -exportArchive -archivePath build-xcode/Pippin.xcarchive -exportOptionsPlist port/apps/Pippin/ExportOptions.plist -exportPath build-xcode/ipa
```

## Before an upload: the checks worth two minutes

Run each against the bundle inside the archive, not against the source.

```sh
./port/apps/Pippin/local/set-branding.sh --show
```

```sh
plutil -p build-xcode/Pippin.xcarchive/Products/Applications/Pippin.app/Info.plist | grep -E "CFBundleDisplayName|PPReportAddress|CFBundleShortVersionString|CFBundleVersion|ITSAppUsesNonExemptEncryption"
```

The name is the shipping name, the address is the alias and not the literal
`$(PP_REPORT_ADDRESS)`, and all three targets moved to the same higher `CURRENT_PROJECT_VERSION`
— the extension fails validation if they disagree.

```sh
ls build-xcode/Pippin.xcarchive/Products/Applications/Pippin.app/Data/
```

No `kiawah_cycle.gpx`, and `kiawah.fvpoints` is the hand-edited one (check its size against
your copy).

```sh
strings -a build-xcode/Pippin.xcarchive/Products/Applications/Pippin.app/Pippin | grep -E "PPPixelProbe|PPViewportProbe|PPGestureDemo|PPShowStats|PPDemoFeed" | head
```

Nothing. The probe screens, the stats overlay, the gesture demo, the replay feed and the
`onOpenURL` `print` are all `#if DEBUG`; a Release build has no reference to any of them. (In a
Debug build these strings live in `Pippin.debug.dylib` beside the binary, not in the binary.)

```sh
ls build-xcode/Pippin.xcarchive/Products/Applications/Pippin.app/PrivacyInfo.xcprivacy build-xcode/Pippin.xcarchive/Products/Applications/Pippin.app/COPYING*
```

The privacy manifest is present and so are both licence texts — LGPL §4 wants them in the
bundle, and the About screen's link should point at a **tag that matches this build**.

## When something goes wrong

| What it says | What it is |
|---|---|
| `error: build-ios/…/libpippin_core.a is missing` | Step 1, with the preset the message names |
| `error: … is older than the core it is built from` | Step 1 again; the message names the newer file |
| `error: the offline data pack is not staged` | Step 2 |
| `pippin: INCOMPLETE — …` | a source in `testdata/` is missing; the listing above it says which row |
| `DANGLING sprite …` | the style's `sprite` pair is not both staged; Step 2 explains the pair |
| `no DejaVu Sans on this machine` | put `DejaVuSans.ttf` + `LICENSE_DEJAVU` in `port/apps/Pippin/fonts/` |
| `ld: warning: ignoring file … found architecture 'arm64', required 'x86_64'` | an Intel simulator slice; add `ARCHS=arm64` or use a named `-destination` |
| `IXUserPresentableErrorDomain error 14` / `0xe8008011` | an expired profile, and probably an install from `Build/Products/`; Step 4 |
| the app builds but is still called Pippin | Xcode cached the settings; reopen the project, or `xcodebuild` fresh |
| BUILD SUCCEEDED and the old behaviour | the derived-data trap; Step 3's warning |
