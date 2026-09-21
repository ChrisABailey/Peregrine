// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// AlertTone.swift — the turn chimes, synthesised rather than shipped.
//
// A handful of two-note chimes would otherwise be a handful of binary assets
// in the app bundle, versioned by hand and unopenable in a diff. They are
// short and simple enough to render, so they are: this builds a 16-bit mono
// PCM WAV in memory and `TurnAlerts` hands it to an `AVAudioPlayer`.
//
// Foundation only, so the arithmetic is testable on the mac
// (`test/run_alert_tone_test.sh`) rather than only audible on a phone.

import Foundation

/// A chime, as playable WAV bytes.
enum AlertTone {
    /// One note. Frequencies are named on the equal-tempered scale in
    /// `TurnAlerts`; this only counts samples.
    struct Note {
        let hertz: Double
        let seconds: Double
    }

    static let sampleRate = 44100.0
    static let bitsPerSample = 16

    /// Relative levels of the fundamental and its first two overtones.
    ///
    /// A pure sine is the wrong shape for a phone speaker: the driver is
    /// inefficient at the bottom of the chime's range and a rider hears the
    /// fundamental and nothing else. Overtones put energy where the speaker
    /// and the ear are both sensitive, which is what makes the chime carry
    /// over wind noise. They cost about 1 dB of RMS at the same peak, which
    /// `defaultAmplitude` more than covers.
    static let harmonics = [1.0, 0.35, 0.2]

    /// Peak of one period of the harmonic sum, so a note can be normalised to
    /// the amplitude it was asked for instead of clipping at 1.21x it.
    static let harmonicPeak: Double = {
        let steps = 4096
        var peak = 0.0
        for i in 0..<steps {
            let x = 2.0 * Double.pi * Double(i) / Double(steps)
            var sum = 0.0
            for (n, weight) in harmonics.enumerated() {
                sum += weight * sin(Double(n + 1) * x)
            }
            peak = max(peak, abs(sum))
        }
        return peak
    }()

    /// Peak amplitude of a chime, as a fraction of full scale. Loud on
    /// purpose: these play through a phone speaker in moving air, mixed under
    /// whatever the rider is listening to, and the headroom below 1.0 buys
    /// nothing a rider can hear.
    static let defaultAmplitude = 0.9

    /// The notes rendered end to end as a mono WAV.
    ///
    /// Each note is enveloped — a few milliseconds of attack and a longer
    /// release — because a tone switched on and off at a zero crossing still
    /// steps the loudspeaker, and the click is louder than the chime.
    ///
    /// An empty sequence is a valid, silent WAV rather than nil: a caller
    /// that plays it hears nothing, which is what it asked for.
    static func wav(_ notes: [Note], amplitude: Double = defaultAmplitude) -> Data {
        var samples: [Int16] = []
        for note in notes {
            let count = Int((note.seconds * sampleRate).rounded())
            guard count > 0 else { continue }
            let attack = min(Int(0.005 * sampleRate), count / 2)
            let release = min(Int(0.030 * sampleRate), count - attack)
            samples.reserveCapacity(samples.count + count)
            for i in 0..<count {
                var envelope = 1.0
                if i < attack {
                    envelope = Double(i) / Double(attack)
                } else if i > count - release {
                    envelope = Double(count - i) / Double(release)
                }
                let phase = 2.0 * Double.pi * note.hertz * Double(i) / sampleRate
                var wave = 0.0
                for (n, weight) in harmonics.enumerated() {
                    wave += weight * sin(Double(n + 1) * phase)
                }
                let value = wave / harmonicPeak * envelope * amplitude
                samples.append(Int16(max(-1.0, min(1.0, value)) * 32767.0))
            }
        }
        return riff(samples)
    }

    /// The canonical 44-byte header plus the samples, little-endian.
    private static func riff(_ samples: [Int16]) -> Data {
        let channels = 1
        let byteRate = Int(sampleRate) * channels * bitsPerSample / 8
        let blockAlign = channels * bitsPerSample / 8
        let dataBytes = samples.count * bitsPerSample / 8

        var out = Data()
        out.append(contentsOf: Array("RIFF".utf8))
        out.append(le32(36 + dataBytes))
        out.append(contentsOf: Array("WAVE".utf8))
        out.append(contentsOf: Array("fmt ".utf8))
        out.append(le32(16))
        out.append(le16(1))  // PCM, uncompressed
        out.append(le16(channels))
        out.append(le32(Int(sampleRate)))
        out.append(le32(byteRate))
        out.append(le16(blockAlign))
        out.append(le16(bitsPerSample))
        out.append(contentsOf: Array("data".utf8))
        out.append(le32(dataBytes))
        for sample in samples { out.append(le16(Int(sample))) }
        return out
    }

    private static func le16(_ value: Int) -> Data {
        let v = UInt16(bitPattern: Int16(truncatingIfNeeded: value))
        return Data([UInt8(v & 0xFF), UInt8((v >> 8) & 0xFF)])
    }

    private static func le32(_ value: Int) -> Data {
        let v = UInt32(truncatingIfNeeded: value)
        return Data([UInt8(v & 0xFF), UInt8((v >> 8) & 0xFF),
                     UInt8((v >> 16) & 0xFF), UInt8((v >> 24) & 0xFF)])
    }
}
