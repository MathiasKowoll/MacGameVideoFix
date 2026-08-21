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
    case mortalShell2
    case lisReunion
    case lisDoubleExposure
    case unrealOther
    case dynastyWarriors

    var id: String { rawValue }

    var name: String {
        switch self {
        case .mortalShell2:      return "Mortal Shell 2"
        case .lisReunion:        return "Life is Strange: Reunion"
        case .lisDoubleExposure: return "Life is Strange: Double Exposure"
        case .unrealOther:       return "Another Unreal Engine 5 title"
        case .dynastyWarriors:   return "DYNASTY WARRIORS: ORIGINS"
        }
    }

    var symptom: String {
        switch self {
        case .mortalShell2:      return "Crash on the first cutscene"
        case .lisReunion,
             .lisDoubleExposure: return "Runs, then freezes after a while"
        case .unrealOther:       return "Crash on the first cutscene, or a freeze after a while"
        case .dynastyWarriors:   return "Cutscene plays with sound, picture black"
        }
    }

    /// Steam names install folders after the project rather than the game, so
    /// checking the shipping executable is the only way to tell someone they
    /// picked Reunion's folder while Double Exposure was selected.
    var executable: String? {
        switch self {
        case .mortalShell2:      return "MortalShell2-Win64-Shipping.exe"
        case .lisReunion:        return "Iris-Win64-Shipping.exe"
        case .lisDoubleExposure: return "Chronos-Win64-Shipping.exe"
        case .unrealOther:       return nil
        case .dynastyWarriors:   return "DWORIGINS.exe"
        }
    }

    /// What to select, in the words of what is inside it.
    var folderHint: String {
        switch self {
        case .dynastyWarriors: return "the folder holding DWORIGINS.exe"
        default:               return "the game folder, the one with Engine inside"
        }
    }

    var example: String {
        switch self {
        case .mortalShell2:      return "…/steamapps/common/Sparta"
        case .lisReunion:        return "…/steamapps/common/LifeisStrangeReunion"
        case .lisDoubleExposure: return "…/steamapps/common/LifeIsStrangeDoubleExposure"
        case .unrealOther:       return "…/steamapps/common/<Game>"
        case .dynastyWarriors:   return "…/steamapps/common/DWORIGINS"
        }
    }

    var modes: [Mode] {
        switch self {
        case .dynastyWarriors: return [.videoBridge]
        default:               return [.runtime]
        }
    }

    /// Said when the folder is not the one this game needs. Naming the thing
    /// that was missing beats saying the folder is wrong.
    var mismatch: String {
        switch self {
        case .dynastyWarriors:
            return "No DWORIGINS.exe there. Pick the folder the game's executable is in."
        case .unrealOther:
            return "No Engine/Binaries/ThirdParty/Ogg below there. "
                 + "Pick the game's own folder, the one with Engine and Content in it."
        default:
            return "That is not \(name) — no \(executable ?? "executable") under it. "
                 + "Pick that game's own folder, or choose the matching title above."
        }
    }

    /// Accepts the folder itself or the one above it, since a Steam library
    /// folder and a game folder look alike from the outside.
    func title(from url: URL) -> Title? {
        let fm = FileManager.default
        if case .dynastyWarriors = self {
            var candidates = [url]
            if let subs = try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil) {
                candidates += subs
            }
            for c in candidates
            where fm.fileExists(atPath: c.appendingPathComponent("DWORIGINS.exe").path) {
                return .dynastyWarriors(c)
            }
            return nil
        }
        guard let g = GameFolder.locate(from: url) else { return nil }
        // A named title has to prove it is that title; "another UE5 title"
        // has nothing to check against and is taken at its word.
        if let exe = executable, !g.hasExecutable(exe) { return nil }
        return .unrealVP9(g)
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
        case .unrealVP9(let g):    return g.root.path
        case .dynastyWarriors(let u): return u.path
        }
    }

    var modes: [Mode] {
        switch self {
        case .unrealVP9:       return [.runtime]
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
    /// Carry the decoded frame from the D3D11 decoder to the D3D12 renderer.
    case videoBridge

    var id: String { rawValue }

    var title: String {
        switch self {
        case .runtime:     return "Runtime patch"
        case .videoBridge: return "Video bridge"
        }
    }

    var blurb: String {
        switch self {
        case .runtime:
            return "Adds one small DLL beside the game's own. "
                 + "Your original VP9 cutscenes play untouched, and it takes a second."
        case .videoBridge:
            return "Adds one small DLL beside the game's own. The game decodes "
                 + "video on a D3D11 device and draws with D3D12, and under "
                 + "D3DMetal the frame cannot cross between them; this carries it."
        }
    }
}


enum Phase {
    case idle, restoringPak, restoringMovies
    case installingRuntime, removingRuntime
    case installingBridge, removingBridge

    var label: String {
        switch self {
        case .idle:              return ""
        case .restoringPak:      return "Restoring the pak index"
        case .restoringMovies:   return "Restoring the original cutscenes"
        case .installingRuntime: return "Installing the runtime patch"
        case .removingRuntime:   return "Removing the runtime patch"
        case .installingBridge:  return "Installing the video bridge"
        case .removingBridge:    return "Removing the video bridge"
        }
    }

    /// Where this phase sits in the overall run, so the bar advances smoothly
    /// instead of resetting between steps.
    var span: ClosedRange<Double> {
        switch self {
        case .idle:              return 0...0
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
    /// The game root -- the folder with Engine/ in it. This used to be the
    /// Content folder, because the removed re-encode mode worked on
    /// Content/Movies and Content/Paks. The runtime patch needs neither: it
    /// rides on Engine/Binaries/ThirdParty/Ogg, and requiring the old pair
    /// turned away every Unreal title that ships no loose movies -- both Life
    /// is Strange titles among them.
    let root: URL

    var ogg: URL { root.appendingPathComponent("Engine/Binaries/ThirdParty/Ogg/Win64") }

    /// Only the removed re-encode touched these, so they are allowed to be
    /// absent; they exist to spot a copy that mode was applied to.
    var content: URL { root.appendingPathComponent("Content") }
    var movies: URL  { content.appendingPathComponent("Movies") }
    var paks:   URL  { content.appendingPathComponent("Paks") }

    /// Accepts the game folder, or a library folder one level above it.
    static func locate(from url: URL) -> GameFolder? {
        let fm = FileManager.default
        var candidates = [url]
        if let subs = try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil) {
            candidates += subs
        }
        for c in candidates {
            var isDir: ObjCBool = false
            let ogg = c.appendingPathComponent("Engine/Binaries/ThirdParty/Ogg/Win64")
            if fm.fileExists(atPath: ogg.path, isDirectory: &isDir), isDir.boolValue {
                return GameFolder(root: c)
            }
        }
        return nil
    }

    /// Unreal puts the shipping binary in <Project>/Binaries/Win64, and the
    /// project name is rarely the game's name, so this looks rather than
    /// guesses at the path.
    func hasExecutable(_ name: String) -> Bool {
        let fm = FileManager.default
        guard let subs = try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)
        else { return false }
        return subs.contains {
            fm.fileExists(atPath: $0.appendingPathComponent("Binaries/Win64/\(name)").path)
        }
    }

    /// The biggest pak, when there is one. Used only to undo an old re-encode.
    var mainPak: URL? {
        (try? FileManager.default.contentsOfDirectory(at: paks,
            includingPropertiesForKeys: nil))?
            .filter { $0.pathExtension == "pak" }
            .sorted { $0.lastPathComponent < $1.lastPathComponent }
            .first
    }
}

/// What we found on disk. Drives which actions are offered, so the same fix
/// cannot be applied twice.
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
    /// An older release could re-encode the cutscenes to H.264 and hide the
    /// pak's VP9 copies. That mode is gone, but anyone who ran it still has a
    /// patched pak and their originals in a backup folder, so it is still
    /// detected and can still be undone -- never applied.
    @Published var legacyReencode: FixState = .notApplied
    @Published var bridgeState: FixState = .unknown
    @Published var log: [String] = []
    @Published var busy = false
    @Published var title: Title?
    @Published var winevideo = Winevideo.detect()
    @Published var chosen: SupportedGame = .mortalShell2 {
        didSet { if oldValue != chosen { clearSelection() } }
    }

    /// Changing the game invalidates whatever folder was picked for the last
    /// one, rather than leaving a stale path on screen under a new title.
    private func clearSelection() {
        title = nil
        runtimeState = .unknown; legacyReencode = .notApplied; bridgeState = .unknown
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
        case .videoBridge: return bridgeState
        }
    }

    /// The runtime patch cures the same crash the old re-encode did, so a
    /// leftover re-encode has to come out first; the bridge has no rival.
    var otherState: FixState {
        switch mode {
        case .runtime:     return legacyReencode
        case .videoBridge: return .notApplied
        }
    }

    /// Having both in place is never useful and makes reverting ambiguous.
    /// Offer Apply only when the other is clear.
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
        legacyReencode = .notApplied
        log.removeAll()
        resetProgress()
        note(chosen.name)
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
        await inspectRuntime(g)
        detectLegacyReencode(g)
        describe()
    }

    /// Looks for the traces the removed re-encode mode left behind. A title
    /// with no loose movies and no paks simply never had it applied, so a
    /// missing pak is an answer here and not a failure.
    private func detectLegacyReencode(_ g: GameFolder) {
        let fm = FileManager.default
        guard let pak = g.mainPak else {
            legacyReencode = .notApplied
            return
        }
        let marker = pak.deletingLastPathComponent()
            .appendingPathComponent(".\(pak.lastPathComponent).hidden-videos.json")
        let patched = fm.fileExists(atPath: marker.path)
        let backedUp = fm.fileExists(atPath: g.content.appendingPathComponent("Movies_VP9_backup").path)

        if patched && backedUp {
            legacyReencode = .applied
            note("This copy still has re-encoded cutscenes from an older version.")
        } else if patched || backedUp {
            legacyReencode = .partial
            note("A half-finished re-encode from an older version is still here.")
        } else {
            legacyReencode = .notApplied
        }
    }

    /// Asks the installer what it sees, rather than repeating its search for
    /// the Ogg folder here -- one definition of "installed", not two.
    private func inspectRuntime(_ g: GameFolder) async {
        let script = resources.appendingPathComponent("install-runtime-fix.sh").path
        statusAnswer = ""
        // The handler fires off the main actor, so hop back rather than
        // capturing a local -- same pattern as run().
        let code = await runStreaming("/bin/bash", [script, g.root.path, "--status"]) { line in
            Task { @MainActor [weak self] in
                guard let self, self.statusAnswer.isEmpty else { return }
                self.statusAnswer = line
            }
        }
        await Task.yield()

        guard code == 0 else {
            // No libogg in this title: the runtime patch has no way in.
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
        if legacyReencode != .notApplied {
            status = "Re-encoded cutscenes from an older version are still in place."
        } else if runtimeState == .applied {
            status = "Runtime patch installed. Cutscenes should play."
        } else if runtimeState == .partial {
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
        case .videoBridge: runBridge(install: true)
        }
    }

    func revert() {
        switch mode {
        case .runtime:     revertRuntime()
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
            guard await run(.installingRuntime, "/bin/bash", [script, g.root.path]) else {
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
            _ = await run(.removingRuntime, "/bin/bash", [script, g.root.path, "--restore"])

            progress = 1
            note("")
            note("Reverted. The game is back to its original files.")
            await inspectTitle()
        }
    }

    // MARK: Scanning a whole library

    @Published var bulk = false
    @Published var scanning = false
    @Published var plan: [ScanHit] = []
    @Published var scanNote = ""
    @Published var batchStep = ""
    /// Honoured between games, never inside one. Interrupting an installer
    /// mid-rename is what manufactures the state that destroys an original.
    @Published var stopping = false

    var selectedHits: [ScanHit] { plan.filter { $0.selected && $0.actionable } }

    func enterBulk() {
        bulk = true
        plan = []
        scanNote = ""
        log.removeAll()
        resetProgress()
        status = "Scan a Steam library, or let the app find them."
    }

    func leaveBulk() {
        bulk = false
        plan = []
        batchStep = ""
        status = "Choose your game folder to begin."
    }

    /// `url` nil means: ask the bottles where their libraries are.
    func startScan(from url: URL?) {
        scanning = true
        plan = []
        log.removeAll()
        indeterminate = true
        status = "Scanning…"
        Task {
            defer { scanning = false; indeterminate = false }

            var roots: [URL] = []
            var describedRoot = ""
            if let url {
                let (root, looksLikeLibrary) = SteamLibrary.normalise(url)
                roots = [root]
                describedRoot = root.path
                if !looksLikeLibrary {
                    note("That folder does not look like a Steam library; scanning it as it is.")
                }
            } else {
                roots = await Task.detached { SteamLibrary.discover() }.value
                describedRoot = roots.count == 1 ? roots[0].path
                                                 : "\(roots.count) Steam libraries"
            }

            guard !roots.isEmpty else {
                status = "No Steam library found. Drop one on the app instead."
                return
            }
            note("Scanning \(describedRoot)")

            let found = await Task.detached { Self.recognise(in: roots) }.value
            guard !found.isEmpty else {
                scanNote = "No supported game found."
                status = "Nothing to do here."
                note("No supported game found.")
                return
            }

            var probed: [ScanHit] = []
            for hit in found { probed.append(await probe(hit)) }
            plan = probed

            let ready = probed.filter { $0.actionable && $0.state != .applied }.count
            scanNote = "\(probed.count) supported game\(probed.count == 1 ? "" : "s") found"
            status = ready == 0 ? "Everything here is already fixed."
                                : "\(ready) can be fixed now."
        }
    }

    /// Only the games we recognise are ever named. The rest of the folder is
    /// walked and forgotten -- what is installed alongside them is nobody's
    /// business, least of all a log someone pastes into a bug report.
    private nonisolated static func recognise(in roots: [URL]) -> [ScanHit] {
        let fm = FileManager.default
        var hits: [ScanHit] = []
        var seen = Set<String>()
        for root in roots {
            guard let entries = try? fm.contentsOfDirectory(
                at: root, includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey],
                options: [.skipsHiddenFiles]) else { continue }
            // The root itself may be a single game folder.
            for folder in [root] + entries {
                let values = try? folder.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
                if values?.isSymbolicLink == true { continue }
                if folder != root && values?.isDirectory != true { continue }
                for game in SupportedGame.scannable where game.isRooted(at: folder) {
                    let key = "\(game.rawValue)\u{1}\(folder.standardizedFileURL.path)"
                    if seen.insert(key).inserted {
                        hits.append(ScanHit(game: game, root: folder))
                    }
                }
            }
        }
        return hits
    }

    /// Asks the installer what it sees, rather than repeating its judgement
    /// here -- one definition of "installed" in this project, not two.
    ///
    /// Only the state word is kept. --status also prints the path it found,
    /// and that belongs nowhere near a log someone might paste in public.
    private func probe(_ hit: ScanHit) async -> ScanHit {
        var hit = hit
        let script = resources.appendingPathComponent(
            hit.game == .dynastyWarriors ? "install-dwo-bridge.sh" : "install-runtime-fix.sh").path

        statusAnswer = ""
        let code = await runStreaming("/bin/bash", [script, hit.root.path, "--status"]) { line in
            Task { @MainActor [weak self] in
                guard let self, self.statusAnswer.isEmpty else { return }
                self.statusAnswer = line
            }
        }
        await Task.yield()

        guard code == 0 else {
            hit.state = .notApplied
            hit.selected = false
            hit.blocker = hit.game == .dynastyWarriors
                ? "This copy ships no libxess.dll for the bridge to ride on."
                : "This copy ships no libogg for the fix to ride on."
            return hit
        }
        switch statusAnswer.split(separator: " ", maxSplits: 1).first.map(String.init) {
        case "installed":
            hit.state = .applied
            hit.selected = false
        case "broken":
            hit.state = .partial
            hit.selected = false
            hit.blocker = "Half-installed. Verify this game's files in Steam, then scan again."
        default:
            hit.state = .notApplied
            hit.blocker = nil
        }
        return hit
    }

    func applyPlan(install: Bool) {
        let targets = selectedHits.filter { install ? $0.state != .applied : $0.state != .notApplied }
        guard !targets.isEmpty else { return }
        busy = true
        stopping = false
        Task {
            defer { busy = false; indeterminate = false; batchStep = ""; phaseLabel = ""; detail = "" }
            progress = 0
            var done = 0
            for hit in targets {
                if stopping { note(""); note("Stopped. \(targets.count - done) game(s) left untouched."); break }
                done += 1
                batchStep = "\(hit.game.name) (\(done) of \(targets.count))"
                status = install ? "Installing…" : "Removing…"
                note(""); note("▸ \(hit.game.name)")

                let script = resources.appendingPathComponent(
                    hit.game == .dynastyWarriors ? "install-dwo-bridge.sh" : "install-runtime-fix.sh").path
                var args = [script, hit.root.path]
                if !install { args.append("--restore") }

                indeterminate = true
                let code = await runStreaming("/bin/bash", args) { line in
                    Task { @MainActor [weak self] in self?.note("  " + line) }
                }
                await Task.yield()
                progress = Double(done) / Double(targets.count)

                if let i = plan.firstIndex(where: { $0.id == hit.id }) {
                    plan[i].outcome = code == 0 ? (install ? "Fixed" : "Removed")
                                                : "Failed — see the log"
                }
            }
            // Re-probe rather than trust what we just did.
            var refreshed: [ScanHit] = []
            for var hit in plan {
                let outcome = hit.outcome
                hit = await probe(hit)
                hit.outcome = outcome
                refreshed.append(hit)
            }
            plan = refreshed
            batchStep = ""
            let ok = plan.filter { $0.outcome == "Fixed" || $0.outcome == "Removed" }.count
            let bad = plan.filter { $0.outcome?.hasPrefix("Failed") == true }.count
            let already = plan.filter { $0.outcome == nil && $0.state == .applied }.count
            var parts = ["\(ok) \(install ? "fixed" : "removed")"]
            if already > 0 { parts.append("\(already) already done") }
            if bad > 0 { parts.append("\(bad) needs attention") }
            status = parts.joined(separator: " · ")
        }
    }

    // MARK: Undoing an older release's re-encode

    /// Puts back what the removed re-encode mode replaced: the pak's video
    /// entries and the original VP9 files. Kept because a copy patched by an
    /// older version cannot be repaired any other way, short of Steam
    /// re-downloading the game.
    func undoLegacyReencode() {
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

}

// MARK: - Scanning a Steam library

/// One recognised game, and what the scan found out about it.
struct ScanHit: Identifiable {
    let id = UUID()
    let game: SupportedGame
    let root: URL
    var state: FixState = .unknown
    /// Set when the row cannot be acted on, and says why in the user's terms.
    var blocker: String?
    var selected = true
    var outcome: String?

    var actionable: Bool { blocker == nil }
}

extension SupportedGame {
    /// Titles a scan can identify.
    ///
    /// `.unrealOther` is excluded deliberately: its `executable` is nil, so it
    /// would match any folder with an Engine directory, claim to be a
    /// supported game, and put unrelated folder names on screen. Identity has
    /// to be something the folder can fail.
    static var scannable: [SupportedGame] { allCases.filter { $0.executable != nil } }

    /// Is this exact folder this game? The scan already knows which folder it
    /// is asking about, so unlike `title(from:)` this neither walks up nor
    /// looks one level down.
    ///
    /// Identity is the shipping executable and nothing else. Whether the game
    /// ships the carrier DLL the fix rides on is a separate question, asked
    /// later by the installer -- a title with no libogg is still that title,
    /// and has to appear as a row saying so rather than vanish from the scan.
    func isRooted(at folder: URL) -> Bool {
        guard let exe = executable else { return false }
        if case .dynastyWarriors = self {
            return FileManager.default.fileExists(
                atPath: folder.appendingPathComponent(exe).path)
        }
        return GameFolder(root: folder).hasExecutable(exe)
    }
}

/// Working out which folder to scan.
enum SteamLibrary {
    /// Steam's own layout, and one step back up it.
    ///
    /// The last rule is the one that matters: someone who drags the game they
    /// were thinking about gets the library it sits in scanned, rather than a
    /// one-row plan and a second trip to Finder.
    static func normalise(_ url: URL) -> (root: URL, looksLikeLibrary: Bool) {
        let fm = FileManager.default
        func isDir(_ u: URL) -> Bool {
            var d: ObjCBool = false
            return fm.fileExists(atPath: u.path, isDirectory: &d) && d.boolValue
        }
        if url.lastPathComponent == "common" { return (url, true) }
        let steamapps = url.appendingPathComponent("steamapps/common")
        if isDir(steamapps) { return (steamapps, true) }
        let common = url.appendingPathComponent("common")
        if isDir(common) { return (common, true) }
        let parent = url.deletingLastPathComponent()
        if parent.lastPathComponent == "common" { return (parent, true) }
        return (url, false)
    }

    /// Libraries the Steam inside each CrossOver bottle knows about.
    ///
    /// The bottle is the only place worth looking. Windows games installed
    /// under CrossOver are managed by the Steam running inside it, and the
    /// native macOS Steam does not record those libraries at all -- trusting
    /// it would find nothing and report, wrongly, that the user owns none of
    /// the supported games.
    ///
    /// These roots are read and never shown. They are the user's storage
    /// layout, unmounted volumes included; only recognised games belong on
    /// screen.
    static func discover() -> [URL] {
        let fm = FileManager.default
        let bottles = (try? fm.contentsOfDirectory(
            at: URL(fileURLWithPath: NSHomeDirectory())
                .appendingPathComponent("Library/Application Support/CrossOver/Bottles"),
            includingPropertiesForKeys: nil)) ?? []

        var roots: [URL] = []
        for bottle in bottles {
            let vdf = bottle.appendingPathComponent(
                "drive_c/Program Files (x86)/Steam/config/libraryfolders.vdf")
            guard let text = try? String(contentsOf: vdf, encoding: .utf8) else { continue }
            for windowsPath in paths(in: text) {
                guard let mac = translate(windowsPath, inBottle: bottle) else { continue }
                let common = mac.appendingPathComponent("steamapps/common")
                var d: ObjCBool = false
                if fm.fileExists(atPath: common.path, isDirectory: &d), d.boolValue {
                    roots.append(common)
                }
            }
        }
        // Several bottles usually share one library.
        var seen = Set<String>()
        return roots.filter { seen.insert($0.standardizedFileURL.path).inserted }
    }

    /// The "path" values out of a libraryfolders.vdf. Backslashes arrive
    /// doubled, as the file is written escaped.
    private static func paths(in vdf: String) -> [String] {
        var found: [String] = []
        for line in vdf.split(separator: "\n") {
            let parts = line.split(separator: "\"", omittingEmptySubsequences: false)
            // "path"<tab>"VALUE"  ->  ["", "path", "\t", "VALUE", ""]
            guard parts.count >= 4, parts[1] == "path" else { continue }
            found.append(String(parts[3]).replacingOccurrences(of: "\\\\", with: "\\"))
        }
        return found
    }

    /// A Windows path to a macOS one, by reading the bottle's own drive map.
    ///
    /// Read rather than assumed: Z: is the root here but Y: is the home
    /// directory, so hardcoding either would silently resolve to the wrong
    /// place on someone else's machine.
    private static func translate(_ windowsPath: String, inBottle bottle: URL) -> URL? {
        guard windowsPath.count >= 2, windowsPath[windowsPath.index(windowsPath.startIndex, offsetBy: 1)] == ":"
        else { return nil }
        let letter = String(windowsPath.first!).lowercased()
        let link = bottle.appendingPathComponent("dosdevices/\(letter):")
        guard let target = try? FileManager.default.destinationOfSymbolicLink(atPath: link.path)
        else { return nil }

        let base = target.hasPrefix("/")
            ? URL(fileURLWithPath: target)
            : bottle.appendingPathComponent("dosdevices").appendingPathComponent(target)
        let rest = windowsPath.dropFirst(2)
            .replacingOccurrences(of: "\\", with: "/")
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        return rest.isEmpty ? base.standardizedFileURL
                            : base.appendingPathComponent(rest).standardizedFileURL
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
                if runner.legacyReencode != .notApplied { legacyBanner }
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

    /// Only ever seen by someone who ran the re-encode mode before it was
    /// removed. Their cutscenes are H.264 and their pak index is edited, and
    /// the runtime patch cannot go in on top of that, so this is the way out.
    private var legacyBanner: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "clock.arrow.circlepath")
                .foregroundStyle(.orange)
            VStack(alignment: .leading, spacing: 4) {
                Text("Re-encoded cutscenes from an older version")
                    .font(.callout.weight(.medium))
                Text("This copy still has H.264 cutscenes and an edited pak index. "
                   + "That mode has been removed — the runtime patch replaces it and "
                   + "leaves your original VP9 files alone. Undo it to switch over.")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer()
            Button("Undo") { runner.undoLegacyReencode() }
                .disabled(runner.busy)
        }
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 10)
            .fill(Color.orange.opacity(0.08)))
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
