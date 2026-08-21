// MacGameVideoFix — restores VP9 cutscenes in UE5 titles running under
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

/// The two ways to stop the crash. They are alternatives, never combined.
/// Is CrossOver patched to decode VP9 at all?
///
/// Neither fix here decodes anything: they get an already-decoded frame to
/// where the game can use it. Without winevideo there is nothing to decode VP9
/// in a WebM container, so installing either would look like it worked and
/// change nothing on screen. Better to say so first.
struct Winevideo {
    let engines: [String]        // CrossOver builds carrying the VP9 plugins

    var found: Bool { !engines.isEmpty }

    /// The plugins are the honest signal. A patched winegstreamer without them
    /// still cannot demux a WebM or decode VP9, and they are the pieces
    /// winevideo actually drops in.
    static func detect() -> Winevideo {
        let fm = FileManager.default
        let roots = ["/Applications",
                     (NSHomeDirectory() as NSString).appendingPathComponent("Applications")]
        var found: [String] = []

        for root in roots {
            guard let apps = try? fm.contentsOfDirectory(atPath: root) else { continue }
            for app in apps where app.hasSuffix(".app") && app.localizedCaseInsensitiveContains("crossover") {
                let base = "\(root)/\(app)/Contents/SharedSupport/CrossOver"
                // The plugin directory moved between CrossOver versions.
                let dirs = ["\(base)/lib64/gstreamer-1.0",
                            "\(base)/lib/x86_64/gstreamer-1.0"]
                for dir in dirs
                where fm.fileExists(atPath: "\(dir)/libgstvpx.dylib")
                   && fm.fileExists(atPath: "\(dir)/libgstmatroska.dylib") {
                    found.append(app)
                    break
                }
            }
        }
        return Winevideo(engines: found)
    }
}


/// The games this knows how to fix.
///
/// Chosen first, folder second. Detecting the game from whatever was dropped
/// works, but it leaves someone who picks the wrong folder with a vague "that
/// is not a game this knows" and no idea which part they got wrong. Choosing
/// the title up front means the app can say exactly what it is looking for,
/// and exactly what was missing when it is not there.
enum SupportedGame: String, CaseIterable, Identifiable {
    case dynastyWarriors
    case unrealVP9

    var id: String { rawValue }

    var name: String {
        switch self {
        case .dynastyWarriors: return "DYNASTY WARRIORS: ORIGINS"
        case .unrealVP9:       return "Unreal Engine 5, VP9 cutscenes"
        }
    }

    var symptom: String {
        switch self {
        case .dynastyWarriors: return "Cutscene plays with sound, picture black"
        case .unrealVP9:       return "Crash on the first cutscene"
        }
    }

    /// What to select, in the words of what is inside it.
    var folderHint: String {
        switch self {
        case .dynastyWarriors: return "the folder holding DWORIGINS.exe"
        case .unrealVP9:       return "the folder containing Content"
        }
    }

    var example: String {
        switch self {
        case .dynastyWarriors: return "…/steamapps/common/DWORIGINS"
        case .unrealVP9:       return "…/steamapps/common/Sparta/MortalShell2"
        }
    }

    var modes: [Mode] {
        switch self {
        case .dynastyWarriors: return [.videoBridge]
        case .unrealVP9:       return [.runtime, .transcode]
        }
    }

    /// Said when the folder is not the one this game needs. Naming the thing
    /// that was missing beats saying the folder is wrong.
    var mismatch: String {
        switch self {
        case .dynastyWarriors:
            return "No DWORIGINS.exe there. Pick the folder the game's executable is in."
        case .unrealVP9:
            return "No Content/Movies and Content/Paks below there. "
                 + "Pick the folder that contains Content, not Content itself."
        }
    }

    /// Accepts the folder itself or the one above it, since a Steam library
    /// folder and a game folder look alike from the outside.
    func title(from url: URL) -> Title? {
        let fm = FileManager.default
        switch self {
        case .dynastyWarriors:
            var candidates = [url]
            if let subs = try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil) {
                candidates += subs
            }
            for c in candidates
            where fm.fileExists(atPath: c.appendingPathComponent("DWORIGINS.exe").path) {
                return .dynastyWarriors(c)
            }
            return nil
        case .unrealVP9:
            return GameFolder.locate(from: url).map { Title.unrealVP9($0) }
        }
    }
}


/// Which game a chosen folder turns out to be. Each has its own fixes, and
/// nothing is offered that does not apply to what was actually found.
enum Title {
    case unrealVP9(GameFolder)      // any UE5 title with VP9 cutscenes
    case dynastyWarriors(URL)       // DYNASTY WARRIORS: ORIGINS

    var name: String {
        switch self {
        case .unrealVP9:      return "Unreal Engine title with VP9 cutscenes"
        case .dynastyWarriors: return "DYNASTY WARRIORS: ORIGINS"
        }
    }

    var path: String {
        switch self {
        case .unrealVP9(let g):    return g.content.path
        case .dynastyWarriors(let u): return u.path
        }
    }

    var modes: [Mode] {
        switch self {
        case .unrealVP9:       return [.runtime, .transcode]
        case .dynastyWarriors: return [.videoBridge]
        }
    }

    /// Looks at the folder itself and one level down, so dropping either the
    /// game folder or the library folder above it works.
    static func detect(from url: URL) -> Title? {
        let fm = FileManager.default
        var candidates = [url]
        if let subs = try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil) {
            candidates += subs
        }
        for c in candidates where fm.fileExists(atPath: c.appendingPathComponent("DWORIGINS.exe").path) {
            return .dynastyWarriors(c)
        }
        if let g = GameFolder.locate(from: url) { return .unrealVP9(g) }
        return nil
    }
}


enum Mode: String, CaseIterable, Identifiable {
    /// Patch Electra in memory as the game starts. Nothing on disk changes.
    case runtime
    /// Re-encode the cutscenes to H.264 and hide the pak's VP9 copies.
    case transcode
    /// Carry the decoded frame from the D3D11 decoder to the D3D12 renderer.
    case videoBridge

    var id: String { rawValue }

    var title: String {
        switch self {
        case .runtime:     return "Runtime patch"
        case .transcode:   return "Re-encode cutscenes"
        case .videoBridge: return "Video bridge"
        }
    }

    var blurb: String {
        switch self {
        case .runtime:
            return "Adds one small DLL beside the game's own. "
                 + "Your original VP9 cutscenes play untouched, and it takes a second."
        case .transcode:
            return "Converts every cutscene to H.264 and edits the pak index. "
                 + "Slower, needs ffmpeg, and slightly softens the picture."
        case .videoBridge:
            return "Adds one small DLL beside the game's own. The game decodes "
                 + "video on a D3D11 device and draws with D3D12, and under "
                 + "D3DMetal the frame cannot cross between them; this carries it."
        }
    }
}


enum Phase {
    case idle, transcoding, patchingPak, restoringPak, restoringMovies
    case installingRuntime, removingRuntime
    case installingBridge, removingBridge

    var label: String {
        switch self {
        case .idle:              return ""
        case .transcoding:       return "Transcoding cutscenes to H.264"
        case .patchingPak:       return "Removing video entries from the pak index"
        case .restoringPak:      return "Restoring the pak index"
        case .restoringMovies:   return "Restoring the original cutscenes"
        case .installingRuntime: return "Installing the runtime patch"
        case .removingRuntime:   return "Removing the runtime patch"
        case .installingBridge:  return "Installing the video bridge"
        case .removingBridge:    return "Removing the video bridge"
        }
    }

    /// Where this phase sits in the overall run, so the bar advances smoothly
    /// instead of resetting between steps. Transcoding is the long pole.
    var span: ClosedRange<Double> {
        switch self {
        case .idle:              return 0...0
        case .transcoding:       return 0...0.92
        case .patchingPak:       return 0.92...1
        case .restoringPak:      return 0...0.15
        case .restoringMovies:   return 0.15...1
        case .installingRuntime,
             .removingRuntime,
             .installingBridge,
             .removingBridge:    return 0...1
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
    @Published var mode: Mode = .runtime
    /// Set when the game ships no libogg, so the runtime patch cannot be used.
    @Published var runtimeUnavailable = false
    @Published var runtimeState: FixState = .unknown
    @Published var transcodeState: FixState = .unknown
    @Published var bridgeState: FixState = .unknown
    @Published var log: [String] = []
    @Published var busy = false
    @Published var title: Title?
    @Published var winevideo = Winevideo.detect()
    @Published var chosen: SupportedGame = .dynastyWarriors {
        didSet { if oldValue != chosen { clearSelection() } }
    }

    /// Changing the game invalidates whatever folder was picked for the last
    /// one, rather than leaving a stale path on screen under a new title.
    private func clearSelection() {
        title = nil
        runtimeState = .unknown; transcodeState = .unknown; bridgeState = .unknown
        log.removeAll()
        resetProgress()
        mode = chosen.modes.first ?? .runtime
        status = "Choose the folder for \(chosen.name)."
    }
    @Published var status = "Choose your game folder to begin."

    /// The Unreal paths still work in terms of a Content folder; this is where
    /// they get it, and it is nil for anything that is not an Unreal title.
    var game: GameFolder? {
        if case .unrealVP9(let g) = title { return g }
        return nil
    }
    var dwFolder: URL? {
        if case .dynastyWarriors(let u) = title { return u }
        return nil
    }

    @Published var progress: Double = 0
    @Published var indeterminate = true
    @Published var phaseLabel = ""
    @Published var detail = ""          // e.g. "34 of 61 · Movie_Tut_Parry.mp4"

    private var resources: URL { Bundle.main.resourceURL ?? URL(fileURLWithPath: ".") }

    /// First line of the installer's --status output, filled on the main actor.
    private var statusAnswer = ""

    /// The selected mode's own state, and the other one's.
    var state: FixState {
        switch mode {
        case .runtime:     return runtimeState
        case .transcode:   return transcodeState
        case .videoBridge: return bridgeState
        }
    }

    /// Only the two Unreal modes exclude each other; the bridge has no rival.
    var otherState: FixState {
        switch mode {
        case .runtime:     return transcodeState
        case .transcode:   return runtimeState
        case .videoBridge: return .notApplied
        }
    }

    /// Both fixes cure the same crash, so having both in place is never useful
    /// and makes reverting ambiguous. Offer Apply only when the other is clear.
    var canApply: Bool { !busy && state.canApply && otherState == .notApplied }
    var canRevert: Bool { !busy && state.canRevert }

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
        guard let t = chosen.title(from: url) else {
            status = chosen.mismatch
            title = nil
            return
        }
        title = t
        mode = chosen.modes.first ?? .runtime
        runtimeState = .unknown
        transcodeState = .unknown
        log.removeAll()
        resetProgress()
        note("\(t.name)")
        note(t.path)
        Task { await inspectTitle() }
    }

    /// Dispatches to whichever inspection the detected game needs.
    func inspectTitle() async {
        switch title {
        case .unrealVP9(let g):     await inspect(g)
        case .dynastyWarriors(let u): await inspectBridge(u)
        case .none:                 break
        }
    }

    private func inspectBridge(_ folder: URL) async {
        let script = resources.appendingPathComponent("install-dwo-bridge.sh").path
        statusAnswer = ""
        let code = await runStreaming("/bin/bash", [script, folder.path, "--status"]) { line in
            Task { @MainActor [weak self] in
                guard let self, self.statusAnswer.isEmpty else { return }
                self.statusAnswer = line
            }
        }
        await Task.yield()

        guard code == 0 else {
            bridgeState = .notApplied
            status = "This copy has no libxess.dll for the bridge to ride on."
            return
        }
        switch statusAnswer.split(separator: " ", maxSplits: 1).first.map(String.init) {
        case "installed": bridgeState = .applied
        case "broken":    bridgeState = .partial
        default:          bridgeState = .notApplied
        }
        status = bridgeState == .applied
            ? "Bridge installed. Cutscenes should play."
            : "Not patched yet."
    }

    func inspect(_ g: GameFolder) async {
        let fm = FileManager.default
        let movies = (try? fm.subpathsOfDirectory(atPath: g.movies.path)) ?? []
        note("Found \(movies.filter { $0.hasSuffix(".mp4") }.count) .mp4 files under Movies/")

        await inspectRuntime(g)

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
            transcodeState = .applied
        } else if patched || backedUp {
            transcodeState = .partial
        } else {
            transcodeState = .notApplied
        }

        describe()
    }

    /// Asks the installer what it sees, rather than repeating its search for
    /// the Ogg folder here -- one definition of "installed", not two.
    private func inspectRuntime(_ g: GameFolder) async {
        let script = resources.appendingPathComponent("install-runtime-fix.sh").path
        statusAnswer = ""
        // The handler fires off the main actor, so hop back rather than
        // capturing a local -- same pattern as run().
        let code = await runStreaming("/bin/bash", [script, g.content.path, "--status"]) { line in
            Task { @MainActor [weak self] in
                guard let self, self.statusAnswer.isEmpty else { return }
                self.statusAnswer = line
            }
        }
        await Task.yield()

        guard code == 0 else {
            // No libogg in this title: the runtime patch has no way in, but the
            // transcode mode still works.
            runtimeState = .notApplied
            note("This game has no libogg for the runtime patch to ride on.")
            runtimeUnavailable = true
            return
        }
        runtimeUnavailable = false
        switch statusAnswer.split(separator: " ", maxSplits: 1).first.map(String.init) {
        case "installed": runtimeState = .applied
        case "broken":    runtimeState = .partial
        default:          runtimeState = .notApplied
        }
    }

    /// One sentence covering whatever is actually in place.
    private func describe() {
        if runtimeState == .applied && transcodeState == .applied {
            status = "Both fixes are in place — revert one."
        } else if runtimeState == .applied {
            status = "Runtime patch installed. Cutscenes should play."
        } else if transcodeState == .applied {
            status = "Re-encoded cutscenes in place. They should play."
        } else if runtimeState == .partial || transcodeState == .partial {
            status = "Partially applied — revert to undo, then try again."
        } else {
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
        switch mode {
        case .runtime:     applyRuntime()
        case .transcode:   applyTranscode()
        case .videoBridge: runBridge(install: true)
        }
    }

    func revert() {
        switch mode {
        case .runtime:     revertRuntime()
        case .transcode:   revertTranscode()
        case .videoBridge: runBridge(install: false)
        }
    }

    // MARK: Video bridge

    private func runBridge(install: Bool) {
        guard let folder = dwFolder else { return }
        busy = true
        Task {
            defer { busy = false; indeterminate = false; phaseLabel = ""; detail = "" }
            status = install ? "Working…" : "Reverting…"
            progress = 0

            let script = resources.appendingPathComponent("install-dwo-bridge.sh").path
            let args = install ? [script, folder.path] : [script, folder.path, "--restore"]
            let ok = await run(install ? .installingBridge : .removingBridge, "/bin/bash", args)

            progress = 1
            note("")
            if ok && install {
                note("Done. Launch the game — the cutscenes should play.")
                note("CrossOver has to be patched with winevideo, or there is nothing")
                note("to decode them: this presents frames, it does not decode.")
                note("Note: Steam's \"verify integrity of game files\" undoes this.")
            } else if ok {
                note("Reverted. The game is back to its original files.")
            }
            await inspectTitle()
        }
    }

    // MARK: Runtime patch

    private func applyRuntime() {
        guard let g = game else { return }
        busy = true
        Task {
            defer { busy = false; indeterminate = false; phaseLabel = ""; detail = "" }
            status = "Working…"
            progress = 0

            let script = resources.appendingPathComponent("install-runtime-fix.sh").path
            guard await run(.installingRuntime, "/bin/bash", [script, g.content.path]) else {
                status = "Could not install the runtime patch."
                await inspect(g)
                return
            }

            progress = 1
            note("")
            note("Done. Launch the game — the original cutscenes should play.")
            note("Note: Steam's \"verify integrity of game files\" undoes this.")
            await inspectTitle()
        }
    }

    private func revertRuntime() {
        guard let g = game else { return }
        busy = true
        Task {
            defer { busy = false; indeterminate = false; phaseLabel = ""; detail = "" }
            status = "Reverting…"
            progress = 0

            let script = resources.appendingPathComponent("install-runtime-fix.sh").path
            _ = await run(.removingRuntime, "/bin/bash", [script, g.content.path, "--restore"])

            progress = 1
            note("")
            note("Reverted. The game is back to its original files.")
            await inspectTitle()
        }
    }

    // MARK: Re-encoding

    private func applyTranscode() {
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
            await inspectTitle()
        }
    }

    private func revertTranscode() {
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
            await inspectTitle()
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
            requirement
            supported
            dropZone
            if let t = runner.title {
                if t.modes.count > 1 { modePicker(t) }
                actions
            }
            if runner.busy || runner.progress > 0 { progressBar }
            logView
        }
        .padding(22)
        .frame(minWidth: 700, minHeight: 720)
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("MacGameVideoFix")
                .font(.system(size: 22, weight: .semibold))
            Text("Makes Windows games show their cutscenes under CrossOver on Apple silicon.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
    }

    /// Says up front whether the thing both fixes depend on is present.
    private var requirement: some View {
        HStack(alignment: .top, spacing: 8) {
            Image(systemName: runner.winevideo.found
                  ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                .foregroundStyle(runner.winevideo.found ? Color.green : Color.orange)
            VStack(alignment: .leading, spacing: 2) {
                if runner.winevideo.found {
                    Text("winevideo found in \(runner.winevideo.engines.joined(separator: ", "))")
                        .font(.callout)
                } else {
                    Text("winevideo not found in any CrossOver on this Mac")
                        .font(.callout.weight(.medium))
                    Text("Neither fix decodes video — they carry an already-decoded frame to "
                       + "where the game can use it. Without winevideo there is nothing to "
                       + "decode VP9, so installing one will change nothing on screen.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                    Link("github.com/Jfishin/winevideo",
                         destination: URL(string: "https://github.com/Jfishin/winevideo")!)
                        .font(.caption)
                }
            }
            Spacer()
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 8)
            .fill((runner.winevideo.found ? Color.green : Color.orange).opacity(0.08)))
    }

    /// Pick the game first. The folder to look for depends on it, and saying
    /// so up front is the difference between guidance and a guessing game.
    private var supported: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
                Text("Game")
                    .font(.callout)
                Picker("", selection: $runner.chosen) {
                    ForEach(SupportedGame.allCases) { g in Text(g.name).tag(g) }
                }
                .labelsHidden()
                .frame(maxWidth: 340)
                .disabled(runner.busy)
                Spacer()
            }
            Text(runner.chosen.symptom)
                .font(.caption)
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
                    if let path = runner.title?.path {
                        Text(path)
                            .font(.system(size: 13, design: .monospaced))
                            .lineLimit(2)
                            .truncationMode(.head)
                            .multilineTextAlignment(.center)
                    } else {
                        Text("Drop \(runner.chosen.folderHint) here")
                            .font(.system(size: 13))
                            .multilineTextAlignment(.center)
                        Text(runner.chosen.example)
                            .font(.system(size: 11, design: .monospaced))
                            .foregroundStyle(.secondary)
                    }
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

    private func modePicker(_ t: Title) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Picker("", selection: $runner.mode) {
                ForEach(t.modes) { m in Text(m.title).tag(m) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .disabled(runner.busy)

            Text(runner.runtimeUnavailable && runner.mode == .runtime
                 ? "This game ships no libogg, so the runtime patch has nothing to load from. Use the other mode."
                 : runner.mode.blurb)
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var actions: some View {
        HStack(spacing: 12) {
            Button("Apply Fix") { runner.apply() }
                .keyboardShortcut(.defaultAction)
                .disabled(!runner.canApply)
                .help(applyHelp)
            Button("Revert") { runner.revert() }
                .disabled(!runner.canRevert)
                .help(runner.state == .notApplied
                      ? "Nothing to revert."
                      : "Put the original files back.")
            Spacer()
            Text(runner.status)
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }

    private var applyHelp: String {
        if runner.state == .applied {
            return "Already applied. Revert first if you want to run it again."
        }
        if runner.otherState != .notApplied {
            return "Revert the other fix first — the two solve the same problem."
        }
        switch runner.mode {
        case .runtime:     return "Install the proxy DLL that patches Electra at startup."
        case .transcode:   return "Re-encode the cutscenes and hide the pak's copies."
        case .videoBridge: return "Install the DLL that carries frames from the decoder to the renderer."
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
        panel.message = "Select \(runner.chosen.folderHint)."
        panel.prompt = "Select"
        if panel.runModal() == .OK, let url = panel.url {
            runner.select(url)
        }
    }
}

@main
struct MacGameVideoFixApp: App {
    var body: some Scene {
        Window("MacGameVideoFix", id: "main") {
            ContentView()
        }
        .windowResizability(.contentMinSize)
    }
}
