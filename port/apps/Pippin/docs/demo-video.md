# Recording the App Review demo video

Drives Pippin on a physical iPhone with a simulated GPS track so the video shows a
route being started from a standstill and then followed.

## Simulation tracks

Generated from `testdata/kiawah_cycle.gpx` (the Kiawah test ride: 6.1 km, 28 min,
12.9 km/h) by `port/tools/make_sim_gpx.py`. Each file holds the first fix for 15
seconds before playback starts, so the app can be launched and the route selected
while the ownship sits still at the trailhead.

| File | Length | Covers |
| --- | --- | --- |
| `testdata/sim/kiawah_demo_2x.gpx` | 15 s hold + 3:45 at 2x (26 km/h) | first 4 min of the ride, through the first turn |
| `testdata/sim/kiawah_demo_1x.gpx` | 15 s hold + 3:00 real time | first 3 min, no turn |
| `testdata/sim/kiawah_full_1x.gpx` | 15 s hold + 28:24 real time | the whole ride |

`kiawah_demo_2x.gpx` is the one to use: the trail's first real turn is at t=320 s,
which 2x playback reaches about 2:50 into the video.

`testdata/` is git-ignored, so the tracked copy of that file is
`port/apps/Pippin/kiawah_demo_2x.gpx`. On a fresh checkout, copy it into place
before running `stage_demo_feed.py`:

```bash
mkdir -p testdata/sim && cp port/apps/Pippin/kiawah_demo_2x.gpx testdata/sim/
```

Other cuts:

```bash
python3 port/tools/make_sim_gpx.py testdata/kiawah_cycle.gpx out.gpx --hold 15 --speed 2 --motion-seconds 225
```

`--hold` is the stationary dwell, `--speed` the playback multiplier, and
`--motion-seconds` trims playback (in output seconds, after the speed-up).

## Keeping the demo feed in a Debug build

The app replays a track from its own data pack when a Debug build is launched
with `-PPDemoFeed YES`, which is how the phone follows the route with no
receiver and no Xcode. Three pack settings point it at a simulation track:

```sh
python3 port/tools/stage_demo_feed.py
```

That copies the track into `port/apps/Pippin/Data/` and sets `demo_track`,
`demo_time_scale = 1.0` and `demo_loop = false`. The time scale matters: the
shipping default of 4 would compress the 15-second hold to under four seconds.

**Re-run it after every `stage_data.py`**, and only then. `stage_data.py`
copies the tracked `pippin.ini` over the pack's, which puts the three keys
back; it leaves the track file alone. A change to Swift or C++ needs neither
script — Xcode copies `Data/` into the bundle as it stands.

## Driving the device

Location simulation on a physical device comes from Xcode; `devicectl` has no
location command and `simctl location` is simulator-only. The iPhone stays
tethered for the whole recording.

1. Connect the iPhone, open `port/apps/Pippin/Pippin.xcodeproj`, select the device.
2. Product > Run. Grant location permission on the device if it asks.
3. In the debug bar at the bottom of Xcode, click the location arrow >
   **Add GPX File to Workspace…**, and pick `testdata/sim/kiawah_demo_2x.gpx`.
   Playback starts from the first fix, i.e. the 15-second hold.
4. Start the iPhone's own screen recording (Control Center) before selecting the
   file, so the hold covers opening the route.
5. When playback ends the last fix sticks; stop the recording and stop the Xcode run.

To pick the track at launch instead, add the GPX to the project (no target
membership needed) and set it under Product > Scheme > Edit Scheme > Run >
Options > Core Location > Default Location.

The recording lands in Photos on the device; AirDrop it to the Mac for trimming.
