import Foundation
import CryptoKit

@main
struct FirmwareStoreTests {
    static func check(_ condition: @autoclosure () -> Bool, _ message: String) {
        if !condition() { FileHandle.standardError.write(Data("FAIL: \(message)\n".utf8)); exit(1) }
    }
    static func source(_ port: String, _ variant: String) -> FirmwareSource {
        var s = FirmwareSource()
        s.latestReleaseURL = URL(string: "http://127.0.0.1:\(port)/\(variant)/release.json")!
        s.requireHTTPS = false
        s.isHostAllowed = { $0 == "127.0.0.1" }
        s.requestTimeout = 3
        return s
    }
    static func pullFails(_ port: String, _ variant: String, _ root: URL, local: FirmwareImage?) async -> Bool {
        do { _ = try await FirmwareStore.pull(source: source(port, variant), localBest: local, root: root); return false } catch { return true }
    }

    static func main() async {
        // Versions
        check(FirmwareStore.isNewer("2.0.2", than: "2.0.1"), "2.0.2 > 2.0.1")
        check(FirmwareStore.isNewer("v2.1", than: "2.0.9"), "v2.1 > 2.0.9")
        check(!FirmwareStore.isNewer("2.0.1", than: "2.0.1.0"), "equal versions are not newer")
        check(!FirmwareStore.isNewer("abc", than: "1.0"), "garbage never wins")
        check(FirmwareStore.normalizedVersion("v2.0.1") == "2.0.1", "tag normalisation")
        check(FirmwareStore.parseVersion("1.2.3.4.5") == nil && FirmwareStore.parseVersion("1..2") == nil, "bad versions rejected")

        let port = CommandLine.arguments[1]
        let temp = FileManager.default.temporaryDirectory.appendingPathComponent("fwtest-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: temp) }
        let root = temp.appendingPathComponent("cache")
        check(FirmwareStore.cached(root: root) == nil, "empty cache")

        // Offline: nothing listens on this port.
        var dead = FirmwareSource(); dead.latestReleaseURL = URL(string: "http://127.0.0.1:1/x")!
        dead.requireHTTPS = false; dead.isHostAllowed = { $0 == "127.0.0.1" }; dead.requestTimeout = 2
        do { _ = try await FirmwareStore.pull(source: dead, localBest: nil, root: root); check(false, "offline must throw") }
        catch { check(error is URLError, "offline error is a URLError") }
        check(FirmwareStore.cached(root: root) == nil, "failed pull leaves cache empty")

        // Refused sources
        var plain = source(port, "good"); plain.requireHTTPS = true
        let failed = await pullFails(port, "good", root, local: nil)
        check(!failed, "good release downloads")
        let first = FirmwareStore.cached(root: root)
        check(first?.version == "9.9.9" && first?.origin == .cached, "cached after download")
        do { _ = try await FirmwareStore.pull(source: plain, localBest: nil, root: root); check(false, "http must be refused") } catch {}
        var foreign = source(port, "good"); foreign.isHostAllowed = { $0 == "example.org" }
        do { _ = try await FirmwareStore.pull(source: foreign, localBest: nil, root: root); check(false, "foreign host must be refused") } catch {}

        // Already up to date: no download.
        let again = try? await FirmwareStore.pull(source: source(port, "good"), localBest: first, root: root)
        check(again == .upToDate("9.9.9"), "second pull is up to date")

        // Older release than what we have.
        let older = try? await FirmwareStore.pull(source: source(port, "old"), localBest: first, root: root)
        check(older == .upToDate("9.9.9"), "older release ignored")

        // Bad releases never replace a valid cache.
        let before = FirmwareStore.cached(root: root)
        let oldLocal = FirmwareImage(directory: temp, version: "1.0.0", origin: .bundled)
        for variant in ["tampered", "manifest", "wrongchip", "missing"] {
            let rejected = await pullFails(port, variant, root, local: oldLocal)
            check(rejected, "\(variant) release must be rejected")
        }
        check(FirmwareStore.cached(root: root) == before, "cache untouched after rejected releases")
        check(!(try! FileManager.default.contentsOfDirectory(atPath: root.path)).contains { $0.hasPrefix("incoming") }, "no leftovers")

        // Local verification catches tampering after the fact.
        let dir = root.appendingPathComponent("current")
        let app = dir.appendingPathComponent("ronin_c6.bin")
        var data = try! Data(contentsOf: app); data[100] ^= 0xFF; try! data.write(to: app)
        check(FirmwareStore.cached(root: root) == nil, "tampered cache is ignored")

        // Fallback choice
        let bundled = FirmwareImage(directory: temp, version: "2.0.1", origin: .bundled)
        let newer = FirmwareImage(directory: temp, version: "2.0.2", origin: .cached)
        let stale = FirmwareImage(directory: temp, version: "2.0.0", origin: .cached)
        check(FirmwareStore.bestLocal(bundled: bundled, cached: newer) == newer, "newer cache wins")
        check(FirmwareStore.bestLocal(bundled: bundled, cached: stale) == bundled, "older cache loses")
        check(FirmwareStore.bestLocal(bundled: bundled, cached: nil) == bundled, "bundled alone")
        check(FirmwareStore.bestLocal(bundled: nil, cached: newer) == newer, "cache alone")
        check(FirmwareStore.bestLocal(bundled: nil, cached: nil) == nil, "nothing")
        print("Firmware store tests passed")
    }
}
