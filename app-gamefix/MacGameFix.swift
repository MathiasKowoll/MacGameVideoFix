// MacGameFix -- the game-folder fixes of MacGameVideoFix, and nothing else.
//
// Every fix this app carries lives in a game's own folder: a proxy DLL beside
// the game, and for one title registry keys in that game's own prefix. None of
// them changes CrossOver, so they work with CrossOver as CodeWeavers ships it,
// with CrossOver Preview, and with a Steam Play tool such as NotProton.
//
// The app decides nothing about a fix. The installers under Resources are the
// same files runtime/ holds, and they answer every question about a game folder
// themselves: --status says what is there, a plain run installs, --restore
// undoes. This file finds the folders, asks, and shows the answers.
//
// Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
// SPDX-License-Identifier: GPL-3.0-or-later

import SwiftUI
import AppKit

// MARK: - Running an installer

/// Collects bytes into lines across reads. A read can end in the middle of a
/// line, and handing that half to the log splits one message into two.
private final class LineBuffer: @unchecked Sendable {
    private let lock = NSLock()
    private var data = Data()

    func take(_ chunk: Data) -> [String] {
        lock.lock(); defer { lock.unlock() }
        data.append(chunk)
        var lines: [String] = []
        while let nl = data.firstIndex(of: 0x0A) {
            let line = data[data.startIndex..<nl]
            lines.append(String(decoding: line, as: UTF8.self))
            data.removeSubrange(data.startIndex...nl)
        }
        return lines
    }

    func flush() -> String? {
        lock.lock(); defer { lock.unlock() }
        guard !data.isEmpty else { return nil }
        defer { data.removeAll() }
        return String(decoding: data, as: UTF8.self)
    }
}

/// Runs a command, delivering each output line as it arrives, and returns the
/// exit status (-1 when it could not be started).
private func runStreaming(_ executable: String, _ arguments: [String],
                          environment extra: [String: String],
                          onLine: @escaping @Sendable (String) -> Void) async -> Int32 {
    await withCheckedContinuation { (cont: CheckedContinuation<Int32, Never>) in
        let proc = Process()
        proc.executableURL = URL(fileURLWithPath: executable)
        proc.arguments = arguments
        var env = ProcessInfo.processInfo.environment
        // The installers phrase their advice for a window rather than a
        // terminal when this is set -- and bottles.sh then insists on being told
        // which prefix, which is the behaviour wanted from any program.
        env["MGVF_FRONTEND"] = "app"
        env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
        for (k, v) in extra { env[k] = v }
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

/// Lines collected from one run, safe to append from the pipe's thread.
private final class Collected: @unchecked Sendable {
    private let lock = NSLock()
    private var lines: [String] = []
    func add(_ s: String) { lock.lock(); lines.append(s); lock.unlock() }
    var all: [String] { lock.lock(); defer { lock.unlock() }; return lines }
}

// MARK: - Model

/// One title, as build-app.sh wrote it into games.json.
struct Game: Decodable, Identifiable, Hashable {
    let name: String
    let exe: String
    let carrierDir: String
    let script: String
    let needsPrefix: Bool
    let symptom: String
    let backend: String
    let gptk: String
    /// A second toolkit generation measured working, beside `gptk`.
    let gptkAlso: String
    /// The CrossOver that ships the codec this title's videos need, or empty
    /// when it needs none. Stable 26.3 ships none of them.
    let codecShippedBy: String
    /// A CrossOver on which a fix that needs an engine of ours on 26.3 was
    /// measured needing none; empty otherwise.
    let engineFreeOn: String
    let why: String

    var id: String { name }

    /// Is this folder this game? Identity is the shipping executable, looked for
    /// where these titles keep it: the folder itself, the carrier's folder (The
    /// Witcher 3 keeps both in bin/x64_dx12), and Unreal's
    /// <Project>/Binaries/Win64.
    func isRooted(at folder: URL) -> Bool {
        let fm = FileManager.default
        if fm.fileExists(atPath: folder.appendingPathComponent(exe).path) { return true }
        if !carrierDir.isEmpty,
           fm.fileExists(atPath: folder.appendingPathComponent(carrierDir).appendingPathComponent(exe).path) {
            return true
        }
        let subs = (try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        for sub in subs where sub.lastPathComponent != "Engine" {
            if fm.fileExists(atPath: sub.appendingPathComponent("Binaries/Win64").appendingPathComponent(exe).path) {
                return true
            }
        }
        return false
    }

    /// What the backend and toolkit fields mean to someone choosing settings.
    var requirementNote: String? {
        var parts: [String] = []
        switch backend {
        case "dxmt": parts.append("runs on DXMT")
        case "d3dmetal": parts.append("runs on D3DMetal")
        default: break
        }
        if !gptk.isEmpty {
            parts.append(gptkAlso.isEmpty ? "needs D3DMetal \(gptk)"
                                          : "needs D3DMetal \(gptk) or \(gptkAlso)")
        }
        if !codecShippedBy.isEmpty {
            parts.append("its videos need \(codecShippedBy), which ships their codec")
        }
        if !engineFreeOn.isEmpty {
            parts.append("needs \(engineFreeOn); stable CrossOver 26.3 needs MacGameVideoFix's engine")
        }
        return parts.isEmpty ? nil : parts.joined(separator: ", ")
    }
}

/// Where a title's registry keys can go: a Steam Play prefix or a CrossOver
/// bottle. Both are a directory holding drive_c, which is what the installer
/// takes as MGVF_BOTTLE.
struct Prefix: Hashable, Identifiable {
    enum Kind: Hashable { case steamPlay, bottle }
    let kind: Kind
    let path: URL
    var id: String { path.path }
    var label: String {
        switch kind {
        case .steamPlay: return "Steam Play prefix (\(path.deletingLastPathComponent().lastPathComponent))"
        case .bottle: return "CrossOver bottle “\(path.lastPathComponent)”"
        }
    }
}

enum FixState: Equatable {
    case unknown, installed, absent, half, broken, noCarrier

    var label: String {
        switch self {
        case .unknown: return "…"
        case .installed: return "Installed"
        case .absent: return "Not installed"
        case .half: return "Half installed"
        case .broken: return "Needs repair"
        case .noCarrier: return "Cannot be fixed here"
        }
    }

    var color: Color {
        switch self {
        case .installed: return .green
        case .half, .broken: return .orange
        case .noCarrier: return .red
        default: return .secondary
        }
    }
}

/// One game found on disk.
struct Hit: Identifiable {
    let game: Game
    let folder: URL
    var prefixes: [Prefix] = []
    var prefix: Prefix?
    var state: FixState = .unknown
    var selected = false
    var outcome: String?

    var id: String { folder.path + "|" + game.name }

    /// Why no button may touch this row, in the user's terms.
    var blocker: String? {
        if state == .noCarrier {
            return "The installer found nothing in this folder for the fix to ride on."
        }
        if game.needsPrefix && prefix == nil {
            return "No prefix for this game was found. Run it once with NotProton or CrossOver, then Scan again."
        }
        return nil
    }
}

// MARK: - Finding libraries and prefixes

enum Libraries {
    static let home = FileManager.default.homeDirectoryForCurrentUser

    /// Every steamapps/common worth scanning: the native Steam's libraries (a
    /// Steam Play tool installs Windows games there), the libraries of the Steam
    /// inside each CrossOver bottle, and the folders the user added.
    static func all(extra: [URL]) -> [URL] {
        var found: [URL] = []
        var seen = Set<String>()
        func add(_ common: URL) {
            let key = common.standardizedFileURL.path.lowercased()
            guard seen.insert(key).inserted else { return }
            found.append(common)
        }
        let steam = home.appendingPathComponent("Library/Application Support/Steam")
        add(steam.appendingPathComponent("steamapps/common"))
        for vdf in [steam.appendingPathComponent("config/libraryfolders.vdf"),
                    steam.appendingPathComponent("steamapps/libraryfolders.vdf")] {
            guard let text = try? String(contentsOf: vdf, encoding: .utf8) else { continue }
            for p in paths(in: text) where p.hasPrefix("/") {
                add(URL(fileURLWithPath: p).appendingPathComponent("steamapps/common"))
            }
        }
        for bottle in bottles() {
            let vdf = bottle.appendingPathComponent(
                "drive_c/Program Files (x86)/Steam/config/libraryfolders.vdf")
            guard let text = try? String(contentsOf: vdf, encoding: .utf8) else { continue }
            for p in paths(in: text) {
                if let mac = translate(p, inBottle: bottle) {
                    add(mac.appendingPathComponent("steamapps/common"))
                }
            }
        }
        for u in extra { add(u) }
        return found
    }

    /// Bottle directories in every root a CrossOver on this Mac uses: the default
    /// one, and any CX_BOTTLE_PATH a copy's etc/CrossOver.conf redirects to.
    static func bottles() -> [URL] {
        let fm = FileManager.default
        var roots = [home.appendingPathComponent("Library/Application Support/CrossOver/Bottles")]
        let apps = ((try? fm.contentsOfDirectory(at: URL(fileURLWithPath: "/Applications"),
                                                 includingPropertiesForKeys: nil)) ?? [])
            + ((try? fm.contentsOfDirectory(at: home.appendingPathComponent("Applications"),
                                            includingPropertiesForKeys: nil)) ?? [])
        for app in apps where app.pathExtension == "app" {
            let conf = app.appendingPathComponent("Contents/SharedSupport/CrossOver/etc/CrossOver.conf")
            guard let text = try? String(contentsOf: conf, encoding: .utf8) else { continue }
            for line in text.split(separator: "\n") where line.hasPrefix("\"CX_BOTTLE_PATH\"") {
                let parts = line.split(separator: "\"")
                if let v = parts.last, v.hasPrefix("/") { roots.append(URL(fileURLWithPath: String(v))) }
            }
        }
        var out: [URL] = []
        var seen = Set<String>()
        for root in roots {
            for b in (try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)) ?? []
            where fm.fileExists(atPath: b.appendingPathComponent("drive_c").path) {
                if seen.insert(b.standardizedFileURL.path).inserted { out.append(b) }
            }
        }
        return out
    }

    /// The "path" values of a libraryfolders.vdf, unescaped.
    static func paths(in vdf: String) -> [String] {
        vdf.split(separator: "\n").compactMap { line in
            let parts = line.split(separator: "\"", omittingEmptySubsequences: false)
            guard parts.count >= 4, parts[1] == "path" else { return nil }
            return String(parts[3]).replacingOccurrences(of: "\\\\", with: "\\")
        }
    }

    /// A Windows path to a macOS one, through the bottle's own drive map.
    static func translate(_ windowsPath: String, inBottle bottle: URL) -> URL? {
        let chars = Array(windowsPath)
        guard chars.count >= 2, chars[1] == ":" else { return nil }
        let link = bottle.appendingPathComponent("dosdevices/\(String(chars[0]).lowercased()):")
        guard let target = try? FileManager.default.destinationOfSymbolicLink(atPath: link.path) else { return nil }
        let base = target.hasPrefix("/") ? URL(fileURLWithPath: target)
            : bottle.appendingPathComponent("dosdevices").appendingPathComponent(target)
        let rest = String(chars.dropFirst(2)).replacingOccurrences(of: "\\", with: "/")
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        return (rest.isEmpty ? base : base.appendingPathComponent(rest)).standardizedFileURL
    }

    /// The places a game's registry keys could go.
    ///
    /// A Steam Play tool keeps one prefix per game, at
    /// steamapps/compatdata/<appid>/pfx beside the library, and the appid is read
    /// from the appmanifest whose installdir is this folder. A CrossOver bottle
    /// qualifies when its Steam declares the library the game sits in -- the
    /// same rule the installer uses, and never "any bottle with a dinput8", which
    /// once put a KINGDOM HEARTS key into a Battle.net bottle.
    static func prefixes(for folder: URL) -> [Prefix] {
        let fm = FileManager.default
        var out: [Prefix] = []
        let common = folder.deletingLastPathComponent()
        let steamapps = common.deletingLastPathComponent()
        if common.lastPathComponent == "common", steamapps.lastPathComponent == "steamapps" {
            let manifests = ((try? fm.contentsOfDirectory(at: steamapps, includingPropertiesForKeys: nil)) ?? [])
                .filter { $0.lastPathComponent.hasPrefix("appmanifest_") && $0.pathExtension == "acf" }
            for m in manifests {
                guard let text = try? String(contentsOf: m, encoding: .utf8),
                      installDir(in: text) == folder.lastPathComponent else { continue }
                let appid = m.deletingPathExtension().lastPathComponent.replacingOccurrences(of: "appmanifest_", with: "")
                let pfx = steamapps.appendingPathComponent("compatdata/\(appid)/pfx")
                if fm.fileExists(atPath: pfx.appendingPathComponent("user.reg").path) {
                    out.append(Prefix(kind: .steamPlay, path: pfx))
                }
            }
            let library = steamapps.deletingLastPathComponent().standardizedFileURL.path.lowercased()
            for bottle in bottles() {
                let vdf = bottle.appendingPathComponent(
                    "drive_c/Program Files (x86)/Steam/config/libraryfolders.vdf")
                guard let text = try? String(contentsOf: vdf, encoding: .utf8),
                      fm.fileExists(atPath: bottle.appendingPathComponent("drive_c/windows/system32/dinput8.dll").path)
                else { continue }
                let declared = paths(in: text).compactMap { translate($0, inBottle: bottle)?.path.lowercased() }
                if declared.contains(library) { out.append(Prefix(kind: .bottle, path: bottle)) }
            }
        }
        return out
    }

    private static func installDir(in acf: String) -> String? {
        for line in acf.split(separator: "\n") {
            let parts = line.split(separator: "\"", omittingEmptySubsequences: false)
            if parts.count >= 4, parts[1] == "installdir" { return String(parts[3]) }
        }
        return nil
    }
}

// MARK: - State

@MainActor
final class Model: ObservableObject {
    @Published var hits: [Hit] = []
    @Published var libraries: [URL] = []
    @Published var log: [String] = []
    @Published var busy = false
    @Published var status = ""

    let games: [Game]
    let resources: URL

    private let extraKey = "extraFolders"

    init() {
        resources = Bundle.main.resourceURL ?? URL(fileURLWithPath: ".")
        struct Table: Decodable { let games: [Game] }
        let data = (try? Data(contentsOf: resources.appendingPathComponent("games.json"))) ?? Data()
        games = (try? JSONDecoder().decode(Table.self, from: data))?.games ?? []
    }

    var extraFolders: [URL] {
        get { (UserDefaults.standard.stringArray(forKey: extraKey) ?? []).map { URL(fileURLWithPath: $0) } }
        set { UserDefaults.standard.set(newValue.map(\.path), forKey: extraKey) }
    }

    var selected: [Hit] { hits.filter { $0.selected && $0.blocker == nil } }

    func note(_ s: String) { log.append(s) }

    func addFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = true
        panel.message = "Choose a Steam library, its steamapps/common folder, or a game folder."
        guard panel.runModal() == .OK else { return }
        var list = extraFolders
        for u in panel.urls where !list.contains(u) { list.append(u) }
        extraFolders = list
        scan()
    }

    func removeExtra(_ url: URL) {
        extraFolders = extraFolders.filter { $0 != url }
        scan()
    }

    func scan() {
        guard !busy else { return }
        busy = true
        status = "Looking for games…"
        let extra = extraFolders
        let games = self.games
        Task {
            let (libs, found) = await Task.detached { () -> ([URL], [Hit]) in
                let fm = FileManager.default
                let libs = Libraries.all(extra: extra)
                var hits: [Hit] = []
                var seen = Set<String>()
                for lib in libs {
                    // A folder the user added may be a game folder itself.
                    var candidates = [lib]
                    candidates += ((try? fm.contentsOfDirectory(at: lib, includingPropertiesForKeys: [.isDirectoryKey])) ?? [])
                        .filter { (try? $0.resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true }
                    for folder in candidates {
                        for g in games where g.isRooted(at: folder) {
                            let key = folder.standardizedFileURL.path + "|" + g.name
                            guard seen.insert(key).inserted else { continue }
                            var h = Hit(game: g, folder: folder)
                            if g.needsPrefix {
                                h.prefixes = Libraries.prefixes(for: folder)
                                h.prefix = h.prefixes.first
                            }
                            hits.append(h)
                        }
                    }
                }
                hits.sort { $0.game.name.lowercased() < $1.game.name.lowercased() }
                return (libs, hits)
            }.value
            libraries = libs
            hits = found
            for i in hits.indices { hits[i] = await probe(hits[i]) }
            busy = false
            status = found.isEmpty ? "No supported game found in these folders." : "\(found.count) game(s) found."
        }
    }

    private func environment(for hit: Hit) -> [String: String] {
        guard hit.game.needsPrefix, let p = hit.prefix else { return [:] }
        return ["MGVF_BOTTLE": p.path.path]
    }

    /// Asks the installer what it sees. Only the state word is kept: --status
    /// also prints paths, and the installer's own judgement is the only one.
    func probe(_ hit: Hit) async -> Hit {
        var hit = hit
        if hit.game.needsPrefix && hit.prefix == nil {
            hit.state = .unknown
            return hit
        }
        let script = resources.appendingPathComponent(hit.game.script).path
        let out = Collected()
        let code = await runStreaming("/bin/bash", [script, hit.folder.path, "--status"],
                                      environment: environment(for: hit)) { out.add($0) }
        let word = out.all.map { $0.trimmingCharacters(in: .whitespaces) }
            .first { ["installed", "broken", "half", "absent"].contains($0) }
        guard code == 0, let word else {
            hit.state = .noCarrier
            hit.selected = false
            return hit
        }
        switch word {
        case "installed": hit.state = .installed
        case "half": hit.state = .half
        case "broken": hit.state = .broken
        default: hit.state = .absent
        }
        return hit
    }

    func apply(install: Bool) {
        guard !busy else { return }
        let targets = selected.filter { install ? $0.state != .installed : $0.state != .absent }
        guard !targets.isEmpty else {
            status = install ? "Nothing selected needs installing." : "Nothing selected is installed."
            return
        }
        busy = true
        Task {
            var done = 0, failed = 0
            for hit in targets {
                done += 1
                status = "\(install ? "Installing" : "Removing") \(hit.game.name) (\(done) of \(targets.count))…"
                note(""); note("▸ \(hit.game.name) — \(hit.folder.path)")
                if let p = hit.prefix, hit.game.needsPrefix { note("  into \(p.label)") }
                let script = resources.appendingPathComponent(hit.game.script).path
                var args = [script, hit.folder.path]
                if !install { args.append("--restore") }
                let code = await runStreaming("/bin/bash", args, environment: environment(for: hit)) { line in
                    Task { @MainActor in self.note("  " + line) }
                }
                await Task.yield()
                if let i = hits.firstIndex(where: { $0.id == hit.id }) {
                    var h = await probe(hits[i])
                    // The re-probe is the authority, not the exit code.
                    let took = install ? h.state == .installed : h.state == .absent
                    if code != 0 { failed += 1; h.outcome = "Failed — see the log" }
                    else if !took { failed += 1; h.outcome = "Did not take — see the log" }
                    else { h.outcome = install ? "Installed" : "Removed" }
                    h.selected = false
                    hits[i] = h
                }
            }
            busy = false
            status = failed == 0 ? "Done: \(done) game(s)." : "Done, \(failed) of \(done) need attention."
        }
    }
}

// MARK: - Interface

struct HitRow: View {
    @Binding var hit: Hit
    let busy: Bool

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Toggle("", isOn: $hit.selected)
                .labelsHidden()
                .disabled(hit.blocker != nil || busy)
            VStack(alignment: .leading, spacing: 3) {
                HStack {
                    Text(hit.game.name).font(.headline)
                    Spacer()
                    if let o = hit.outcome {
                        Text(o).font(.caption).foregroundStyle(o.hasPrefix("Installed") || o.hasPrefix("Removed") ? .green : .orange)
                    }
                    Text(hit.state.label).font(.callout).foregroundStyle(hit.state.color)
                }
                Text(hit.folder.path).font(.caption).foregroundStyle(.secondary).lineLimit(1).truncationMode(.middle)
                Text(hit.game.symptom).font(.caption)
                if let r = hit.game.requirementNote {
                    Text(r).font(.caption).foregroundStyle(.secondary)
                }
                if hit.game.needsPrefix {
                    if hit.prefixes.count > 1 {
                        Picker("Registry keys go into", selection: $hit.prefix) {
                            ForEach(hit.prefixes) { p in Text(p.label).tag(Optional(p)) }
                        }
                        .font(.caption)
                        .frame(maxWidth: 420)
                        .disabled(busy)
                    } else if let p = hit.prefix {
                        Text("Registry keys go into the \(p.label)").font(.caption).foregroundStyle(.secondary)
                    }
                }
                if let b = hit.blocker {
                    Text(b).font(.caption).foregroundStyle(.red)
                }
            }
        }
        .padding(.vertical, 4)
        .help(hit.game.why)
    }
}

struct ContentView: View {
    @StateObject private var model = Model()

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            VStack(alignment: .leading, spacing: 4) {
                Text("MacGameFix").font(.title2.bold())
                Text("Fixes that go into a game's own folder. Nothing here changes CrossOver, so they work with "
                     + "CrossOver as it ships, CrossOver Preview, and Steam Play tools such as NotProton. "
                     + "Quit a game before installing or removing its fix.")
                    .font(.callout).foregroundStyle(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }

            HStack {
                Button("Scan") { model.scan() }.disabled(model.busy)
                Button("Add Folder…") { model.addFolder() }.disabled(model.busy)
                Spacer()
                Button("Select All") {
                    for i in model.hits.indices where model.hits[i].blocker == nil { model.hits[i].selected = true }
                }.disabled(model.busy || model.hits.isEmpty)
                Button("Install Selected") { model.apply(install: true) }
                    .keyboardShortcut(.defaultAction)
                    .disabled(model.busy || model.selected.isEmpty)
                Button("Remove Selected") { model.apply(install: false) }
                    .disabled(model.busy || model.selected.isEmpty)
            }

            if model.hits.isEmpty {
                VStack(spacing: 8) {
                    Spacer()
                    Text(model.busy ? "Looking…" : "No supported game found yet.").foregroundStyle(.secondary)
                    Text("Supported: " + model.games.map(\.name).joined(separator: ", "))
                        .font(.caption).foregroundStyle(.secondary)
                        .multilineTextAlignment(.center)
                    Spacer()
                }
                .frame(maxWidth: .infinity, minHeight: 180)
            } else {
                List($model.hits) { $hit in
                    HitRow(hit: $hit, busy: model.busy)
                }
                .frame(minHeight: 260)
            }

            DisclosureGroup("Folders searched (\(model.libraries.count))") {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(model.libraries, id: \.self) { lib in
                        HStack {
                            Text(lib.path).font(.caption).lineLimit(1).truncationMode(.middle)
                            if model.extraFolders.contains(lib) {
                                Button("Remove") { model.removeExtra(lib) }.font(.caption).disabled(model.busy)
                            }
                        }
                    }
                }
            }

            DisclosureGroup("Log") {
                ScrollView {
                    Text(model.log.joined(separator: "\n"))
                        .font(.system(.caption, design: .monospaced))
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .textSelection(.enabled)
                }
                .frame(height: 160)
            }

            HStack {
                if model.busy { ProgressView().controlSize(.small) }
                Text(model.status).font(.callout)
            }
        }
        .padding(16)
        .frame(minWidth: 720, minHeight: 560)
        .onAppear { model.scan() }
    }
}

@main
struct MacGameFixApp: App {
    var body: some Scene {
        WindowGroup("MacGameFix") { ContentView() }
            .windowResizability(.contentMinSize)
    }
}
