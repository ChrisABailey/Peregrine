# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Chris Bailey
# Part of Peregrine, a cross-platform port of FalconView(tm).
# See COPYING.LESSER and NOTICE.md for the full licensing picture.

"""A Python overlay that declares properties, read through the C++ capability."""

import copy

import pytest

import pyfvw

ov = pyfvw.overlay


class Rings(ov.Overlay):
    def __init__(self):
        super().__init__("Rings")
        self.values = {"count": 3, "spacing": 1.0, "colour": (255, 0, 0, 255)}

    def properties_schema(self):
        return [
            {"key": "count", "type": "int", "default": 3, "min": 1, "max": 10},
            {"key": "spacing", "label": "Spacing", "type": "double", "default": 1.0},
            {"key": "colour", "type": "color", "default": (255, 0, 0, 255)},
        ]

    def read_property(self, key):
        return self.values[key]

    def write_property(self, key, value):
        self.values[key] = value


def test_schema_comes_from_properties_schema():
    rows = Rings().describe_properties()
    assert [r["key"] for r in rows] == ["count", "spacing", "colour"]
    assert rows[0]["min"] == 1 and rows[0]["max"] == 10
    assert rows[1]["label"] == "Spacing"
    assert rows[2]["default"] == (255, 0, 0, 255)


def test_set_property_reaches_write_property():
    r = Rings()
    r.set_property("count", 5)
    r.set_property("colour", (0, 0, 255))
    assert r.values["count"] == 5
    assert r.get_property("count") == 5
    assert r.get_property("colour") == (0, 0, 255, 255)


def test_out_of_range_is_refused_before_python_sees_it():
    r = Rings()
    with pytest.raises(pyfvw.FvError):
        r.set_property("count", 11)
    assert r.values["count"] == 3


def test_an_overlay_without_a_schema_has_no_properties():
    assert ov.Overlay("plain").describe_properties() == []


def test_a_projection_copies():
    p = pyfvw.engine.MapProjection()
    p.set_surface_size(64, 32)
    q = copy.copy(p)
    p.set_surface_size(8, 8)
    assert q.surface_size == (64, 32)
