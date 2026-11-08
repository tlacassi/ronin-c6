import AppKit
import Combine
import SwiftUI
import WebKit

@main
struct RoninControlApp: App {
    var body: some Scene {
        WindowGroup {
            MainView()
        }
        .windowResizability(.contentSize)
        .defaultSize(width: 1180, height: 800)
    }
}

private enum AppPage: String, CaseIterable, Identifiable {
    case setup = "Configurer l’ESP"
    case control = "Contrôler le Ronin"

    var id: String { rawValue }
}

struct MainView: View {
    @State private var page: AppPage = .setup

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 14) {
                Image(systemName: "camera.macro")
                    .font(.system(size: 25, weight: .semibold))
                    .foregroundStyle(.teal)
                VStack(alignment: .leading, spacing: 2) {
                    Text("ESP Ronin Control")
                        .font(.title2.weight(.semibold))
                    Text("Configuration USB et pilotage local des DJI RS 3 et RS 4 Pro")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
            .padding(.horizontal, 24)
            .padding(.top, 20)
            .padding(.bottom, 16)

            Picker("Espace", selection: $page) {
                ForEach(AppPage.allCases) { item in
                    Text(item.rawValue).tag(item)
                }
            }
            .pickerStyle(.segmented)
            .padding(.horizontal, 24)
            .padding(.bottom, 16)

            Group {
                switch page {
                case .setup:
                    SetupView()
                case .control:
                    ControlView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(minWidth: 980, minHeight: 680)
        .background(Color(nsColor: .windowBackgroundColor))
    }
}

private struct SetupView: View {
    @AppStorage("projectRoot") private var projectRoot = ProjectLocator.defaultRoot
    @AppStorage("idfRoot") private var idfRoot = IDFLocator.defaultRoot
    @State private var serialPorts = SerialPortLocator.ports()
    @State private var selectedPort = ""
    @State private var wifiSSID = ""
    @State private var wifiPassword = ""
    @State private var keepMonitorOpen = true
    @State private var errorMessage: String?
    @StateObject private var flasher = FirmwareFlasher()

    private let refreshPorts = Timer.publish(every: 2, on: .main, in: .common).autoconnect()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                Text("Préparer un ESP32-C6")
                    .font(.largeTitle.weight(.semibold))
                Text("Choisis le projet ESP-IDF, le port USB et le réseau Wi‑Fi. L’app construit le firmware, le flashe et peut ensuite afficher les logs série.")
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)

                HStack(alignment: .top, spacing: 18) {
                    VStack(alignment: .leading, spacing: 16) {
                        GroupBox("Projet et outil ESP-IDF") {
                            VStack(alignment: .leading, spacing: 12) {
                                PathRow(title: "Projet Ronin C6", path: $projectRoot, action: chooseProject)
                                Divider()
                                PathRow(title: "Dossier ESP-IDF", path: $idfRoot, action: chooseIDF)
                                Text("ESP-IDF 5.2 ou compatible doit être installé sur ce Mac. L’app utilise idf.py depuis son dossier d’installation.")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                            .padding(.top, 6)
                        }

                        GroupBox("Connexion USB et Wi‑Fi") {
                            VStack(alignment: .leading, spacing: 12) {
                                HStack {
                                    Text("Port USB")
                                    Spacer()
                                    Picker("Port USB", selection: $selectedPort) {
                                        Text("Choisir un port…").tag("")
                                        ForEach(serialPorts, id: \.self) { port in
                                            Text(port).tag(port)
                                        }
                                    }
                                    .frame(width: 300)
                                    Button {
                                        refreshSerialPorts()
                                    } label: {
                                        Image(systemName: "arrow.clockwise")
                                    }
                                    .help("Rechercher les ports USB")
                                }
                                TextField("Nom du réseau Wi‑Fi (SSID)", text: $wifiSSID)
                                    .textFieldStyle(.roundedBorder)
                                SecureField("Mot de passe Wi‑Fi (laisser vide pour un réseau ouvert)", text: $wifiPassword)
                                    .textFieldStyle(.roundedBorder)
                                Toggle("Garder le moniteur série ouvert après le flash", isOn: $keepMonitorOpen)
                                Text("Le firmware actuel reçoit le Wi‑Fi à la compilation. Chaque changement de réseau reconstruit puis reflashe l’ESP par USB. Le mot de passe reste dans le fichier sdkconfig local ignoré par Git et dans le firmware flashé.")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                    .fixedSize(horizontal: false, vertical: true)
                            }
                            .padding(.top, 6)
                        }

                        HStack {
                            Button {
                                flashFirmware()
                            } label: {
                                Label("Construire et flasher", systemImage: "bolt.fill")
                                    .frame(minWidth: 190)
                            }
                            .buttonStyle(.borderedProminent)
                            .tint(.teal)
                            .disabled(flasher.isRunning)

                            if flasher.isRunning {
                                Button("Arrêter", role: .destructive) {
                                    flasher.stop()
                                }
                            }
                            Spacer()
                        }

                        if let errorMessage {
                            Label(errorMessage, systemImage: "exclamationmark.triangle.fill")
                                .foregroundStyle(.red)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                    }
                    .frame(maxWidth: 620)

                    VStack(alignment: .leading, spacing: 12) {
                        GroupBox("État") {
                            VStack(alignment: .leading, spacing: 9) {
                                Label(flasher.isRunning ? flasher.status : "Prêt", systemImage: flasher.isRunning ? "arrow.triangle.2.circlepath" : "checkmark.circle")
                                    .foregroundStyle(flasher.isRunning ? .orange : .secondary)
                                if !flasher.isRunning, let exitCode = flasher.lastExitCode {
                                    Label(exitCode == 0 ? "Opération terminée" : "idf.py s’est arrêté avec le code \(exitCode)", systemImage: exitCode == 0 ? "checkmark.circle.fill" : "xmark.octagon.fill")
                                        .foregroundStyle(exitCode == 0 ? .green : .red)
                                }
                                if flasher.isRunning {
                                    ProgressView()
                                        .controlSize(.small)
                                }
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(.top, 5)
                        }

                        GroupBox("Sortie de compilation et logs USB") {
                            TextEditor(text: $flasher.output)
                                .font(.system(.caption, design: .monospaced))
                                .scrollContentBackground(.hidden)
                                .frame(minHeight: 390)
                                .padding(.top, 5)
                        }
                    }
                    .frame(minWidth: 340, maxWidth: .infinity)
                }
            }
            .padding(24)
        }
        .onAppear {
            refreshSerialPorts()
        }
        .onReceive(refreshPorts) { _ in
            refreshSerialPorts()
        }
    }

    private func refreshSerialPorts() {
        serialPorts = SerialPortLocator.ports()
        if !serialPorts.contains(selectedPort) {
            selectedPort = serialPorts.count == 1 ? serialPorts[0] : ""
        }
    }

    private func chooseProject() {
        guard let path = chooseDirectory(prompt: "Choisir le dossier racine du dépôt ronin-c6") else { return }
        projectRoot = path
    }

    private func chooseIDF() {
        guard let path = chooseDirectory(prompt: "Choisir le dossier d’installation ESP-IDF (il contient export.sh)") else { return }
        idfRoot = path
    }

    private func chooseDirectory(prompt: String) -> String? {
        let panel = NSOpenPanel()
        panel.title = prompt
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        return panel.runModal() == .OK ? panel.url?.path : nil
    }

    private func flashFirmware() {
        errorMessage = nil
        do {
            try flasher.buildAndFlash(
                projectPath: projectRoot,
                idfPath: idfRoot,
                port: selectedPort,
                ssid: wifiSSID,
                password: wifiPassword,
                keepMonitorOpen: keepMonitorOpen
            )
        } catch {
            errorMessage = error.localizedDescription
        }
    }
}

private struct PathRow: View {
    let title: String
    @Binding var path: String
    let action: () -> Void

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 3) {
                Text(title).font(.subheadline.weight(.medium))
                Text(path.isEmpty ? "Non sélectionné" : path)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(2)
                    .textSelection(.enabled)
            }
            Spacer(minLength: 8)
            Button("Choisir…", action: action)
        }
    }
}

private struct ControlView: View {
    @StateObject private var discovery = RoninDiscovery()
    @StateObject private var gamepadRelay = MacGamepadRelay()
    @State private var address = "http://ronin-c6.local"
    @State private var loadedURL = URL(string: "http://ronin-c6.local")!
    @State private var reloadToken = UUID()

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                VStack(alignment: .leading, spacing: 3) {
                    Text(discovery.message)
                        .font(.headline)
                        .foregroundStyle(discovery.isOnline ? .green : .secondary)
                    Text("Recherche automatique de ronin-c6.local sur le réseau local. Tu peux aussi saisir l’adresse IP affichée dans les logs ou le routeur.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Spacer(minLength: 12)
                Label(
                    gamepadRelay.controllerName,
                    systemImage: gamepadRelay.isConnected ? "gamecontroller.fill" : "gamecontroller"
                )
                .font(.caption.weight(.medium))
                .foregroundStyle(gamepadRelay.isConnected ? .green : .secondary)
                .help(gamepadRelay.linkMessage)
            }
            Text(gamepadRelay.linkMessage)
                .font(.caption)
                .foregroundStyle(.secondary)
            HStack(spacing: 8) {
                TextField("http://ronin-c6.local ou adresse IP", text: $address)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit(loadAddress)
                Button("Ouvrir") {
                    loadAddress()
                }
                .buttonStyle(.borderedProminent)
                Button {
                    reloadToken = UUID()
                    discovery.checkNow()
                } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .help("Recharger la page et rechercher l’ESP")
            }

            LocalWebView(url: loadedURL, reloadToken: reloadToken, relay: gamepadRelay)
                .background(.white)
                .clipShape(RoundedRectangle(cornerRadius: 10))
                .overlay {
                    RoundedRectangle(cornerRadius: 10)
                        .stroke(.quaternary, lineWidth: 1)
                }
        }
        .padding(18)
        .task {
            await discovery.run()
        }
        .onAppear {
            gamepadRelay.setBaseURL(loadedURL)
            gamepadRelay.start()
        }
        .onDisappear {
            gamepadRelay.stop()
        }
        .onChange(of: loadedURL) { newURL in
            gamepadRelay.setBaseURL(newURL)
        }
    }

    private func loadAddress() {
        let trimmed = address.trimmingCharacters(in: .whitespacesAndNewlines)
        let candidate = trimmed.contains("://") ? trimmed : "http://\(trimmed)"
        guard let url = URL(string: candidate), ["http", "https"].contains(url.scheme?.lowercased() ?? ""), url.host != nil else {
            return
        }
        loadedURL = url
        address = url.absoluteString
        reloadToken = UUID()
    }
}

private struct LocalWebView: NSViewRepresentable {
    let url: URL
    let reloadToken: UUID
    let relay: MacGamepadRelay

    func makeCoordinator() -> Coordinator { Coordinator(relay: relay) }

    func makeNSView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = .nonPersistent()
        let userContentController = WKUserContentController()
        userContentController.addUserScript(WKUserScript(
            source: "window.__RONIN_NATIVE_APP__ = true;",
            injectionTime: .atDocumentStart,
            forMainFrameOnly: true
        ))
        userContentController.add(context.coordinator, name: "roninNative")
        configuration.userContentController = userContentController
        let webView = WKWebView(frame: .zero, configuration: configuration)
        webView.allowsBackForwardNavigationGestures = true
        webView.navigationDelegate = context.coordinator
        context.coordinator.load(url, token: reloadToken, in: webView)
        return webView
    }

    func updateNSView(_ webView: WKWebView, context: Context) {
        context.coordinator.relay = relay
        context.coordinator.load(url, token: reloadToken, in: webView)
    }

    @MainActor
    final class Coordinator: NSObject, WKScriptMessageHandler, WKNavigationDelegate {
        weak var relay: MacGamepadRelay?
        private var lastURL: URL?
        private var lastToken: UUID?

        init(relay: MacGamepadRelay) {
            self.relay = relay
        }

        func load(_ url: URL, token: UUID, in webView: WKWebView) {
            guard lastURL != url || lastToken != token else { return }
            lastURL = url
            lastToken = token
            webView.load(URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 10))
        }

        func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
            relay?.receiveWebMessage(message.body)
        }

        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            relay?.attach(webView)
        }
    }
}

@MainActor
private final class RoninDiscovery: ObservableObject {
    @Published var isOnline = false
    @Published var message = "Recherche de l’ESP…"
    private var checkTask: Task<Void, Never>?

    func run() async {
        while !Task.isCancelled {
            await check()
            try? await Task.sleep(nanoseconds: 4_000_000_000)
        }
    }

    func checkNow() {
        checkTask?.cancel()
        checkTask = Task { await check() }
    }

    private func check() async {
        guard let url = URL(string: "http://ronin-c6.local/status") else { return }
        var request = URLRequest(url: url, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 2)
        request.httpMethod = "GET"
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard let response = response as? HTTPURLResponse, response.statusCode == 200,
                  let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  json["ble"] is NSNumber, json["session"] is NSNumber else {
                throw URLError(.cannotParseResponse)
            }
            isOnline = true
            message = "ESP Ronin détecté à ronin-c6.local"
        } catch {
            isOnline = false
            message = "ESP non détecté — nouvelle recherche automatique"
        }
    }
}

@MainActor
private final class FirmwareFlasher: ObservableObject {
    @Published var output = "Les logs de compilation et les messages du moniteur USB apparaîtront ici.\n"
    @Published var status = "Prêt"
    @Published var isRunning = false
    @Published var lastExitCode: Int32?

    private var process: Process?
    private var outputPipe: Pipe?
    private var stopRequested = false

    func buildAndFlash(projectPath: String, idfPath: String, port: String, ssid: String, password: String, keepMonitorOpen: Bool) throws {
        let projectURL = URL(fileURLWithPath: projectPath, isDirectory: true)
        let idfURL = URL(fileURLWithPath: idfPath, isDirectory: true)
        guard FileManager.default.fileExists(atPath: projectURL.appendingPathComponent("main/Kconfig.projbuild").path),
              FileManager.default.fileExists(atPath: projectURL.appendingPathComponent("CMakeLists.txt").path) else {
            throw FlashError.invalidProject
        }
        guard FileManager.default.fileExists(atPath: idfURL.appendingPathComponent("export.sh").path) else {
            throw FlashError.invalidIDF
        }
        guard port.hasPrefix("/dev/cu."), FileManager.default.fileExists(atPath: port) else {
            throw FlashError.invalidPort
        }
        guard !ssid.isEmpty, ssid.utf8.count <= 32, !ssid.contains("\n"), !ssid.contains("\r") else {
            throw FlashError.invalidSSID
        }
        guard password.utf8.count <= 63, !password.contains("\n"), !password.contains("\r") else {
            throw FlashError.invalidPassword
        }
        guard !isRunning else { throw FlashError.alreadyRunning }

        try updateLocalWiFiConfig(projectURL: projectURL, ssid: ssid, password: password)
        launch(idfURL: idfURL, projectURL: projectURL, port: port, keepMonitorOpen: keepMonitorOpen)
    }

    func stop() {
        guard let process, process.isRunning else { return }
        stopRequested = true
        process.terminate()
    }

    private func updateLocalWiFiConfig(projectURL: URL, ssid: String, password: String) throws {
        let configURL = projectURL.appendingPathComponent("sdkconfig")
        let current = (try? String(contentsOf: configURL, encoding: .utf8)) ?? ""
        let keys = ["CONFIG_RONIN_WIFI_SSID", "CONFIG_RONIN_WIFI_PASSWORD"]
        var lines = current.components(separatedBy: .newlines).filter { line in
            !keys.contains(where: { key in
                line.hasPrefix("\(key)=") || line == "# \(key) is not set"
            })
        }
        lines.append("CONFIG_RONIN_WIFI_SSID=\"\(escapeKconfig(ssid))\"")
        lines.append("CONFIG_RONIN_WIFI_PASSWORD=\"\(escapeKconfig(password))\"")
        let contents = lines.joined(separator: "\n") + "\n"
        try contents.write(to: configURL, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: configURL.path)
    }

    private func escapeKconfig(_ value: String) -> String {
        value.replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
    }

    private func launch(idfURL: URL, projectURL: URL, port: String, keepMonitorOpen: Bool) {
        output = ""
        lastExitCode = nil
        stopRequested = false
        status = keepMonitorOpen ? "Compilation, flash, puis logs série…" : "Compilation et flash en cours…"

        let runner = Process()
        runner.executableURL = URL(fileURLWithPath: "/bin/zsh")
        let monitor = keepMonitorOpen ? " monitor" : ""
        let command = "set -e; source \"$RONIN_IDF_ROOT/export.sh\" >/dev/null; cd \"$RONIN_PROJECT_ROOT\"; exec idf.py -p \"$RONIN_SERIAL_PORT\" build flash" + monitor
        runner.arguments = ["-c", command]
        var environment = ProcessInfo.processInfo.environment
        environment["RONIN_IDF_ROOT"] = idfURL.path
        environment["RONIN_PROJECT_ROOT"] = projectURL.path
        environment["RONIN_SERIAL_PORT"] = port
        environment["TERM"] = "dumb"
        runner.environment = environment

        let pipe = Pipe()
        runner.standardOutput = pipe
        runner.standardError = pipe
        outputPipe = pipe
        pipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let data = handle.availableData
            guard !data.isEmpty else {
                handle.readabilityHandler = nil
                return
            }
            let chunk = String(decoding: data, as: UTF8.self)
            Task { @MainActor [weak self] in self?.appendOutput(chunk) }
        }
        runner.terminationHandler = { [weak self] process in
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.outputPipe?.fileHandleForReading.readabilityHandler = nil
                self.lastExitCode = self.stopRequested ? nil : process.terminationStatus
                self.isRunning = false
                if self.stopRequested {
                    self.status = "Opération arrêtée"
                } else {
                    self.status = process.terminationStatus == 0 ? "Terminé" : "Échec, voir les logs"
                }
                self.process = nil
                self.outputPipe = nil
            }
        }

        do {
            try runner.run()
            process = runner
            isRunning = true
        } catch {
            output += "\nImpossible de lancer idf.py : \(error.localizedDescription)\n"
            status = "Échec du lancement"
            lastExitCode = -1
        }
    }

    private func appendOutput(_ chunk: String) {
        output.append(chunk)
        if output.count > 30_000 {
            output = String(output.suffix(30_000))
        }
    }
}

private enum FlashError: LocalizedError {
    case invalidProject
    case invalidIDF
    case invalidPort
    case invalidSSID
    case invalidPassword
    case alreadyRunning

    var errorDescription: String? {
        switch self {
        case .invalidProject: return "Le dossier choisi ne contient pas le projet ronin-c6."
        case .invalidIDF: return "Le dossier ESP-IDF choisi doit contenir le script export.sh."
        case .invalidPort: return "Choisis un port USB /dev/cu.* connecté à l’ESP32-C6."
        case .invalidSSID: return "Le SSID doit contenir entre 1 et 32 octets et ne peut pas contenir de retour à la ligne."
        case .invalidPassword: return "Le mot de passe Wi‑Fi doit faire au maximum 63 octets et ne peut pas contenir de retour à la ligne."
        case .alreadyRunning: return "Une opération ESP-IDF est déjà en cours."
        }
    }
}

private enum SerialPortLocator {
    static func ports() -> [String] {
        guard let entries = try? FileManager.default.contentsOfDirectory(atPath: "/dev") else { return [] }
        return entries
            .filter { name in
                let lower = name.lowercased()
                return lower.hasPrefix("cu.usb") || lower.contains("usbserial") || lower.contains("wch") || lower.contains("slab")
            }
            .map { "/dev/\($0)" }
            .sorted()
    }
}

private enum ProjectLocator {
    static var defaultRoot: String {
        let appDirectory = Bundle.main.bundleURL.deletingLastPathComponent()
        let candidate = appDirectory.deletingLastPathComponent().deletingLastPathComponent()
        if FileManager.default.fileExists(atPath: candidate.appendingPathComponent("main/Kconfig.projbuild").path) {
            return candidate.path
        }
        return ""
    }
}

private enum IDFLocator {
    static var defaultRoot: String {
        if let environmentPath = ProcessInfo.processInfo.environment["IDF_PATH"],
           FileManager.default.fileExists(atPath: URL(fileURLWithPath: environmentPath).appendingPathComponent("export.sh").path) {
            return environmentPath
        }

        let home = FileManager.default.homeDirectoryForCurrentUser
        let candidates = [
            home.appendingPathComponent("esp/esp-idf"),
            home.appendingPathComponent("Espressif/frameworks/esp-idf"),
            home.appendingPathComponent(".espressif/frameworks/esp-idf")
        ]
        if let found = candidates.first(where: { FileManager.default.fileExists(atPath: $0.appendingPathComponent("export.sh").path) }) {
            return found.path
        }
        return ""
    }
}
