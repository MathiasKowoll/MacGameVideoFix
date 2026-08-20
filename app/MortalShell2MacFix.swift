// MortalShell2MacFix — restores VP9 cutscenes in UE5 titles running under
// CrossOver on Apple Silicon.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import AppKit
import UniformTypeIdentifiers

// MARK: - Running the bundled scripts

enum Step: String {
    case transcode = "Transcoding cutscenes to H.264"
    case hidePak   = "Removing video entries from the pak index"
    case showPak   = "Restoring the pak index"
    case restore   = "Restoring the original cutscenes"
}

struct GameFolder {
    let content: URL          // .../<Game>/Content
    var movies: URL  { content.appendingPathComponent("Movies") }
    var paks:   URL  { content.appendingPathComponent("Paks") }

    /// Accepts either the Content folder itself or any parent that contains one.
    static func locate(from url: URL) -> GameFolder? {
        let fm = FileManager.default
        var candidates = [url, url.appendingPathComponent("Content")]
        // .../<Game>/<Game>/Content is the usual Unreal layout
        if let subs = try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil) {
            candidates += subs.map { $0.appendingPathComponent("Content") }
        }
        for c in candidates {
            var isDir: ObjCBool = false
            let movies = c.appendingPathComponent("Movies")
            let paks = c.appendingPathComponent("Paks")
            if fm.fileExists(atPath: movies.path, isDirectory: &isDir), isDir.boolValue,
               fm.fileExists(atPath: paks.path, isDirectory: &isDir), isDir.boolValue {
                return GameFolder(content: c)
            }
        }
        return nil
    }

    var mainPak: URL? {
        let fm = FileManager.default
        guard let files = try? fm.contentsOfDirectory(at: paks, includingPropertiesForKeys: nil)
        else { return nil }
        // The chunk holding Content/Movies is normally pakchunk0.
        return files.filter { $0.pathExtension == "pak" }
                    .sorted { $0.lastPathComponent < $1.lastPathComponent }
                    .first
    }
}

@MainActor
final class Runner: ObservableObject {
    @Published var log: [String] = []
    @Published var busy = false
    @Published var game: GameFolder?
    @Published var status = "Choose your game folder to begin."

    private var resources: URL {
        Bundle.main.resourceURL ?? URL(fileURLWithPath: ".")
    }

    func note(_ line: String) { log.append(line) }

    func select(_ url: URL) {
        guard let g = GameFolder.locate(from: url) else {
            status = "That folder has no Content/Movies and Content/Paks inside."
            game = nil
            return
        }
        game = g
        log.removeAll()
        note("Game content: \(g.content.path)")
        inspect(g)
    }

    /// Reports what is actually on disk, so the user knows the starting point.
    func inspect(_ g: GameFolder) {
        let fm = FileManager.default
        let movies = (try? fm.subpathsOfDirectory(atPath: g.movies.path)) ?? []
        let count = movies.filter { $0.hasSuffix(".mp4") }.count
        note("Found \(count) .mp4 files under Movies/")

        guard let pak = g.mainPak else {
            status = "No .pak found in Content/Paks."
            return
        }
        note("Main pak: \(pak.lastPathComponent)")

        let patched = fm.fileExists(atPath:
            pak.deletingLastPathComponent()
               .appendingPathComponent(".\(pak.lastPathComponent).hidden-videos.json").path)
        let backedUp = fm.fileExists(atPath: g.content.appendingPathComponent("Movies_VP9_backup").path)

        if patched && backedUp {
            status = "Fix is applied. Cutscenes should play."
        } else if patched || backedUp {
            status = "Partially applied — run Apply Fix to finish, or Revert to undo."
        } else {
            status = "Not patched yet."
        }
    }

    private func run(_ step: Step, _ launchPath: String, _ args: [String]) async -> Bool {
        note("")
        note("▸ \(step.rawValue)")

        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: launchPath)
        proc.arguments = args
        // Homebrew lives outside the default PATH a GUI app inherits.
        var env = ProcessInfo.processInfo.environment
        env["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + (env["PATH"] ?? "/usr/bin:/bin")
        proc.environment = env

        let pipe = Pipe()
        proc.standardOutput = pipe
        proc.standardError = pipe

        do { try proc.run() } catch {
            note("  could not start: \(error.localizedDescription)")
            return false
        }

        let handle = pipe.fileHandleForReading
        var buffer = Data()
        while true {
            let chunk = handle.availableData
            if chunk.isEmpty { break }
            buffer.append(chunk)
            while let nl = buffer.firstIndex(of: 0x0A) {
                let line = String(data: buffer[..<nl], encoding: .utf8) ?? ""
                buffer.removeSubrange(...nl)
                if !line.isEmpty { note("  " + line) }
            }
        }
        proc.waitUntilExit()

        if proc.terminationStatus != 0 {
            note("  failed (exit \(proc.terminationStatus))")
            return false
        }
        return true
    }

    func apply() {
        guard let g = game, let pak = g.mainPak else { return }
        busy = true
        Task {
            defer { busy = false }
            status = "Working…"

            guard which("ffmpeg") != nil else {
                note("")
                note("ffmpeg is required and was not found.")
                note("Install it with:  brew install ffmpeg")
                status = "ffmpeg missing."
                return
            }

            let transcode = resources.appendingPathComponent("transcode-movies.sh").path
            let hide = resources.appendingPathComponent("pak-hide-videos.py").path

            guard await run(.transcode, "/bin/bash", [transcode, g.content.path]) else {
                status = "Transcoding failed — nothing was changed in the pak."
                return
            }
            guard await run(.hidePak, "/usr/bin/python3", [hide, pak.path, "--apply"]) else {
                status = "Pak patching failed. Your transcodes are in place but unused."
                return
            }

            note("")
            note("Done. Launch the game — the cutscenes should play.")
            note("Note: Steam's \"verify integrity of game files\" undoes this.")
            status = "Fix applied."
            inspect(g)
        }
    }

    func revert() {
        guard let g = game, let pak = g.mainPak else { return }
        busy = true
        Task {
            defer { busy = false }
            status = "Reverting…"

            let transcode = resources.appendingPathComponent("transcode-movies.sh").path
            let hide = resources.appendingPathComponent("pak-hide-videos.py").path

            _ = await run(.showPak, "/usr/bin/python3", [hide, pak.path, "--restore"])
            _ = await run(.restore, "/bin/bash", [transcode, g.content.path, "--restore"])

            note("")
            note("Reverted. The game is back to its original files.")
            status = "Reverted."
            inspect(g)
        }
    }

    private func which(_ tool: String) -> String? {
        for dir in ["/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"] {
            let p = "\(dir)/\(tool)"
            if FileManager.default.isExecutableFile(atPath: p) { return p }
        }
        return nil
    }
}

// MARK: - Interface

struct ContentView: View {
    @StateObject private var runner = Runner()
    @State private var dropping = false

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            header
            dropZone
            if runner.game != nil { actions }
            logView
        }
        .padding(22)
        .frame(minWidth: 640, minHeight: 560)
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("MortalShell2MacFix")
                .font(.system(size: 22, weight: .semibold))
            Text("Restores VP9 cutscenes in UE5 games running under CrossOver.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
    }

    private var dropZone: some View {
        RoundedRectangle(cornerRadius: 10)
            .strokeBorder(style: StrokeStyle(lineWidth: 1.5, dash: [6, 4]))
            .foregroundStyle(dropping ? Color.accentColor : Color.secondary.opacity(0.5))
            .background(
                RoundedRectangle(cornerRadius: 10)
                    .fill(dropping ? Color.accentColor.opacity(0.08) : Color.clear)
            )
            .frame(height: 92)
            .overlay(
                VStack(spacing: 6) {
                    Text(runner.game.map { $0.content.path } ?? "Drop your game folder here")
                        .font(.system(size: 13, design: runner.game == nil ? .default : .monospaced))
                        .lineLimit(2)
                        .truncationMode(.head)
                        .multilineTextAlignment(.center)
                    Button("Choose…") { chooseFolder() }
                        .buttonStyle(.link)
                }
                .padding(.horizontal, 16)
            )
            .onDrop(of: [.fileURL], isTargeted: $dropping) { providers in
                guard let p = providers.first else { return false }
                _ = p.loadObject(ofClass: URL.self) { url, _ in
                    guard let url else { return }
                    Task { @MainActor in runner.select(url) }
                }
                return true
            }
    }

    private var actions: some View {
        HStack(spacing: 12) {
            Button("Apply Fix") { runner.apply() }
                .keyboardShortcut(.defaultAction)
                .disabled(runner.busy)
            Button("Revert") { runner.revert() }
                .disabled(runner.busy)
            if runner.busy { ProgressView().controlSize(.small) }
            Spacer()
            Text(runner.status)
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }

    private var logView: some View {
        ScrollViewReader { proxy in
            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(Array(runner.log.enumerated()), id: \.offset) { i, line in
                        Text(line)
                            .font(.system(size: 11, design: .monospaced))
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .id(i)
                    }
                }
                .padding(10)
            }
            .background(Color(nsColor: .textBackgroundColor))
            .clipShape(RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8)
                .strokeBorder(Color.secondary.opacity(0.25)))
            .onChange(of: runner.log.count) { _, n in
                withAnimation { proxy.scrollTo(n - 1, anchor: .bottom) }
            }
        }
    }

    private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.message = "Select the game folder (the one containing Content)."
        panel.prompt = "Select"
        if panel.runModal() == .OK, let url = panel.url {
            runner.select(url)
        }
    }
}

@main
struct MortalShell2MacFixApp: App {
    var body: some Scene {
        Window("MortalShell2MacFix", id: "main") {
            ContentView()
        }
        .windowResizability(.contentMinSize)
    }
}
