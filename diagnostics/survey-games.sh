#!/usr/bin/env bash
#
# Survey installed games for the two things that decide whether their videos
# will play under CrossOver: what the cutscenes are encoded as, and which API
# the game uses to play them.
#
#   survey-games.sh <steamapps/common> [more dirs...]
#
# Prints one TSV row per game: engine, video assets, codec, media API.
# Feeds the compatibility list in the wiki -- so that list stays a record of
# what was measured rather than what was remembered.
#
# Part of MortalShell2MacFix — https://github.com/MathiasKowoll/MortalShell2MacFix
# SPDX-License-Identifier: GPL-3.0-or-later

set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PE="$HERE/../runtime/pe.py"

usage() { sed -n '3,11p' "$0" >&2; exit 1; }
[ $# -ge 1 ] || usage

printf 'game\tengine\tvideos\tformats\tcodec\tmedia API\n'

for root in "$@"; do
  [ -d "$root" ] || continue
  for game in "$root"/*/; do
    name="$(basename "$game")"
    [ -d "$game" ] || continue

    # Engine. Unreal and Unity announce themselves; everything else is the
    # studio's own and has to be looked at individually.
    if [ -d "$game/Engine/Binaries" ]; then engine="Unreal"
    elif find "$game" -maxdepth 2 -name 'UnityPlayer.dll' -print -quit 2>/dev/null | grep -q .; then engine="Unity"
    elif find "$game" -maxdepth 2 -iname '*.exe' -print -quit 2>/dev/null | grep -q .; then engine="custom"
    else engine="?"; fi

    # Video assets. Bink and Criware bring their own decoders and never touch
    # Media Foundation, so they are worth telling apart from the rest.
    formats=""; total=0
    for ext in webm mp4 bk2 bik usm mkv wmv ogv; do
      n=$(find "$game" -iname "*.$ext" 2>/dev/null | head -4000 | wc -l | tr -d ' ')
      [ "$n" -gt 0 ] && { formats="$formats ${ext}:${n}"; total=$((total + n)); }
    done
    formats="${formats# }"

    # Loose files are only half the story: Unreal keeps its movies inside the
    # .pak, and the pak wins over disk. Ask the index how many are in there, so
    # a game does not get recorded as having no videos when it has hundreds.
    paked=0
    if [ "$engine" = "Unreal" ]; then
      pak=$(find "$game" -name 'pakchunk0*.pak' 2>/dev/null | head -1)
      [ -n "$pak" ] && paked=$(python3 "$HERE/../scripts/pak-hide-videos.py" "$pak" 2>/dev/null |
                               sed -n 's/^\([0-9][0-9]*\) video entries.*/\1/p' | head -1)
      [ -n "$paked" ] || paked=0
    fi
    if [ "$paked" -gt 0 ]; then
      formats="$formats in-pak:$paked"
      total=$((total + paked))
    fi

    formats="${formats# }"
    [ -n "$formats" ] || formats="-"

    # Codec of one sample. Container tells us little; VP9 is the interesting one.
    codec="-"
    if [ "$total" -gt 0 ] && command -v ffprobe >/dev/null; then
      sample=$(find "$game" \( -iname '*.webm' -o -iname '*.mp4' -o -iname '*.mkv' \) 2>/dev/null | head -1)
      [ -n "$sample" ] && codec=$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=codec_name -of csv=p=0 "$sample" 2>/dev/null | head -1)
      [ -n "$codec" ] || codec="?"
    fi

    # Which media API the main executable is linked against. The biggest .exe
    # is the game in practice; launchers and crash handlers are small.
    # -print0/-0: a game called "Ghost of Tsushima DIRECTOR'S CUT" is enough to
    # make xargs eat the path if the delimiter is whitespace.
    exe=$(find "$game" -iname '*.exe' -size +8M -print0 2>/dev/null |
          xargs -0 -I{} stat -f '%z %N' {} 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)
    api="-"
    if [ -n "$exe" ]; then
      dlls=$(python3 "$PE" imports "$exe" --dlls 2>/dev/null | tr 'A-Z' 'a-z')
      hits=""
      case "$dlls" in *mfreadwrite*)  hits="$hits MFSourceReader";; esac
      case "$dlls" in *mfplat*)       hits="$hits mfplat";; esac
      case "$dlls" in *quartz*)       hits="$hits DirectShow";; esac
      case "$dlls" in *bink*)         hits="$hits Bink";; esac
      case "$dlls" in *cri*)          hits="$hits CriWare";; esac
      hits="${hits# }"
      [ -n "$hits" ] && api="$hits" || api="none (bundled decoder)"
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$name" "$engine" "$total" "$formats" "$codec" "$api"
  done
done
