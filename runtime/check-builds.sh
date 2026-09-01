#!/usr/bin/env bash
#
# Does every shipped DLL still match the source it claims to come from?
#
#     runtime/check-builds.sh            report
#     runtime/check-builds.sh --verbose  and say what differs
#
# WHY THIS EXISTS. One afternoon this repository held three different builds of
# the same proxy at once: the source said one thing, runtime/amd_ags_x64.dll was
# several revisions behind it, and the copy installed in the game was a third
# build that matched neither. Nothing anywhere said so. Every measurement taken
# against "the fix" was really a measurement of whichever of the three happened
# to be in the path, and a fix that had been rewritten twice was still shipping
# its oldest form.
#
# It asks the binaries, not a manifest. A manifest is one more thing to forget
# to update; a rebuild cannot be out of date with itself.
#
# NOTHING IS HARDCODED except one compile flag. For each shipped DLL:
#   - the carrier is read from the forwarders it exports  (X_real.<symbol>)
#   - the source is read from the log file it names        (<name>.log)
#   - the export table is read from the DLL itself, so no game has to be
#     installed for this to run
# It then rebuilds from that source and compares.
#
# Builds are not bit-reproducible -- the linker stamps a fresh build id every
# time -- so the comparison is on what the source decides: the export table,
# exactly, and the set of long printable strings, which is where the messages,
# the hook names and the format strings live.
#
# Needs llvm-mingw (or any x86_64-w64-mingw32 toolchain), same as the build
# scripts. Without one it says so and stops rather than reporting everything
# clean, because "no compiler" and "no drift" must never look the same.
#
# Part of MacGameVideoFix — https://github.com/MathiasKowoll/MacGameVideoFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PE="$HERE/pe.py"
VERBOSE=0
[ "${1:-}" = "--verbose" ] && VERBOSE=1

MINGW_BIN="${MINGW_BIN:-$HOME/.local/cxge/toolchains/llvm-mingw/bin}"
CC=""
for c in "$MINGW_BIN/x86_64-w64-mingw32-clang" "$MINGW_BIN/x86_64-w64-mingw32-gcc" \
         "$(command -v x86_64-w64-mingw32-clang 2>/dev/null || true)" \
         "$(command -v x86_64-w64-mingw32-gcc 2>/dev/null || true)"; do
  [ -n "$c" ] && [ -x "$c" ] && { CC="$c"; break; }
done
[ -n "$CC" ] || {
  echo "error: no x86_64-w64-mingw32 compiler found." >&2
  echo "       set MINGW_BIN, or install llvm-mingw:" >&2
  echo "       https://github.com/mstorsjo/llvm-mingw/releases" >&2
  exit 2
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# The one thing that cannot be read off a binary: ng4-observe.c compiles either
# as a probe or, with this flag, as the shipped repair. The DLL that ships is
# the repair.
flags_for() {
  case "$1" in
    ng4-observe.c) echo "-DNG4_FIX" ;;
    *)             echo "" ;;
  esac
}

# Strings the linker invents rather than the source: build ids, PDB names.
# Filtered so a fresh stamp does not read as a changed fix.
meaningful() {
  LC_ALL=C strings -a "$1" \
    | grep -aE '^[ -~]{12,}$' \
    | grep -avE 'RSDS|LLD PDB|\.pdb$' \
    | sort -u
}

drifted=0
checked=0
missing_source=0

for dll in "$HERE"/*.dll; do
  name="$(basename "$dll")"

  # Third-party binaries we redistribute rather than build. They cannot be
  # rebuilt from a source in this repository, so the proxy test below says
  # nothing useful about them -- and reported as unknown they read like drift.
  # What is checked instead is that they still hash to what was published.
  case "$name" in
    ng3-*.dll)
      want="$(grep -E "\`$name\`" "$HERE/ng3-THIRD-PARTY-LICENCES.md" 2>/dev/null | grep -oE '[0-9a-f]{64}' | tail -1)"
      have="$(shasum -a256 "$dll" | cut -d' ' -f1)"
      if [ -z "$want" ]; then
        printf '  %-32s ?  third-party, and no sha256 recorded for it\n' "$name"
      elif [ "$want" = "$have" ]; then
        printf '  %-32s ok   third-party, matches its recorded sha256\n' "$name"
      else
        printf '  %-32s DRIFTED  third-party, does not match its recorded sha256\n' "$name"
        drift=1
      fi
      continue ;;
  esac

  stem="$(LC_ALL=C strings -a "$dll" | grep -aoE '^[A-Za-z0-9_]+_real\.' | head -1 | sed 's/_real\.$//')"
  mark="$(LC_ALL=C strings -a "$dll" | grep -aoE '[A-Za-z0-9_-]+\.log' | head -1)"
  if [ -z "$stem" ] || [ -z "$mark" ]; then
    printf '  %-32s ?  not a proxy of ours, or it names neither carrier nor log\n' "$name"
    continue
  fi

  src=""
  for d in runtime diagnostics; do
    for candidate in "$ROOT/$d"/*.c; do
      [ -f "$candidate" ] || continue
      LC_ALL=C grep -qaF "\"$mark\"" "$candidate" 2>/dev/null && { src="$candidate"; break 2; }
      LC_ALL=C grep -qaF "$mark" "$candidate" 2>/dev/null && { src="$candidate"; break 2; }
    done
  done
  if [ -z "$src" ]; then
    printf '  %-32s ?  names %s and no source writes that\n' "$name" "$mark"
    missing_source=$((missing_source + 1))
    continue
  fi

  out="$TMP/$name.d"; mkdir -p "$out"
  def="$out/$stem.def"
  {
    echo "LIBRARY $stem.dll"
    echo "EXPORTS"
    python3 "$PE" exports "$dll" --ordinals |
      while read -r ordinal sym; do
        printf '    %s = %s_real.%s @%s\n' "$sym" "$stem" "$sym" "$ordinal"
      done
  } > "$def"

  if ! "$CC" -shared -O2 $(flags_for "$(basename "$src")") -I"$HERE" \
        -o "$out/$stem.dll" "$src" "$def" \
        -Wl,--enable-stdcall-fixup \
        -lmfuuid -lole32 -luuid -lshlwapi -lkernel32 -static-libgcc 2>"$out/err"; then
    printf '  %-32s !! %s does not compile\n' "$name" "$(basename "$src")"
    [ "$VERBOSE" = 1 ] && sed 's/^/       /' "$out/err"
    drifted=$((drifted + 1)); continue
  fi

  checked=$((checked + 1))
  python3 "$PE" exports "$dll"           | sort > "$out/exp.old"
  python3 "$PE" exports "$out/$stem.dll" | sort > "$out/exp.new"
  meaningful "$dll"           > "$out/str.old"
  meaningful "$out/$stem.dll" > "$out/str.new"

  exp_delta="$(comm -3 "$out/exp.old" "$out/exp.new" | wc -l | tr -d ' ')"
  gone="$(comm -23 "$out/str.old" "$out/str.new")"
  came="$(comm -13 "$out/str.old" "$out/str.new")"
  n_gone="$(printf '%s' "$gone" | grep -c . )"
  n_came="$(printf '%s' "$came" | grep -c . )"

  if [ "$exp_delta" = 0 ] && [ "$n_gone" = 0 ] && [ "$n_came" = 0 ]; then
    printf '  %-32s ok   %s\n' "$name" "$(basename "$src")"
    continue
  fi

  drifted=$((drifted + 1))
  printf '  %-32s DRIFT  %s  (exports %s, strings -%s +%s)\n' \
    "$name" "$(basename "$src")" "$exp_delta" "$n_gone" "$n_came"
  if [ "$VERBOSE" = 1 ]; then
    printf '%s\n' "$gone" | grep . | head -8 | sed 's/^/       shipped only: /'
    printf '%s\n' "$came" | grep . | head -8 | sed 's/^/       source only:  /'
  fi
done

# Nobody else's bottle names in anything we ship.
#
# Bottle names are made up by whoever makes the bottle: ours are not a fact
# about anyone else's machine, and they are not ours to publish. They had leaked
# twice -- once into the wiki, once into comments inside installers that travel
# in the fixes bundle -- so this reads the names actually on THIS machine and
# refuses to find them in anything that goes out.
#
# A bottle simply called "Steam" cannot be checked this way and is not: the word
# appears legitimately all over a project about Steam games. Names that are
# merely the default are not the ones that identify somebody's setup anyway.
echo
name_leak=0
while IFS= read -r root; do
  for b in "$root"/*/; do
    [ -d "$b" ] || continue
    n="$(basename "${b%/}")"
    [ ${#n} -gt 5 ] || continue
    case "$n" in Steam|steam) continue ;; esac
    hits="$(grep -rl -- "$n" "$HERE" "$ROOT/crossover" "$ROOT/wiki" "$ROOT/README.md" 2>/dev/null \
            | grep -v '/check-builds\.sh$' || true)"
    [ -n "$hits" ] || continue
    echo "  bottle name leaked: \"$n\" appears in" >&2
    printf '%s\n' "$hits" | sed "s|$ROOT/|       |" >&2
    name_leak=1
  done
done < <(. "$HERE/bottles.sh"; bottle_roots)
[ "$name_leak" = 0 ] && echo "  no bottle name of this machine appears in anything shipped or published"

# The MGVF-GAME lines against the app's own table.
#
# Those lines are a second copy of something the app already knows -- name,
# executable, which installer -- and this repository has been bitten by a second
# copy often enough to know what happens next. They exist because the fixes
# tarball ships the installers and not the app, so a launcher applying one fix
# to one game has nowhere else to read it from. This is what makes the copy
# safe: it is compared on every run, and the executable is the identity, since
# there is no AppID anywhere here and the folder name is Valve's to choose.
echo
decl_drift=0
if python3 - "$ROOT" <<'PY'
import re, sys, pathlib
root = pathlib.Path(sys.argv[1])
sw = (root/"app/MacGameVideoFix.swift").read_text()

def switch(anchor):
    i = sw.index(anchor); body = sw[i:sw.index("\n    }", i)]
    out = {}
    for m in re.finditer(r'case ([^:\n]+):\s*return ("([^"]*)"|nil)', body):
        for c in m.group(1).split(","):
            out[c.strip().lstrip(".")] = m.group(3)
    d = re.search(r'default:\s*return "([^"]*)"', body)
    return out, (d.group(1) if d else None)

names, _ = switch("    var name: String {")
exes, _ = switch("    var executable: String? {")
inst, di = switch("    var installer: String {")
# A case with no executable is a catch-all, not a title a launcher can match.
want = {names[c]: (exes[c], inst.get(c, di)) for c in names if exes.get(c)}

got = {}
for f in sorted((root/"runtime").glob("install-*.sh")):
    for line in re.findall(r'^# MGVF-GAME: (.+)$', f.read_text(), re.M):
        parts = [x.strip() for x in line.split("|")]
        name, exe = parts[0], (parts[1] if len(parts) > 1 else "")
        got[name] = (exe, f.name)

bad = []
note = []
for name, (exe, script) in sorted(got.items()):
    if name not in want:
        # A bottle-scoped installer is not in the app's LIST. The reason has now
        # changed twice, so it is worth being exact about what is true today.
        #
        # Originally: the app asked for a game folder and could not know the
        # bottle, so it could not run this installer at all.
        #
        # That stopped being true when the wizard gained a bottle picker: the app
        # sets MGVF_BOTTLE from the bottle a person chose, and install-ng3-fix.sh
        # takes a bottle as its first argument. So the app CAN run it. It does not
        # yet, because nobody has wired the title into the app's table -- and that
        # is a different sentence from "it cannot", which is what this note used
        # to say. Whoever adds it should know the pieces are already there.
        #
        # This note used to end "-- a launcher runs it", and on that reasoning the
        # files were kept out of the bundle as well. That was right while a
        # launcher downloaded the fixes tarball separately. It inverted on
        # 2026-08-31, when the launcher became the app's HOST instead: a launcher
        # that embeds this bundle knows the bottle and can run the installer, but
        # only if the files travelled with it. So they ship now, and the check
        # below says whether they actually did -- because a subset payload fails
        # as "this title has no fix", which is not a sentence anyone can act on.
        #
        # Replaced rather than deleted. An exception that simply disappears reads
        # later as though it was never reasoned about, which is how a note saying
        # four errors were expected sat over a real defect for months.
        if "MGVF-SCOPE: bottle" in (root/"runtime"/script).read_text():
            shipped = (root/"app"/"MacGameVideoFix.app"/"Contents"/"Resources"/script)
            if shipped.exists():
                note.append(f"{name}: bottle-scoped, so not in the app's list "
                            f"({script}) -- but it ships, for a host launcher to run")
            else:
                bad.append(f"{name}: bottle-scoped and {script} does NOT ship; "
                           f"a launcher hosting the app cannot fix this title")
            continue
        bad.append(f"declared but not a game the app knows: {name}"); continue
    if want[name] != (exe, script):
        bad.append(f"{name}: declares {exe} via {script}, app says {want[name][0]} via {want[name][1]}")
for name in sorted(want):
    if name not in got:
        bad.append(f"the app has {name} and no installer declares it")

for n in note:
    print(f"  declaration: {n}")
for b in bad:
    print(f"  declaration: {b}")
sys.exit(1 if bad else 0)
PY
then
  echo "  declarations: every MGVF-GAME line agrees with the app's table"
else
  decl_drift=1
fi

# The other copy of every one of these: the app runs the bundle's, not these.
echo
bundle="$ROOT/app/MacGameVideoFix.app/Contents/Resources"
bundle_drift=0
if [ -d "$bundle" ]; then
  for f in "$HERE"/*.dll "$HERE"/install-*.sh "$HERE"/stage-codecs.sh "$HERE"/pe.pl; do
    [ -f "$f" ] || continue
    b="$bundle/$(basename "$f")"
    [ -f "$b" ] || continue
    cmp -s "$f" "$b" || { echo "  bundle stale: $(basename "$f")"; bundle_drift=$((bundle_drift + 1)); }
  done
  [ "$bundle_drift" = 0 ] && echo "  bundle: every copy matches runtime/"

# Everything the fixes tarball ships must also be in the app.
#
# The two artefacts are for different consumers -- the tarball for anyone who is
# not the launcher, the app for a launcher that hosts it -- and they are allowed
# to differ. They are not allowed to differ BY OMISSION. A launcher embedding an
# app that is a subset of the tarball fails as "this title has no fix", which is
# not a sentence anyone can act on, and it fails at a user rather than here.
#
# Written the day this stopped being hypothetical: the app was missing NINJA
# GAIDEN 3's four DLLs, its installer, its licence file and manifest.json.
if [ -d "$ROOT/app/MacGameVideoFix.app/Contents/Resources" ]; then
  tb="$(mktemp -d)"
  if "$HERE/make-fixes-bundle.sh" "$tb" >/dev/null 2>&1; then
    missing=$(comm -23 \
      <(tar tzf "$(ls "$tb"/fixes-*.tar.gz | head -1)" | sed 's|.*/||' | grep -v '^$' | sort -u) \
      <(ls "$ROOT/app/MacGameVideoFix.app/Contents/Resources" | sort -u))
    if [ -z "$missing" ]; then
      echo "  payload: everything the fixes bundle ships is also in the app"
    else
      echo "  payload: the app is a SUBSET of the fixes bundle -- missing:"
      printf '%s\n' "$missing" | sed 's/^/    /'
      drift=1
    fi
  fi
  rm -rf "$tb"
fi
else
  echo "  bundle: not built"
fi

# The payload folder is the same bytes as the flat engine-* files, laid out the
# way an engine is so a patcher can overlay it. Two copies drift; that is the
# whole reason this check exists.
echo
payload_drift=0
for pair in "engine-winegstreamer.dll:wine/x86_64-windows/winegstreamer.dll" \
            "engine-winegstreamer.so:wine/x86_64-unix/winegstreamer.so" \
            "engine-built-for.json:built-for.json"; do
  flat="$HERE/${pair%%:*}"
  laid="$HERE/engine-payload/${pair##*:}"
  if [ ! -f "$flat" ] || [ ! -f "$laid" ]; then
    echo "  payload missing: ${pair##*:}"; payload_drift=$((payload_drift + 1)); continue
  fi
  cmp -s "$flat" "$laid" || { echo "  payload drifted: ${pair##*:}"; payload_drift=$((payload_drift + 1)); }
done
[ "$payload_drift" = 0 ] && echo "  payload: engine-payload/ matches the flat engine-* files"

# The codecs are other people's binaries copied out of winevideo's build, so
# they are checked the way the NINJA GAIDEN 3 DLLs are: against a recorded
# hash. Three of these sat in an engine for days while nobody could say where
# they came from, which is the whole argument for the table.
codec_bad=0
LIC="$HERE/engine-payload/CODEC-LICENCES.md"
if [ -f "$LIC" ]; then
  while IFS='|' read -r _ rel bytes sha _; do
    rel="$(echo "$rel" | tr -d ' `')"; sha="$(echo "$sha" | tr -d ' `')"
    case "$rel" in lib64/*) ;; *) continue ;; esac
    f="$HERE/engine-payload/$rel"
    if [ ! -f "$f" ]; then
      echo "  codec missing: $rel"; codec_bad=$((codec_bad + 1)); continue
    fi
    have="$(shasum -a 256 "$f" | cut -d' ' -f1)"
    [ "$have" = "$sha" ] || { echo "  codec drifted: $rel"; codec_bad=$((codec_bad + 1)); }
  done < "$LIC"
  [ "$codec_bad" = 0 ] && echo "  codecs: all match the sha256s in CODEC-LICENCES.md"
else
  echo "  codecs: no CODEC-LICENCES.md"; codec_bad=1
fi

echo
echo "  $checked rebuilt and compared, $drifted drifted, $missing_source unattributed"
[ "$drifted" = 0 ] && [ "$bundle_drift" = 0 ] && [ "$missing_source" = 0 ] \
  && [ "$decl_drift" = 0 ] && [ "$name_leak" = 0 ] && [ "$payload_drift" = 0 ] \
  && [ "$codec_bad" = 0 ] || exit 1
exit 0
