/*
 nightcoop mac launcher
 a tiny window to pick host or join, which writes session.txt for the plugin and starts the game
 with launch_red4ext.sh. while the game runs it shows session-status.txt (the hosting code lands there)
*/
import AppKit
import SwiftUI

enum SessionRole: String, CaseIterable, Identifiable {
    case host = "Host"
    case join = "Join"
    var id: String { rawValue }
}

final class LauncherModel: ObservableObject {
    @AppStorage("gameFolder") var gameFolder = NSHomeDirectory() + "/Library/Application Support/Steam/steamapps/common/Cyberpunk 2077"
    @AppStorage("role") var roleText = SessionRole.host.rawValue
    @AppStorage("joinCode") var joinCode = ""
    @AppStorage("hub") var hub = ""
    @AppStorage("nickname") var nickname = "guest"

    @Published var statusLine = "not running"
    @Published var launchOutput = ""
    @Published var gameRunning = false

    private var gameProcess: Process?
    private var statusTimer: Timer?

    var role: SessionRole {
        get { SessionRole(rawValue: roleText) ?? .host }
        set { roleText = newValue.rawValue }
    }

    var pluginFolder: URL {
        URL(fileURLWithPath: gameFolder).appendingPathComponent("red4ext/plugins/NightCoopMac")
    }

    var pluginInstalled: Bool {
        FileManager.default.fileExists(atPath: pluginFolder.appendingPathComponent("NightCoopMac.dylib").path)
    }

    var hostingCode: String? {
        guard let range = statusLine.range(of: "code ") else { return nil }
        let rest = statusLine[range.upperBound...]
        let code = rest.prefix { $0.isLetter || $0.isNumber }
        return code.isEmpty ? nil : String(code)
    }

    var canStart: Bool {
        guard pluginInstalled, !gameRunning, !hub.trimmingCharacters(in: .whitespaces).isEmpty else { return false }
        return role == .host || !joinCode.trimmingCharacters(in: .whitespaces).isEmpty
    }

    func startWithCoop() {
        var lines: [String] = []
        let trimmedHub = hub.trimmingCharacters(in: .whitespaces)
        switch role {
        case .host:
            lines.append("host \(trimmedHub)")
        case .join:
            lines.append("join \(joinCode.trimmingCharacters(in: .whitespaces).uppercased()) \(trimmedHub)")
        }
        let trimmedName = nickname.trimmingCharacters(in: .whitespaces)
        if !trimmedName.isEmpty {
            lines.append("name \(trimmedName)")
        }
        do {
            try (lines.joined(separator: "\n") + "\n").write(to: pluginFolder.appendingPathComponent("session.txt"), atomically: true, encoding: .utf8)
        } catch {
            launchOutput = "could not write session.txt: \(error.localizedDescription)"
            return
        }
        launchGame()
    }

    func startWithoutCoop() {
        try? FileManager.default.removeItem(at: pluginFolder.appendingPathComponent("session.txt"))
        launchGame()
    }

    private func launchGame() {
        try? FileManager.default.removeItem(at: pluginFolder.appendingPathComponent("session-status.txt"))
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/bin/bash")
        process.arguments = [URL(fileURLWithPath: gameFolder).appendingPathComponent("launch_red4ext.sh").path]
        process.currentDirectoryURL = URL(fileURLWithPath: gameFolder)
        let outputPipe = Pipe()
        process.standardOutput = outputPipe
        process.standardError = outputPipe
        outputPipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
            let text = String(decoding: handle.availableData, as: UTF8.self)
            guard !text.isEmpty else { return }
            DispatchQueue.main.async { self?.launchOutput = (self?.launchOutput ?? "") + text }
        }
        process.terminationHandler = { [weak self] _ in
            DispatchQueue.main.async {
                self?.gameRunning = false
                self?.statusTimer?.invalidate()
                self?.readStatus()
            }
        }
        do {
            launchOutput = ""
            try process.run()
        } catch {
            launchOutput = "could not start the game: \(error.localizedDescription)"
            return
        }
        gameProcess = process
        gameRunning = true
        statusLine = "starting the game"
        statusTimer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.readStatus() }
    }

    private func readStatus() {
        let statusFile = pluginFolder.appendingPathComponent("session-status.txt")
        if let text = try? String(contentsOf: statusFile, encoding: .utf8) {
            statusLine = text.trimmingCharacters(in: .whitespacesAndNewlines)
        } else if !gameRunning {
            statusLine = "not running"
        }
    }

    func copyCode() {
        guard let code = hostingCode else { return }
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(code, forType: .string)
    }
}

struct LauncherView: View {
    @StateObject private var model = LauncherModel()

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("NightCoop Mac").font(.title2).bold()
            Text("unofficial, early. theres no co-op in the world yet, only the session and positions")
                .font(.caption).foregroundStyle(.secondary)

            Form {
                TextField("Game folder", text: $model.gameFolder)
                if !model.pluginInstalled {
                    Text("NightCoopMac.dylib is not in this game folder yet").foregroundStyle(.red).font(.caption)
                }
                Picker("Mode", selection: Binding(get: { model.role }, set: { model.role = $0 })) {
                    ForEach(SessionRole.allCases) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented)
                if model.role == .join {
                    TextField("Session code", text: $model.joinCode)
                }
                TextField("Hub (host:port#FINGERPRINT)", text: $model.hub)
                TextField("Your name", text: $model.nickname)
            }

            HStack {
                Button(model.role == .host ? "Host and start game" : "Join and start game") { model.startWithCoop() }
                    .keyboardShortcut(.defaultAction)
                    .disabled(!model.canStart)
                Button("Start without co-op") { model.startWithoutCoop() }
                    .disabled(!model.pluginInstalled || model.gameRunning)
            }

            GroupBox("Status") {
                HStack {
                    Text(model.statusLine).textSelection(.enabled)
                    Spacer()
                    if model.hostingCode != nil {
                        Button("Copy code") { model.copyCode() }
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }

            if !model.launchOutput.isEmpty {
                ScrollView {
                    Text(model.launchOutput).font(.system(.caption, design: .monospaced)).textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(height: 110)
            }
        }
        .padding(20)
        .frame(width: 520)
    }
}

@main
struct NightCoopLauncherApp: App {
    var body: some Scene {
        WindowGroup("NightCoop Mac") {
            LauncherView()
        }
        .windowResizability(.contentSize)
    }
}
