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
        // Lets the shell installers phrase their advice for someone looking at
        // this window rather than a terminal: the app streams their output
        // into its own log, so "run this script" is the wrong thing to read
        // next to a button that does it.
        env["MGVF_FRONTEND"] = "app"
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
/// in a WebM container, so installing either would look like it worked and
/// change nothing on screen. Better to say so first.
/// What this needs from CrossOver, and what it does not.
///
/// needs it any more -- Preview decodes VP9, H.264 and AAC on its own -- and
/// warning about something irrelevant is worse than not warning at all: it
/// sends people to install something that will not change their problem.
///
/// Persona 5 Strikers does need a VC-1 decoder CrossOver does not ship, and
/// that is said on its own row rather than as a blanket requirement.
enum Requirements {
    static var note: String {
        // Most, not all: five of these were validated on Preview, which decodes
        // their formats itself. Persona 5 Strikers stages its own decoder and
        // so depends on no engine in particular -- it is the one measured on a
        // stable build too.
        "CrossOver 26.2 or later on Apple Silicon. Nothing else, for most games."
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
    case beastOfReincarnation
    case lisReunion
    case lisDoubleExposure
    case unrealOther
    case dynastyWarriors
    case personaStrikers

    var id: String { rawValue }

    var name: String {
        switch self {
        case .mortalShell2:      return "Mortal Shell 2"
        case .beastOfReincarnation: return "Beast of Reincarnation"
        case .lisReunion:        return "Life is Strange: Reunion"
        case .lisDoubleExposure: return "Life is Strange: Double Exposure"
        case .unrealOther:       return "Another Unreal Engine 5 title"
        case .dynastyWarriors:   return "DYNASTY WARRIORS: ORIGINS"
        case .personaStrikers:   return "Persona 5 Strikers"
        }
    }

    var symptom: String {
        switch self {
        case .mortalShell2:      return "Crash on the first cutscene"
        case .beastOfReincarnation: return "Startup video plays with sound, no picture"
        case .lisReunion,
             .lisDoubleExposure: return "Runs, then freezes after a while"
        case .unrealOther:       return "Crash on the first cutscene, or a freeze after a while"
        case .dynastyWarriors:   return "Cutscene plays with sound, picture black"
        case .personaStrikers:   return "Video never starts; sound only"
        }
    }

    /// Steam names install folders after the project rather than the game, so
    /// checking the shipping executable is the only way to tell someone they
    /// picked Reunion's folder while Double Exposure was selected.
    var executable: String? {
        switch self {
        case .mortalShell2:      return "MortalShell2-Win64-Shipping.exe"
        case .beastOfReincarnation: return "BeastOfReincarnation-Win64-Shipping.exe"
        case .lisReunion:        return "Iris-Win64-Shipping.exe"
        case .lisDoubleExposure: return "Chronos-Win64-Shipping.exe"
        case .unrealOther:       return nil
        case .dynastyWarriors:   return "DWORIGINS.exe"
        case .personaStrikers:   return "game.exe"
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
        case .beastOfReincarnation: return "…/steamapps/common/BeastOfReincarnation"
        case .lisReunion:        return "…/steamapps/common/LifeisStrangeReunion"
        case .lisDoubleExposure: return "…/steamapps/common/LifeIsStrangeDoubleExposure"
        case .unrealOther:       return "…/steamapps/common/<Game>"
        case .dynastyWarriors:   return "…/steamapps/common/DWORIGINS"
        case .personaStrikers:   return "…/steamapps/common/P5S"
        }
    }

    var modes: [Mode] {
        switch self {
        case .dynastyWarriors, .personaStrikers: return [.videoBridge]
        default:                                 return [.runtime]
        }
    }

    /// The script that installs this game's fix.
    ///
    /// Three of them now, and which one a game needs is a property of the game
    /// rather than of the mode: DYNASTY WARRIORS and Persona 5 Strikers both
    /// ride a bridge, on different carrier DLLs and with different code.
    var installer: String {
        switch self {
        case .dynastyWarriors: return "install-dwo-bridge.sh"
        case .personaStrikers: return "install-p5s-bridge.sh"
        default:               return "install-runtime-fix.sh"
        }
    }

    /// Said on a row that installs cleanly and still will not play.
    ///
    /// Persona 5 Strikers is the only game here needing a codec CrossOver does
    /// not ship. The bridge goes in either way; without the codec there is
    /// nothing for it to carry.
    var extraRequirement: String? {
        if case .personaStrikers = self {
            return "Also needs the VC-1 codec staged."
        }
        return nil
    }

    /// Said when the folder is not the one this game needs. Naming the thing
    /// that was missing beats saying the folder is wrong.
    var mismatch: String {
        switch self {
        case .dynastyWarriors:
            return "No DWORIGINS.exe there. Pick the folder the game's executable is in."
        case .personaStrikers:
            return "No game.exe and data/pd there. Pick the folder Persona 5 Strikers is in."
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
        if case .personaStrikers = self {
            var candidates = [url]
            if let subs = try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil) {
                candidates += subs
            }
            for c in candidates
            where fm.fileExists(atPath: c.appendingPathComponent("game.exe").path)
               && fm.fileExists(atPath: c.appendingPathComponent("data/pd").path) {
                return .bridgeGame(c, .personaStrikers)
            }
            return nil
        }
        if case .dynastyWarriors = self {
            var candidates = [url]
            if let subs = try? fm.contentsOfDirectory(at: url, includingPropertiesForKeys: nil) {
                candidates += subs
            }
            for c in candidates
            where fm.fileExists(atPath: c.appendingPathComponent("DWORIGINS.exe").path) {
                return .bridgeGame(c, .dynastyWarriors)
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
    case unrealVP9(GameFolder)          // any UE5 title with VP9 cutscenes
    case bridgeGame(URL, SupportedGame) // a title with its own video bridge

    var name: String {
        switch self {
        case .unrealVP9:      return "Unreal Engine title with VP9 cutscenes"
        case .bridgeGame(_, let g): return g.name
        }
    }

    var path: String {
        switch self {
        case .unrealVP9(let g):    return g.root.path
        case .bridgeGame(let u, _): return u.path
        }
    }

    var modes: [Mode] {
        switch self {
        case .unrealVP9:       return [.runtime]
        case .bridgeGame:      return [.videoBridge]
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
            return .bridgeGame(c, .dynastyWarriors)
        }
        for c in candidates
        where fm.fileExists(atPath: c.appendingPathComponent("game.exe").path)
           && fm.fileExists(atPath: c.appendingPathComponent("data/pd").path) {
            return .bridgeGame(c, .personaStrikers)
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
    /// The folder of a title fixed by its own bridge, and which title it is --
    /// there are two of them now, on different carrier DLLs.
    var bridgeFolder: (url: URL, game: SupportedGame)? {
        if case .bridgeGame(let u, let g) = title { return (u, g) }
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
        case .unrealVP9(let g):        await inspect(g)
        case .bridgeGame(let u, let g): await inspectBridge(u, g)
        case .none:                    break
        }
    }

    private func inspectBridge(_ folder: URL, _ game: SupportedGame) async {
        let script = resources.appendingPathComponent(game.installer).path
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
            status = "This copy has nothing for the bridge to ride on."
            return
        }
        switch statusAnswer.split(separator: " ", maxSplits: 1).first.map(String.init) {
        case "installed": bridgeState = .applied
        case "half":      bridgeState = .partial
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
        case "half":      runtimeState = .partial
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
        guard let (folder, game) = bridgeFolder else { return }
        busy = true
        Task {
            defer { busy = false; indeterminate = false; phaseLabel = ""; detail = "" }
            status = install ? "Working…" : "Reverting…"
            progress = 0

            let script = resources.appendingPathComponent(game.installer).path
            let args = install ? [script, folder.path] : [script, folder.path, "--restore"]
            let ok = await run(install ? .installingBridge : .removingBridge, "/bin/bash", args)

            progress = 1
            note("")
            if ok && install {
                note("Done. Launch the game — the cutscenes should play.")
                if let extra = game.extraRequirement { note(extra) }
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
    /// The folder step 2 will scan, and what step 3 belongs to. nil is step 1,
    /// which is a normal place to be and not an error.
    @Published var libraryRoot: URL?
    /// The chosen folder did not look like a Steam library. Not a refusal --
    /// it is still scanned as it is, exactly as before; this is only what lets
    /// the screen say so, and what makes an empty result explain itself.
    @Published var libraryIsOdd = false
    /// nil means no scan has run against the folder currently chosen.
    ///
    /// The empty string used to carry that meaning as well as "a scan is
    /// running right now" and "a scan finished and found nothing", which is why
    /// the screen could never say which of the three it was in.
    @Published var scanOutcome: ScanOutcome?
    /// nil means the bottles have not been asked yet.
    @Published var survey: LibrarySurvey?
    @Published var lookingForLibraries = false
    @Published var batchStep = ""
    /// Honoured between games, never inside one. Interrupting an installer
    /// mid-rename is what manufactures the state that destroys an original.
    @Published var stopping = false

    var selectedHits: [ScanHit] { plan.filter { $0.selected && $0.actionable } }

    /// The scan in flight, and the number that says whether it still speaks for
    /// the folder currently chosen.
    ///
    /// A scan takes seconds -- it asks each installer what it sees, which is a
    /// process per hit -- and every way out of step 2 is a way to change the
    /// folder its results would belong to. Without this the Task outlives the
    /// folder and writes a plan into a flow that has been reset or repointed:
    /// step 1 naming one library while step 3 tabulates another, which is the
    /// exact contradiction the three steps exist to prevent. Disabling buttons
    /// is not enough on its own, because a disabled state is decided at render
    /// time and only covers the paths that exist today.
    private var scanTask: Task<Void, Never>?
    private var scanGeneration = 0

    /// Does this scan still speak for the folder currently chosen? A method on
    /// the Runner rather than a function nested in the Task, because a nested
    /// func does not inherit the actor and cannot read the counter.
    private func scanIsCurrent(_ generation: Int) -> Bool {
        generation == scanGeneration
    }

    /// Abandons an in-flight scan. Called from every path that changes or
    /// clears the folder, so results can never outlive what they describe.
    private func cancelScan() {
        scanTask?.cancel()
        scanTask = nil
        scanGeneration &+= 1
        scanning = false
        indeterminate = false
    }

    /// Derived, never stored. A stored step would be a fourth thing to keep in
    /// sync with the folder, the outcome and the plan, and the first time it
    /// disagreed with them the screen would be lying about where the user is.
    var bulkStep: BulkStep {
        if libraryRoot == nil { return .choose }
        if scanOutcome == nil { return .scan }
        return .patch
    }

    /// A scan ran against the current folder and produced no rows. Distinct
    /// from not having scanned at all, which is the confusion this whole flow
    /// exists to end.
    var scanFoundNothing: Bool {
        switch scanOutcome {
        case .some(.nothing(_)):   return true
        case .some(.games), .none: return false
        }
    }

    /// Step 3's one-line headline, and the only reader of `scanOutcome`'s
    /// wording. Every ending has its own sentence.
    var scanHeadline: String {
        if scanning { return "Scanning…" }
        switch scanOutcome {
        case .none:
            return "Nothing scanned yet."
        case .some(.games(let n)):
            return "\(n) supported game\(n == 1 ? "" : "s") found"
        case .some(.nothing(.noSupportedGames)):
            // Calling the folder a library here, one line above the sentence
            // saying it does not look like one, is the panel contradicting
            // itself about the thing the user is trying to get right.
            return libraryIsOdd ? "No supported game in that folder"
                                : "No supported game in this library"
        case .some(.nothing(.volumeNotMounted(let v))):
            return "“\(v)” is not connected"
        case .some(.nothing(.folderGone)):
            return "That folder is not there any more"
        case .some(.nothing(.unreadable)):
            return "macOS did not allow reading that folder"
        case .some(.nothing(.notAFolder)):
            return "That is a file, not a folder"
        }
    }

    /// Stages the codec and points every bottle that runs a game at it.
    func stageCodecs() {
        guard Codecs.gstreamerInstalled else {
            note("")
            note("GStreamer is not installed. Get the macOS runtime package")
            note("(1.24 series) and run this again:")
            note("  \(Codecs.downloadPage)")
            note("Nothing is redistributed here -- the decoder is borrowed from")
            note("your own install, which is how winevideo does it too.")
            status = "GStreamer missing."
            return
        }
        busy = true
        Task {
            defer { busy = false; indeterminate = false; phaseLabel = "" }
            if let v = Codecs.version {
                note("GStreamer \(v) found"
                     + (Codecs.versionIsTested ? ""
                        : " — 1.24.14 is the verified one; carrying on with yours"))
            }
            status = "Staging the codec…"
            let script = resources.appendingPathComponent("stage-codecs.sh").path
            let ok = await run(.installingBridge, "/bin/bash", [script, "x86_64"])
            guard ok else { status = "Staging failed."; return }

            var touched: [String] = []
            for bottle in Bottle.candidates(forProject: "P5S").map(\.url)
                        + ((try? FileManager.default.contentsOfDirectory(
                              at: Bottle.root, includingPropertiesForKeys: nil)) ?? []) {
                if FileManager.default.fileExists(
                    atPath: bottle.appendingPathComponent("cxbottle.conf").path),
                   Codecs.configure(bottle: bottle) == nil {
                    touched.append(bottle.lastPathComponent)
                }
            }
            note("")
            Codecs.refresh()
            note("Codec staged, and \(touched.count) bottle(s) pointed at it.")
            note("Close Steam completely before relaunching: this is bottle")
            note("configuration, and a live wineserver keeps the old copy.")
            status = "Codec ready."
        }
    }

    func enterBulk() {
        cancelScan()
        bulk = true
        plan = []
        libraryRoot = nil
        libraryIsOdd = false
        scanOutcome = nil
        log.removeAll()
        resetProgress()
        status = "Step 1 of 3: choose your Steam library folder."
        // A convenience inside step 1, not a step of its own: it fills the
        // folder in, it does not act on it.
        findLibraries()
    }

    func leaveBulk() {
        // Deliberately reachable during a scan, and deliberately not a disabled
        // button: a probe that hangs would otherwise leave step 2 with no way
        // out at all. Cancelling here is what makes leaving safe.
        cancelScan()
        bulk = false
        plan = []
        batchStep = ""
        libraryRoot = nil
        libraryIsOdd = false
        scanOutcome = nil
        survey = nil
        status = "Choose your game folder to begin."
    }

    /// Step 1, and only step 1.
    ///
    /// Choosing a folder used to start a scan in the same gesture, which is
    /// what made "nothing was found" impossible to tell apart from "nothing was
    /// ever looked at". The folder is recorded and the user presses Scan.
    func chooseLibrary(_ url: URL) {
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: url.path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
            // Only a drop reaches this: the panel is set to folders only. Saying
            // it now beats accepting it, calling it chosen, and then reporting
            // at scan time that a file the user can see is not there any more.
            status = "That is a file, not a folder. Drop the folder it sits in."
            return
        }
        cancelScan()
        let (root, looksLikeLibrary) = SteamLibrary.normalise(url)
        libraryRoot = root
        libraryIsOdd = !looksLikeLibrary
        plan = []
        scanOutcome = nil
        status = looksLikeLibrary
            ? "Step 2 of 3: scan this folder for games."
            : "That folder does not look like a Steam library. Scan it anyway, or change it."
    }

    /// A discovered library's `common` passes through `normalise` unchanged by
    /// its first rule, so this is the same choice made from a row.
    func chooseLibrary(_ lib: DiscoveredLibrary) {
        // A second lock behind the disabled row. A disk that went away between
        // the survey and the click must not become step 2's problem to explain.
        guard lib.ready else { return }
        chooseLibrary(lib.common)
    }

    /// Back to step 1 with all three ways in. The plan goes with it: a plan
    /// belongs to the folder it came from, and keeping it beside a different
    /// folder is how a table starts describing somewhere it never looked.
    func clearLibrary() {
        cancelScan()
        libraryRoot = nil
        libraryIsOdd = false
        scanOutcome = nil
        plan = []
        status = "Step 1 of 3: choose your Steam library folder."
        // The survey is a snapshot: whether each row was ready, and whether its
        // volume was mounted, were decided when it ran. Someone who plugs in a
        // drive because this app just told them to and presses Change would
        // otherwise meet the same dimmed row saying it is not connected.
        findLibraries()
    }

    /// Asks the bottles which libraries they know about.
    ///
    /// Off the main actor and never called from a view body: it lists every
    /// bottle, reads a vdf out of each and stats one directory per library.
    private var surveyTask: Task<Void, Never>?

    func findLibraries() {
        guard !lookingForLibraries else { return }
        lookingForLibraries = true
        surveyTask = Task {
            // Cleared on every exit, cancellation included. A flag set here and
            // cleared only on the happy path is how a spinner outlives its work
            // and leaves someone watching an app that is doing nothing.
            defer { lookingForLibraries = false; surveyTask = nil }
            let found = await Task.detached { SteamLibrary.survey() }.value
            guard !Task.isCancelled else { return }
            survey = found
        }
    }

    /// Abandons the search. It is quick, but "quick" is a measurement from this
    /// machine, and a disconnected network volume is not obliged to agree.
    func stopLooking() {
        surveyTask?.cancel()
        surveyTask = nil
        lookingForLibraries = false
    }

    /// Abandons a scan from the UI. `cancelScan` is the private version the
    /// folder-changing paths use; this is the button.
    func stopScan() {
        cancelScan()
        status = "Scan stopped."
    }

    /// Step 2. Scans the one folder step 1 chose, and there is no other way in
    /// -- the guard is the single place "no scan without a folder" is enforced.
    func startScan() {
        guard let root = libraryRoot else {
            status = "Choose a Steam library folder first."
            return
        }
        // The button is disabled while a scan runs, but a disabled state is
        // decided at render time: two activations delivered before a re-render
        // both arrive here. Two scans share one `statusAnswer`, so each would
        // read the other's answer and label rows with it.
        guard !scanning, !busy else { return }

        scanGeneration &+= 1
        let generation = scanGeneration
        scanning = true
        plan = []
        scanOutcome = nil
        log.removeAll()
        // Not just `indeterminate = true`: a finished install leaves the bar
        // full, and "Scan again" would redraw that install's bar underneath a
        // scan that has nothing to do with it.
        resetProgress()
        status = "Scanning…"
        scanTask = Task {
            // Only the scan that still speaks for the chosen folder is allowed
            // to touch shared state -- including on the way out.
            defer {
                if generation == scanGeneration {
                    scanning = false; indeterminate = false; scanTask = nil
                }
            }
            // Checked before the first line of log, not only before the first
            // published result: a Task is not guaranteed to have started before
            // the folder changes underneath it.
            guard scanIsCurrent(generation) else { return }

            if libraryIsOdd {
                note("That folder does not look like a Steam library; scanning it as it is.")
            }
            note("Scanning \(root.path)")

            // Asked before recognise() rather than inside it. To a function
            // that reports only recognised games, a root it could not open and
            // a root holding none of the six are the same empty list.
            let bad = await Task.detached(operation: { SteamLibrary.readability(of: root) }).value
            guard scanIsCurrent(generation) else { return }
            if let bad {
                scanOutcome = .nothing(bad)
                status = "Nothing to do here."
                note(bad.logLine)
                return
            }

            let found = await Task.detached { Self.recognise(in: [root]) }.value
            guard scanIsCurrent(generation) else { return }
            guard !found.isEmpty else {
                scanOutcome = .nothing(.noSupportedGames)
                status = "Nothing to do here."
                note("No supported game found.")
                return
            }

            var probed: [ScanHit] = []
            for hit in found {
                // Checked between hits so an abandoned scan stops spawning
                // processes rather than merely discarding what they said.
                guard scanIsCurrent(generation) else { return }
                probed.append(await probe(hit))
            }
            guard scanIsCurrent(generation) else { return }
            plan = probed
            scanOutcome = .games(probed.count)

            // Four endings, not two. A row carrying a blocker is in neither
            // count -- it cannot be fixed and is not fixed -- so a library
            // holding one applied game and one blocked one used to be announced
            // as entirely fixed, directly above a row saying it was not.
            let ready = probed.filter { $0.actionable && $0.state != .applied }.count
            let done = probed.filter { $0.actionable && $0.state == .applied }.count
            let blocked = probed.count - ready - done
            let cannot = "\(blocked) cannot be fixed as \(blocked == 1 ? "it is" : "they are"). "
                       + "Each row says why."
            if ready > 0 {
                status = "\(ready) can be fixed now." + (blocked > 0 ? " " + cannot : "")
            } else if done > 0 && blocked > 0 {
                status = "\(done) already fixed; " + cannot
            } else if done > 0 {
                status = "Everything here is already fixed."
            } else {
                status = "None of these can be fixed as they are. Each row says why."
            }
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
        let script = resources.appendingPathComponent(hit.game.installer).path

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
            hit.blocker = "This copy has no carrier DLL for the fix to ride on."
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
        // Same reason as `startScan`'s: the confirm button's disabled state is
        // decided at render time, and two runs over one plan would fight over
        // every row they share.
        guard !busy else { return }
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

                let script = resources.appendingPathComponent(hit.game.installer).path
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

// MARK: - The codec Persona 5 Strikers needs

/// Staging a decoder CrossOver does not ship, and telling the bottle about it.
///
/// Only one game here needs this, and it needs it absolutely: without a VC-1
/// decoder there is nothing for its bridge to carry. Nothing is redistributed
/// -- the plugin is borrowed from the official GStreamer install the user
/// already has, which is also how winevideo does it. What differs is only that
/// this stages a folder and writes one line of bottle configuration instead of
/// patching the CrossOver installation.
enum Codecs {
    static let framework = "/Library/Frameworks/GStreamer.framework"

    /// Read once and remembered, for the same reason `version` is: this is
    /// consulted from a SwiftUI body, and a body is re-evaluated on every
    /// published change -- which during an install means once per line the
    /// installer prints. `codecBanner` asks this and `staged` seven times
    /// between them, so uncached it is seven stats per log line.
    private static var cachedFramework: Bool?

    static var gstreamerInstalled: Bool {
        if let c = cachedFramework { return c }
        let found = FileManager.default.fileExists(atPath: framework)
        cachedFramework = found
        return found
    }

    /// The installed version, read from the library's compatibility number
    /// rather than a plist -- it encodes 1.MINOR.PATCH directly.
    ///
    /// winevideo specifies 1.24.13 for exactly these titles. 1.24.14 is what
    /// is measured working here, so what actually has to hold is the 1.24
    /// series rather than the exact patch: the plugin must be ABI-compatible
    /// with the CrossOver core it is re-homed onto, which GStreamer guarantees
    /// across 1.x. Anything else is reported, not refused.
    /// Read once and remembered. This is a computed property consulted from a
    /// SwiftUI body, and a body is re-evaluated on every published change --
    /// which during a scan means continuously. Spawning otool at that rate made
    /// scanning visibly slow, and only when the codec was NOT staged, because
    /// that is the only branch of codecMessage that asks for the version.
    private static var cached: String??

    static var version: String? {
        if let c = cached { return c }
        let v = readVersion()
        cached = v
        return v
    }

    /// Drops everything remembered here, so "Check again" genuinely checks
    /// again after the user installs GStreamer, and so the survey is re-read
    /// once staging has pointed new bottles at the codec.
    static func refresh() {
        cached = nil
        cachedSurvey = nil
        cachedChoices = nil
        cachedEngines = nil
        cachedEngineNames = nil
        cachedFramework = nil
        cachedStaged = nil
    }

    private static func readVersion() -> String? {
        let lib = (framework as NSString)
            .appendingPathComponent("Versions/1.0/lib/libgstreamer-1.0.0.dylib")
        guard FileManager.default.fileExists(atPath: lib) else { return nil }
        let task = Process()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/otool")
        task.arguments = ["-L", lib]
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError = Pipe()
        guard (try? task.run()) != nil else { return nil }
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        task.waitUntilExit()
        guard let text = String(data: data, encoding: .utf8),
              let range = text.range(of: "compatibility version "),
              let compat = Int(text[range.upperBound...].prefix(while: \.isNumber)),
              compat > 0
        else { return nil }
        return "1.\(compat / 100).\(compat % 100)"
    }

    /// Uses the cached read; asking twice in one body evaluation is free.
    static var versionIsTested: Bool { version?.hasPrefix("1.24") ?? false }

    static let downloadPage = "https://gstreamer.freedesktop.org/data/pkg/osx/1.24.14/"

    static var stagedRoot: String {
        (NSHomeDirectory() as NSString)
            .appendingPathComponent("Library/Application Support/MacGameVideoFix/gst-codecs")
    }

    /// One staged directory per engine, keyed by the CFBundleVersion that engine
    /// declares. The support libraries inside are symlinks into that engine's
    /// own bundle, so a directory is bound to the engine it was built for:
    /// pointing a bottle at another one gives dyld two GStreamer cores and a
    /// crash, which is the failure this whole arrangement exists to avoid.
    static func stagedPath(forEngine version: String) -> String {
        ((stagedRoot as NSString).appendingPathComponent(version) as NSString)
            .appendingPathComponent("x86_64/gstreamer-1.0")
    }

    /// Where the layout before this one put the single staging there was.
    ///
    /// It is ours, but which CrossOver it was built against is not knowable now
    /// -- so a bottle still pointing at it has to be re-pointed rather than
    /// trusted, and that is a different verdict from a value someone else set.
    static var legacyStagedPath: String {
        ((stagedRoot as NSString).appendingPathComponent("x86_64") as NSString)
            .appendingPathComponent("gstreamer-1.0")
    }

    /// Trailing slashes, and nothing that touches the disk. Every path compared
    /// here is one this app wrote; resolving symlinks to be thorough would mean
    /// a stat per bottle inside the survey, for a case that does not occur.
    private static func tidy(_ path: String) -> String {
        var p = path
        while p.count > 1, p.hasSuffix("/") { p.removeLast() }
        return p
    }

    /// The engine a GST_PLUGIN_PATH was staged for, or nil when the path is not
    /// one of the per-engine directories.
    ///
    /// The test is the shape of the first component under `stagedRoot`: dot
    /// separated digits, which is what a CFBundleVersion looks like. A string
    /// test rather than a stat, deliberately -- an engine the user has since
    /// uninstalled stays nameable, so the row can say which CrossOver to put
    /// back instead of calling the path unrecognisable; and the old layout's
    /// "x86_64" fails it, which is how the two are told apart without asking
    /// the disk anything.
    static func engine(ofStagedPath path: String) -> String? {
        let root = tidy(stagedRoot)
        let full = tidy(path)
        guard full.hasPrefix(root + "/") else { return nil }
        guard let first = full.dropFirst(root.count + 1).split(separator: "/").first
        else { return nil }
        let parts = first.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count >= 2,
              parts.allSatisfy({ !$0.isEmpty && $0.allSatisfy(\.isNumber) })
        else { return nil }
        return String(first)
    }

    /// Is this a value this app wrote -- either layout? What decides whether a
    /// GST_PLUGIN_PATH may be replaced without asking.
    static func isOurs(_ path: String) -> Bool {
        engine(ofStagedPath: path) != nil || tidy(path) == tidy(legacyStagedPath)
    }

    /// The plugin, and something for it to resolve through @loader_path/../lib.
    ///
    /// Both halves, because stage-codecs.sh copies the plugin before the loop
    /// that fills lib/ -- and staging can now be stopped, which makes that
    /// half-finished directory reachable on purpose rather than only after a
    /// crash. A plugin with nothing to link against is not a staging, and
    /// pointing a bottle at it is the same silence as pointing it nowhere.
    static func staged(forEngine version: String) -> Bool {
        let fm = FileManager.default
        let dir = stagedPath(forEngine: version)
        guard fm.fileExists(
            atPath: (dir as NSString).appendingPathComponent("libgstlibav.dylib"))
        else { return false }
        let lib = ((dir as NSString).deletingLastPathComponent as NSString)
            .appendingPathComponent("lib")
        return !((try? fm.contentsOfDirectory(atPath: lib))?.isEmpty ?? true)
    }

    private static var cachedEngines: [String: URL]?
    private static var cachedEngineNames: [String: String]?

    /// Every CrossOver on this Mac, keyed by CFBundleVersion.
    ///
    /// Found by what the bundle declares rather than by what it is called. One
    /// of the installs this was fixed on is a Preview build living in
    /// Crossover_patched.app, which no search for "CrossOver Preview.app" would
    /// ever have seen -- and staging against the wrong engine does not warn,
    /// it crashes.
    static func installedEngines() -> [String: URL] {
        if let c = cachedEngines { return c }
        let fm = FileManager.default
        var found: [String: URL] = [:]
        var names: [String: String] = [:]
        // /Applications only. A CrossOver in ~/Applications is as likely to be
        // a copy kept for an experiment as one someone actually runs games
        // with, and staging against an engine the user does not use is work
        // that can only mislead.
        for dir in ["/Applications"] {
            for app in (try? fm.contentsOfDirectory(
                            at: URL(fileURLWithPath: dir),
                            includingPropertiesForKeys: nil)) ?? [] {
                guard app.pathExtension == "app",
                      fm.fileExists(atPath: app.appendingPathComponent(
                          "Contents/SharedSupport/CrossOver").path),
                      let plist = NSDictionary(contentsOf: app.appendingPathComponent(
                          "Contents/Info.plist")),
                      let version = plist["CFBundleVersion"] as? String
                else { continue }
                found[version] = app
                names[version] = (plist["CFBundleName"] as? String)
                    ?? app.deletingPathExtension().lastPathComponent
            }
        }
        cachedEngines = found
        cachedEngineNames = names
        return found
    }

    /// "CrossOver Preview (27.0.0.40921)" -- the name the bundle declares, and
    /// the version, because two installs can call themselves the same thing and
    /// only the version says which staging a bottle needs. An engine that is no
    /// longer on this Mac still gets a name, so a row about it can be read.
    /// No extra IO: the names come from the same pass that found the bundles.
    static func label(_ version: String) -> String {
        _ = installedEngines()
        guard let name = cachedEngineNames?[version] else { return "CrossOver \(version)" }
        return "\(name) (\(version))"
    }

    private static var cachedStaged: Bool?

    /// True when every installed engine has a staging of its own.
    ///
    /// Deliberately strict: installing a second CrossOver later turns the banner
    /// orange again, which is right, because a bottle migrated to that engine
    /// would otherwise be pointed at a directory built for a different one.
    static var staged: Bool {
        if let c = cachedStaged { return c }
        let engines = installedEngines().keys
        let found = !engines.isEmpty && engines.allSatisfy { staged(forEngine: $0) }
        cachedStaged = found
        return found
    }

    /// The engine a bottle last ran under: CrossOver stamps its own
    /// CFBundleVersion into cxbottle.conf as "Version", and re-stamps it when
    /// another install migrates the bottle. That makes it the right signal --
    /// it follows the engine that actually runs the games, not the one that
    /// created the bottle.
    static func engineVersion(ofBottle bottle: URL) -> String? {
        guard let text = try? String(
            contentsOf: bottle.appendingPathComponent("cxbottle.conf"), encoding: .utf8)
        else { return nil }
        for line in text.split(separator: "\n") where line.hasPrefix("\"Version\"") {
            guard let open = line.range(of: "= \""),
                  let close = line.range(of: "\"", range: open.upperBound..<line.endIndex)
            else { continue }
            return String(line[open.upperBound..<close.lowerBound])
        }
        return nil
    }

    /// Bottles that already point at the staged folder.
    /// Cached for the same reason the version is: a SwiftUI body asks for this
    /// on every published change, and answering means reading every bottle's
    /// cxbottle.conf off disk.
    static func bottlesConfigured() -> [String] {
        if let c = cachedBottles { return c }
        let found = readBottlesConfigured()
        cachedBottles = found
        return found
    }

    private static func readBottlesConfigured() -> [String] {
        (try? FileManager.default.contentsOfDirectory(at: Bottle.root, includingPropertiesForKeys: nil))?
            .filter { bottle in
                guard let conf = try? String(contentsOf: bottle.appendingPathComponent("cxbottle.conf"),
                                             encoding: .utf8) else { return false }
                return conf.contains("GST_PLUGIN_PATH")
            }
            .map(\.lastPathComponent) ?? []
    }

    /// Adds GST_PLUGIN_PATH to a bottle, once.
    ///
    /// The bottle's environment is applied before CrossOver's launcher runs,
    /// and the launcher never sets this variable, so the entry survives. A
    /// live wineserver caches the old configuration, which is why the caller
    /// is told to close the game entirely rather than just relaunch it.
    static func configure(bottle: URL) -> String? {
        let conf = bottle.appendingPathComponent("cxbottle.conf")
        guard var text = try? String(contentsOf: conf, encoding: .utf8) else {
            return "Cannot read \(bottle.lastPathComponent)'s configuration."
        }
        guard let engine = engineVersion(ofBottle: bottle) else {
            return "\(bottle.lastPathComponent) does not record which CrossOver it runs under."
        }
        guard staged(forEngine: engine) else {
            return "\(bottle.lastPathComponent) runs under a CrossOver with no staged codec."
        }
        let want = stagedPath(forEngine: engine)

        // Every existing line is stripped and exactly one is inserted.
        //
        // It used to leave an existing key alone, which meant a bottle migrated
        // to another CrossOver kept pointing at the previous engine's directory
        // for good -- the two-cores crash, arriving silently, months later.
        // Replacing in place was the obvious fix and was wrong too: a key
        // outside [EnvironmentVariables] left the file with two, and a config
        // file with two answers has no answer. Stripping first is idempotent.
        text = text.replacingOccurrences(
            of: #"(?m)^"GST_PLUGIN_PATH"[^\n]*\n?"#, with: "", options: .regularExpression)
        guard let range = text.range(of: "[EnvironmentVariables]") else {
            return "\(bottle.lastPathComponent) has no [EnvironmentVariables] section."
        }
        text.replaceSubrange(range,
            with: "[EnvironmentVariables]\n\"GST_PLUGIN_PATH\" = \"\(want)\"")

        do {
            try text.write(to: conf, atomically: true, encoding: .utf8)
            return nil
        } catch {
            return error.localizedDescription
        }
    }
}


// MARK: - Bottles, and the per-game Engine.ini

/// A CrossOver bottle, and the Unreal config that has to live inside one.
///
/// Some Unreal titles need a user `Engine.ini` with Electra's old output path
/// switched on. That file does not live beside the game -- it lives in the
/// bottle, under the *project* name rather than the game's, and a machine
/// usually has several bottles. Getting it into the wrong one is silent: the
/// game reads nothing, behaves exactly as before, and the natural conclusion is
/// that the setting does not help.
///
/// So the bottle is not guessed. The game writes `AppData/Local/<Project>/Saved`
/// the first time it runs, and that folder is the evidence of where it actually
/// runs.
struct Bottle: Identifiable {
    let url: URL
    /// When this game last wrote anything here -- the tell for which bottle is
    /// really in use when more than one has been tried.
    let lastUsed: Date?

    var id: String { url.path }
    var name: String { url.lastPathComponent }

    static var root: URL {
        URL(fileURLWithPath: NSHomeDirectory())
            .appendingPathComponent("Library/Application Support/CrossOver/Bottles")
    }

    /// Bottles this project has actually run in, most recently used first.
    static func candidates(forProject project: String) -> [Bottle] {
        let fm = FileManager.default
        guard let bottles = try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)
        else { return [] }

        var found: [Bottle] = []
        for bottle in bottles {
            let users = bottle.appendingPathComponent("drive_c/users")
            guard let people = try? fm.contentsOfDirectory(at: users, includingPropertiesForKeys: nil)
            else { continue }
            for person in people {
                let saved = person.appendingPathComponent("AppData/Local/\(project)/Saved")
                var isDir: ObjCBool = false
                guard fm.fileExists(atPath: saved.path, isDirectory: &isDir), isDir.boolValue
                else { continue }
                found.append(Bottle(url: bottle, lastUsed: Self.lastWrite(under: saved)))
                break
            }
        }
        return found.sorted { ($0.lastUsed ?? .distantPast) > ($1.lastUsed ?? .distantPast) }
    }

    /// The newest modification anywhere under a folder, shallow enough to stay
    /// quick on a sleeping external drive.
    private static func lastWrite(under url: URL) -> Date? {
        let fm = FileManager.default
        guard let e = fm.enumerator(at: url, includingPropertiesForKeys: [.contentModificationDateKey],
                                    options: [.skipsHiddenFiles]) else { return nil }
        var newest: Date?
        var seen = 0
        for case let f as URL in e {
            seen += 1
            if seen > 400 { break }
            if let d = try? f.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate,
               d > (newest ?? .distantPast) { newest = d }
        }
        return newest
    }

    func engineIni(project: String) -> URL? {
        let fm = FileManager.default
        let users = url.appendingPathComponent("drive_c/users")
        guard let people = try? fm.contentsOfDirectory(at: users, includingPropertiesForKeys: nil)
        else { return nil }
        for person in people {
            let saved = person.appendingPathComponent("AppData/Local/\(project)/Saved")
            if fm.fileExists(atPath: saved.path) {
                return saved.appendingPathComponent("Config/Windows/Engine.ini")
            }
        }
        return nil
    }

    func hasEngineIni(project: String) -> Bool {
        guard let path = engineIni(project: project) else { return false }
        guard let text = try? String(contentsOf: path, encoding: .utf8) else { return false }
        return text.contains("H264UseOldOutputPath=1")
    }

    /// Removes an Engine.ini an older release wrote.
    ///
    /// Nothing needs that file any more: the DLL sets the console variable
    /// itself, which removes the most fragile part of the whole arrangement --
    /// a path depending on the Unreal project name, inside a bottle that had to
    /// be guessed. It is only still findable so a stale one can be cleared.
    @discardableResult
    func removeEngineIni(project: String) -> String? {
        guard let path = engineIni(project: project),
              FileManager.default.fileExists(atPath: path.path) else { return nil }
        do {
            try FileManager.default.setAttributes([.posixPermissions: 0o644],
                                                  ofItemAtPath: path.path)
            try FileManager.default.removeItem(at: path)
            return nil
        } catch {
            return error.localizedDescription
        }
    }

    @discardableResult
    private func writeEngineIni(project: String) -> String? {
        guard let path = engineIni(project: project) else {
            return "This game has not run in \(name) yet, so there is nowhere to put it."
        }
        let fm = FileManager.default
        let dir = path.deletingLastPathComponent()
        do {
            try fm.createDirectory(at: dir, withIntermediateDirectories: true)
            if fm.fileExists(atPath: path.path) {
                try? fm.setAttributes([.posixPermissions: 0o644], ofItemAtPath: path.path)
            }
            try Self.contents.write(to: path, atomically: true, encoding: .utf8)
            try fm.setAttributes([.posixPermissions: 0o444], ofItemAtPath: path.path)
            return nil
        } catch {
            return error.localizedDescription
        }
    }

    static let contents = """
    [SystemSettings]
    Electra.Win.H264UseOldOutputPath=1
    Electra.Win.H265UseOldOutputPath=1
    """
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

/// One Steam library a bottle's Steam knows about, with everything the screen
/// needs to describe it already worked out.
///
/// A bare `URL` was not enough, and the reason is the evening this change came
/// out of. The old discovery returned only the roots whose steamapps/common
/// existed right then, so an external drive that happened to be unplugged and
/// "you own none of these games" arrived as the same empty list and produced
/// the same wrong sentence. A library that cannot be scanned at this moment is
/// still worth showing -- dimmed, named, with the reason beside it -- because
/// the difference between those two is the difference between information and
/// silence.
///
/// Every property below reads stored fields only. Rows are drawn from a
/// SwiftUI body, and a body is re-evaluated on every published change; a
/// `fileExists` in here would put a stat on a spun-down USB disk in that path,
/// which is the mistake `Codecs.version` already made once with a cheaper call.
struct DiscoveredLibrary: Identifiable, Hashable, Sendable {
    /// The steamapps/common folder -- what a scan is actually pointed at.
    let common: URL
    /// The library root above steamapps, which is the path worth showing.
    let root: URL
    /// The /Volumes name this sits on, when it sits on one.
    let volume: String?
    /// The bottle whose Steam declared it.
    let bottle: String
    /// The library lives inside the bottle rather than on a disk the user
    /// chose, so it ranks below the user's own libraries.
    let insideBottle: Bool
    /// steamapps/common was there when the survey ran.
    let ready: Bool
    /// The volume it names is not mounted. Decided at survey time, once.
    let volumeMissing: Bool

    var id: String { common.path }

    var title: String {
        // The bottle's name is deliberately not the heading. CrossOver names a
        // bottle after whatever was installed into it, so a bottle holding a
        // game this app does not support would put that game's name on screen
        // as a row title -- and nothing here may name the user's other games.
        // The storage path below is a different thing: it is a folder the user
        // can point at again, which is what makes it fair to show.
        if insideBottle { return "Steam inside a CrossOver bottle" }
        if let volume { return "\(volume) — \(root.lastPathComponent)" }
        return root.lastPathComponent
    }

    var shownPath: String { root.path }

    /// Why this row cannot be used, in the user's terms. nil when it can.
    var blocker: String? {
        if volumeMissing {
            return "“\(volume ?? root.lastPathComponent)” is not connected. "
                 + "Plug it in, then press Look again."
        }
        if !ready {
            return "Steam records this library, but there is no steamapps/common in it."
        }
        return nil
    }

    var actionWord: String {
        if ready { return "Use" }
        return volumeMissing ? "Not mounted" : "Unavailable"
    }

    var icon: String {
        if volumeMissing { return "externaldrive.badge.xmark" }
        if !ready { return "questionmark.folder" }
        if insideBottle { return "shippingbox" }
        return volume == nil ? "internaldrive" : "externaldrive"
    }

    /// Usable libraries on the user's own disks first, then the ones that need
    /// a drive plugged in, then the copies of Steam inside the bottles -- which
    /// are real, and are almost never where the games are.
    var rank: Int {
        switch (ready, insideBottle) {
        case (true, false):  return 0
        case (false, false): return 1
        case (true, true):   return 2
        case (false, true):  return 3
        }
    }
}

/// What the bottles had to say, including when that was nothing.
struct LibrarySurvey: Sendable {
    let libraries: [DiscoveredLibrary]
    /// Set only when `libraries` is empty, and says which kind of empty.
    let nothing: NoLibraries?
}

/// The three ways the bottles can come back with no libraries. They need
/// different sentences because they need different actions from the user.
enum NoLibraries: Sendable {
    case noBottles
    case noSteamConfig
    case nothingDeclared
}

/// How a scan ended. `nil` -- no value at all -- means no scan has run against
/// the folder currently chosen, which is a normal starting state and not one of
/// these.
enum ScanOutcome: Equatable, Sendable {
    case games(Int)
    case nothing(Empty)

    /// Every way a scan can come back with no rows. They used to be one empty
    /// list and one sentence about the user's games that was never checked.
    enum Empty: Equatable, Sendable {
        case noSupportedGames
        case volumeNotMounted(String)
        case folderGone
        /// The chosen path exists and is not a directory. Its own case because
        /// "that folder is not there any more" about a file the user is looking
        /// at on their desktop is simply untrue.
        case notAFolder
        case unreadable

        /// One line for the log, in the same words as the panel on screen.
        var logLine: String {
            switch self {
            case .noSupportedGames:        return "No supported game found."
            case .volumeNotMounted(let v): return "“\(v)” is not connected."
            case .folderGone:              return "That folder is not there any more."
            case .notAFolder:              return "That is a file, not a folder."
            case .unreadable:              return "macOS did not allow reading that folder."
            }
        }
    }
}

/// Where the bulk flow is. Derived from the state that already exists, never
/// stored -- see `Runner.bulkStep`.
enum BulkStep: Int {
    case choose = 1
    case scan = 2
    case patch = 3
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
        /// Two layouts, and getting them the wrong way round loses a game
        /// silently. Unreal titles keep the shipping binary at
        /// <folder>/<Project>/Binaries/Win64; the Koei Tecmo ones put it in the
        /// folder itself. Persona 5 Strikers was missing from every scan
        /// because it was being looked for in the Unreal place.
        switch self {
        case .dynastyWarriors, .personaStrikers:
            return FileManager.default.fileExists(
                atPath: folder.appendingPathComponent(exe).path)
        default:
            return GameFolder(root: folder).hasExecutable(exe)
        }
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
    /// These roots are offered as choices, so the paths themselves reach the
    /// screen. They are folders the user picked as storage and can point at
    /// again; what is installed inside one is a different thing entirely and
    /// still never appears.
    ///
    /// Nothing here is dropped for being unreachable. The old version kept only
    /// the libraries whose steamapps/common existed, which is why an unplugged
    /// external drive and an empty library were the same answer.
    static func survey() -> LibrarySurvey {
        let fm = FileManager.default
        let bottles = ((try? fm.contentsOfDirectory(
            at: Bottle.root, includingPropertiesForKeys: nil)) ?? [])
            .filter { fm.fileExists(atPath: $0.appendingPathComponent("cxbottle.conf").path) }

        guard !bottles.isEmpty else {
            return LibrarySurvey(libraries: [], nothing: .noBottles)
        }

        // Told apart so the empty case can name itself: a bottle with no Steam
        // config at all is a different problem from a Steam that records no
        // library, and they need different advice.
        var readAnyConfig = false
        var found: [DiscoveredLibrary] = []
        var seen = Set<String>()

        for bottle in bottles {
            let vdf = bottle.appendingPathComponent(
                "drive_c/Program Files (x86)/Steam/config/libraryfolders.vdf")
            guard let text = try? String(contentsOf: vdf, encoding: .utf8) else { continue }
            readAnyConfig = true
            for windowsPath in paths(in: text) {
                guard let mac = translate(windowsPath, inBottle: bottle) else { continue }
                let common = mac.appendingPathComponent("steamapps/common")
                // Several bottles usually declare one library. Deduplicated
                // before the existence test, never after: the whole point is
                // that an absent folder still produces a row.
                guard seen.insert(common.standardizedFileURL.path).inserted else { continue }

                var d: ObjCBool = false
                let ready = fm.fileExists(atPath: common.path, isDirectory: &d) && d.boolValue
                let volume = volumeName(of: mac)
                let missing = !ready && (volume.map { !isMounted($0) } ?? false)

                found.append(DiscoveredLibrary(
                    common: common,
                    root: mac,
                    volume: volume,
                    bottle: bottle.lastPathComponent,
                    insideBottle: mac.path.hasPrefix(bottle.path + "/"),
                    ready: ready,
                    volumeMissing: missing))
            }
        }

        guard !found.isEmpty else {
            return LibrarySurvey(libraries: [],
                                 nothing: readAnyConfig ? .nothingDeclared : .noSteamConfig)
        }

        // Hand-sorted by rank rather than sorted(by:), which is not documented
        // stable: two bottles' copies of the same library should keep the order
        // the bottles were read in rather than shuffle between launches.
        var ordered: [DiscoveredLibrary] = []
        for r in 0...3 { ordered += found.filter { $0.rank == r } }
        return LibrarySurvey(libraries: ordered, nothing: nil)
    }

    /// The /Volumes name a path sits on, read out of the path itself.
    ///
    /// Textual on purpose. Every filesystem call that could answer this
    /// properly -- `volumeNameKey`, `mountedVolumeURLs` -- needs the volume to
    /// be mounted, and the one case this exists to describe is exactly the one
    /// where it is not. Limitation worth knowing: a volume mounted somewhere
    /// other than /Volumes goes unnamed, and its row falls back to saying the
    /// folder is not there. Weaker, still not silent.
    private static func volumeName(of url: URL) -> String? {
        let parts = url.standardizedFileURL.pathComponents
        guard parts.count >= 3, parts[1] == "Volumes" else { return nil }
        return parts[2]
    }

    /// Is /Volumes/<name> a volume that is mounted right now?
    ///
    /// `fileExists` is not enough to ask this. macOS routinely leaves an empty
    /// /Volumes/<name> directory behind after an unclean eject, and a volume
    /// that comes back can mount as "<name> 1" beside the leftover. Taking that
    /// leftover for the drive puts an unplugged disk into the "there is no
    /// steamapps/common in it" branch and sends the user looking for a folder
    /// they never moved -- the original failure in a narrower form. statfs
    /// reports what is actually mounted at a path, costs one syscall, and does
    /// not need the volume to be there to answer.
    private static func isMounted(_ volume: String) -> Bool {
        let path = "/Volumes/\(volume)"
        var info = statfs()
        guard statfs(path, &info) == 0 else { return false }
        let mountedHere = withUnsafeBytes(of: &info.f_mntonname) { raw -> Bool in
            guard let base = raw.baseAddress else { return false }
            return String(cString: base.assumingMemoryBound(to: CChar.self)) == path
        }
        if mountedHere { return true }
        // Being a mount point is not the only way to be real: the boot volume
        // appears under /Volumes as a firmlink, and statfs reports "/" for it.
        // Measured here rather than assumed -- treating that as unmounted would
        // trade one wrong sentence for another. What a leftover cannot be is
        // non-empty, so that is the second question. The listing is counted and
        // dropped; nothing about what is on the disk goes any further.
        let contents = try? FileManager.default.contentsOfDirectory(atPath: path)
        return !(contents?.isEmpty ?? true)
    }

    /// Why a chosen root cannot be scanned, or nil when it can be.
    ///
    /// A pre-flight rather than a change to `recognise`. That function's doc
    /// comment is the privacy guarantee of this app -- it takes roots and
    /// returns only recognised games -- and it swallows an unreadable root by
    /// design, which is right for what it promises and useless for telling the
    /// user why nothing came back. Every fact needed to separate the failures
    /// is available before it runs, so it is asked here and `recognise` stays
    /// exactly as it is. Cost is one extra directory listing, off the main
    /// actor.
    static func readability(of root: URL) -> ScanOutcome.Empty? {
        let fm = FileManager.default
        var d: ObjCBool = false
        guard fm.fileExists(atPath: root.path, isDirectory: &d) else {
            if let volume = volumeName(of: root), !isMounted(volume) {
                return .volumeNotMounted(volume)
            }
            return .folderGone
        }
        // Split off from the guard above rather than folded into it. The drop
        // zone takes any file URL and `normalise` hands a file back unchanged,
        // so a dropped .exe or screenshot arrives here as a path that exists
        // and is not a folder -- and reporting that as a folder that is gone is
        // a false statement on a path the user can reach.
        guard d.boolValue else { return .notAFolder }
        guard (try? fm.contentsOfDirectory(atPath: root.path)) != nil else { return .unreadable }
        return nil
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
    @State private var confirming = false
    /// Bumped to re-read the GStreamer state after the user installs it,
    /// so the banner updates without relaunching the app.
    @State private var codecCheck = 0
    @State private var installAction = true
    @StateObject private var runner = Runner()
    @State private var dropping = false
    /// Its own flag rather than sharing `dropping`. The two zones never appear
    /// together today, and a shared highlight would be a quiet bug the first
    /// time they did.
    @State private var droppingLibrary = false
    @State private var follow = true

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            header
            requirement
            if runner.bulk {
                bulkHeader
                stepChoose
                stepScan
                stepPatch
            } else {
                allAtOnce
                supported
                dropZone
                if let t = runner.title {
                    if runner.legacyReencode != .notApplied { legacyBanner }
                    if t.modes.count > 1 { modePicker(t) }
                    actions
                }
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
        HStack(spacing: 8) {
            Image(systemName: "info.circle").foregroundStyle(.secondary)
            Text(Requirements.note).font(.caption).foregroundStyle(.secondary)
            Spacer()
        }
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

    /// The plan, which is also the result.
    ///
    /// One table, not a wizard. It shows what was found, what will happen to
    /// each row, and afterwards what did happen -- in the same rows, so the
    /// before and after are read in one place. A separate confirmation screen
    /// that is thrown away would be ceremony for four rows.
    ///
    /// Rows the app cannot act on carry no checkbox and say why instead. A
    /// disabled control with no explanation is the thing that makes people
    /// think software is broken.
    /// The way in, for someone who does not want to do this six times.
    ///
    /// Steam names install folders after the project rather than the game --
    /// Mortal Shell 2 lives under Sparta, Persona 5 Strikers under P5S -- so
    /// asking someone to find each one by hand is asking them to know
    /// something they have no reason to know. The library folder they can
    /// find, and everything past it is ours to work out.
    private var allAtOnce: some View {
        HStack(spacing: 12) {
            Image(systemName: "square.stack.3d.down.right")
                .font(.title2).foregroundStyle(.tint)
            VStack(alignment: .leading, spacing: 2) {
                Text("All your games at once").font(.body.weight(.medium))
                Text("Point at your Steam library, scan it, and fix everything "
                   + "supported in one pass. Nothing is read until you say so.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer()
            Button("Find my games") { runner.enterBulk() }
            Button("Choose folder…") { chooseLibraryFolder() }
        }
        .padding(12)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color.accentColor.opacity(0.07)))
    }

    /// Completes step 1 through the panel. It no longer scans: going through
    /// Finder is a way of naming the folder, not a decision to read it.
    private func chooseLibraryFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Choose"
        panel.message = "Choose your Steam library — the folder holding steamapps, "
                      + "or the common folder inside it. One game's folder works too."
        if panel.runModal() == .OK, let url = panel.url {
            // Re-entering bulk mode would clear the log and re-survey the
            // bottles for nothing; this link also lives inside step 1.
            if !runner.bulk { runner.enterBulk() }
            runner.chooseLibrary(url)
        }
    }

    /// Only shown when a scanned game needs it, so it is not a permanent
    /// button for a thing five of the six games have nothing to do with.
    private var codecBanner: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: Codecs.staged ? "checkmark.seal" : "shippingbox")
                .foregroundStyle(Codecs.staged ? .green : .orange)
            VStack(alignment: .leading, spacing: 3) {
                Text(Codecs.staged ? "VC-1 codec staged" : "Persona 5 Strikers needs a VC-1 codec")
                    .font(.callout.weight(.medium))
                Text(codecMessage)
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer()
            if !Codecs.staged {
                if Codecs.gstreamerInstalled {
                    Button("Stage codec") { runner.stageCodecs() }
                        .disabled(runner.busy)
                        .keyboardShortcut(.defaultAction)
                } else {
                    /// The button that resolves the problem, rather than a URL
                    /// to copy out by hand. It is the only thing standing
                    /// between this row and a working game.
                    Button("Get GStreamer…") {
                        if let url = URL(string: Codecs.downloadPage) {
                            NSWorkspace.shared.open(url)
                        }
                    }
                    .keyboardShortcut(.defaultAction)
                    Button("Check again") {
                        Codecs.refresh()
                        codecCheck &+= 1
                    }
                        .help("After installing it, check without restarting.")
                }
            }
        }
        .id(codecCheck)
        .padding(11)
        .background(RoundedRectangle(cornerRadius: 10)
            .fill((Codecs.staged ? Color.green : Color.orange).opacity(0.08)))
    }

    /// Built as a plain string rather than inline: the compiler gave up
    /// type-checking the nested conditionals in reasonable time.
    /// True when every row that can be acted on already is.
    private var allSelected: Bool {
        let actionable = runner.plan.filter(\.actionable)
        return !actionable.isEmpty && actionable.allSatisfy(\.selected)
    }

    private var codecMessage: String {
        if Codecs.staged {
            let n = Codecs.bottlesConfigured().count
            return "Borrowed from your GStreamer install, and \(n) bottle(s) point at it."
        }
        guard Codecs.gstreamerInstalled else {
            return "GStreamer is not installed. Get the macOS runtime package: "
                 + "1.24.14 is the version this was verified with, and others in "
                 + "the 1.24 series should work. Nothing is redistributed here — "
                 + "the decoder is borrowed from your install."
        }
        guard let found = Codecs.version else {
            return "GStreamer is installed but its version cannot be read. "
                 + "Staging will go ahead and say what it finds."
        }
        if Codecs.versionIsTested {
            return "CrossOver ships no VC-1 decoder. GStreamer \(found) is "
                 + "installed and will be borrowed from — nothing is redistributed."
        }
        return "GStreamer \(found) is installed. 1.24.14 is the version this "
             + "was verified with; yours may work perfectly well. Staging will go "
             + "ahead and say what it finds."
    }

    /// The way out of bulk mode, and the only thing on screen that is not one
    /// of the three steps. It used to live in the plan table's header, which
    /// was safe while that table was always drawn and is not now that step 3
    /// can be empty.
    private var bulkHeader: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("All your games at once").font(.headline)
                Spacer()
                // Not locked during a scan. `leaveBulk` cancels it, and a probe
                // that hangs would otherwise leave this window with no way out.
                Button("One game at a time") { runner.leaveBulk() }
                    .disabled(runner.busy)
            }
            // Bulk mode's one always-drawn sentence. It used to live at the
            // bottom of the plan table, which meant every line steps 1 and 2
            // wrote -- including "Choose a Steam library folder first." -- was
            // written and never shown, because the table is not drawn until
            // there is a plan.
            Text(runner.batchStep.isEmpty ? runner.status : runner.batchStep)
                .font(.callout).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    /// The number, the title, and where the user is relative to it. Every step
    /// stays on screen and stays legible -- being able to see what is still
    /// needed is the point of numbering them at all.
    ///
    /// Three appearances, not two. With only current and not-current, a step
    /// already behind the user and a step not yet reached drew the same grey
    /// circle, so the badges alone could not say which way round they were.
    private func stepBadge(_ number: Int, _ title: String) -> some View {
        let here = runner.bulkStep.rawValue
        let active = number == here
        let done = number < here
        return HStack(spacing: 8) {
            Group {
                if done {
                    Image(systemName: "checkmark")
                        .font(.system(size: 10, weight: .bold))
                        .foregroundStyle(Color.white)
                } else {
                    Text("\(number)")
                        .font(.caption.weight(.bold))
                        .foregroundStyle(active ? Color.white : Color.secondary)
                }
            }
            .frame(width: 18, height: 18)
            .background(Circle().fill(active ? Color.accentColor
                                             : done ? Color.secondary
                                                    : Color.secondary.opacity(0.18)))
            Text(title)
                .font(.callout.weight(.medium))
                .foregroundStyle(active ? Color.primary : Color.secondary)
            Spacer()
        }
    }

    /// Step 1. Open until a folder is chosen, then one line -- which is exactly
    /// when step 3 needs the height.
    private var stepChoose: some View {
        VStack(alignment: .leading, spacing: 8) {
            stepBadge(1, "Choose your Steam library folder")
            Text(chooseStepNote)
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            if let root = runner.libraryRoot {
                chosenLibraryLine(root)
                if runner.libraryIsOdd {
                    Text("This does not look like a Steam library. Scanning it "
                       + "will look only inside this one folder.")
                        .font(.caption).foregroundStyle(.orange)
                        .fixedSize(horizontal: false, vertical: true)
                }
            } else {
                libraryDropZone
                Text("The folder holding steamapps works too, and so does one "
                   + "game's folder — it is traced back to the library it sits in.")
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                discoveredLibraries
            }
        }
    }

    private var chooseStepNote: String {
        runner.libraryRoot == nil
            ? "Nothing is read until you choose a folder and press Scan."
            : "Chosen. Step 2 scans this folder."
    }

    private func chosenLibraryLine(_ root: URL) -> some View {
        HStack(spacing: 10) {
            Image(systemName: "checkmark.circle.fill").foregroundStyle(.tint)
            VStack(alignment: .leading, spacing: 2) {
                Text(root.path)
                    .font(.system(size: 12, design: .monospaced))
                    .lineLimit(1).truncationMode(.head)
                Text("Will scan").font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            // No ellipsis and no dialog: this puts all three ways in back,
            // and the discovered list is the one a panel cannot offer.
            Button("Change") { runner.clearLibrary() }
                .disabled(runner.busy || runner.scanning)
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color.primary.opacity(0.04)))
    }

    /// The same dashed zone the single-game mode uses, pointed at a library.
    private var libraryDropZone: some View {
        RoundedRectangle(cornerRadius: 10)
            .strokeBorder(style: StrokeStyle(lineWidth: 1.5, dash: [6, 4]))
            .foregroundStyle(droppingLibrary ? Color.accentColor : Color.secondary.opacity(0.5))
            .background(
                RoundedRectangle(cornerRadius: 10)
                    .fill(droppingLibrary ? Color.accentColor.opacity(0.08) : Color.clear)
            )
            .frame(height: 88)
            .overlay(
                VStack(spacing: 6) {
                    Text("Drop your Steam library folder here")
                        .font(.system(size: 13))
                        .multilineTextAlignment(.center)
                    Text("…/steamapps/common")
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(.secondary)
                    Button("Choose…") { chooseLibraryFolder() }
                        .buttonStyle(.link)
                        .disabled(runner.busy || runner.scanning)
                }
                .padding(.horizontal, 16)
            )
            .onDrop(of: [.fileURL], isTargeted: $droppingLibrary) { providers in
                guard !runner.busy, !runner.scanning, let p = providers.first else { return false }
                _ = p.loadObject(ofClass: URL.self) { url, _ in
                    guard let url else { return }
                    Task { @MainActor in runner.chooseLibrary(url) }
                }
                return true
            }
    }

    /// Auto-discovery, kept and demoted: it offers libraries that fill step 1
    /// in. Picking one completes the choice and nothing more.
    private var discoveredLibraries: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Libraries your CrossOver bottles know about")
                    .font(.caption.weight(.medium))
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Look again") { runner.findLibraries() }
                    .buttonStyle(.link)
                    .font(.caption)
                    .disabled(runner.lookingForLibraries)
                    .help("After connecting a drive, check without relaunching.")
            }

            if runner.lookingForLibraries {
                HStack(spacing: 8) {
                    ProgressView().controlSize(.small)
                    Text("Looking in your CrossOver bottles…")
                        .font(.caption).foregroundStyle(.secondary)
                    Button("Stop") { runner.stopLooking() }
                        .buttonStyle(.link).font(.caption)
                }
            } else if runner.survey == nil {
                // Reached when the search was stopped, or never ran. It used to
                // render the same "Looking…" line as the state above, purely
                // because both mean survey == nil -- so a search that ended
                // left a sentence saying it was still going, over an app doing
                // nothing at all, with no way out.
                Text("Not searched yet. Drop your library above, or look again.")
                    .font(.caption).foregroundStyle(.secondary)
            } else if let found = runner.survey?.libraries, !found.isEmpty {
                ScrollView {
                    VStack(spacing: 0) {
                        ForEach(found) { lib in
                            libraryRow(lib)
                            Divider()
                        }
                    }
                }
                .frame(maxHeight: 132)
                .background(RoundedRectangle(cornerRadius: 8).fill(Color.primary.opacity(0.03)))
            } else {
                Text(discoveryEmptyNote)
                    .font(.caption).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    private var discoveryEmptyNote: String {
        switch runner.survey?.nothing {
        case .some(.noBottles):
            return "CrossOver has no bottles, so there is nothing to ask about "
                 + "Steam libraries. Drop the folder above instead."
        case .some(.noSteamConfig):
            // Hedged to what was measured. The survey reads one path per bottle,
            // so a Steam installed anywhere else produces this same silence, and
            // stating it as a fact about the user's history is the shape of
            // over-claim this whole flow exists to remove.
            return "No Steam library configuration was found in any of your "
                 + "CrossOver bottles — Steam may never have run in one, or it "
                 + "may be installed somewhere this app does not look. Drop the "
                 + "folder above instead."
        case .some(.nothingDeclared), .none:
            return "The Steam inside your bottles records no library folder this "
                 + "app can reach. Drop the folder above instead."
        }
    }

    /// A library that cannot be used stays here, dimmed, saying why. Removing
    /// it is what made an unplugged drive indistinguishable from an empty
    /// account, and cost the user an evening.
    private func libraryRow(_ lib: DiscoveredLibrary) -> some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: lib.icon)
                .foregroundStyle(lib.ready ? Color.secondary : Color.orange)
                .frame(width: 20)
            VStack(alignment: .leading, spacing: 2) {
                Text(lib.title).font(.callout.weight(.medium))
                Text(lib.shownPath)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(.secondary)
                    .lineLimit(1).truncationMode(.head)
                if let why = lib.blocker {
                    Text(why).font(.caption).foregroundStyle(.orange)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            Spacer()
            Button(lib.actionWord) { runner.chooseLibrary(lib) }
                .disabled(!lib.ready || runner.busy || runner.scanning)
        }
        .padding(.vertical, 8).padding(.horizontal, 10)
        .opacity(lib.ready ? 1 : 0.75)
    }

    /// Step 2. Deliberate, separate, and impossible without step 1.
    private var stepScan: some View {
        VStack(alignment: .leading, spacing: 8) {
            stepBadge(2, "Scan that folder for supported games")
            HStack(spacing: 12) {
                Button(scanButtonTitle) { runner.startScan() }
                    .keyboardShortcut(runner.bulkStep == .scan
                                      ? KeyboardShortcut.defaultAction : nil)
                    .disabled(runner.libraryRoot == nil || runner.busy || runner.scanning)
                if runner.scanning {
                    ProgressView().progressViewStyle(.linear).frame(maxWidth: 200)
                    // A running scan spawns one --status per game found. Fast
                    // here, but the folder may be on a drive that is not, and a
                    // progress bar with no way out is a worse answer than a
                    // slow one.
                    Button("Stop") { runner.stopScan() }
                        .buttonStyle(.link)
                }
                Spacer()
            }
            Text(scanStepNote)
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var scanButtonTitle: String {
        runner.scanOutcome == nil ? "Scan games" : "Scan again"
    }

    private var scanStepNote: String {
        if runner.libraryRoot == nil {
            return "Waiting for step 1. Nothing is read until you choose a folder "
                 + "and press Scan."
        }
        switch runner.scanOutcome {
        case .none:
            return "Only the six supported games are ever named. Everything else "
                 + "in the folder is walked and forgotten."
        case .some(.games):
            return "Scan again after installing a game or moving one to another drive."
        case .some(.nothing):
            // The generic advice belongs to a scan that worked. Offering it over
            // an ejected drive is step 2 talking about something else while step
            // 3 says what actually happened.
            return "Step 3 says what happened."
        }
    }

    /// Step 3. The plan table, unchanged in behaviour, plus a named ending for
    /// every way it can be empty.
    private var stepPatch: some View {
        VStack(alignment: .leading, spacing: 8) {
            stepBadge(3, "Fix the games that were found")
            Text(runner.scanHeadline).font(.headline)
            if !runner.plan.isEmpty {
                planTable
            } else if runner.scanFoundNothing {
                nothingFound
            }
        }
    }

    private var nothingFound: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "magnifyingglass").foregroundStyle(.orange)
            Text(nothingFoundWhy)
                .font(.caption).foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Spacer()
        }
        .padding(11)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color.orange.opacity(0.08)))
    }

    private var nothingFoundWhy: String {
        switch runner.scanOutcome {
        case .some(.nothing(.noSupportedGames)):
            let odd = runner.libraryIsOdd
                ? "This folder does not look like a Steam library either, so it "
                + "may not be the one you meant. "
                : ""
            return odd
                 + "The folder was read and none of the six games is installed in "
                 + "it. If your games are on another drive, choose that library in step 1."
        case .some(.nothing(.volumeNotMounted)):
            return "That folder was there when you chose it and is not now. "
                 + "Connect the drive and scan again."
        case .some(.nothing(.folderGone)):
            // Names the control that is on screen. With a folder chosen, step 1
            // has collapsed to one line and a Change button; the Choose… link
            // lives in the drop zone, which is not drawn in that state.
            return "It may have been renamed or moved. Press Change in step 1, "
                 + "then choose the library folder again."
        case .some(.nothing(.notAFolder)):
            return "Step 1 takes a folder: the Steam library, the folder holding "
                 + "steamapps, or one game's folder inside it. Press Change in "
                 + "step 1, then choose the folder this file sits in."
        case .some(.nothing(.unreadable)):
            // Not "choose it again": this app is not sandboxed, so going through
            // the panel grants nothing, and saying it does sends the user round
            // a loop that cannot resolve. The grant is a system one.
            return "macOS blocked reading it, and choosing it again will not "
                 + "change that. Allow access in System Settings → Privacy & "
                 + "Security → Files and Folders — or Removable Volumes for an "
                 + "external drive — then scan again."
        case .some(.games), .none:
            return ""
        }
    }

    private var planTable: some View {
        VStack(alignment: .leading, spacing: 10) {
            if runner.plan.contains(where: { $0.game.extraRequirement != nil }) {
                codecBanner
            }

            ScrollView {
                VStack(spacing: 0) {
                    ForEach($runner.plan) { $hit in
                        planRow($hit)
                        Divider()
                    }
                }
            }
            .frame(minHeight: 160, maxHeight: 280)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color.primary.opacity(0.03)))

            HStack(spacing: 12) {
                Button(allSelected ? "Deselect all" : "Select all") {
                    let target = !allSelected
                    for i in runner.plan.indices where runner.plan[i].actionable {
                        runner.plan[i].selected = target
                    }
                }
                .disabled(runner.busy || !runner.plan.contains { $0.actionable })

                Divider().frame(height: 16)

                Button(action: { confirming = true }) {
                    Text(installAction
                         ? "Fix \(runner.selectedHits.filter { $0.state != .applied }.count) game(s)"
                         : "Remove from \(runner.selectedHits.filter { $0.state != .notApplied }.count) game(s)")
                }
                .keyboardShortcut(.defaultAction)
                .disabled(runner.busy || runner.selectedHits.isEmpty)

                Picker("", selection: $installAction) {
                    Text("Install").tag(true)
                    Text("Remove").tag(false)
                }
                .pickerStyle(.segmented)
                .frame(width: 160)
                .disabled(runner.busy)

                if runner.busy {
                    Button("Stop") { runner.stopping = true }
                        .help("Finishes the game it is on, then stops.")
                }
                Spacer()
            }
        }
        .alert("Ready to change \(runner.selectedHits.count) game(s)?",
               isPresented: $confirming) {
            Button(installAction ? "Install" : "Remove") {
                runner.applyPlan(install: installAction)
            }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text(runner.selectedHits.map(\.game.name).joined(separator: "\n")
                 + "\n\nEach game's own DLL is moved aside and can be put back "
                 + "with Remove.")
        }
    }

    private func planRow(_ hit: Binding<ScanHit>) -> some View {
        HStack(alignment: .top, spacing: 10) {
            if hit.wrappedValue.actionable {
                Toggle("", isOn: hit.selected).labelsHidden()
            } else {
                Image(systemName: "exclamationmark.triangle")
                    .foregroundStyle(.orange).frame(width: 22)
            }

            VStack(alignment: .leading, spacing: 2) {
                Text(hit.wrappedValue.game.name).font(.body.weight(.medium))
                Text(hit.wrappedValue.root.lastPathComponent)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(.secondary)
                if let why = hit.wrappedValue.blocker {
                    Text(why).font(.caption).foregroundStyle(.orange)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if let extra = hit.wrappedValue.game.extraRequirement,
                   hit.wrappedValue.blocker == nil {
                    Text(extra).font(.caption).foregroundStyle(.secondary)
                }
            }
            Spacer()
            Text(hit.wrappedValue.outcome ?? stateWord(hit.wrappedValue.state))
                .font(.callout)
                .foregroundStyle(outcomeColour(hit.wrappedValue))
        }
        .padding(.vertical, 8).padding(.horizontal, 10)
    }

    private func stateWord(_ s: FixState) -> String {
        switch s {
        case .applied:    return "Already fixed"
        case .partial:    return "Half-installed"
        case .notApplied: return "Not fixed"
        case .unknown:    return "…"
        }
    }

    private func outcomeColour(_ hit: ScanHit) -> Color {
        if let o = hit.outcome { return o.hasPrefix("Failed") ? .orange : .green }
        switch hit.state {
        case .applied: return .green
        case .partial: return .orange
        default:       return .secondary
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
