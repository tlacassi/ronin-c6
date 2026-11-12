import Foundation
@main
struct ProvisioningCLI {
    static func main() {
        do {
            let configure = CommandLine.arguments.count > 2
            let value = try USBProvisioning.exchange(
                port: CommandLine.arguments[1], operation: configure ? "wifi.set" : "wifi.status",
                ssid: configure ? "Réseau \"test\"" : nil,
                password: configure ? "test-only-password" : nil
            )
            print("SSID=\(value.ssid);CONNECTED=\(value.connected);IP=\(value.ip)")
        } catch {
            FileHandle.standardError.write(Data(error.localizedDescription.utf8))
            exit(1)
        }
    }
}
