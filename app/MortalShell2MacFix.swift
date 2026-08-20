// MortalShell2MacFix — restores VP9 cutscenes in UE5 titles running under
// CrossOver on Apple Silicon.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import AppKit
import UniformTypeIdentifiers

// MARK: - Subprocess plumbing

/// Accumulates bytes from a pipe and hands back whole lines.
/// `readabilityHandler` fires on a private serial queue, so a lock is enough.
private final class LineBuffer: @unchecked Sendable {
    private var data = Data()
    private let lock = NSLock()

    func take(_ chunk: Data) -> [String] {
        lock.lock(); defer { lock.unlock() }
        data.append(chunk)
        var lines: [String] = []
        while let nl = data.firstIndex(of: 0x0A) {
            if let s = String(data: data[..<nl], encoding: .utf8) { lines.append(s) }
            data.removeSubrange(...nl)
        }
        return lines
    }

    func flush() -> String? {
        lock.lock(); defer { lock.unlock() }
        guard !data.isEmpty, let s = String(data: data, encoding: .utf8) else { return nil }
        data.removeAll()
        return s
    }
}

/// Runs a command and delivers each output line as it arrives, without ever
/// blocking the caller's actor. Returns the exit status, or -1 if it could not
/// be started at all.
private func runStreaming(_ executable: String,
                          _ arguments: [String],
                          onLine: @escaping @Sendable (String) -> Void) async -> Int32 {
    await withCheckedContinuation { (cont: CheckedContinuation<Int32, Never>) in
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: executable)
        proc.arguments = arguments

        // A GUI app does not inherit the shell's PATH, so Homebrew is invisible
        // unless we put it back.
        var env = ProcessInfo.processInfo.environment
        env["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + (env["PATH"] ?? "/usr/bin:/bin")
        proc.environment = env

        let pipe = Pipe()
        proc.standardOutput = pipe
        proc.standardError = pipe

        let buffer = LineBuffer()
        pipe.fileHandleForReading.readabilityHandler = { handle in
            let chunk = handle.availableData
            guard !chunk.isEmpty else { return }
            for line in buffer.take(chunk) { onLine(line) }
        }

        proc.terminationHandler = { finished in
            pipe.fileHandleForReading.readabilityHandler = nil
            // Anything left without a trailing newline.
            if let tail = buffer.flush(), !tail.isEmpty { onLine(tail) }
            cont.resume(returning: finished.terminationStatus)
        }

        do {
            try proc.run()
        } catch {
            pipe.fileHandleForReading.readabilityHandler = nil
            proc.terminationHandler = nil
            onLine("could not start \(executable): \(error.localizedDescription)")
            cont.resume(returning: -1)
        }
    }
}

// MARK: - Model

enum Phase {
    case idle, transcoding, patchingPak, restoringPak, restoringMovies

    var label: String {
        switch self {
        case .idle:             return ""
        case .transcoding:      return "Transcoding cutscenes to H.264"
        case .patchingPak:      return "Removing video entries from the pak index"
        case .restoringPak:     return "Restoring the pak index"
        case .restoringMovies:  return "Restoring the original cutscenes"
        }
    }

    /// Where this phase sits in the overall run, so the bar advances smoothly
    /// instead of resetting between steps. Transcoding is the long pole.
    var span: ClosedRange<Double> {
        switch self {
        case .idle:            return 0...0
        case .transcoding:     return 0...0.92
        case .patchingPak:     return 0.92...1
        case .restoringPak:    return 0...0.15
        case .restoringMovies: return 0.15...1
        }
    }
}

struct GameFolder {
    let content: URL          // .../<Game>/Content
    var movies: URL  { content.appendingPathComponent("Movies") }
    var paks:   URL  { content.appendingPathComponent("Paks") }

    /// Accepts the Content folder itself, or any parent that contains one.
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

/// What we found on disk. Drives which actions are offered, so the same fix
/// cannot be applied twice (which would transcode already-transcoded files and
/// leave the backup holding H.264 instead of the originals).
enum FixState {
    case unknown, notApplied, partial, applied

    var canApply: Bool { self == .notApplied || self == .partial }
    var canRevert: Bool { self == .applied || self == .partial }
}

@MainActor
final class Runner: ObservableObject {
    @Published var state: FixState = .unknown
    @Published var log: [String] = []
    @Published var busy = false
    @Published var game: GameFolder?
    @Published var status = "Choose your game folder to begin."

    @Published var progress: Double = 0
    @Published var indeterminate = true
    @Published var phaseLabel = ""
    @Published var detail = ""          // e.g. "34 of 61 · Movie_Tut_Parry.mp4"

    private var resources: URL { Bundle.main.resourceURL ?? URL(fileURLWithPath: ".") }

    /// Parses the "[12/61] ok some/file.mp4" lines the scripts emit.
    /// Hand-rolled rather than a regex so the parse is obvious and cheap --
    /// this runs for every output line.
    private static func parseStep(_ line: String) -> (done: Int, total: Int, name: String)? {
        guard line.hasPrefix("["), let close = line.firstIndex(of: "]") else { return nil }
        let inside = line[line.index(after: line.startIndex)..<close]
        let parts = inside.split(separator: "/")
        guard parts.count == 2,
              let done = Int(parts[0]), let total = Int(parts[1]), total > 0
        else { return nil }

        // Everything after "] " is "<verb> <path>"; the path is what we show.
        let rest = line[line.index(after: close)...].trimmingCharacters(in: .whitespaces)
        let path = rest.split(separator: " ", maxSplits: 1).count > 1
            ? String(rest.split(separator: " ", maxSplits: 1)[1])
            : rest
        return (done, total, (path as NSString).lastPathComponent)
    }

    func note(_ line: String) {
        log.append(line)
        if log.count > 4000 { log.removeFirst(log.count - 4000) }   // keep memory bounded
    }

    func select(_ url: URL) {
        guard let g = GameFolder.locate(from: url) else {
            status = "That folder has no Content/Movies and Content/Paks inside."
            game = nil
            return
        }
        game = g
        state = .unknown
        log.removeAll()
        resetProgress()
        note("Game content: \(g.content.path)")
        inspect(g)
    }

    func inspect(_ g: GameFolder) {
        let fm = FileManager.default
        let movies = (try? fm.subpathsOfDirectory(atPath: g.movies.path)) ?? []
        note("Found \(movies.filter { $0.hasSuffix(".mp4") }.count) .mp4 files under Movies/")

        guard let pak = g.mainPak else {
            status = "No .pak found in Content/Paks."
            return
        }
        note("Main pak: \(pak.lastPathComponent)")

        let marker = pak.deletingLastPathComponent()
            .appendingPathComponent(".\(pak.lastPathComponent).hidden-videos.json")
        let patched = fm.fileExists(atPath: marker.path)
        let backedUp = fm.fileExists(atPath: g.content.appendingPathComponent("Movies_VP9_backup").path)

        if patched && backedUp {
            state = .applied
            status = "Fix is applied. Cutscenes should play."
        } else if patched || backedUp {
            state = .partial
            status = "Partially applied — finish it, or revert to undo."
        } else {
            state = .notApplied
            status = "Not patched yet."
        }
    }

    private func resetProgress() {
        progress = 0; indeterminate = true; phaseLabel = ""; detail = ""
    }

    /// Runs one phase, streaming its output into the log and moving the bar.
    private func run(_ phase: Phase, _ exe: String, _ args: [String]) async -> Bool {
        phaseLabel = phase.label
        indeterminate = true
        detail = ""
        progress = phase.span.lowerBound
        note("")
        note("▸ \(phase.label)")

        let span = phase.span
        let status = await runStreaming(exe, args) { line in
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.note("  " + line)

                // "[n/total] verb file" drives the bar and the detail line.
                if let step = Self.parseStep(line) {
                    let fraction = Double(step.done) / Double(step.total)
                    self.indeterminate = false
                    self.progress = span.lowerBound + (span.upperBound - span.lowerBound) * fraction
                    self.detail = "\(step.done) of \(step.total)"
                        + (step.name.isEmpty ? "" : " · \(step.name)")
                }
            }
        }

        // Let the queued log updates land before we judge the outcome.
        await Task.yield()

        if status != 0 {
            note("  failed (exit \(status))")
            return false
        }
        progress = span.upperBound
        return true
    }

    func apply() {
        guard let g = game, let pak = g.mainPak else { return }
        busy = true
        Task {
            defer { busy = false; indeterminate = false; phaseLabel = ""; detail = "" }
            status = "Working…"
            progress = 0

            guard which("ffmpeg") != nil else {
                note("")
                note("ffmpeg is required and was not found.")
                note("Install it with:  brew install ffmpeg")
                status = "ffmpeg missing."
                return
            }

            let transcode = resources.appendingPathComponent("transcode-movies.sh").path
            let hide = resources.appendingPathComponent("pak-hide-videos.py").path

            guard await run(.transcoding, "/bin/bash", [transcode, g.content.path]) else {
                status = "Transcoding failed — the pak was left untouched."
                return
            }
            guard await run(.patchingPak, "/usr/bin/python3", [hide, pak.path, "--apply"]) else {
                status = "Pak patching failed. Your transcodes are in place but unused."
                return
            }

            progress = 1
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
            defer { busy = false; indeterminate = false; phaseLabel = ""; detail = "" }
            status = "Reverting…"
            progress = 0

            let transcode = resources.appendingPathComponent("transcode-movies.sh").path
            let hide = resources.appendingPathComponent("pak-hide-videos.py").path

            _ = await run(.restoringPak, "/usr/bin/python3", [hide, pak.path, "--restore"])
            _ = await run(.restoringMovies, "/bin/bash", [transcode, g.content.path, "--restore"])

            progress = 1
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
    @State private var follow = true

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            header
            dropZone
            if runner.game != nil { actions }
            if runner.busy || runner.progress > 0 { progressBar }
            logView
        }
        .padding(22)
        .frame(minWidth: 680, minHeight: 620)
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
            .frame(height: 88)
            .overlay(
                VStack(spacing: 6) {
                    Text(runner.game.map { $0.content.path } ?? "Drop your game folder here")
                        .font(.system(size: 13, design: runner.game == nil ? .default : .monospaced))
                        .lineLimit(2)
                        .truncationMode(.head)
                        .multilineTextAlignment(.center)
                    Button("Choose…") { chooseFolder() }
                        .buttonStyle(.link)
                        .disabled(runner.busy)
                }
                .padding(.horizontal, 16)
            )
            .onDrop(of: [.fileURL], isTargeted: $dropping) { providers in
                guard !runner.busy, let p = providers.first else { return false }
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
                .disabled(runner.busy || !runner.state.canApply)
                .help(runner.state == .applied
                      ? "Already applied. Revert first if you want to run it again."
                      : "Transcode the cutscenes and hide the pak's copies.")
            Button("Revert") { runner.revert() }
                .disabled(runner.busy || !runner.state.canRevert)
                .help(runner.state == .notApplied
                      ? "Nothing to revert."
                      : "Put the original files back.")
            Spacer()
            Text(runner.status)
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }

    private var progressBar: some View {
        VStack(alignment: .leading, spacing: 6) {
            if runner.indeterminate {
                ProgressView().progressViewStyle(.linear)
            } else {
                ProgressView(value: runner.progress)
            }
            HStack {
                Text(runner.phaseLabel)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Text(runner.detail)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
        }
    }

    private var logView: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Log").font(.caption).foregroundStyle(.secondary)
                Spacer()
                Toggle("Follow", isOn: $follow)
                    .toggleStyle(.checkbox)
                    .font(.caption)
                Button {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(runner.log.joined(separator: "\n"), forType: .string)
                } label: {
                    Image(systemName: "doc.on.doc")
                }
                .buttonStyle(.borderless)
                .help("Copy the whole log")
                .disabled(runner.log.isEmpty)
            }

            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 2) {
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
                    guard follow, n > 0 else { return }
                    proxy.scrollTo(n - 1, anchor: .bottom)
                }
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
