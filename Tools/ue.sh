#!/bin/bash
# Heatline MX build / test helper (macOS).
#   Tools/ue.sh build        compile the editor target
#   Tools/ue.sh content      create materials / data assets / blueprints / map (editor Python)
#   Tools/ue.sh tests        headless automation tests (HeatlineMX.*)
#   Tools/ue.sh game [args]  run the game (standalone, 1920x1080 windowed) with extra args
#   Tools/ue.sh autotest <loop|loopkb|perf|designer|shots>  scripted end-to-end run
#   Tools/ue.sh package      cook + stage a Mac build and copy the app into Build/Mac
#   Tools/ue.sh packaged-autotest <scenario>  run a scripted scenario on the packaged app
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJ="$ROOT/HeatlineMX.uproject"

find_ue() {
  if [[ -n "${UE_ROOT:-}" && -d "$UE_ROOT/Engine" ]]; then echo "$UE_ROOT"; return; fi
  local dat="$HOME/Library/Application Support/Epic/UnrealEngineLauncher/LauncherInstalled.dat"
  if [[ -f "$dat" ]]; then
    local loc
    loc=$(python3 -c "import json,sys;d=json.load(open(sys.argv[1]));L=[i['InstallLocation'] for i in d.get('InstallationList',[]) if i.get('AppName','').startswith('UE_')];print(sorted(L)[-1] if L else '')" "$dat")
    if [[ -n "$loc" && -d "$loc/Engine" ]]; then echo "$loc"; return; fi
  fi
  local c
  c=$(ls -d "/Users/Shared/Epic Games"/UE_5* 2>/dev/null | sort -V | tail -1 || true)
  if [[ -n "$c" ]]; then echo "$c"; return; fi
  echo "Unreal Engine not found (set UE_ROOT)" >&2; exit 1
}

UE="$(find_ue)"
EDITOR="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
[[ -x "$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" ]] && EDITOR_CMD="$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" || EDITOR_CMD="$EDITOR"
LOGS="$ROOT/Saved/Logs"
mkdir -p "$LOGS" "$ROOT/Saved/AutoTest"

case "${1:-}" in
  build)
    "$UE/Engine/Build/BatchFiles/Mac/Build.sh" HeatlineMXEditor Mac Development -Project="$PROJ" -WaitMutex -NoHotReloadFromIDE 2>&1 | tee "$LOGS/build_editor.log"
    ;;
  content)
    "$EDITOR_CMD" "$PROJ" -run=pythonscript -script="$ROOT/Tools/build_content.py" -unattended -nosplash -nop4 -nosound -stdout -FullStdOutLogOutput 2>&1 | tee "$LOGS/content.log" | grep -E "HEATLINE CONTENT|Error|error:" || true
    ;;
  tests)
    "$EDITOR_CMD" "$PROJ" -ExecCmds="Automation RunTests HeatlineMX; Quit" -unattended -nullrhi -nosound -nosplash -nop4 -stdout -FullStdOutLogOutput \
      -ReportExportPath="$ROOT/Saved/AutomationReport" 2>&1 | tee "$LOGS/tests.log" | grep -E "Test Completed|Result=|Error|Warning: .*HeatlineMX|AUTOMATION" || true
    ;;
  game)
    shift
    "$EDITOR" "$PROJ" -game -windowed -ResX=1920 -ResY=1080 -nosplash -log "$@"
    ;;
  autotest)
    scen="${2:-loop}"; shift 2 || true
    "$EDITOR" "$PROJ" -game -windowed -ResX=1920 -ResY=1080 -nosplash -unattended -mxautotest="$scen" -mxquit -stdout -FullStdOutLogOutput "$@" 2>&1 | tee "$LOGS/autotest_$scen.log" | grep -E "AUTOTEST|Fatal|Ensure" || true
    ;;
  package)
    "$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun -project="$PROJ" -platform=Mac -clientconfig=Development -build -cook -stage -pak \
      -noP4 -utf8output -nocompileeditor -unattended 2>&1 | tee "$LOGS/package.log" | grep -E "BUILD SUCCESSFUL|BUILD FAILED|Error|AutomationTool exiting" || true
    # UE 5.8's archive step copies the bare Binaries/Mac app (no content); the staged app has the paks in Contents/UE.
    STAGED="$ROOT/Saved/StagedBuilds/Mac/HeatlineMX.app"
    if [[ -d "$STAGED/Contents/UE" ]]; then
      mkdir -p "$ROOT/Build/Mac"
      rsync -a --delete "$STAGED/" "$ROOT/Build/Mac/HeatlineMX.app/"
      echo "Packaged app: $ROOT/Build/Mac/HeatlineMX.app ($(du -sh "$ROOT/Build/Mac/HeatlineMX.app" | cut -f1))"
    else
      echo "Staged app not found at $STAGED" >&2; exit 1
    fi
    ;;
  packaged-autotest)
    scen="${2:-loop}"; shift 2 || true
    APP="$ROOT/Build/Mac/HeatlineMX.app/Contents/MacOS/HeatlineMX"
    "$APP" -windowed -ResX=1920 -ResY=1080 -nosplash -unattended -mxautotest="$scen" -mxquit -stdout -FullStdOutLogOutput "$@" 2>&1 | tee "$LOGS/packaged_$scen.log" | grep -E "AUTOTEST|Fatal|Ensure" || true
    ;;
  *)
    sed -n '2,9p' "$0"; echo "UE: $UE"
    ;;
esac
