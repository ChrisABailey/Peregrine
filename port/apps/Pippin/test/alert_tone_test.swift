// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// Tests for AlertTone (guidance plan GD4).
//
// A chime cannot be checked by ear here, so what is pinned is the thing that
// would actually break: a WAV a player refuses. The header is the format's
// own, the byte counts agree with each other, and the envelope starts and
// ends at silence — which is the difference between a chime and a click.

import Foundation

var failures = 0

func check(_ condition: Bool, _ what: String) {
    if !condition {
        FileHandle.standardError.write("FAIL \(what)\n".data(using: .utf8)!)
        failures += 1
    }
}

func ascii(_ data: Data, _ range: Range<Int>) -> String {
    String(decoding: data[range], as: UTF8.self)
}

func le32(_ data: Data, _ at: Int) -> Int {
    Int(data[at]) | Int(data[at + 1]) << 8 | Int(data[at + 2]) << 16
        | Int(data[at + 3]) << 24
}

func le16(_ data: Data, _ at: Int) -> Int {
    Int(data[at]) | Int(data[at + 1]) << 8
}

func sample(_ data: Data, _ index: Int) -> Int16 {
    let at = 44 + index * 2
    return Int16(bitPattern: UInt16(data[at]) | UInt16(data[at + 1]) << 8)
}

// A quarter-second note.
let tone = AlertTone.wav([.init(hertz: 1000.0, seconds: 0.25)])

// The header a player looks for.
check(ascii(tone, 0..<4) == "RIFF", "RIFF magic")
check(ascii(tone, 8..<12) == "WAVE", "WAVE magic")
check(ascii(tone, 12..<16) == "fmt ", "fmt chunk")
check(le32(tone, 16) == 16, "PCM fmt chunk is 16 bytes")
check(le16(tone, 20) == 1, "uncompressed PCM")
check(le16(tone, 22) == 1, "mono")
check(le32(tone, 24) == 44100, "44.1 kHz")
check(le16(tone, 34) == 16, "16 bits per sample")
check(ascii(tone, 36..<40) == "data", "data chunk")

// The three lengths in the file agree with the file itself. A player that
// trusted a wrong one would read past the end or stop early.
let dataBytes = le32(tone, 40)
check(dataBytes == tone.count - 44, "data size matches the payload")
check(le32(tone, 4) == tone.count - 8, "RIFF size matches the file")
check(le32(tone, 28) == 44100 * 2, "byte rate is sample rate times block align")
check(le16(tone, 32) == 2, "block align")

// 0.25 s at 44.1 kHz is 11025 samples.
check(dataBytes / 2 == 11025, "sample count is the duration, got \(dataBytes / 2)")

// The envelope: silent at both ends, loud in the middle. This is the click.
check(sample(tone, 0) == 0, "starts at silence, got \(sample(tone, 0))")
check(abs(Int(sample(tone, 11024))) < 200,
      "ends at silence, got \(sample(tone, 11024))")
var peak = 0
for i in 0..<11025 { peak = max(peak, abs(Int(sample(tone, i)))) }
check(peak > 10000, "the middle is audible, peak \(peak)")
check(peak <= 32767, "and never clips, peak \(peak)")

// Several notes run end to end.
let pair = AlertTone.wav([.init(hertz: 784.0, seconds: 0.10),
                          .init(hertz: 1046.5, seconds: 0.16)])
check(le32(pair, 40) / 2 == 4410 + 7056, "two notes are both notes' samples")

// Degenerate inputs are a silent WAV, not a crash and not a malformed one.
let empty = AlertTone.wav([])
check(empty.count == 44, "an empty chime is a bare header")
check(le32(empty, 40) == 0, "...with no data")
let zero = AlertTone.wav([.init(hertz: 1000.0, seconds: 0.0)])
check(zero.count == 44, "a zero-length note contributes nothing")

// Amplitude is honoured, so a quieter chime is actually quieter.
let quiet = AlertTone.wav([.init(hertz: 1000.0, seconds: 0.25)], amplitude: 0.1)
var quietPeak = 0
for i in 0..<11025 { quietPeak = max(quietPeak, abs(Int(sample(quiet, i)))) }
check(quietPeak < peak / 2, "amplitude 0.1 is quieter than the default")

// And the peak IS the amplitude asked for: the harmonic sum is normalised by
// its own peak, so overtones brighten the tone without clipping or costing
// level. The middle of the note is past the attack and before the release.
let asked = AlertTone.defaultAmplitude * 32767.0
check(Double(peak) > asked * 0.98 && Double(peak) <= asked,
      "the peak is the amplitude asked for, got \(peak) of \(Int(asked))")

// The overtones are there. A pure sine has a mean-to-peak ratio of 2/pi;
// this waveform's crest factor is higher, which is the audible difference.
var sum = 0.0
for i in 2205..<8820 { sum += abs(Double(sample(tone, i))) }
let meanToPeak = sum / 6615.0 / Double(peak)
check(meanToPeak < 0.60, "the tone has overtones, mean/peak \(meanToPeak)")

if failures > 0 {
    FileHandle.standardError.write("\(failures) failure(s)\n".data(using: .utf8)!)
    exit(1)
}
print("alert_tone_test: ok")
