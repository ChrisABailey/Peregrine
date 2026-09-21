// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// TurnAlerts.swift — what a turn sounds and feels like (guidance plan GD4).
//
// THE RINGER DECIDES, AND iOS DECIDES IT FOR US. There is no public API for
// the Ring/Silent switch, and none is needed: an `AVAudioSession` in the
// `.ambient` category is silenced by that switch, while `UIFeedbackGenerator`
// is not — it follows the rider's own Sounds & Haptics setting instead. So
// every alert fires its haptic and offers its chime, and the system drops the
// chime when the phone is silent. Ringer on is sound and haptic together;
// silent is haptic alone. Nothing here reads a switch, because nothing can.
//
// `.ambient` also mixes rather than interrupts, so a rider listening to music
// keeps hearing it. Ducking is deliberately not asked for: a chime this short
// under music is still audible, and ducking a podcast for 200 ms is worse
// than the chime being quiet.
//
// Foreground only, which is the step's own scope: GD4 is the screen-on case,
// and `UIFeedbackGenerator` does nothing from a suspended app anyway. The
// backgrounded rider is BG5's notification, where the sound and the vibration
// are already the rider's per-app notification settings.

import AVFoundation
import PippinKit
import UIKit

/// Plays the guidance's alerts. One per app, driven from `MapModel` as
/// frames arrive.
@MainActor
final class TurnAlerts {
    /// The session is configured once, on the first alert of a ride rather
    /// than at launch: a rider who never presses GPS never touches the audio
    /// system, and one who never plans a route never hears from it either.
    private var sessionReady = false

    /// Held across alerts. An `AVAudioPlayer` released while playing stops
    /// playing, so the chime needs an owner for as long as it sounds.
    private var player: AVAudioPlayer?

    /// Peak level of every chime, 0 to 1, from `guidance.alert_amplitude`.
    /// Setting it re-renders them, so a level can be tried without a rebuild.
    var amplitude: Double = AlertTone.defaultAmplitude {
        didSet {
            guard amplitude != oldValue else { return }
            rendered = [:]
        }
    }

    /// Chimes are rendered on first use and kept. Cleared by `amplitude`.
    private var rendered: [Chime: Data] = [:]

    private let impact = UIImpactFeedbackGenerator(style: .medium)
    private let light = UIImpactFeedbackGenerator(style: .light)
    private let notice = UINotificationFeedbackGenerator()

    /// Everything one frame's worth of events has to say.
    func play(_ events: [PPGuidanceEvent]) {
        for event in events { play(event) }
    }

    private func play(_ event: PPGuidanceEvent) {
        switch event.kind {
        case .approach:
            switch event.ring {
            case .headsUp:
                light.impactOccurred()
                chime(.headsUp)
            case .actNow:
                impact.impactOccurred()
                chime(.actNow)
            case .at:
                // The corner itself. The act-now chime was seconds ago and a
                // third sound for one turn is nagging, so this is felt and
                // not heard.
                impact.impactOccurred()
            case .none:
                break
            @unknown default:
                break
            }
        case .arrived:
            notice.notificationOccurred(.success)
            chime(.arrived)
        case .offRoute:
            notice.notificationOccurred(.warning)
            chime(.offRoute)
        case .rejoined:
            // Reassurance, not an instruction: the next corner will announce
            // itself in its own time.
            light.impactOccurred()
        case .passed:
            // Not an alert. It is how the banner knows to move on.
            break
        @unknown default:
            break
        }
    }

    /// Plays `chime`, replacing anything still sounding. Failures are silent
    /// on purpose: a chime that will not play is not worth interrupting a
    /// ride over, and the banner has already said the same thing.
    private func chime(_ which: Chime) {
        prepareSession()
        let data = rendered[which] ?? {
            let wav = AlertTone.wav(Self.notes(for: which), amplitude: amplitude)
            rendered[which] = wav
            return wav
        }()
        do {
            let next = try AVAudioPlayer(data: data)
            next.prepareToPlay()
            next.play()
            player = next
        } catch {
            player = nil
        }
    }

    private func prepareSession() {
        guard !sessionReady else { return }
        sessionReady = true
        // `.ambient` is the whole of the ringer rule: it is the category the
        // silent switch silences, and the one that mixes with the rider's
        // own audio.
        try? AVAudioSession.sharedInstance().setCategory(.ambient)
        try? AVAudioSession.sharedInstance().setActive(true)
    }

    // MARK: - The chimes

    enum Chime: Hashable {
        case headsUp, actNow, arrived, offRoute
    }

    /// Two notes a fifth apart for a turn, rising for the heads-up and a
    /// brighter pair for act-now, so the two are told apart with a phone in a
    /// bar bag rather than by looking.
    ///
    /// Pitched high on purpose: the phone's speaker is inefficient below
    /// about 1 kHz and the ear is most sensitive from 2 to 4 kHz, so a chime
    /// an octave down is quieter for the same amplitude. Notes are long
    /// enough to read as tones rather than ticks — the ear needs roughly
    /// 200 ms to hear a sound at its full loudness.
    private static func notes(for chime: Chime) -> [AlertTone.Note] {
        switch chime {
        case .headsUp:
            return [.init(hertz: 1568.0, seconds: 0.12),   // G6
                    .init(hertz: 2093.0, seconds: 0.22)]   // C7
        case .actNow:
            return [.init(hertz: 2093.0, seconds: 0.10),   // C7
                    .init(hertz: 2093.0, seconds: 0.10),
                    .init(hertz: 2093.0, seconds: 0.10),
                    .init(hertz: 2637.0, seconds: 0.24)]   // E7
        case .arrived:
            return [.init(hertz: 1568.0, seconds: 0.12),   // G6
                    .init(hertz: 2093.0, seconds: 0.12),   // C7
                    .init(hertz: 2637.0, seconds: 0.30)]   // E7
        case .offRoute:
            // Falling, and the only one that does: leaving the route is the
            // one alert that is not an instruction.
            return [.init(hertz: 1318.5, seconds: 0.14),   // E6
                    .init(hertz: 987.8, seconds: 0.26)]    // B5
        }
    }
}
