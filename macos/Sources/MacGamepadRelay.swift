import Combine
import Foundation
import GameController
import WebKit

private enum RelayAction {
    case savePreset(Int)
    case goToPreset(Int)
}

private enum RelayCommand {
    case move(Int, Int, UInt32)
    case zoom(Int, Int, UInt32)
    case zoomKeepAlive(UInt32)
    case zoomStop(UInt32)
    case preset(RelayAction, Int, Int, UInt32)
}

@MainActor
final class MacGamepadRelay: ObservableObject {
    @Published private(set) var isConnected = false
    @Published private(set) var controllerName = "Manette non détectée"
    @Published private(set) var linkMessage = "Manette en attente"

    private var baseURL = URL(string: "http://ronin-c6.local")!
    private var controller: GCController?
    private var webView: WKWebView?
    private var connectObserver: NSObjectProtocol?
    private var disconnectObserver: NSObjectProtocol?
    private var inputTask: Task<Void, Never>?
    private var statusTask: Task<Void, Never>?
    private var commandTask: Task<Void, Never>?
    private var commands: [RelayCommand] = []
    private var started = false
    private var processActivity: NSObjectProtocol?
    private var linkReady = false
    private var zoomReady = false
    private var currentSession: UInt32?
    private var lastStatusAt = Date.distantPast
    private var lastPresetsAt = Date.distantPast
    private var savedPresets = Array(repeating: false, count: 5)
    private var zoomActive = false
    private var lastZoomKeepAlive = Date.distantPast
    private var lastMoveWasActive = false
    private var controllerIdentity: ObjectIdentifier?
    private var armed = false
    private var motionSuppressedUntilRelease = false
    private var previousFaces = Array(repeating: false, count: 4)
    private var pendingFace: Int?
    private var pendingFaceAt = Date.distantPast
    private var longPressSent = false
    private var zoomArmed = false
    private var zoomDirection = 0
    private var gamepadSpeed = 100
    private var presetDuration = 20
    private var presetZoomSpeed = 20
    private var zoomSpeed = 20
    private var zoomInverted = false

    func setBaseURL(_ url: URL) {
        guard var components = URLComponents(url: url, resolvingAgainstBaseURL: false) else { return }
        components.path = ""
        components.query = nil
        components.fragment = nil
        guard let normalized = components.url else { return }
        guard normalized != baseURL else { return }
        let oldBaseURL = baseURL
        let oldSession = currentSession
        let stopZoom = zoomActive
        commands.removeAll()
        if let oldSession {
            Task { [weak self] in
                await self?.sendSafetyStop(to: oldBaseURL, session: oldSession, stopZoom: stopZoom)
            }
        }
        baseURL = normalized
        linkReady = false
        zoomReady = false
        currentSession = nil
        lastMoveWasActive = false
        zoomActive = false
        updateProcessActivity(active: false)
        savedPresets = Array(repeating: false, count: 5)
        linkMessage = "Recherche de l’ESP à \(normalized.host ?? "l’adresse choisie")"
    }

    func start() {
        guard !started else { return }
        started = true
        GCController.shouldMonitorBackgroundEvents = true
        connectObserver = NotificationCenter.default.addObserver(
            forName: .GCControllerDidConnect,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            Task { @MainActor in self?.selectController() }
        }
        disconnectObserver = NotificationCenter.default.addObserver(
            forName: .GCControllerDidDisconnect,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            Task { @MainActor in self?.selectController() }
        }
        GCController.startWirelessControllerDiscovery(completionHandler: nil)
        selectController()
        inputTask = Task { [weak self] in
            while let self, !Task.isCancelled {
                self.sampleController()
                try? await Task.sleep(nanoseconds: 100_000_000)
            }
        }
        statusTask = Task { [weak self] in
            while let self, !Task.isCancelled {
                await self.refreshStatus()
                try? await Task.sleep(nanoseconds: 700_000_000)
            }
        }
    }

    func stop() {
        guard started else { return }
        started = false
        inputTask?.cancel()
        statusTask?.cancel()
        inputTask = nil
        statusTask = nil
        if let connectObserver { NotificationCenter.default.removeObserver(connectObserver) }
        if let disconnectObserver { NotificationCenter.default.removeObserver(disconnectObserver) }
        connectObserver = nil
        disconnectObserver = nil
        GCController.stopWirelessControllerDiscovery()
        GCController.shouldMonitorBackgroundEvents = false
        updateProcessActivity(active: false)
        commands.removeAll()
        if let session = currentSession {
            enqueue(.move(0, 0, session))
            if zoomActive { enqueue(.zoomStop(session)) }
        }
        setController(nil)
        linkReady = false
        zoomReady = false
        currentSession = nil
        linkMessage = "Manette en attente"
    }

    func receiveWebMessage(_ body: Any) {
        guard let message = body as? [String: Any], message["type"] as? String == "settings" else { return }
        if let value = (message["gamepadSpeed"] as? NSNumber)?.intValue {
            gamepadSpeed = min(200, max(10, value))
        }
        if let value = (message["presetDuration"] as? NSNumber)?.intValue {
            presetDuration = min(50, max(5, value))
        }
        if let value = (message["presetZoomSpeed"] as? NSNumber)?.intValue {
            presetZoomSpeed = min(100, max(1, value))
        }
        if let value = (message["zoomSpeed"] as? NSNumber)?.intValue {
            zoomSpeed = min(100, max(1, value))
        }
        if let value = message["zoomInverted"] as? Bool {
            zoomInverted = value
        }
    }

    func attach(_ webView: WKWebView) {
        self.webView = webView
        publishControllerState()
    }

    func publishControllerState() {
        let connected = isConnected ? "true" : "false"
        webView?.evaluateJavaScript(
            "window.dispatchEvent(new CustomEvent('ronin-native-gamepad', {detail:{connected:\(connected)}}));",
            completionHandler: nil
        )
    }

    private func selectController() {
        let controllers = GCController.controllers().filter { $0.extendedGamepad != nil }
        let candidate = GCController.current.flatMap { $0.extendedGamepad == nil ? nil : $0 }
            ?? controllers.first
        if candidate === controller { return }
        if lastMoveWasActive, let session = currentSession { enqueue(.move(0, 0, session)) }
        if zoomActive, let session = currentSession { enqueue(.zoomStop(session)) }
        setController(candidate)
    }

    private func setController(_ newController: GCController?) {
        controller = newController
        controllerIdentity = newController.map(ObjectIdentifier.init)
        isConnected = newController != nil
        controllerName = newController?.vendorName ?? (newController == nil ? "Manette non détectée" : "Manette détectée")
        armed = false
        motionSuppressedUntilRelease = false
        if let pad = newController?.extendedGamepad {
            previousFaces = faceButtons(pad)
            zoomArmed = !pad.dpad.up.isPressed && !pad.dpad.down.isPressed
        } else {
            previousFaces = Array(repeating: false, count: 4)
            zoomArmed = false
        }
        pendingFace = nil
        longPressSent = false
        zoomDirection = 0
        publishControllerState()
        if newController == nil {
            lastMoveWasActive = false
            zoomActive = false
            updateProcessActivity(active: false)
            linkMessage = "Manette déconnectée"
        } else {
            linkMessage = "Manette détectée — gâchette gauche + stick pour piloter"
        }
    }

    private func refreshStatus() async {
        do {
            let data = try await request(path: "status", method: "GET", body: nil, session: nil)
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let sessionNumber = json["session"] as? NSNumber,
                  sessionNumber.doubleValue.isFinite,
                  sessionNumber.doubleValue >= 0,
                  sessionNumber.doubleValue <= Double(UInt32.max) else {
                throw RelayError.invalidStatus
            }
            let newSession = sessionNumber.uint32Value
            let ready = (json["ble"] as? Bool == true) && (json["controlEnabled"] as? Bool == true)
            let zoom = json["zoom"] as? [String: Any]
            let zoomAvailable = (zoom?["available"] as? Bool == true) && (zoom?["present"] as? Bool == true)
                && (zoom?["calibrated"] as? Bool == true)
            if currentSession != newSession || !ready {
                if lastMoveWasActive, let oldSession = currentSession { enqueue(.move(0, 0, oldSession)) }
                if zoomActive, let oldSession = currentSession { enqueue(.zoomStop(oldSession)) }
                lastMoveWasActive = false
                zoomActive = false
                zoomDirection = 0
                updateProcessActivity(active: false)
            }
            currentSession = ready ? newSession : nil
            linkReady = ready
            zoomReady = ready && zoomAvailable
            lastStatusAt = Date()
            linkMessage = ready ? "ESP connecté — commandes manette disponibles" : "ESP détecté — Ronin déconnecté ou commandes indisponibles"
            if ready && Date().timeIntervalSince(lastPresetsAt) >= 2.5 {
                await refreshPresets()
            }
        } catch {
            if lastMoveWasActive, let oldSession = currentSession { enqueue(.move(0, 0, oldSession)) }
            if zoomActive, let oldSession = currentSession { enqueue(.zoomStop(oldSession)) }
            lastMoveWasActive = false
            zoomActive = false
            updateProcessActivity(active: false)
            linkReady = false
            zoomReady = false
            currentSession = nil
            savedPresets = Array(repeating: false, count: 5)
            linkMessage = "ESP inaccessible — nouvelle recherche automatique"
        }
    }

    private func refreshPresets() async {
        do {
            let data = try await request(path: "presets", method: "GET", body: nil, session: nil)
            guard let values = try JSONSerialization.jsonObject(with: data) as? [Any], values.count == 5 else {
                throw RelayError.invalidStatus
            }
            savedPresets = values.map { !($0 is NSNull) }
            lastPresetsAt = Date()
        } catch {
            savedPresets = Array(repeating: false, count: 5)
        }
    }

    private func sampleController() {
        guard let controller, let pad = controller.extendedGamepad else { return }
        let x = Double(pad.leftThumbstick.xAxis.value)
        let y = Double(pad.leftThumbstick.yAxis.value)
        let trigger = pad.leftTrigger.value > 0.5
        let neutralStick = abs(x) <= 0.12 && abs(y) <= 0.12
        if !trigger && neutralStick {
            armed = true
            motionSuppressedUntilRelease = false
        }
        if controllerIdentity != ObjectIdentifier(controller) {
            controllerIdentity = ObjectIdentifier(controller)
            previousFaces = faceButtons(pad)
            pendingFace = nil
            zoomArmed = !pad.dpad.up.isPressed && !pad.dpad.down.isPressed
            armed = false
            motionSuppressedUntilRelease = false
        }

        guard canControl else {
            previousFaces = faceButtons(pad)
            pendingFace = nil
            zoomDirection = 0
            if lastMoveWasActive, let session = currentSession { enqueue(.move(0, 0, session)) }
            if zoomActive, let session = currentSession { enqueue(.zoomStop(session)) }
            lastMoveWasActive = false
            zoomActive = false
            updateProcessActivity(active: false)
            return
        }

        if let action = processFaceButtons(faceButtons(pad)) {
            motionSuppressedUntilRelease = true
            if let session = currentSession {
                enqueue(.move(0, 0, session))
                if zoomActive { enqueue(.zoomStop(session)) }
                enqueue(.preset(action, presetDuration, presetZoomSpeed, session))
            }
            lastMoveWasActive = false
            zoomActive = false
            zoomDirection = 0
            zoomArmed = false
            updateProcessActivity(active: false)
            return
        }

        processDPadZoom(pad)

        let yaw: Int
        let pitch: Int
        if armed && trigger && !motionSuppressedUntilRelease {
            yaw = Int((deadZone(x) * 300.0 * Double(gamepadSpeed) / 100.0).rounded())
            pitch = Int((deadZone(-y) * 300.0 * Double(gamepadSpeed) / 100.0).rounded())
        } else {
            yaw = 0
            pitch = 0
        }
        if yaw != 0 || pitch != 0 {
            if let session = currentSession { enqueue(.move(yaw, pitch, session)) }
            lastMoveWasActive = true
        } else if lastMoveWasActive {
            if let session = currentSession { enqueue(.move(0, 0, session)) }
            lastMoveWasActive = false
        }
        updateProcessActivity(active: lastMoveWasActive || zoomActive)
    }

    private var canControl: Bool {
        linkReady && currentSession != nil && Date().timeIntervalSince(lastStatusAt) <= 1.8
    }

    private func deadZone(_ value: Double) -> Double {
        let bounded = min(1.0, max(-1.0, value))
        return abs(bounded) <= 0.12 ? 0 : (bounded.sign == .minus ? -1 : 1) * (abs(bounded) - 0.12) / 0.88
    }

    private func faceButtons(_ pad: GCExtendedGamepad) -> [Bool] {
        [pad.buttonA, pad.buttonB, pad.buttonX, pad.buttonY].map { $0.isPressed || $0.value > 0.5 }
    }

    private func processFaceButtons(_ faces: [Bool]) -> RelayAction? {
        let pressedCount = faces.filter { $0 }.count
        if pressedCount > 1 {
            pendingFace = nil
            longPressSent = false
            previousFaces = faces
            return nil
        }
        if let index = pendingFace {
            let elapsed = Date().timeIntervalSince(pendingFaceAt)
            if faces[index] {
                if !longPressSent && elapsed >= 1.0 {
                    longPressSent = true
                    return .savePreset(index)
                }
            } else {
                var action: RelayAction?
                if pressedCount == 0 && !longPressSent {
                    action = elapsed >= 1.0 ? .savePreset(index) : .goToPreset(index)
                }
                pendingFace = nil
                longPressSent = false
                previousFaces = faces
                return action
            }
        } else if pressedCount == 1, let index = faces.firstIndex(of: true), !previousFaces[index] {
            pendingFace = index
            pendingFaceAt = Date()
            longPressSent = false
        }
        previousFaces = faces
        return nil
    }

    private func processDPadZoom(_ pad: GCExtendedGamepad) {
        let up = pad.dpad.up.isPressed
        let down = pad.dpad.down.isPressed
        if !up && !down { zoomArmed = true }
        if up && down { zoomArmed = false }
        let direction = zoomArmed && up != down ? (up ? 1 : -1) : 0
        guard direction != zoomDirection else {
            if zoomActive && Date().timeIntervalSince(lastZoomKeepAlive) >= 0.5,
               let session = currentSession {
                enqueue(.zoomKeepAlive(session))
                lastZoomKeepAlive = Date()
            }
            return
        }
        if zoomActive, let session = currentSession { enqueue(.zoomStop(session)) }
        zoomDirection = direction
        zoomActive = false
        guard direction != 0, zoomReady, let session = currentSession else {
            updateProcessActivity(active: lastMoveWasActive)
            return
        }
        let position = zoomInverted ? (direction > 0 ? 0 : 4095) : (direction > 0 ? 4095 : 0)
        enqueue(.zoom(position, zoomSpeed, session))
        zoomActive = true
        lastZoomKeepAlive = Date()
        updateProcessActivity(active: true)
    }

    private func updateProcessActivity(active: Bool) {
        if active, processActivity == nil {
            processActivity = ProcessInfo.processInfo.beginActivity(
                options: [.userInitiated],
                reason: "Forwarding active Ronin game controller input"
            )
        } else if !active, let processActivity {
            ProcessInfo.processInfo.endActivity(processActivity)
            self.processActivity = nil
        }
    }

    private func enqueue(_ command: RelayCommand) {
        if case .move = command, let last = commands.last, case .move = last {
            commands[commands.count - 1] = command
        } else if case .zoomKeepAlive = command, let last = commands.last, case .zoomKeepAlive = last {
            commands[commands.count - 1] = command
        } else {
            commands.append(command)
        }
        guard commandTask == nil else { return }
        commandTask = Task { [weak self] in
            await self?.drainCommands()
        }
    }

    private func drainCommands() async {
        while !commands.isEmpty {
            let command = commands.removeFirst()
            do {
                try await send(command)
            } catch {
                linkMessage = "Commande manette refusée — vérifie la liaison Ronin"
            }
        }
        commandTask = nil
    }

    private func send(_ command: RelayCommand) async throws {
        switch command {
        case let .move(yaw, pitch, session):
            _ = try await request(path: "move", method: "POST", body: "\(yaw),\(pitch)", session: session)
        case let .zoom(position, speed, session):
            _ = try await request(path: "zoom", method: "POST", body: "\(position),\(speed)", session: session)
        case let .zoomKeepAlive(session):
            _ = try await request(path: "zoom/keepalive", method: "POST", body: "", session: session)
        case let .zoomStop(session):
            _ = try await request(path: "zoom/stop", method: "POST", body: "", session: session)
        case let .preset(action, duration, zoomSpeed, session):
            switch action {
            case let .savePreset(index):
                _ = try await request(path: "preset/save", method: "POST", body: "\(index)", session: session)
            case let .goToPreset(index):
                guard savedPresets.indices.contains(index), savedPresets[index] else {
                    linkMessage = "Preset \(index + 1) non enregistré"
                    return
                }
                let body = "\(index),\(String(format: "%02d", duration)),\(String(format: "%03d", zoomSpeed))"
                _ = try await request(path: "preset/goto", method: "POST", body: body, session: session)
            }
        }
    }

    private func sendSafetyStop(to oldBaseURL: URL, session: UInt32, stopZoom: Bool) async {
        _ = try? await request(path: "move", method: "POST", body: "0,0", session: session, using: oldBaseURL)
        if stopZoom {
            _ = try? await request(path: "zoom/stop", method: "POST", body: "", session: session, using: oldBaseURL)
        }
    }

    private func request(path: String, method: String, body: String?, session: UInt32?, using alternateBaseURL: URL? = nil) async throws -> Data {
        let requestBaseURL = alternateBaseURL ?? baseURL
        let url = path.split(separator: "/").reduce(requestBaseURL) { partial, component in
            partial.appendingPathComponent(String(component))
        }
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 1.2)
        request.httpMethod = method
        if let body {
            request.httpBody = Data(body.utf8)
            request.setValue("text/plain", forHTTPHeaderField: "Content-Type")
        }
        if let session { request.setValue(String(session), forHTTPHeaderField: "X-Ronin-Session") }
        let (data, response) = try await URLSession.shared.data(for: request)
        guard let response = response as? HTTPURLResponse, (200..<300).contains(response.statusCode) else {
            throw RelayError.httpFailure
        }
        return data
    }

    private enum RelayError: Error {
        case invalidStatus
        case httpFailure
    }
}
