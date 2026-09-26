#!/usr/bin/env bash
# run.sh <shader dir> <out dir> <hash>...  -- rewrite, compile and check each
# pixel shader ps_<hash>.dxbc. Needs TOOLS (dxil-spirv, SPIRV-Cross, dxc x64),
# LLVM_DIS, and a CrossOver bottle to run dxc in (CX_ROOT, CX_APP, BOTTLE).
set -u
IN=$1; OUT=$2; shift 2
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$CX_ROOT/$BOTTLE/drive_c/ctl-build"; mkdir -p "$B" "$OUT"
for f in dxc.exe dxcompiler.dll dxil.dll; do [ -f "$B/$f" ] || cp "$TOOLS/dxc/bin/x64/$f" "$B/"; done
for h in "$@"; do
  w="$OUT/$h"; mkdir -p "$w"
  "$TOOLS/dxil-spirv/build/dxil-spirv" "$IN/ps_$h.dxbc" --output "$w/a.spv" >/dev/null 2>&1 || { echo "$h: dxil-spirv failed"; continue; }
  "$TOOLS/SPIRV-Cross/build/spirv-cross" "$w/a.spv" --hlsl --shader-model 62 --output "$w/raw.hlsl" >/dev/null 2>&1 || { echo "$h: spirv-cross failed"; continue; }
  python3 "$HERE/extract_dxil.py" "$IN/ps_$h.dxbc" "$w/a.bc" && "$LLVM_DIS" "$w/a.bc" -o "$w/a.ll" || { echo "$h: llvm-dis failed"; continue; }
  msg=$(python3 "$HERE/fixshader.py" "$w/a.ll" "$w/raw.hlsl" "$w/fixed.hlsl" 2>&1) || { echo "$h: $msg"; continue; }
  cp "$w/fixed.hlsl" "$B/$h.hlsl"; rm -f "$B/$h.dxbc"
  CX_BOTTLE_PATH="$CX_ROOT" "$CX_APP/bin/wine" --bottle "$BOTTLE" 'C:\ctl-build\dxc.exe' -T ps_6_2 -E main -O3 -HV 2021 \
      "C:\\ctl-build\\$h.hlsl" -Fo "C:\\ctl-build\\$h.dxbc" > "$w/dxc.txt" 2>&1
  [ -f "$B/$h.dxbc" ] || { echo "$h: dxc failed: $(grep -m3 ' error' "$w/dxc.txt")"; continue; }
  cp "$B/$h.dxbc" "$w/fixed.dxbc"
  echo "$h: $msg; $(python3 "$HERE/check.py" "$IN/ps_$h.dxbc" "$w/fixed.dxbc")"
done
