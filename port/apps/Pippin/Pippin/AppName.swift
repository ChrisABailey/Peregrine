// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// AppName.swift — the app's display name, read from the bundle.
//
// The open-source project is Pippin; the build that goes to the App Store is
// called something else. The name therefore has to be data rather than
// source, or renaming the product would mean an edit that either lives in the
// working tree forever or is committed to a public repository that is
// supposed to say Pippin.
//
// The value is `CFBundleDisplayName`, filled in by the build settings from
// `$(PP_DISPLAY_NAME)`: "Pippin" in the tracked `Pippin.xcconfig`, overridden
// by a git-ignored `local/Local.xcconfig` on the machine that ships it. One
// value moves the home screen, the share sheet row, the permission alert and
// every sentence in the UI.
//
// Its own file because it is compiled into the app and into the share
// extension. In the extension `Bundle.main` is the extension's own bundle,
// which carries the same `$(PP_DISPLAY_NAME)`, so both answer alike without
// reaching across to each other.

import Foundation

enum AppName {
    /// What to call this app in a sentence a user reads.
    ///
    /// The last fallback is a literal because an empty string would print
    /// sentences with a hole in them ("Location is off for ."). The build
    /// settings set `CFBundleDisplayName`; `CFBundleName` is always present
    /// in a built bundle; the literal covers a SwiftUI preview, which has no
    /// bundle keys at all.
    static let display: String = {
        for key in ["CFBundleDisplayName", "CFBundleName"] {
            let value = Bundle.main.object(forInfoDictionaryKey: key) as? String
            let trimmed = value?.trimmingCharacters(in: .whitespaces) ?? ""
            if !trimmed.isEmpty { return trimmed }
        }
        return "Pippin"
    }()
}
