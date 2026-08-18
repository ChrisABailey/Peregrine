# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See LICENSE and NOTICE.md for the full licensing picture.

"""pyfvw.nav — the moving map through the Python surface (MM4).

Not a translation of the C++ nav tests, which already pin the apron geometry,
the easing and the heading derivation (port/fvkit/nav/test/, and
port/fvkit/test/moving_map_overlay_test.cpp for the wiring). What is only
testable HERE is the seam:

  * a Python class really is an IPositionSource — the overlay drives it, and a
    fix emitted from Python reaches the camera;
  * the tick really returns everything a shell needs and applies NOTHING, so a
    Python shell moves its own map;
  * the callback seam (FixQueue.listener) survives the crossing;
  * MM5's two libraries meet HERE and nowhere else — fvkit declares the road
    network it wants and port/Routing supplies one, and pyfvw is the thing that
    links both, so "a routing.RoadGraph becomes a nav.RoadGraphNetwork the
    moving map snaps through" is a binding-level property.

Run via ctest (pyfvw_pytest) or:
  PYTHONPATH=build/port/bindings/pyfvw pytest -q port/bindings/pyfvw/test
"""

import os

import pytest

import pyfvw

nav = pyfvw.nav


def proj():
    p = pyfvw.engine.MapProjection()
    p.set_surface_size(800, 600)
    p.set_center(pyfvw.geo.GeoPoint(32.60, -80.08))
    p.set_scale(100000.0)
    return p


def fix(lat, lon, heading=None):
    f = nav.PositionFix()
    f.set_position(lat, lon)
    if heading is not None:
        f.true_heading_deg = heading
        f.has_true_heading = True
    return f


def jump():
    """duration 0 — the FalconView jump, so a camera assertion is exact."""
    s = nav.SlewSettings()
    s.duration_s = 0.0
    return s


# ---------------------------------------------------------------------------
# The fix
# ---------------------------------------------------------------------------


def test_a_fix_carries_validity_per_field():
    f = nav.PositionFix()
    assert not f.has_position and not f.has_speed
    f.set_position(32.6, -80.1)
    assert f.has_position and f.lat == 32.6

    # Merge is how two sentences of one epoch become one fix: the VTG's speed
    # lands on the GLL's position and nothing else moves.
    vtg = nav.PositionFix()
    vtg.speed_mps = 7.5
    vtg.has_speed = True
    f.merge(vtg)
    assert f.has_speed and f.speed_mps == 7.5
    assert f.lat == 32.6, "a field the other fix does not have is left alone"


def test_normalize_heading():
    assert nav.normalize_heading_deg(-30.0) == 330.0
    assert nav.normalize_heading_deg(360.0) == 0.0


# ---------------------------------------------------------------------------
# A source written in Python
# ---------------------------------------------------------------------------


class PySource(nav.PositionSource):
    """The seam D6 exists for: a receiver implemented outside C++."""

    def __init__(self):
        super().__init__()
        self.starts = 0
        self.stops = 0

    def start(self):
        # Python never sees a Status: returning normally IS success, and a
        # failure is raised.
        self.starts += 1
        self.set_running(True)

    def stop(self):
        self.stops += 1
        self.set_running(False)


def test_a_python_source_is_a_first_class_feed():
    seen = []
    src = PySource()
    src.set_listener(seen.append)
    src.start()
    assert src.running

    src.emit(fix(32.60, -80.08, 90.0))
    assert len(seen) == 1
    assert seen[0].lat == 32.60 and seen[0].true_heading_deg == 90.0

    src.stop()
    assert not src.running and src.stops == 1


def test_a_python_source_drives_the_overlay():
    p = proj()
    src = PySource()
    ovl = nav.MovingMapOverlay()
    ovl.set_slew_settings(jump())
    ovl.set_source(src)
    ovl.start()
    assert src.starts == 1

    src.emit(fix(32.62, -80.02, 45.0))
    t = ovl.tick(p, 0.1)
    assert t.new_fix
    assert t.fix.lat == 32.62
    assert t.target.changed, "no draw yet, so no apron, so the first fix centres"
    assert t.slew.changed


def test_the_source_comes_back_as_what_it_is():
    # The shell polls a scripted source through `overlay.source`, so the
    # accessor has to hand back the DERIVED type and not the interface -- a
    # bare IPositionSource has no poll() and the demo feed would sit still.
    ovl = nav.MovingMapOverlay()
    src = nav.ScriptedSource()
    ovl.set_source(src)
    assert isinstance(ovl.source, nav.ScriptedSource)
    assert ovl.source.poll() == 0


def test_the_queue_listener_crosses_the_binding():
    q = nav.FixQueue(4)
    src = PySource()
    src.set_listener(q.listener())
    src.start()
    for i in range(6):
        src.emit(fix(32.60 + i * 0.001, -80.08))
    # Full drops the OLDEST and counts it: a position feed is a stream of the
    # present.
    assert len(q) == 4
    assert q.dropped == 2
    latest = q.drain_latest()
    assert latest is not None and round(latest.lat, 4) == 32.605
    assert len(q) == 0
    assert q.drain_latest() is None


# ---------------------------------------------------------------------------
# The scripted source
# ---------------------------------------------------------------------------


def test_the_scripted_source_has_an_injectable_clock():
    track = nav.build_scripted_track(
        [pyfvw.geo.GeoPoint(32.60, -80.10), pyfvw.geo.GeoPoint(32.60, -80.05)],
        20.0,
    )
    assert len(track) > 2

    now = [0.0]
    src = nav.ScriptedSource(track)
    src.set_clock(lambda: now[0])
    seen = []
    src.set_listener(seen.append)
    src.start()
    assert len(seen) == 1, "the fix at t=0 is emitted from inside start()"

    now[0] += 3.5
    assert src.poll() == 3
    assert len(seen) == 4
    assert seen[-1].lon > seen[0].lon, "eastbound"
    assert seen[-1].has_true_heading


# ---------------------------------------------------------------------------
# The overlay: the tick applies nothing
# ---------------------------------------------------------------------------


def test_the_tick_answers_and_the_shell_applies():
    p = proj()
    ovl = nav.MovingMapOverlay()
    ovl.set_slew_settings(jump())
    before = p.center.lat

    ovl.push_fix(fix(32.70, -80.08, 0.0))
    t = ovl.tick(p, 0.1)
    assert t.target.changed
    # THE OVERLAY DID NOT MOVE THE MAP. That is the whole contract: the shell
    # applies what the tick returned, and until it does the projection is
    # exactly where it was.
    assert p.center.lat == before
    p.set_center(t.slew.center)
    assert p.center.lat != before


def test_a_mode_change_forces_a_recentre():
    p = proj()
    ovl = nav.MovingMapOverlay()
    ovl.set_slew_settings(jump())
    canvas = pyfvw.canvas.CpuCanvas(800, 600)

    ovl.push_fix(fix(32.60, -80.08, 0.0))
    ovl.tick(p, 0.1)
    ovl.on_draw(p, canvas)
    assert ovl.has_drawn
    assert not ovl.camera.apron.empty

    ovl.push_fix(fix(32.6001, -80.08, 0.0))
    assert not ovl.tick(p, 0.1).target.changed, "still inside its apron"

    ovl.set_auto_center(False)
    ovl.push_fix(fix(32.6002, -80.08, 0.0))
    assert not ovl.tick(p, 0.1).target.changed, "auto-centring off"

    ovl.set_auto_center(True)
    ovl.push_fix(fix(32.6003, -80.08, 0.0))
    assert ovl.tick(p, 0.1).target.changed, "the toggle forces it"


def test_the_ship_is_drawn_and_the_angle_is_the_drawn_one():
    p = proj()
    ovl = nav.MovingMapOverlay()
    ovl.set_slew_settings(jump())
    ovl.set_modes(nav.CameraModes(auto_center=False))
    ovl.set_color((0, 90, 200))
    ovl.size_px = 32.0
    ovl.push_fix(fix(32.60, -80.08, 45.0))
    ovl.tick(p, 0.1)

    canvas = pyfvw.canvas.CpuCanvas(800, 600)
    canvas.clear((0, 0, 0, 0))
    ovl.on_draw(p, canvas)
    buf = canvas.buffer
    assert buf.width == 800
    # The ship is at the map centre, so there is ink there.
    assert abs(ovl.drawn_x - 400.0) <= 0.5 and abs(ovl.drawn_y - 300.0) <= 0.5
    assert ovl.screen_angle_deg == 45.0

    # The drawn angle is the camera's own point_angle and ADDS the rotation.
    ovl.map_rotation_deg = 45.0
    assert ovl.screen_angle_deg == 90.0


def test_a_shell_that_cannot_rotate_says_so():
    # PythonView is no longer one — PR3 made both of its draw paths honour
    # MapProjection.set_rotation — but a shell that drops
    # tick().slew.rotation_deg still has to say so. Left unsaid, the overlay
    # adopts a rotation nobody applied and counter-rotates the ship to match.
    p = proj()
    ovl = nav.MovingMapOverlay()
    ovl.set_slew_settings(jump())
    assert ovl.rotation_supported, "the contract is that the shell applies"
    ovl.rotation_supported = False
    ovl.set_modes(nav.CameraModes(auto_center=True, auto_rotate=True))

    ovl.push_fix(fix(32.60, -80.10, 90.0))
    t = ovl.tick(p, 0.1)
    assert t.target.rotation_deg == 270.0, "the camera still answers"
    assert ovl.map_rotation_deg == 0.0, "the overlay does not adopt it"
    assert ovl.screen_angle_deg == 90.0, "the ship is drawn steaming east"


def test_the_symbol_id_is_a_builtin_id():
    ovl = nav.MovingMapOverlay()
    assert ovl.symbol_id == pyfvw.symbol.builtin.OWNSHIP
    ovl.symbol_id = pyfvw.symbol.builtin.NORTH
    assert ovl.symbol_id == "fv.north"


def test_a_non_zero_duration_arrives_over_several_frames():
    p = proj()
    ovl = nav.MovingMapOverlay()
    s = nav.SlewSettings()
    s.duration_s = 0.4
    s.easing = nav.SlewEasing.LINEAR
    ovl.set_slew_settings(s)

    ovl.push_fix(fix(32.70, -80.02, 45.0))
    t = ovl.tick(p, 0.1)
    assert t.slew.active
    assert abs(t.slew.center.lat - t.target.center.lat) > 1e-9

    goal = t.target.center
    for _ in range(10):
        if not ovl.slew.active:
            break
        ovl.tick(p, 0.1)
    assert not ovl.slew.active
    assert abs(ovl.slew.center.lat - goal.lat) < 1e-9


def test_the_type_is_registered_as_static():
    registry = pyfvw.app.OverlayTypeRegistry()
    pyfvw.app.register_builtin_types(registry)
    desc = registry.find(nav.MovingMapOverlay.TYPE_ID)
    assert desc is not None
    assert registry.is_static(nav.MovingMapOverlay.TYPE_ID)
    assert not desc.restore_at_startup


# ---------------------------------------------------------------------------
# Snap to road (MM5)
# ---------------------------------------------------------------------------

# One straight residential road running due east through the projection's
# centre, and a footway 20 m north of it that a car must not snap to.
SNAP_OSM = """<?xml version="1.0" encoding="UTF-8"?>
<osm version="0.6">
 <node id="1" lat="32.6000000" lon="-80.1200000"/>
 <node id="2" lat="32.6000000" lon="-80.0400000"/>
 <node id="3" lat="32.6001800" lon="-80.1200000"/>
 <node id="4" lat="32.6001800" lon="-80.0400000"/>
 <way id="101">
  <nd ref="1"/><nd ref="2"/>
  <tag k="highway" v="residential"/><tag k="name" v="Test Road"/>
 </way>
 <way id="102">
  <nd ref="3"/><nd ref="4"/>
  <tag k="highway" v="footway"/><tag k="name" v="Boardwalk"/>
 </way>
</osm>
"""

# 10 m, in degrees of latitude.
TEN_M = 10.0 / (6371008.8 * 3.14159265358979323846 / 180.0)


def snap_graph(tmp_path):
    path = tmp_path / "snap.osm"
    path.write_text(SNAP_OSM)
    return pyfvw.routing.RoadGraph.build([str(path)])


def test_snapping_is_off_until_a_network_is_given():
    ovl = nav.MovingMapOverlay()
    assert not ovl.snapping
    p = proj()
    ovl.set_slew_settings(jump())
    ovl.push_fix(fix(32.60 + TEN_M, -80.08))
    t = ovl.tick(p, 0.1)
    assert not t.snap.snapped and not t.snap_applied
    assert abs(ovl.last_fix.lat - (32.60 + TEN_M)) < 1e-12


def test_a_road_graph_becomes_a_network_the_moving_map_snaps_through(tmp_path):
    net = nav.RoadGraphNetwork(snap_graph(tmp_path))
    # The footway is not a road a car may be on, so only one arc is indexed.
    assert net.indexed_arcs == 1

    ovl = nav.MovingMapOverlay()
    ovl.set_slew_settings(jump())
    ovl.set_road_network(net)
    assert ovl.snapping

    ovl.push_fix(fix(32.60 + TEN_M, -80.08))
    t = ovl.tick(proj(), 0.1)
    assert t.snap.snapped and t.snap_applied
    assert t.snap.road_name == "Test Road"
    assert abs(t.snap.offset_m - 10.0) < 0.5
    # The ship is drawn on the road ...
    assert abs(ovl.last_fix.lat - 32.60) < 1e-9
    # ... and the raw fix survives, which is the rule the whole layer is built
    # around.
    assert abs(t.snap.raw.lat - (32.60 + TEN_M)) < 1e-12
    assert abs(t.snap.applied().lat - 32.60) < 1e-9


def test_a_walker_may_have_the_footway_a_car_may_not(tmp_path):
    graph = snap_graph(tmp_path)
    car = nav.RoadGraphNetwork(graph)
    every = nav.RoadGraphNetwork(graph, nav.RoadSnapFilter.ALL)
    assert every.indexed_arcs > car.indexed_arcs


def test_the_snapper_can_be_driven_on_its_own(tmp_path):
    snapper = nav.RoadSnapper(nav.RoadGraphNetwork(snap_graph(tmp_path)))
    assert snapper.enabled

    settings = nav.RoadSnapSettings()
    settings.stay_bonus_m = 0.0
    snapper.set_settings(settings)
    assert snapper.settings.stay_bonus_m == 0.0

    f = fix(32.60 + TEN_M, -80.08)
    f.speed_mps = 10.0
    f.has_speed = True
    s = snapper.snap(f, 90.0)
    assert s.snapped
    assert s.road_name == "Test Road"
    assert abs(s.bearing_deg - 90.0) < 1.0  # the road, in the direction driven
    assert len(snapper.last_candidates) == 1
    assert snapper.last.arc == s.arc

    snapper.reset()
    assert not snapper.last.snapped

    snapper.set_network(None)
    assert not snapper.enabled
    assert not snapper.snap(f).snapped


def test_a_snap_the_overlay_is_not_sure_enough_of_is_not_applied(tmp_path):
    ovl = nav.MovingMapOverlay()
    ovl.set_slew_settings(jump())
    ovl.set_road_network(nav.RoadGraphNetwork(snap_graph(tmp_path)))
    ovl.snap_min_confidence = 0.99
    ovl.push_fix(fix(32.60 + TEN_M, -80.08))
    t = ovl.tick(proj(), 0.1)
    assert t.snap.snapped and not t.snap_applied
    assert abs(ovl.last_fix.lat - (32.60 + TEN_M)) < 1e-12


# ---------------------------------------------------------------------------
# The three real feeds (MM6), through the binding (MM7)
# ---------------------------------------------------------------------------
#
# Same rule as the rest of this file: the C++ tests already pin the parse, the
# framing and the schedule (port/fvkit/nav/test/{nmea,gpx,line_transport,
# scripted_source}_test.cpp). What is only testable HERE is the crossing --
# that a Status never reaches Python, that an out-parameter became a return or
# a None, and above all that the CHAIN the app is about to walk really joins up
# in Python: a file on disk -> fixes -> a schedule -> a source -> the overlay.


def _gpx_fixture():
    d = os.environ.get("FVW_TESTDATA_DIR")
    if not d:
        return None
    p = os.path.join(d, "kiawah_cycle.gpx")
    return p if os.path.isfile(p) else None


# A minimal GPX, so the shape of the reader is testable with no fixture at all.
TINY_GPX = """<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="pyfvw-test" xmlns="http://www.topografix.com/GPX/1/1">
 <trk><name>Two Legs</name><trkseg>
  <trkpt lat="32.6000" lon="-80.0800"><ele>3.0</ele><time>2026-05-08T20:00:00Z</time></trkpt>
  <trkpt lat="32.6010" lon="-80.0800"><ele>4.0</ele><time>2026-05-08T20:00:10Z</time></trkpt>
  <trkpt lat="32.6020" lon="-80.0800"><ele>5.0</ele><time>2026-05-08T20:00:20Z</time></trkpt>
 </trkseg></trk>
</gpx>
"""


def test_a_gpx_document_crosses_whole():
    doc = nav.parse_gpx(TINY_GPX)
    assert doc.version == "1.1"
    assert doc.creator == "pyfvw-test"
    assert doc.track_point_count == 3
    track = doc.longest_track          # None when there is none, not a null
    assert track is not None and track.name == "Two Legs"
    assert track.point_count == 3
    assert len(track.segments) == 1 and len(track.segments[0]) == 3

    # Rule 3: <ele> is read as MSL, and it arrives as an altitude.
    first = track.segments[0].points[0]
    assert first.has_altitude and abs(first.altitude_msl_m - 3.0) < 1e-9
    # Rule 2: speed is derived (not on the first point of a segment), heading
    # is NOT -- HeadingResolver derives one in screen space and a true bearing
    # written here would look reported and quietly win.
    assert not first.has_speed
    assert not first.has_true_heading
    second = track.segments[0].points[1]
    assert second.has_speed and second.speed_mps > 0.0
    assert not second.has_true_heading


def test_gpx_options_cross_and_really_apply():
    opts = nav.GpxReadOptions()
    opts.derive_true_heading = True
    doc = nav.parse_gpx(TINY_GPX, opts)
    p = doc.longest_track.segments[0].points[0]
    assert p.has_true_heading
    assert abs(p.true_heading_deg) < 1.0   # due north, and it is a bearing

    opts = nav.GpxReadOptions()
    opts.split_gap_s = 5.0                 # the points are 10 s apart
    doc = nav.parse_gpx(TINY_GPX, opts)
    assert len(doc.longest_track.segments) == 3


def test_a_reader_failure_raises_rather_than_returning_a_status(tmp_path):
    with pytest.raises(pyfvw.FvError):
        nav.read_gpx_file(str(tmp_path / "not-here.gpx"))
    with pytest.raises(pyfvw.FvError):
        nav.read_nmea_log(str(tmp_path / "not-here.nmea"))
    bad = tmp_path / "bad.gpx"
    bad.write_text("<gpx><trk>")
    with pytest.raises(pyfvw.FvError):
        nav.read_gpx_file(str(bad))


def test_a_recorded_ride_reaches_the_moving_map():
    """THE WHOLE MM7 CHAIN, in the three calls the app makes.

    file -> fixes -> schedule -> ScriptedSource -> MovingMapOverlay. If this
    passes, "File > Open Track" is wiring rather than design."""
    path = _gpx_fixture()
    if path is None:
        pytest.skip("no TestData")
    doc = nav.read_gpx_file(path)
    assert doc.track_point_count == 1705          # the real ride, 1 Hz
    fixes = nav.flatten_gpx_fixes(doc)
    assert len(fixes) == 1705
    script = nav.build_scripted_track_from_fixes(fixes)
    # The fixes' own stamps ARE the schedule: 1705 points at 1 Hz is 1704 s.
    assert len(script) == 1705
    assert abs(script[-1].t_s - 1704.0) < 1e-6
    # And the fixes themselves are passed through untouched.
    assert abs(script[0].fix.lat - fixes[0].lat) < 1e-12

    source = nav.ScriptedSource(script)
    clock = [0.0]
    source.set_clock(lambda: clock[0])
    ovl = nav.MovingMapOverlay()
    ovl.set_slew_settings(jump())
    ovl.set_source(source)
    ovl.start()
    clock[0] = 5.0
    assert source.poll() >= 5
    p = proj()
    p.set_center(pyfvw.geo.GeoPoint(fixes[0].lat, fixes[0].lon))
    t = ovl.tick(p, 0.1)
    assert t.new_fix and ovl.has_fix
    assert abs(ovl.last_fix.lat - fixes[5].lat) < 1e-12
    # The ride really is on Kiawah, and its elevation really does go below sea
    # level -- which is what an unsigned or clamped <ele> would hide.
    assert 32.5 < ovl.last_fix.lat < 32.7 and -80.2 < ovl.last_fix.lon < -80.0
    assert min(f.altitude_msl_m for f in fixes) < 0.0


def test_a_gpx_segment_is_a_path_a_drawing_can_take():
    doc = nav.parse_gpx(TINY_GPX)
    path = nav.gpx_segment_path(doc.longest_track.segments[0])
    assert len(path) == 3
    assert abs(path[0].lat - 32.6000) < 1e-9
    # It is the same argument build_scripted_track (MM1) takes.
    assert len(nav.build_scripted_track(path, 5.0)) > 0


# ---------------------------------------------------------------------------
# NMEA


def test_a_sentence_that_will_not_parse_is_None_and_not_a_false():
    reading = nav.parse_nmea_sentence(
        "$GNRMC,123519.00,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*5A")
    assert reading is not None
    assert reading.type == nav.NmeaType.RMC
    assert reading.talker == "GN"          # any talker, not just GP
    assert reading.has_date and reading.year == 1994
    assert reading.fix.has_position
    # TIME IS SPLIT IN TWO, and a single-sentence parse leaves it split even
    # for an RMC that carries both halves: stamping the fix is the
    # ASSEMBLER's job, and until one has run has_time is False rather than a
    # 1970 stamp.
    assert reading.has_time_of_day and reading.has_date
    assert not reading.fix.has_time
    # Q1: a sentence one field short is rejected WHOLE.
    assert nav.parse_nmea_sentence("$GNRMC,123519.00,A,4807.038,N") is None
    assert nav.parse_nmea_sentence("not a sentence") is None
    assert nav.nmea_type_of("$GPVTG,,T,,M,,N,,K")[0] == nav.NmeaType.VTG
    assert nav.nmea_sentence_looks_valid("$GPGLL,4807.038,N,01131.000,E")
    assert nav.split_nmea_fields("$GPGLL,,,,")[0] == "GPGLL"


def test_the_assembler_answers_with_a_fix_or_with_None():
    a = nav.NmeaFixAssembler()
    a.set_date_hint(2026, 5, 8)
    assert a.has_date
    # Two sentences of ONE epoch merge; the group closes on the next epoch.
    assert a.add_line("$GPGGA,120000.00,3236.000,N,08004.000,W,1,08,1.0,5.0,M,,M,,*67") is None
    assert a.add_line("$GPRMC,120001.00,A,3236.100,N,08004.000,W,010.0,090.0,080526,,*2B") is not None
    merged = a.flush()
    assert merged is not None and merged.has_position
    assert a.lines_seen == 2 and a.sentences_parsed == 2
    assert a.sentences_rejected == 0 and a.fixes_emitted == 2

    live = nav.NmeaFixAssembler()
    live.emit_per_sentence = True
    assert live.emit_per_sentence
    got = live.add_line("$GPGGA,120000.00,3236.000,N,08004.000,W,1,08,1.0,5.0,M,,M,,*67")
    assert got is not None                 # no epoch of latency
    assert not got.has_time                # ... and no invented date


def test_a_string_transport_is_the_python_door_into_a_live_feed():
    """A Python object with bytes has TWO doors and this is the cheaper one:
    hand StringLineTransport whatever your own socket just read.

    auto_end=False is what makes it a LIVE feed rather than a recording: a
    transport that has run out of canned data but has not ended answers AGAIN,
    which is exactly what a socket does."""
    transport = nav.StringLineTransport("", auto_end=False)
    source = nav.NmeaLineSource(transport)
    seen = []
    source.set_listener(seen.append)
    source.start()
    assert source.running

    transport.add_data("$GPRMC,120000.00,A,3236.000,N,08004.000,W,000.0,000.0,080526,,*23\r\n")
    # ONE EPOCH OF LATENCY: the group is only known to be over when the next
    # one starts, which is what set_emit_per_sentence is the way out of.
    assert source.poll() == 0
    transport.add_data("$GPRMC,120001.00,A,3236.100,N,")   # half a sentence
    assert source.poll() == 0                              # framing holds it
    transport.add_data("08004.000,W,010.0,090.0,080526,,*2B\r\n")
    assert source.poll() == 1                              # the FIRST epoch
    assert seen[0].has_position and abs(seen[0].lat - 32.60) < 1e-9

    transport.set_ended()
    assert source.poll() == 1                              # flushed, once
    assert source.at_end and not source.running
    assert len(seen) == 2
    assert source.lines_read == 2 and source.fixes_emitted == 2
    assert source.assembler.sentences_rejected == 0


def test_a_transport_reads_a_tuple_and_end_is_not_an_error():
    t = nav.StringLineTransport("one\r\ntwo\n")
    t.open()
    assert t.is_open
    assert t.read_line() == (nav.LineResult.LINE, "one")
    assert t.read_line() == (nav.LineResult.LINE, "two")
    assert t.read_line()[0] == nav.LineResult.END
    assert t.error_message == ""           # END is not an error
    assert "string" in t.description

    live = nav.StringLineTransport("", auto_end=False)
    live.open()
    assert live.read_line()[0] == nav.LineResult.AGAIN

    buf = nav.LineBuffer(8)
    buf.append("hi\r\n\nthere\n")
    assert buf.next_line() == "hi"         # '\r' dropped, blank line dropped
    assert buf.next_line() == "there"
    assert buf.next_line() is None


def test_a_recorded_log_round_trips_through_the_build_side(tmp_path):
    """The build side and the read side meet, and the metre they meet to is
    the WIRE FORMAT: minutes go out with three decimals."""
    f = nav.PositionFix()
    f.set_position(32.6000, -80.0800)
    f.time_s = nav.utc_to_epoch_seconds(2026, 5, 8, 12 * 3600.0)
    f.has_time = True
    f.speed_mps = 10.0
    f.has_speed = True
    f.true_heading_deg = 90.0
    f.has_true_heading = True
    sentence = nav.build_rmc(f)
    assert sentence.startswith("$GPRMC") and "*" in sentence
    assert nav.nmea_sentence_looks_valid(sentence.strip())
    assert nav.build_gga(f).startswith("$GPGGA")
    assert nav.build_vtg(f).startswith("$GPVTG")

    log = tmp_path / "ride.nmea"
    log.write_text(sentence + nav.build_rmc(f).replace("120000", "120001"))
    fixes = nav.read_nmea_log(str(log))
    assert len(fixes) >= 1
    assert abs(fixes[0].lat - 32.6000) < 1e-4     # ~1 m, the wire format
    assert abs(fixes[0].lon - (-80.0800)) < 1e-4

    # And a FileLineTransport reads the same file through the live path.
    source = nav.NmeaLineSource(nav.FileLineTransport(str(log)))
    assert source.transport.follow is False
    source.start()
    source.poll()
    source.poll()
    assert source.fixes_emitted >= 1
    assert source.lines_read >= 1


def test_a_recorded_log_and_a_gpx_arrive_at_the_same_seam(tmp_path):
    """One replay path, not one per format -- which is the whole reason MM6
    has two readers and no second kind of track."""
    lines = []
    for i in range(4):
        f = nav.PositionFix()
        f.set_position(32.60 + i * 0.001, -80.08)
        f.time_s = nav.utc_to_epoch_seconds(2026, 5, 8, 12 * 3600.0 + i)
        f.has_time = True
        lines.append(nav.build_rmc(f))
    log = tmp_path / "same.nmea"
    log.write_text("".join(lines))

    from_log = nav.build_scripted_track_from_fixes(nav.read_nmea_log(str(log)))
    from_gpx = nav.build_scripted_track_from_fixes(
        nav.flatten_gpx_fixes(nav.parse_gpx(TINY_GPX)))
    assert len(from_log) >= 3 and len(from_gpx) == 3
    # Both schedules start at 0 and pace themselves from the fixes' stamps.
    assert from_log[0].t_s == 0.0 and from_gpx[0].t_s == 0.0
    assert abs(from_gpx[-1].t_s - 20.0) < 1e-9

    opts = nav.FixScriptOptions()
    opts.max_gap_s = 5.0        # the coffee-stop cap, and the points are 10 s
    capped = nav.build_scripted_track_from_fixes(
        nav.flatten_gpx_fixes(nav.parse_gpx(TINY_GPX)), opts)
    assert abs(capped[-1].t_s - 10.0) < 1e-9


def test_the_network_transports_construct_without_a_network(tmp_path):
    """No socket is opened here -- what is asserted is that the two 'phone
    GPS' shapes exist in Python with the fields a host/port dialog fills in."""
    tcp = nav.TcpLineTransport("127.0.0.1", 10110)
    assert (tcp.host, tcp.port) == ("127.0.0.1", 10110)
    assert "10110" in tcp.description and not tcp.is_open
    udp = nav.UdpLineTransport(0)
    assert udp.port == 0
    udp.open()                  # an ephemeral port, so no test races another
    assert udp.bound_port != 0
    assert udp.read_line()[0] == nav.LineResult.AGAIN
    assert udp.bytes_received == 0
    udp.close()
