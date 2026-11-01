// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

import CryptoKit
import Foundation
import PippinKit
import Security
import os

/// This install's Open Waters identity: an Ed25519 key made on first use and
/// the personal token minted with it (`POST /v1/keys`), both kept in the
/// Keychain on this device only. Nothing for the user to enter or copy.
///
/// A token the server issued without checking the signature is used but
/// minted again once per launch, as the Open Waters Signal K plugin does.
actor OpenWatersIdentity {
    static let shared = OpenWatersIdentity()

    /// `<App>/<version> (<build>; github.com/ChrisABailey/Peregrine)`, sent on
    /// every Open Waters request.
    nonisolated static let userAgent: String = {
        let info = Bundle.main.infoDictionary ?? [:]
        let version = info["CFBundleShortVersionString"] as? String ?? "0"
        let build = info["CFBundleVersion"] as? String ?? "0"
        let name = AppName.display.replacingOccurrences(of: " ", with: "")
        return "\(name)/\(version) (\(build); github.com/ChrisABailey/Peregrine)"
    }()

    private static let log = Logger(subsystem: "org.peregrine.Pippin", category: "ais")
    private static let service = "org.peregrine.openwaters"
    private static let keyAccount = "ed25519"
    private static let tokenAccount = "token"
    /// The keys endpoint takes 10 requests a minute per address.
    private static let mintRetry: TimeInterval = 300

    /// What is kept under `tokenAccount`: the mint response and the key it
    /// was minted for.
    private struct Kept: Codable {
        let pubkey: String
        let response: Data
    }

    private var key: Curve25519.Signing.PrivateKey?
    private var kept: PPOpenWatersToken?
    private var loaded = false
    private var mintedThisLaunch = false
    private var nextMint = Date.distantPast
    private var minting: Task<PPOpenWatersToken?, Never>?

    /// The token to stream with, minting one when none is kept or the kept
    /// one is unsigned. nil when there is none and minting fails; the caller
    /// then streams anonymously.
    func credential() async -> PPOpenWatersToken? {
        load()
        if let kept, kept.isSigned || mintedThisLaunch { return kept }
        // Callers arriving during a mint share it.
        if let minting { return await minting.value }
        guard Date() >= nextMint, let key = signingKey() else { return kept }
        let task = Task { await self.mintAndKeep(with: key) }
        minting = task
        let token = await task.value
        minting = nil
        return token
    }

    private func mintAndKeep(with key: Curve25519.Signing.PrivateKey) async -> PPOpenWatersToken? {
        do {
            let (token, body) = try await Self.mint(with: key)
            mintedThisLaunch = true
            kept = token
            let record = Kept(pubkey: Self.base64url(key.publicKey.rawRepresentation), response: body)
            if let data = try? JSONEncoder().encode(record) {
                Self.store(data, account: Self.tokenAccount)
            }
            Self.log.notice("Open Waters token minted, signed=\(token.isSigned)")
        } catch {
            nextMint = Date().addingTimeInterval(Self.mintRetry)
            Self.log.notice("Open Waters mint failed: \(error.localizedDescription, privacy: .public)")
        }
        return kept
    }

    /// Drops `token` after the server refused it; the next `credential()`
    /// mints a new one with the same key.
    func forget(_ token: String) {
        guard kept?.token == token else { return }
        kept = nil
        mintedThisLaunch = false
        Self.delete(account: Self.tokenAccount)
    }

    // MARK: - Minting

    private enum MintError: LocalizedError {
        case http(Int, String)
        var errorDescription: String? {
            switch self {
            case let .http(status, why): return "HTTP \(status) \(why)"
            }
        }
    }

    private static func mint(
        with key: Curve25519.Signing.PrivateKey
    ) async throws -> (PPOpenWatersToken, Data) {
        let pubkey = base64url(key.publicKey.rawRepresentation)
        let ts = Int64(Date().timeIntervalSince1970)
        let signature = try key.signature(for: openWatersMintMessage(pubkey: pubkey, timestamp: ts))
        var request = URLRequest(url: openWatersKeysURL(), timeoutInterval: 15)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue(userAgent, forHTTPHeaderField: "User-Agent")
        request.httpBody = openWatersMintBody(
            pubkey: pubkey, timestamp: ts, signature: base64url(signature))
        let (body, response) = try await URLSession.shared.data(for: request)
        let status = (response as? HTTPURLResponse)?.statusCode ?? 0
        var why: NSString?
        guard status == 200, let token = PPOpenWatersToken(response: body, error: &why) else {
            let text = why as String? ?? String(decoding: body.prefix(200), as: UTF8.self)
            throw MintError.http(status, text.trimmingCharacters(in: .whitespacesAndNewlines))
        }
        return (token, body)
    }

    static func base64url(_ data: Data) -> String {
        data.base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "=", with: "")
    }

    // MARK: - Keychain

    /// Reads the key and the token once per launch. A token minted for a
    /// different key is ignored.
    private func load() {
        guard !loaded else { return }
        loaded = true
        if let raw = Self.read(account: Self.keyAccount) {
            key = try? Curve25519.Signing.PrivateKey(rawRepresentation: raw)
        }
        guard let key, let data = Self.read(account: Self.tokenAccount),
              let record = try? JSONDecoder().decode(Kept.self, from: data),
              record.pubkey == Self.base64url(key.publicKey.rawRepresentation) else { return }
        kept = PPOpenWatersToken(response: record.response, error: nil)
    }

    /// The install's key, made and kept on first use. nil when the Keychain
    /// will not take it, so a key that cannot be kept never mints a token.
    private func signingKey() -> Curve25519.Signing.PrivateKey? {
        if let key { return key }
        let made = Curve25519.Signing.PrivateKey()
        guard Self.store(made.rawRepresentation, account: Self.keyAccount) else { return nil }
        Self.delete(account: Self.tokenAccount)
        key = made
        return made
    }

    private static func query(_ account: String) -> [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: service,
         kSecAttrAccount as String: account]
    }

    private static func read(account: String) -> Data? {
        var q = query(account)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess else { return nil }
        return out as? Data
    }

    @discardableResult
    private static func store(_ data: Data, account: String) -> Bool {
        delete(account: account)
        var q = query(account)
        q[kSecValueData as String] = data
        q[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        let status = SecItemAdd(q as CFDictionary, nil)
        if status != errSecSuccess { log.error("Keychain add \(account, privacy: .public): \(status)") }
        return status == errSecSuccess
    }

    private static func delete(account: String) {
        SecItemDelete(query(account) as CFDictionary)
    }
}
