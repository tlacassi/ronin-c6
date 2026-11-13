import CryptoKit
import Foundation

enum FirmwareError: LocalizedError {
    case message(String)
    var errorDescription: String? {
        if case let .message(text) = self { return text }
        return nil
    }
}

/// A firmware directory that passed verification.
struct FirmwareImage: Equatable {
    enum Origin: String { case downloaded, cached, bundled }
    let directory: URL
    let version: String
    let origin: Origin

    var label: String {
        switch origin {
        case .downloaded: return "v\(version) (téléchargé)"
        case .cached: return "v\(version) (dernier téléchargement)"
        case .bundled: return "v\(version) (intégré à l’app)"
        }
    }
}

/// Where the latest release is published. Tests inject a local server.
struct FirmwareSource {
    var latestReleaseURL = URL(string: "https://api.github.com/repos/tlacassi/ronin-c6/releases/latest")!
    var requireHTTPS = true
    var isHostAllowed: (String) -> Bool = { host in
        host == "api.github.com" || host == "github.com" || host.hasSuffix(".githubusercontent.com")
    }
    var requestTimeout: TimeInterval = 8

    func accepts(_ url: URL) -> Bool {
        guard let host = url.host?.lowercased(), isHostAllowed(host) else { return false }
        return !requireHTTPS || url.scheme?.lowercased() == "https"
    }
}

enum FirmwareStore {
    static let files = ["bootloader.bin", "partition-table.bin", "ronin_c6.bin"]
    /// Flash layout: bootloader 0x0–0x8000, partition table 0x8000–0x9000, app partition 0x180000.
    static let sizeLimits = ["bootloader.bin": 0x8000, "partition-table.bin": 0x1000, "ronin_c6.bin": 0x18_0000]
    static let manifestAssetName = "firmware-manifest.json"
    static let esp32c6ChipID: UInt16 = 13

    static func assetName(for file: String) -> String { "firmware-\(file)" }

    static var cacheRoot: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ESP Ronin Control/Firmware", isDirectory: true)
    }

    // MARK: Versions

    static func parseVersion(_ text: String) -> [Int]? {
        var value = text.trimmingCharacters(in: .whitespaces)
        if value.hasPrefix("v") || value.hasPrefix("V") { value.removeFirst() }
        let parts = value.split(separator: ".", omittingEmptySubsequences: false)
        guard !parts.isEmpty, parts.count <= 4 else { return nil }
        var numbers: [Int] = []
        for part in parts {
            guard !part.isEmpty, part.count <= 6, part.allSatisfy(\.isASCII), part.allSatisfy(\.isNumber),
                  let number = Int(part) else { return nil }
            numbers.append(number)
        }
        return numbers
    }

    static func normalizedVersion(_ text: String) -> String? {
        parseVersion(text)?.map(String.init).joined(separator: ".")
    }

    /// True when `lhs` is strictly newer than `rhs`. Unparseable versions never win.
    static func isNewer(_ lhs: String, than rhs: String) -> Bool {
        guard let a = parseVersion(lhs) else { return false }
        guard let b = parseVersion(rhs) else { return true }
        for index in 0..<max(a.count, b.count) {
            let x = index < a.count ? a[index] : 0
            let y = index < b.count ? b[index] : 0
            if x != y { return x > y }
        }
        return false
    }

    // MARK: Verification

    static func sha256(_ data: Data) -> String {
        SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    }

    /// Checks sizes, SHA-256 digests from `manifest.json` and the image headers
    /// (ESP image magic and ESP32-C6 chip id, partition table magic).
    static func verify(directory: URL) throws {
        let manifestData = try Data(contentsOf: directory.appendingPathComponent("manifest.json"))
        guard manifestData.count <= 16_384,
              let hashes = try JSONSerialization.jsonObject(with: manifestData) as? [String: Any] else {
            throw FirmwareError.message("Manifeste du firmware invalide.")
        }
        for name in files {
            let data = try Data(contentsOf: directory.appendingPathComponent(name))
            guard let limit = sizeLimits[name], data.count > 0, data.count <= limit else {
                throw FirmwareError.message("Taille de \(name) invalide.")
            }
            guard let expected = hashes[name] as? String, expected == sha256(data) else {
                throw FirmwareError.message("Le firmware est corrompu : \(name).")
            }
            if name == "partition-table.bin" {
                guard data[0] == 0xAA, data[1] == 0x50 else { throw FirmwareError.message("Table des partitions invalide.") }
            } else {
                guard data.count >= 16, data[0] == 0xE9 else { throw FirmwareError.message("\(name) n’est pas une image ESP.") }
                let chip = UInt16(data[12]) | (UInt16(data[13]) << 8)
                guard chip == esp32c6ChipID else { throw FirmwareError.message("\(name) n’est pas destiné à un ESP32-C6.") }
            }
        }
    }

    // MARK: Local images

    static func bundled(resources: URL?, version: String) -> FirmwareImage? {
        guard let resources, let normalized = normalizedVersion(version) else { return nil }
        let directory = resources.appendingPathComponent("Firmware", isDirectory: true)
        guard (try? verify(directory: directory)) != nil else { return nil }
        return FirmwareImage(directory: directory, version: normalized, origin: .bundled)
    }

    static func cached(root: URL = cacheRoot) -> FirmwareImage? {
        let directory = root.appendingPathComponent("current", isDirectory: true)
        guard let data = try? Data(contentsOf: directory.appendingPathComponent("info.json")),
              let info = try? JSONSerialization.jsonObject(with: data) as? [String: String],
              let version = info["version"].flatMap(normalizedVersion),
              (try? verify(directory: directory)) != nil else { return nil }
        return FirmwareImage(directory: directory, version: version, origin: .cached)
    }

    /// The newest verified firmware available without network. Ties go to the bundled one.
    static func bestLocal(bundled: FirmwareImage?, cached: FirmwareImage?) -> FirmwareImage? {
        switch (bundled, cached) {
        case let (b?, c?): return isNewer(c.version, than: b.version) ? c : b
        case let (b?, nil): return b
        case let (nil, c?): return c
        default: return nil
        }
    }

    // MARK: Download

    enum PullResult: Equatable {
        case downloaded(FirmwareImage)
        case upToDate(String)
    }

    private struct ReleaseAsset { let url: URL; let size: Int; let digest: String? }

    private final class RedirectGuard: NSObject, URLSessionTaskDelegate {
        let source: FirmwareSource
        init(source: FirmwareSource) { self.source = source }
        func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                        newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
            if let url = request.url, source.accepts(url) { completionHandler(request) } else { completionHandler(nil) }
        }
    }

    /// Looks up the latest published release and, if it is newer than everything
    /// already available locally, downloads, verifies and caches its firmware.
    /// Nothing is cached unless every digest and header check passes.
    static func pull(source: FirmwareSource = FirmwareSource(), localBest: FirmwareImage?,
                     root: URL = cacheRoot) async throws -> PullResult {
        guard source.accepts(source.latestReleaseURL) else { throw FirmwareError.message("Source de firmware non autorisée.") }
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = source.requestTimeout
        configuration.timeoutIntervalForResource = 45
        configuration.waitsForConnectivity = false
        configuration.httpCookieStorage = nil
        let session = URLSession(configuration: configuration)
        defer { session.finishTasksAndInvalidate() }
        let guardian = RedirectGuard(source: source)

        func fetch(_ url: URL, limit: Int, json: Bool = false) async throws -> Data {
            guard source.accepts(url) else { throw FirmwareError.message("Adresse de téléchargement refusée.") }
            var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: source.requestTimeout)
            request.setValue("ESP-Ronin-Control", forHTTPHeaderField: "User-Agent")
            if json { request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept") }
            let (data, response) = try await session.data(for: request, delegate: guardian)
            guard let http = response as? HTTPURLResponse, http.statusCode == 200, let final = http.url, source.accepts(final) else {
                throw FirmwareError.message("Téléchargement refusé par le serveur.")
            }
            guard data.count <= limit else { throw FirmwareError.message("Fichier distant trop volumineux.") }
            return data
        }

        let releaseData = try await fetch(source.latestReleaseURL, limit: 512 * 1024, json: true)
        guard let release = try JSONSerialization.jsonObject(with: releaseData) as? [String: Any],
              let tag = release["tag_name"] as? String, let version = normalizedVersion(tag) else {
            throw FirmwareError.message("Réponse de la source de firmware invalide.")
        }
        if let best = localBest, !isNewer(version, than: best.version) { return .upToDate(best.version) }

        var assets: [String: ReleaseAsset] = [:]
        for entry in release["assets"] as? [[String: Any]] ?? [] {
            guard let name = entry["name"] as? String, let text = entry["browser_download_url"] as? String,
                  let url = URL(string: text), source.accepts(url) else { continue }
            let digest = (entry["digest"] as? String).flatMap { $0.hasPrefix("sha256:") ? String($0.dropFirst(7)).lowercased() : nil }
            assets[name] = ReleaseAsset(url: url, size: (entry["size"] as? Int) ?? 0, digest: digest)
        }
        let required = [manifestAssetName] + files.map(assetName(for:))
        guard required.allSatisfy({ assets[$0] != nil }) else {
            throw FirmwareError.message("La version v\(version) ne publie pas de firmware téléchargeable.")
        }

        let fileManager = FileManager.default
        try fileManager.createDirectory(at: root, withIntermediateDirectories: true)
        let incoming = root.appendingPathComponent("incoming-\(UUID().uuidString)", isDirectory: true)
        try fileManager.createDirectory(at: incoming, withIntermediateDirectories: true)
        defer { try? fileManager.removeItem(at: incoming) }

        func download(_ asset: String, as local: String, limit: Int) async throws {
            let info = assets[asset]!
            let data = try await fetch(info.url, limit: limit)
            if let digest = info.digest, digest != sha256(data) { throw FirmwareError.message("Somme de contrôle GitHub incorrecte : \(asset).") }
            try data.write(to: incoming.appendingPathComponent(local), options: .atomic)
        }
        try await download(manifestAssetName, as: "manifest.json", limit: 16_384)
        for name in files { try await download(assetName(for: name), as: name, limit: sizeLimits[name] ?? 0) }
        try verify(directory: incoming)
        try JSONSerialization.data(withJSONObject: ["version": version]).write(to: incoming.appendingPathComponent("info.json"))

        // Swap in with a backup so a failure never leaves the cache without a valid copy.
        let current = root.appendingPathComponent("current", isDirectory: true)
        let previous = root.appendingPathComponent("previous", isDirectory: true)
        try? fileManager.removeItem(at: previous)
        if fileManager.fileExists(atPath: current.path) { try fileManager.moveItem(at: current, to: previous) }
        do { try fileManager.moveItem(at: incoming, to: current) } catch {
            if fileManager.fileExists(atPath: previous.path) { try? fileManager.moveItem(at: previous, to: current) }
            throw error
        }
        try? fileManager.removeItem(at: previous)
        guard let image = cached(root: root) else { throw FirmwareError.message("Le firmware téléchargé n’a pas pu être enregistré.") }
        return .downloaded(FirmwareImage(directory: image.directory, version: image.version, origin: .downloaded))
    }
}
