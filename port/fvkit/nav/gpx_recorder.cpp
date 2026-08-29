// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/nav/gpx_recorder.h"

#include <utility>

namespace fv {
namespace {

// The footer, written after every point and overwritten by the next one. Its
// LENGTH is what makes the append trick work — see the header — so it is
// built in one place and never assembled inline.
std::string Footer(bool segment_open, bool pretty) {
  const std::string tab = pretty ? "  " : "";
  std::string footer;
  if (segment_open) footer += tab + tab + "</trkseg>\n";
  footer += tab + "</trk>\n";
  footer += "</gpx>\n";
  return footer;
}

std::string XmlEscaped(const std::string& text) {
  std::string out;
  for (const char c : text) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&apos;"; break;
      default: out.push_back(c); break;
    }
  }
  return out;
}

}  // namespace

GpxRecorder::~GpxRecorder() { Close(); }

Status GpxRecorder::Open(const std::string& path, const GpxRecorderOptions& options) {
  Close();

  stream_.open(path, std::ios::binary | std::ios::trunc | std::ios::out);
  if (!stream_.is_open()) return Status::Error(kIoError, "GpxRecorder: cannot open " + path);

  path_ = path;
  options_ = options;
  segment_open_ = false;
  point_count_ = 0;
  segment_count_ = 0;
  has_time_span_ = false;
  first_time_s_ = 0.0;
  last_time_s_ = 0.0;
  have_last_time_ = false;

  // The <metadata><time> a whole-document write would carry is deliberately
  // ABSENT: it is "when the file was written", and this file is written over
  // the length of the ride. The track points carry the time that matters, and
  // an empty metadata block beats a stamp that means the moment the rider
  // pressed record and looks like the moment they finished.
  const std::string tab = options_.write.pretty ? "  " : "";
  std::string header;
  header += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  header += "<gpx version=\"1.1\" creator=\"" + XmlEscaped(options_.write.creator) + "\"\n";
  header += "     xmlns=\"http://www.topografix.com/GPX/1/1\"\n";
  header += "     xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n";
  header +=
      "     xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1"
      " http://www.topografix.com/GPX/1/1/gpx.xsd\">\n";
  if (!options_.track_name.empty()) {
    header += tab + "<metadata>\n";
    header += tab + tab + "<name>" + XmlEscaped(options_.track_name) + "</name>\n";
    header += tab + "</metadata>\n";
  }
  header += tab + "<trk>\n";
  if (!options_.track_name.empty()) {
    header += tab + tab + "<name>" + XmlEscaped(options_.track_name) + "</name>\n";
  }
  if (!options_.track_type.empty()) {
    header += tab + tab + "<type>" + XmlEscaped(options_.track_type) + "</type>\n";
  }
  stream_.write(header.data(), static_cast<std::streamsize>(header.size()));
  if (!stream_.good()) {
    stream_.close();
    return Status::Error(kIoError, "GpxRecorder: header write failed for " + path);
  }
  return WriteFooterAndFlush();
}

Status GpxRecorder::Add(const PositionFix& fix) {
  if (!stream_.is_open()) return Status::Error(kInvalidArg, "GpxRecorder::Add: not open");
  if (!fix.has_position) return Status::Ok();

  if (options_.drop_non_monotonic_time && have_last_time_ && fix.has_time &&
      fix.time_s <= last_time_s_) {
    return Status::Ok();
  }

  // A gap the recorder SAW is a boundary it KNOWS — the header says why this
  // is written rather than left for the reader to infer.
  const bool gap = options_.split_gap_s > 0.0 && have_last_time_ && fix.has_time &&
                   fix.time_s - last_time_s_ > options_.split_gap_s;

  const std::string tab = options_.write.pretty ? "  " : "";
  std::string body;
  if (gap && segment_open_) {
    body += tab + tab + "</trkseg>\n";
    segment_open_ = false;
  }
  if (!segment_open_) {
    body += tab + tab + "<trkseg>\n";
    segment_open_ = true;
    ++segment_count_;
  }
  body += FormatGpxTrackPoint(fix, options_.write, /*indent_level=*/options_.write.pretty ? 3 : 0);

  // Back over the footer, the point, then the footer again.
  stream_.seekp(footer_pos_);
  stream_.write(body.data(), static_cast<std::streamsize>(body.size()));
  if (!stream_.good()) return Status::Error(kIoError, "GpxRecorder: write failed for " + path_);

  ++point_count_;
  if (fix.has_time) {
    if (!has_time_span_) {
      has_time_span_ = true;
      first_time_s_ = fix.time_s;
    }
    last_time_s_ = fix.time_s;
    have_last_time_ = true;
  }
  return WriteFooterAndFlush();
}

Status GpxRecorder::WriteFooterAndFlush() {
  footer_pos_ = stream_.tellp();
  const std::string footer = Footer(segment_open_, options_.write.pretty);
  stream_.write(footer.data(), static_cast<std::streamsize>(footer.size()));
  stream_.flush();
  if (!stream_.good()) return Status::Error(kIoError, "GpxRecorder: flush failed for " + path_);
  return Status::Ok();
}

Status GpxRecorder::Close() {
  if (!stream_.is_open()) return Status::Ok();
  // Nothing to finish: the footer already on disk is the right one, and has
  // been after every fix. Close() exists to release the handle, not to make
  // the file valid.
  stream_.flush();
  const bool good = stream_.good();
  stream_.close();
  if (!good) return Status::Error(kIoError, "GpxRecorder: close failed for " + path_);
  return Status::Ok();
}

}  // namespace fv
