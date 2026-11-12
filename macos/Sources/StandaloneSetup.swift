import Combine
import CryptoKit
import Darwin
import Foundation

private enum SetupFailure: LocalizedError {
    case message(String)
    var errorDescription: String? {
        if case let .message(text) = self { return text }
        return nil
    }
}

struct USBWiFiStatus {
    let ssid: String
    let connected: Bool
    let ip: String
    let reason: Int
}

// Serial I/O runs on a worker; this owns the port for one bounded transaction.
enum USBProvisioning {
    static func exchange(port: String, operation: String, ssid: String? = nil, password: String? = nil) throws -> USBWiFiStatus {
        let descriptor = Darwin.open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard descriptor >= 0 else { throw SetupFailure.message("Port USB inaccessible : \(String(cString: strerror(errno)))") }
        defer { Darwin.close(descriptor) }
        guard ioctl(descriptor, TIOCEXCL) == 0 else { throw SetupFailure.message("Le port USB est déjà utilisé. Ferme le moniteur série.") }
        var settings = termios()
        guard tcgetattr(descriptor, &settings) == 0 else { throw SetupFailure.message("Configuration USB impossible.") }
        cfmakeraw(&settings)
        cfsetspeed(&settings, speed_t(B115200))
        settings.c_cflag |= tcflag_t(CLOCAL | CREAD)
        settings.c_cflag &= ~tcflag_t(HUPCL)
        guard tcsetattr(descriptor, TCSANOW, &settings) == 0 else { throw SetupFailure.message("Configuration USB impossible.") }
        tcflush(descriptor, TCIFLUSH)
        // Opening USB can coincide with ROM-to-app startup. Probe readiness on
        // the same descriptor before sending a non-idempotent configuration.
        if operation != "wifi.status" {
            _ = try transaction(descriptor: descriptor, operation: "wifi.status")
        }
        return try transaction(descriptor: descriptor, operation: operation, ssid: ssid, password: password)
    }

    private static func transaction(descriptor: Int32, operation: String, ssid: String? = nil, password: String? = nil) throws -> USBWiFiStatus {
        let id = UInt32.random(in: 1...UInt32(Int32.max))
        var message: [String: Any] = ["id": id, "op": operation]
        if let ssid, let password { message["ssid"] = ssid; message["password"] = password }
        let json = try JSONSerialization.data(withJSONObject: message, options: [.sortedKeys])
        var wire = Data("RONIN ".utf8)
        wire.append(json)
        wire.append(10)
        let deadline = ProcessInfo.processInfo.systemUptime + 6
        var sent = 0
        while sent < wire.count && ProcessInfo.processInfo.systemUptime < deadline {
            let count = wire.withUnsafeBytes { buffer in
                Darwin.write(descriptor, buffer.baseAddress!.advanced(by: sent), wire.count - sent)
            }
            if count > 0 { sent += count }
            else if errno != EAGAIN && errno != EINTR { throw SetupFailure.message("Écriture USB interrompue.") }
            else { var event = pollfd(fd: descriptor, events: Int16(POLLOUT), revents: 0); _ = poll(&event, 1, 100) }
        }
        guard sent == wire.count else { throw SetupFailure.message("Délai d’écriture USB dépassé.") }
        var pending = Data()
        var bytes = [UInt8](repeating: 0, count: 1024)
        var nextProbe = ProcessInfo.processInfo.systemUptime + 1
        var retries = 0
        while ProcessInfo.processInfo.systemUptime < deadline {
            // Status probes are idempotent and bounded; Wi-Fi writes are never
            // retried, so a lost acknowledgement cannot cause repeated writes.
            if operation == "wifi.status", retries < 3, ProcessInfo.processInfo.systemUptime >= nextProbe {
                let count = wire.withUnsafeBytes { Darwin.write(descriptor, $0.baseAddress!, wire.count) }
                if count != wire.count { throw SetupFailure.message("Écriture USB interrompue.") }
                retries += 1
                nextProbe += 1
            }
            var event = pollfd(fd: descriptor, events: Int16(POLLIN), revents: 0)
            let ready = poll(&event, 1, 100)
            if ready < 0 && errno != EINTR { throw SetupFailure.message("Lecture USB interrompue.") }
            if event.revents & Int16(POLLHUP | POLLERR | POLLNVAL) != 0 { throw SetupFailure.message("ESP débranché pendant la configuration.") }
            if ready <= 0 { continue }
            let count = Darwin.read(descriptor, &bytes, bytes.count)
            if count <= 0 { continue }
            pending.append(contentsOf: bytes.prefix(count))
            if pending.count > 8192 { pending.removeAll(); continue }
            while let newline = pending.firstIndex(of: 10) {
                let line = pending.prefix(upTo: newline)
                pending.removeSubrange(...newline)
                guard line.starts(with: Data("RONIN ".utf8)),
                      let reply = try? JSONSerialization.jsonObject(with: Data(line.dropFirst(6))) as? [String: Any],
                      (reply["id"] as? NSNumber)?.uint32Value == id else { continue }
                guard reply["ok"] as? Bool == true else {
                    throw SetupFailure.message("L’ESP a refusé la configuration : \(reply["error"] as? String ?? "erreur USB")")
                }
                return USBWiFiStatus(ssid: reply["ssid"] as? String ?? "", connected: reply["connected"] as? Bool == true,
                                     ip: reply["ip"] as? String ?? "", reason: (reply["reason"] as? NSNumber)?.intValue ?? 0)
            }
        }
        throw SetupFailure.message("L’ESP ne répond pas au protocole de configuration. Flashe le firmware intégré, puis réessaie.")
    }
}

@MainActor
final class StandaloneSetup: ObservableObject {
    @Published var isRunning = false
    @Published var output = "Firmware et outil de flash intégrés. Aucun SDK ni Python à installer.\n"
    @Published var status = "Prêt"
    @Published var deviceSSID = ""
    @Published var deviceIP = ""

    func flash(port: String) {
        guard !isRunning else { return }
        isRunning = true
        status = "Flash de l’ESP32-C6…"
        output = "Écriture du firmware intégré, sans effacer les presets ni le Wi‑Fi enregistré.\n"
        let resources = Bundle.main.resourceURL
        Task {
            do {
                let log = try await Task.detached { try Self.flashBundledFirmware(port: port, resources: resources) }.value
                output += log
                status = "Firmware installé. Tu peux maintenant régler le Wi‑Fi par USB."
            } catch { status = error.localizedDescription }
            isRunning = false
        }
    }

    func readStatus(port: String) {
        guard !isRunning else { return }
        isRunning = true
        Task {
            do {
                let value = try await Task.detached { try USBProvisioning.exchange(port: port, operation: "wifi.status") }.value
                show(value)
            } catch { status = error.localizedDescription }
            isRunning = false
        }
    }

    func configure(port: String, ssid: String, password: String) {
        guard !isRunning else { return }
        guard !ssid.isEmpty, ssid.utf8.count <= 32, !ssid.contains("\0"),
              password.isEmpty || (8...63).contains(password.utf8.count), !password.contains("\0") else {
            status = "SSID : 1 à 32 octets. Mot de passe : 8 à 63 octets, ou vide pour un réseau ouvert."
            return
        }
        isRunning = true
        status = "Enregistrement du réseau sur l’ESP…"
        Task {
            do {
                var value = try await Task.detached { try USBProvisioning.exchange(port: port, operation: "wifi.set", ssid: ssid, password: password) }.value
                for _ in 0..<20 {
                    if value.connected { break }
                    try await Task.sleep(nanoseconds: 1_000_000_000)
                    value = try await Task.detached { try USBProvisioning.exchange(port: port, operation: "wifi.status") }.value
                }
                show(value)
                if !value.connected { status = "Réseau enregistré, mais connexion non établie (raison Wi‑Fi \(value.reason)). Vérifie le réseau 2,4 GHz et le mot de passe." }
            } catch { status = error.localizedDescription }
            isRunning = false
        }
    }

    private func show(_ value: USBWiFiStatus) {
        deviceSSID = value.ssid
        deviceIP = value.ip
        status = value.connected ? "ESP connecté à \(value.ssid) — http://\(value.ip)/" : "ESP non connecté — réseau enregistré : \(value.ssid.isEmpty ? "aucun" : value.ssid)"
    }

    nonisolated private static func flashBundledFirmware(port: String, resources: URL?) throws -> String {
        guard port.hasPrefix("/dev/cu."), FileManager.default.fileExists(atPath: port), let resources else {
            throw SetupFailure.message("Choisis un ESP32-C6 branché en USB.")
        }
        #if arch(arm64)
        let architecture = "arm64"
        #else
        let architecture = "x86_64"
        #endif
        let tool = resources.appendingPathComponent("Tools/\(architecture)/esptool")
        let firmware = resources.appendingPathComponent("Firmware")
        let manifestData = try Data(contentsOf: firmware.appendingPathComponent("manifest.json"))
        guard let hashes = try JSONSerialization.jsonObject(with: manifestData) as? [String: String] else {
            throw SetupFailure.message("Manifeste du firmware invalide.")
        }
        let files = ["bootloader.bin", "partition-table.bin", "ronin_c6.bin"]
        for name in files {
            let data = try Data(contentsOf: firmware.appendingPathComponent(name))
            let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
            guard hashes[name] == digest else { throw SetupFailure.message("Le firmware intégré est corrompu : \(name).") }
        }
        guard FileManager.default.isExecutableFile(atPath: tool.path) else { throw SetupFailure.message("L’outil de flash intégré est manquant.") }
        let process = Process()
        process.executableURL = tool
        process.arguments = ["--chip", "esp32c6", "--port", port, "--baud", "460800", "write-flash",
                             "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "4MB",
                             "0x0", firmware.appendingPathComponent(files[0]).path,
                             "0x8000", firmware.appendingPathComponent(files[1]).path,
                             "0x10000", firmware.appendingPathComponent(files[2]).path]
        process.environment = ["PATH": "/usr/bin:/bin", "TMPDIR": NSTemporaryDirectory()]
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = pipe
        try process.run()
        let timeout = DispatchWorkItem { if process.isRunning { process.terminate() } }
        DispatchQueue.global().asyncAfter(deadline: .now() + 180, execute: timeout)
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        timeout.cancel()
        let log = String(decoding: data.suffix(30000), as: UTF8.self)
        guard process.terminationStatus == 0 else { throw SetupFailure.message("Flash interrompu ou refusé. \(log)") }
        return log
    }
}
