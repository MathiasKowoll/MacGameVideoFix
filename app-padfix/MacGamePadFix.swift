// MacGamePadFix — installs the controller-bus engine set this project builds
// into a CrossOver application, so a PlayStation controller on Bluetooth can
// rumble.
//
// A sibling of MacGameVideoFix, and deliberately far smaller. That app knows
// about games, bottles, Steam libraries and codecs; this one knows about one
// engine set and three files. It exists to be handed to somebody who has that
// one problem, so the whole of it is: which CrossOver, what is in it now, put
// it in, take it out.
//
// It reimplements none of the installer's logic. runtime/install-engine-controller.sh
// travels in Contents/Resources with its four payload files beside it, and every
// answer on this window is that script's own -- its refusals most of all. The
// script refuses an engine it was not built for and refuses while a bottle is
// running, and those refusals are the two sentences a person handed this app is
// most likely to see. Paraphrasing them here would mean maintaining a second,
// worse copy of a decision the script already makes correctly.
//
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import AppKit

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

/// Everything a run said, kept for the caller beside the log.
///
/// The log is filled by hopping to the main actor from the pipe's queue, so a
/// caller reading the answer immediately after the process ends would be racing
/// those hops. This is filled under a lock on the queue itself, so the full
/// output is there the moment the run returns -- which is when the status word,
/// or the refusal, is read.
private final class Collected: @unchecked Sendable {
    private var lines: [String] = []
    private let lock = NSLock()

    func add(_ line: String) { lock.lock(); lines.append(line); lock.unlock() }
    var all: [String] { lock.lock(); defer { lock.unlock() }; return lines }
}

/// Runs a command and delivers each output line as it arrives, without ever
/// blocking the caller's actor. Returns the exit status, or -1 if it could not
/// be started at all.
private func runStreaming(_ executable: String,
                          _ arguments: [String],
                          extraEnv: [String: String] = [:],
                          onLine: @escaping @Sendable (String) -> Void) async -> Int32 {
    await withCheckedContinuation { (cont: CheckedContinuation<Int32, Never>) in
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: executable)
        proc.arguments = arguments

        var env = ProcessInfo.processInfo.environment
        // Lets the script phrase its advice for someone looking at this window
        // rather than a terminal: this app streams its output into the log
        // below, so "run this script" is the wrong thing to read next to a
        // button that does it.
        env["MGVF_FRONTEND"] = "app"
        // A GUI app does not inherit the shell's PATH. The script calls
        // /usr/bin/codesign and friends by absolute path, so this is belt and
        // braces rather than a requirement.
        env["PATH"] = "/opt/homebrew/bin:/usr/local/bin:" + (env["PATH"] ?? "/usr/bin:/bin")
        for (k, v) in extraEnv { env[k] = v }
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

// MARK: - The set this app carries

/// What `engine-controller-built-for.json` records about the three files in
/// Resources: the CrossOver they were built for, by name and by version, the
/// wine revision they came out of, and the patches applied on top.
///
/// Read rather than compiled in. The stamp travels with the binaries, so a
/// build that swaps them and forgets to change a constant here is impossible:
/// there is no constant here to forget.
struct BuiltFor {
    let engineApp: String
    let engineVersion: String
    let wineBuild: String
    let patches: String

    static func read(_ url: URL) -> BuiltFor? {
        guard let data = try? Data(contentsOf: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any]
        else { return nil }
        func s(_ key: String) -> String { (json[key] as? String) ?? "" }
        let app = s("engine_app"), version = s("engine_version")
        guard !app.isEmpty, !version.isEmpty else { return nil }
        return BuiltFor(engineApp: app, engineVersion: version,
                        wineBuild: s("wine_build"), patches: s("patches"))
    }

    var sentence: String {
        "These three files were built for \(engineApp) \(engineVersion), from "
      + "\(wineBuild.isEmpty ? "the engine's own wine source" : wineBuild)"
      + (patches.isEmpty ? "" : " with \(patches)") + "."
    }
}

/// One CrossOver on this Mac.
struct Engine: Identifiable, Hashable {
    let url: URL
    let version: String
    /// True when the bundle records, in `mgvf-origin.json`, that this project
    /// copied it from another CrossOver. The installer serves such a copy under
    /// the name it was copied from, so it is worth showing.
    let ours: Bool

    var id: String { url.path }
    var name: String { url.deletingPathExtension().lastPathComponent }
    var label: String {
        ours ? "\(name) (\(version)) — a copy made by MacGameVideoFix"
             : "\(name) (\(version))"
    }
}

enum Engines {
    /// Every CrossOver in /Applications and ~/Applications, found by what the
    /// bundle contains rather than by what it is called.
    ///
    /// A name filter would be the obvious thing and it is the wrong thing. The
    /// engine this project patches is called Crossover_MGVF.app, other people
    /// rename theirs, and the installer itself accepts any name that records its
    /// origin. So the test is Contents/SharedSupport/CrossOver, which is what
    /// makes a bundle a CrossOver, and the name is only ever shown.
    ///
    /// Not deduplicated by version. Several installs on one Mac declare the
    /// same CFBundleVersion -- that is what a copy is -- so the path is the
    /// identity here, and every install gets its own row.
    static func candidates() -> [Engine] {
        let fm = FileManager.default
        var out: [Engine] = []
        for dir in ["/Applications",
                    (NSHomeDirectory() as NSString).appendingPathComponent("Applications")] {
            let apps = ((try? fm.contentsOfDirectory(at: URL(fileURLWithPath: dir),
                                                     includingPropertiesForKeys: nil)) ?? [])
                .sorted { $0.lastPathComponent < $1.lastPathComponent }
            for app in apps {
                if let e = read(app) { out.append(e) }
            }
        }
        return out
    }

    /// The same test, for a bundle somebody picked by hand.
    static func read(_ app: URL) -> Engine? {
        let fm = FileManager.default
        guard app.pathExtension == "app",
              fm.fileExists(atPath: app.appendingPathComponent(
                  "Contents/SharedSupport/CrossOver").path),
              let plist = NSDictionary(contentsOf: app.appendingPathComponent(
                  "Contents/Info.plist")),
              let version = plist["CFBundleVersion"] as? String
        else { return nil }
        let ours = fm.fileExists(atPath: app.appendingPathComponent(
            "Contents/SharedSupport/CrossOver/mgvf-origin.json").path)
        return Engine(url: app, version: version, ours: ours)
    }
}

/// What `--status` said, in the script's own vocabulary.
///
/// `refused` carries the script's sentence unaltered, because that is the whole
/// value of it: "these were built for CrossOver.app and this is X" tells the
/// reader what to do, and any summary of it here would not.
enum SetState: Equatable {
    case unknown
    case installed
    case absent
    case broken
    case refused(String)

    var headline: String {
        switch self {
        case .unknown:   return "Not checked yet."
        case .installed: return "Installed."
        case .absent:    return "Not installed."
        case .broken:    return "Half installed."
        case .refused:   return "Refused."
        }
    }

    var detail: String {
        switch self {
        case .unknown:
            return "Choose a CrossOver above and this will say what is in it."
        case .installed:
            return "This CrossOver is running the three patched files, and "
                 + "CodeWeavers' own three are kept beside them as .mgvf-stock. "
                 + "Restore puts them back."
        case .absent:
            return "This CrossOver has CodeWeavers' own files. A controller on "
                 + "Bluetooth will be seen as if it were on USB, and will not rumble."
        case .broken:
            return "Some of the three originals are backed up and some are not "
                 + "-- or this CrossOver still carries winebus.so from the "
                 + "version of this set that installed a fourth file. Either "
                 + "way it is neither state, and the three only work together. "
                 + "Install again, or restore: both put CodeWeavers' winebus.so "
                 + "back, and the log will say what happened."
        case .refused(let sentence):
            return sentence
        }
    }

    var canInstall: Bool { self == .absent || self == .broken }
    var canRestore: Bool { self == .installed || self == .broken }

    var tint: Color {
        switch self {
        case .installed: return .green
        case .broken, .refused: return .orange
        case .absent, .unknown: return .secondary
        }
    }
}

// MARK: - Model

@MainActor
final class Runner: ObservableObject {
    @Published var engines: [Engine] = []
    @Published var chosen: Engine? { didSet { if oldValue != chosen { state = .unknown } } }
    @Published var state: SetState = .unknown
    @Published var log: [String] = []
    @Published var busy = false

    /// Nil when the build forgot the stamp, which the build script is supposed
    /// to make impossible. Shown rather than swallowed if it ever happens.
    let builtFor: BuiltFor?
    /// Nil when the installer is not in Resources at all -- again a build
    /// failure, and the one thing this app cannot work around.
    let script: URL?

    private var resources: URL { Bundle.main.resourceURL ?? URL(fileURLWithPath: ".") }

    init() {
        let res = Bundle.main.resourceURL ?? URL(fileURLWithPath: ".")
        let installer = res.appendingPathComponent("install-engine-controller.sh")
        script = FileManager.default.fileExists(atPath: installer.path) ? installer : nil
        builtFor = BuiltFor.read(res.appendingPathComponent("engine-controller-built-for.json"))
    }

    func note(_ line: String) {
        log.append(line)
        if log.count > 4000 { log.removeFirst(log.count - 4000) }   // keep memory bounded
    }

    /// Rescans the Applications folders, keeping the chosen engine if it is
    /// still there and otherwise picking the first.
    ///
    /// A person who installs CrossOver while this window is open should not have
    /// to relaunch to see it, which is why this runs again when the app comes
    /// back to the front.
    func rescan() {
        engines = Engines.candidates()
        if let c = chosen, engines.contains(c) { return }
        if let c = chosen, let again = engines.first(where: { $0.url == c.url }) {
            chosen = again                      // same install, new version string
        } else {
            chosen = preferred() ?? engines.first
        }
    }

    /// The engine this build was actually made for, if it is on this Mac.
    ///
    /// Directory order put a CrossOver Preview 27 first on the machine this was
    /// written on. That is a real CrossOver, `--status` reports it as absent
    /// quite correctly -- the status verb checks no version, it only looks for
    /// the backups -- and Install would then be refused. The refusal is right
    /// and is shown as it comes; landing on it by default is this app choosing,
    /// unprompted, the one engine it cannot serve.
    ///
    /// So the version in the stamp decides the first selection. The name only
    /// breaks a tie: whether a differently named engine is served is the
    /// script's decision, through `mgvf-origin.json`, and not one to be
    /// second-guessed here.
    private func preferred() -> Engine? {
        guard let want = builtFor else { return nil }
        let sameVersion = engines.filter { $0.version == want.engineVersion }
        let wantedName = (want.engineApp as NSString).deletingPathExtension
        return sameVersion.first { $0.name == wantedName }
            ?? sameVersion.first { $0.ours }
            ?? sameVersion.first
    }

    func choose(_ app: URL) {
        guard let engine = Engines.read(app) else {
            note("")
            note("\(app.lastPathComponent) is not a CrossOver: it has no "
               + "Contents/SharedSupport/CrossOver.")
            return
        }
        // A hand-picked engine joins the list rather than replacing it, so the
        // picker keeps showing every engine this session has seen.
        if !engines.contains(where: { $0.url == engine.url }) { engines.append(engine) }
        chosen = engine
    }

    /// The one read-only verb.
    ///
    /// Both belts are worn: the literal --status, and MGVF_STATUS_ONLY=1, which
    /// the script honours by forcing --status whatever else it was handed. The
    /// script's default action is the destructive one, so a survey that is
    /// read-only only because of one literal on one line is read-only by luck.
    func refresh() async {
        guard let engine = chosen else { state = .unknown; return }
        guard let script else {
            state = .refused("This build has no install-engine-controller.sh in "
                           + "its Resources, so it cannot do anything at all.")
            return
        }
        busy = true
        defer { busy = false }

        let collected = Collected()
        let code = await runStreaming("/bin/bash",
                                      [script.path, engine.url.path, "--status"],
                                      extraEnv: ["MGVF_STATUS_ONLY": "1"]) { line in
            collected.add(line)
        }
        let out = collected.all

        // The word is the answer; anything else the script said is a refusal,
        // and is shown as it was written.
        if code == 0, let word = out.last(where: {
            ["installed", "absent", "broken"].contains($0.trimmingCharacters(in: .whitespaces))
        }) {
            switch word.trimmingCharacters(in: .whitespaces) {
            case "installed": state = .installed
            case "broken":    state = .broken
            default:          state = .absent
            }
        } else {
            let said = out.filter { !$0.trimmingCharacters(in: .whitespaces).isEmpty }
            state = .refused(said.isEmpty
                ? "The installer said nothing and exited \(code)."
                : said.joined(separator: " "))
            for line in said { note("  " + line) }
        }
    }

    func install() async { await run(arguments: [], verb: "Installing") }

    func restore() async { await run(arguments: ["--restore"], verb: "Restoring CrossOver's files") }

    /// Runs the installer and shows what it says, line by line.
    ///
    /// The arguments are exactly the ones the command line takes -- the engine
    /// path alone installs, `--restore` puts the originals back -- so anything
    /// somebody reads in the repository's own documentation is what this runs.
    private func run(arguments: [String], verb: String) async {
        guard let engine = chosen, let script else { return }
        busy = true
        defer { busy = false }

        note("")
        note("▸ \(verb) — \(engine.name)")
        let code = await runStreaming("/bin/bash",
                                      [script.path, engine.url.path] + arguments) { line in
            Task { @MainActor [weak self] in self?.note("  " + line) }
        }
        // Let the queued log lines land before the verdict is written under them.
        await Task.yield()
        note(code == 0 ? "  done" : "  failed (exit \(code))")

        // Whatever happened, the answer on screen comes from asking again rather
        // than from assuming the run did what it was asked.
        await refresh()
    }
}

// MARK: - Interface

struct ContentView: View {
    @StateObject private var runner = Runner()
    @State private var confirming = false
    @State private var follow = true

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            header
            enginePicker
            statusCard
            warning
            actions
            logView
        }
        .padding(22)
        .frame(minWidth: 660, minHeight: 720)
        .task {
            runner.rescan()
            await runner.refresh()
        }
        // Somebody who leaves to install CrossOver, or to close Steam because
        // the script refused while a bottle was up, comes back to an answer that
        // was true when they left. Asking again on the way in is cheaper than
        // explaining that.
        .onReceive(NotificationCenter.default.publisher(
            for: NSApplication.didBecomeActiveNotification)) { _ in
            guard !runner.busy else { return }
            runner.rescan()
            Task { await runner.refresh() }
        }
        .alert("Install into \(runner.chosen?.name ?? "this CrossOver")?",
               isPresented: $confirming) {
            Button("Install") { Task { await runner.install() } }
            Button("Cancel", role: .cancel) { }
        } message: {
            Text(consequences)
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("MacGamePadFix")
                .font(.system(size: 22, weight: .semibold))
            Text("A PlayStation controller on Bluetooth can rumble, because the "
               + "Windows side can finally learn which bus the controller is on.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            // Which CrossOver this particular build serves, read out of the
            // stamp that travels with the binaries. One version and no other:
            // mixing wine binaries across versions breaks a bottle quietly.
            if let b = runner.builtFor {
                Text(b.sentence)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            } else {
                Text("This build carries no engine-controller-built-for.json, so "
                   + "it cannot say which CrossOver it serves.")
                    .font(.caption)
                    .foregroundStyle(.orange)
            }
        }
    }

    private var enginePicker: some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 8) {
                if runner.engines.isEmpty {
                    Text("No CrossOver found in /Applications or ~/Applications.")
                        .foregroundStyle(.secondary)
                } else {
                    Picker("CrossOver", selection: Binding(
                        get: { runner.chosen },
                        set: { runner.chosen = $0; Task { await runner.refresh() } })) {
                        ForEach(runner.engines) { engine in
                            Text(engine.label).tag(Optional(engine))
                        }
                    }
                    .labelsHidden()
                }
                HStack {
                    Button("Choose another…") { pickEngine() }
                    Button("Check again") { Task { await runner.refresh() } }
                        .disabled(runner.busy || runner.chosen == nil)
                    Spacer()
                }
                if let e = runner.chosen {
                    Text(e.url.path)
                        .font(.system(size: 11, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)
                }
            }
            .padding(4)
        } label: {
            Text("Which CrossOver").font(.headline)
        }
    }

    private var statusCard: some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 8) {
                    Circle().frame(width: 9, height: 9)
                        .foregroundStyle(runner.state.tint)
                    Text(runner.state.headline).font(.headline)
                    if runner.busy { ProgressView().controlSize(.small) }
                }
                Text(runner.state.detail)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
                    .textSelection(.enabled)
            }
            .padding(4)
            .frame(maxWidth: .infinity, alignment: .leading)
        } label: {
            Text("What is in it now").font(.headline)
        }
    }

    /// On the window, not only in the confirmation. Somebody deciding whether to
    /// run this at all should be able to read what it does without pressing the
    /// button that does it.
    private var warning: some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 6) {
                Text(consequences)
                    .font(.callout)
                    .fixedSize(horizontal: false, vertical: true)
                Text("A CrossOver update will put its own three files back, and "
                   + "the fix has to be installed again afterwards.")
                    .font(.callout)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .padding(4)
            .frame(maxWidth: .infinity, alignment: .leading)
        } label: {
            Text("What this does to CrossOver").font(.headline)
        }
    }

    private var consequences: String {
        "This replaces three files inside the CrossOver application — "
      + "winebus.sys, setupapi.dll and ntoskrnl.exe — keeps CodeWeavers' "
      + "originals beside them as .mgvf-stock, and re-signs the application "
      + "ad hoc, which replaces CodeWeavers' own signature. Restore puts the "
      + "three originals back. Close Steam and let every bottle shut down "
      + "first: the installer refuses while one is running, in both directions."
    }

    private var actions: some View {
        HStack(spacing: 12) {
            Button("Install") { confirming = true }
                .keyboardShortcut(.defaultAction)
                .disabled(runner.busy || !runner.state.canInstall)
            Button("Restore CrossOver's files") { Task { await runner.restore() } }
                .disabled(runner.busy || !runner.state.canRestore)
            Spacer()
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
                    NSPasteboard.general.setString(runner.log.joined(separator: "\n"),
                                                   forType: .string)
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
            .frame(minHeight: 140)
        }
    }

    private func pickEngine() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = false
        // An .app is a directory to the file system and a file to the user;
        // allowing both is what makes it selectable in one click.
        panel.message = "Choose the CrossOver application to work on."
        panel.prompt = "Choose"
        panel.directoryURL = URL(fileURLWithPath: "/Applications")
        guard panel.runModal() == .OK, let url = panel.url else { return }
        runner.choose(url)
        Task { await runner.refresh() }
    }
}

@main
struct MacGamePadFixApp: App {
    var body: some Scene {
        Window("MacGamePadFix", id: "main") {
            ContentView()
        }
        .windowResizability(.contentMinSize)
    }
}
