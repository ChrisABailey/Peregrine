<!-- SPDX-License-Identifier: LGPL-3.0-or-later
     Copyright (C) 2026 Chris Bailey
     Part of Peregrine, a cross-platform port of FalconView(tm).
     See COPYING.LESSER and NOTICE.md for the full licensing picture. -->

# Osprey

An iOS moving map for boats that draws NOAA S-57 electronic navigational charts with S-52
symbology. It is built from Pippin's sources: the `Osprey` target in
`port/apps/Pippin/Pippin.xcodeproj` compiles the same Swift files and links the same
PippinKit framework. Plan: `port/pippin-enc-plan.md` (CH).

This directory holds only what differs from Pippin:

| File | Role |
|---|---|
| `Info.plist` | `PPAppFlavor = osprey`, which `AppFlavor.swift` reads |
| `Assets.xcassets` | the app icon, from `art/make_icon.py` |
| `Data/` | the chart pack (git-ignored), staged as below |

Display name and bundle id are `OSPREY_DISPLAY_NAME` and `OSPREY_BUNDLE_ID` in
`Pippin.xcconfig`, overridable in `local/Local.xcconfig`. The share and Live Activity
extensions are not built for Osprey.

## Differences from Pippin

- Distances in nautical miles, speeds in knots, heights in feet (kilometres and miles stay
  in the menu).
- No beach routing, avoid hills or slope colours. No tide card or wind forecast.
- Recordings are "tracks", not "rides".
- A "Not for Navigation" notice on first launch, until acknowledged.

## Build

From the repo root:

```sh
python3 port/apps/Pippin/stage_data.py --region charleston-enc --pack port/apps/Osprey/Data
cmake --preset ios-sim && cmake --build build-ios-sim -j
xcodebuild -project port/apps/Pippin/Pippin.xcodeproj -scheme Osprey -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,name=iPhone 17 Pro' -derivedDataPath build-xcode-sim build
xcrun simctl install booted build-xcode-sim/Build/Products/Debug-iphonesimulator/Osprey.app
xcrun simctl launch booted org.peregrine.Osprey
```

The pack check in the build names the stage command when the pack is missing.

For a real device, use `ios` instead of `ios-sim` and `-destination 'generic/platform=iOS'`
```sh
python3 port/apps/Pippin/stage_data.py --region charleston-enc --pack port/apps/Osprey/Data
cmake --preset ios && cmake --build build-ios -j

xcodebuild -project port/apps/Pippin/Pippin.xcodeproj -scheme Osprey -sdk iphoneos -destination 'generic/platform=iOS' -configuration Release -allowProvisioningUpdates -archivePath build-xcode/Pippin.xcarchive archive

xcodebuild -exportArchive -archivePath build-xcode/Pippin.xcarchive -exportOptionsPlist port/apps/Pippin/ExportOptions.plist -exportPath build-xcode/ipa

xcrun devicectl device install app --device "$IPHONE" build-xcode/ipa/Osprey.ipa
```