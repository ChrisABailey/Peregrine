// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// ShareViewController.swift — Pippin's row in another app's share sheet.
//
// A share sheet lists app extensions and nothing else, so receiving a place
// from Apple Maps requires a second target: a separate process, bundle and
// signing identity. That is the cost of the Maps → Share → Pippin gesture.
//
// This class is deliberately thin. It knows how to get a string out of an
// `NSItemProvider` and how to open a URL; what a shared place is lives in
// `PlaceLink.swift`, which is compiled into both targets and tested on the
// mac. An extension is the worst place in the system to debug logic: almost
// no console, a memory limit in tens of megabytes, and a host app that kills
// it at will.
//
// The handoff is a URL rather than a shared container (see
// `PlaceLink.callbackURL(for:)`), which costs no App Group entitlement but
// does cost the `open(_:)` dance below.

import UIKit
import UniformTypeIdentifiers

final class ShareViewController: UIViewController {
    private let card = UIView()
    private let titleLabel = UILabel()
    private let detailLabel = UILabel()
    private let spinner = UIActivityIndicatorView(style: .medium)
    private let closeButton = UIButton(type: .system)

    /// Whether the app hosting the share sheet has resigned active, which is
    /// how this process learns that Pippin came forward. See `hand(over:)`.
    private var hostResigned = false
    private var hostObserver: NSObjectProtocol?

    override func viewDidLoad() {
        super.viewDidLoad()
        buildCard()
        hostObserver = NotificationCenter.default.addObserver(
            forName: NSNotification.Name.NSExtensionHostWillResignActive,
            object: nil, queue: .main) { [weak self] _ in
                self?.hostResigned = true
            }
        Task { await run() }
    }

    deinit {
        if let hostObserver { NotificationCenter.default.removeObserver(hostObserver) }
    }

    // MARK: - The errand

    /// Reads what arrived, turns it into a place, and hands it to the app.
    /// The offline parse runs on everything first, so an Apple Maps share
    /// that already carries its coordinate never waits on a network the rider
    /// may not have. Only a share that yielded nothing is looked up.
    private func run() async {
        guard let shared = await sharedText() else {
            PippinLog.share.error("extension: the share carried no url and no text")
            fail("Nothing in that share looked like a place.")
            return
        }
        PippinLog.share.notice(
            "extension: url=\(shared.url?.absoluteString ?? "-", privacy: .public) text=\(shared.text.prefix(200), privacy: .public)")

        if let place = parse(shared) {
            PippinLog.share.notice(
                "extension: parsed offline -> \(place.latitude, privacy: .public),\(place.longitude, privacy: .public) \(place.displayName, privacy: .public)")
            await hand(over: place)
            return
        }

        // A short link: the iOS 26 Apple Maps case and every Google Maps
        // case. The only moment Pippin uses the network.
        guard let url = shared.url ?? PlaceLink.firstURL(in: shared.text),
              PlaceLink.mayBeShortened(url), PlaceLink.allowsNetworkLookup else {
            PippinLog.share.error("extension: nothing here is worth looking up")
            fail("That link doesn't carry a position.")
            return
        }

        show(title: "Looking up the link…",
             detail: url.host ?? "", busy: true)
        if let place = await PlaceLink.resolve(url) {
            PippinLog.share.notice(
                "extension: resolved -> \(place.latitude, privacy: .public),\(place.longitude, privacy: .public) \(place.displayName, privacy: .public)")
            await hand(over: place)
        } else {
            // Distinct from "no position in that link": this one is worth
            // retrying with a signal.
            fail("Couldn't get a position out of that link. "
                 + "Short links have to be looked up, and that needs a signal.")
        }
    }

    /// Everything the parser might find something in.
    private struct Shared {
        var url: URL?
        var text: String
    }

    private func parse(_ shared: Shared) -> SharedPlace? {
        if let url = shared.url, let place = PlaceLink.place(in: url) { return place }
        if !shared.text.isEmpty, let place = PlaceLink.place(in: shared.text) { return place }
        return nil
    }

    /// Pulls both the URL and the text out of the extension's input items. A
    /// Google Maps share is a string with the name on one line and the link
    /// on the next; an Apple Maps share is a URL with everything in it.
    private func sharedText() async -> Shared? {
        var url: URL?
        var text = ""

        for case let item as NSExtensionItem in extensionContext?.inputItems ?? [] {
            // The prose beside the attachment, which for a Maps share is the
            // place's name.
            if let content = item.attributedContentText?.string, !content.isEmpty,
               text.isEmpty {
                text = content
            }
            for provider in item.attachments ?? [] {
                if url == nil, provider.hasItemConformingToTypeIdentifier(UTType.url.identifier) {
                    url = await load(UTType.url, from: provider) as? URL
                }
                if provider.hasItemConformingToTypeIdentifier(UTType.vCard.identifier),
                   let card = await load(UTType.vCard, from: provider) {
                    text = string(from: card) ?? text
                }
                if provider.hasItemConformingToTypeIdentifier(UTType.plainText.identifier),
                   let plain = await load(UTType.plainText, from: provider),
                   let string = string(from: plain), !string.isEmpty {
                    // A plain-text attachment beats the attributed content,
                    // which is often the same string with the link stripped.
                    text = string
                }
            }
        }

        if url == nil && text.isEmpty { return nil }
        return Shared(url: url, text: text)
    }

    /// Wraps `loadItem`, a completion-handler API from before async. The
    /// result is `NSSecureCoding?` because one identifier may yield a `URL`,
    /// a `String`, an `NSData` or a file URL depending on who is sharing;
    /// `string(from:)` sorts that out.
    private func load(_ type: UTType, from provider: NSItemProvider) async -> NSSecureCoding? {
        await withCheckedContinuation { continuation in
            provider.loadItem(forTypeIdentifier: type.identifier) { value, _ in
                continuation.resume(returning: value)
            }
        }
    }

    /// A string out of whatever the item provider handed over.
    private func string(from value: NSSecureCoding) -> String? {
        if let string = value as? String { return string }
        if let data = value as? Data { return String(data: data, encoding: .utf8) }
        if let url = value as? URL, url.isFileURL {
            return try? String(contentsOf: url, encoding: .utf8)
        }
        if let url = value as? URL { return url.absoluteString }
        return nil
    }

    // MARK: - Back to the app

    /// Opens `pippin://place?…` and closes the sheet.
    ///
    /// `NSExtensionContext.open` does not open the app from a share
    /// extension. It returns false here, in the simulator as well as on
    /// device, and its documentation has always said it is implemented for
    /// the Today extension point only. It is still called first so that the
    /// day Apple implements it this file needs no change, but its answer is
    /// logged rather than trusted.
    ///
    /// The responder chain is the actual mechanism; see
    /// `openThroughResponderChain`.
    ///
    /// Neither call reports whether Pippin came forward, so that is observed
    /// instead: launching another app makes the host resign active, and
    /// `NSExtensionHostWillResignActive` is the one signal in this process
    /// that means Pippin is on screen. Without it inside two and a half
    /// seconds the card says so rather than dismissing, because a share sheet
    /// that flickers shut having done nothing looks like a feature that was
    /// never built.
    private func hand(over place: SharedPlace) async {
        guard let url = PlaceLink.callbackURL(for: place) else {
            PippinLog.share.error("extension: could not build the callback URL")
            fail("That place could not be passed to \(AppName.display).")
            return
        }
        show(title: "Opening \(AppName.display)…", detail: place.displayName,
             busy: true)
        PippinLog.share.notice(
            "extension: opening \(url.absoluteString, privacy: .public)")

        extensionContext?.open(url) { reported in
            PippinLog.share.notice("extension: open reported \(reported, privacy: .public)")
        }
        let sent = openThroughResponderChain(url)
        PippinLog.share.notice(
            "extension: responder chain \(sent ? "sent it" : "found nobody", privacy: .public)")

        for _ in 0..<25 {
            if hostResigned {
                PippinLog.share.notice("extension: Pippin came forward; done")
                extensionContext?.completeRequest(returningItems: nil)
                return
            }
            try? await Task.sleep(for: .milliseconds(100))
        }

        PippinLog.share.error("extension: Pippin never came forward")
        fail("\(AppName.display) wouldn't open. Make sure \(AppName.display) "
             + "is installed from the current build — the place is not lost, "
             + "it is just not saved.")
    }

    /// Walks the responder chain to the application object and asks it to
    /// open the URL. This is the call that works.
    ///
    /// Both the cast and the selector are load-bearing. Sending the
    /// deprecated one-argument `openURL:` through `perform` reports success
    /// and does nothing, because something else in an extension's responder
    /// chain answers to that selector and swallows it. It must be the modern
    /// three-argument `open` on an object that really is the `UIApplication`.
    ///
    /// A share extension does have a `UIApplication` in its process; what it
    /// lacks is `UIApplication.shared`. This target is therefore not built
    /// `APPLICATION_EXTENSION_API_ONLY`, which would forbid naming the type
    /// that is the whole mechanism.
    @discardableResult
    private func openThroughResponderChain(_ url: URL) -> Bool {
        var responder: UIResponder? = self
        while let current = responder {
            if let application = current as? UIApplication {
                application.open(url, options: [:], completionHandler: nil)
                return true
            }
            responder = current.next
        }
        return false
    }

    // MARK: - The card

    /// One label, one spinner, one button. A share extension is passed
    /// through, so the success path shows a sentence and leaves; only a
    /// failure needs a button.
    private func buildCard() {
        view.backgroundColor = UIColor.black.withAlphaComponent(0.25)
        // An extension gets the size it asks for. Without this the host may
        // hand it a sheet too short to show the card, turning an error
        // message into another invisible failure.
        preferredContentSize = CGSize(width: 375, height: 260)

        card.backgroundColor = .secondarySystemBackground
        card.layer.cornerRadius = 18
        card.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(card)

        titleLabel.font = .preferredFont(forTextStyle: .headline)
        titleLabel.textAlignment = .center
        titleLabel.numberOfLines = 0
        titleLabel.text = "Reading the share…"

        detailLabel.font = .preferredFont(forTextStyle: .footnote)
        detailLabel.textColor = .secondaryLabel
        detailLabel.textAlignment = .center
        detailLabel.numberOfLines = 0

        closeButton.setTitle("Close", for: .normal)
        closeButton.addTarget(self, action: #selector(close), for: .touchUpInside)
        closeButton.isHidden = true

        spinner.startAnimating()

        let stack = UIStackView(arrangedSubviews: [spinner, titleLabel, detailLabel, closeButton])
        stack.axis = .vertical
        stack.spacing = 10
        stack.alignment = .fill
        stack.translatesAutoresizingMaskIntoConstraints = false
        card.addSubview(stack)

        // The card is given a width, not just a maximum and a margin: with
        // only those, autolayout satisfies both by shrinking the card to the
        // longest word, and the message becomes a column of fragments.
        let width = card.widthAnchor.constraint(equalToConstant: 340)
        width.priority = .defaultHigh
        NSLayoutConstraint.activate([
            card.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            card.centerYAnchor.constraint(equalTo: view.centerYAnchor),
            card.leadingAnchor.constraint(greaterThanOrEqualTo: view.leadingAnchor, constant: 20),
            width,
            card.widthAnchor.constraint(lessThanOrEqualToConstant: 340),
            stack.topAnchor.constraint(equalTo: card.topAnchor, constant: 22),
            stack.bottomAnchor.constraint(equalTo: card.bottomAnchor, constant: -18),
            stack.leadingAnchor.constraint(equalTo: card.leadingAnchor, constant: 22),
            stack.trailingAnchor.constraint(equalTo: card.trailingAnchor, constant: -22),
        ])
    }

    private func show(title: String, detail: String, busy: Bool) {
        titleLabel.text = title
        detailLabel.text = detail
        detailLabel.isHidden = detail.isEmpty
        closeButton.isHidden = busy
        spinner.isHidden = !busy
        if busy { spinner.startAnimating() } else { spinner.stopAnimating() }
    }

    /// Shows a refusal and leaves the sheet up until the user closes it. An
    /// extension that dismisses itself on failure gets reported as "I shared
    /// it and nothing happened".
    private func fail(_ message: String) {
        show(title: AppName.display, detail: message, busy: false)
    }

    @objc private func close() {
        extensionContext?.completeRequest(returningItems: nil)
    }
}
