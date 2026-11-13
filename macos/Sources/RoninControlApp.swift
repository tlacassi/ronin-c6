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
    @StateObject private var setup = StandaloneSetup()
    @State private var ports = SerialPortLocator.ports()
    @State private var selectedPort = ""
    @State private var ssid = ""
    @State private var password = ""
    private let refresh = Timer.publish(every: 2, on: .main, in: .common).autoconnect()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                Text("Configurer un ESP32-C6").font(.largeTitle.weight(.semibold))
                Text("Branche l’ESP en USB. L’outil de flash et un firmware de secours sont intégrés à l’app.")
                Picker("ESP connecté", selection: $selectedPort) {
                    Text("Choisir un port…").tag("")
                    ForEach(ports, id: \.self) { Text($0).tag($0) }
                }
                .disabled(setup.isRunning)
                GroupBox("Firmware") {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Pour un nouvel ESP ou une mise à jour. Les presets et le réseau enregistré sont conservés.")
                        Toggle("Récupérer automatiquement le dernier firmware quand Internet est disponible", isOn: $setup.autoUpdate)
                        Text("Sans Internet, l’app utilise le dernier firmware téléchargé, ou celui intégré s’il est plus récent.")
                            .font(.caption).foregroundStyle(.secondary)
                        HStack {
                            Text(setup.firmwareInfo).textSelection(.enabled)
                            if setup.isCheckingFirmware { ProgressView().controlSize(.small) }
                            Spacer()
                            Button("Vérifier maintenant") { setup.refreshFirmware() }
                                .disabled(setup.isRunning || setup.isCheckingFirmware)
                        }
                        Button("Installer le firmware") { setup.flash(port: selectedPort) }
                            .disabled(setup.isRunning || setup.isCheckingFirmware || selectedPort.isEmpty)
                    }.padding(8)
                }
                GroupBox("Réseau Wi‑Fi 2,4 GHz") {
                    VStack(alignment: .leading, spacing: 12) {
                        Text("Le changement de réseau se fait par USB, sans recompiler ni reflasher. Le mot de passe reste sur l’ESP.")
                        TextField("Nom du réseau (SSID)", text: $ssid).textFieldStyle(.roundedBorder)
                        SecureField("Mot de passe (vide pour un réseau ouvert)", text: $password).textFieldStyle(.roundedBorder)
                        HStack {
                            Button("Lire la configuration") { setup.readStatus(port: selectedPort) }
                            Button("Enregistrer le Wi‑Fi sur l’ESP") {
                                setup.configure(port: selectedPort, ssid: ssid, password: password)
                                password = ""
                            }.buttonStyle(.borderedProminent)
                        }.disabled(setup.isRunning || selectedPort.isEmpty)
                        if !setup.deviceSSID.isEmpty { Text("Réseau enregistré : \(setup.deviceSSID)") }
                        if !setup.deviceIP.isEmpty { Text("Interface de contrôle : http://\(setup.deviceIP)/").textSelection(.enabled) }
                    }.padding(8)
                }
                if setup.isRunning { ProgressView().controlSize(.small) }
                Text(setup.status).textSelection(.enabled)
                TextEditor(text: $setup.output).font(.system(.caption, design: .monospaced)).frame(minHeight: 180)
            }.padding(24)
        }
        .onAppear {
            updatePorts()
            if setup.autoUpdate { setup.refreshFirmware() }
        }
        .onReceive(refresh) { _ in if !setup.isRunning { updatePorts() } }
    }
    private func updatePorts() {
        ports = SerialPortLocator.ports()
        if !ports.contains(selectedPort) { selectedPort = ports.count == 1 ? ports[0] : "" }
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
