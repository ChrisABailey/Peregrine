# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""settings_file.save_keys: in-place key updates that `fv::Settings` reads back.

Run via ctest (pythonview_pytest) or:
  PYTHONPATH=build/port/bindings/pyfvw:port/apps pytest -q port/apps/test
"""

import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

pyfvw = pytest.importorskip("pyfvw")

import settings_file                                         # noqa: E402

ORIGINAL = """# user notes stay
[display]
# mm_per_pixel = 0.25
mm_per_pixel = 0.2   # tuned for the laptop
projection = "equalarc"

[geosym]
data_dir = "/old/path"
unknown_key = 7
"""


def _load(path):
    s = pyfvw.Settings()
    s.load(str(path))
    return s


def test_replaces_in_place_and_keeps_everything_else(tmp_path):
    p = tmp_path / "peregrine.ini"
    p.write_text(ORIGINAL)
    settings_file.save_keys(str(p), {"display.mm_per_pixel": 0.3,
                                     "geosym.data_dir": "/new path/# ok"})
    text = p.read_text()
    assert "# user notes stay" in text
    assert "# mm_per_pixel = 0.25" in text
    assert "unknown_key = 7" in text
    assert text.index("mm_per_pixel = 0.3") < text.index("projection")
    s = _load(p)
    assert s.get_float("display.mm_per_pixel", 0) == pytest.approx(0.3)
    assert s.get("geosym.data_dir") == "/new path/# ok"
    assert s.get("display.projection") == "equalarc"


def test_appends_missing_keys_and_sections(tmp_path):
    p = tmp_path / "peregrine.ini"
    p.write_text(ORIGINAL)
    settings_file.save_keys(str(p), {"geosym.brightness": -10,
                                     "tamask.altitude": 3000.0,
                                     "enc.show_meta_objects": True})
    s = _load(p)
    assert s.get_int("geosym.brightness", 0) == -10
    assert s.get_float("tamask.altitude", 0) == pytest.approx(3000.0)
    assert s.get_bool("enc.show_meta_objects", False) is True
    assert s.get("geosym.data_dir") == "/old/path"


def test_creates_a_missing_file(tmp_path):
    p = tmp_path / "cfg" / "settings.ini"
    settings_file.save_keys(str(p), {"routing.profile": ""})
    assert _load(p).has("routing.profile")
