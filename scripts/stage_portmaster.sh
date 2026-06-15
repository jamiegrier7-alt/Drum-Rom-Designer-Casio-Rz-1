#!/usr/bin/env bash
set -euo pipefail

# Stage a PortMaster-ready folder tree under STAGE_DIR.
# This does not create a final .zip archive, it prepares a clean staging folder.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build-portmaster}"
STAGE_DIR="${STAGE_DIR:-${ROOT_DIR}/dist/portmaster}"
GAME_DIR="${STAGE_DIR}/drumrom"

mkdir -p "${GAME_DIR}"
mkdir -p "${GAME_DIR}/roms"
mkdir -p "${GAME_DIR}/settings"
mkdir -p "${STAGE_DIR}"

if [[ ! -f "${BUILD_DIR}/drumrom_handheld" ]]; then
  echo "Missing binary: ${BUILD_DIR}/drumrom_handheld"
  echo "Run scripts/build_portmaster.sh first."
  exit 1
fi

cp "${BUILD_DIR}/drumrom_handheld" "${GAME_DIR}/drumrom_handheld"
chmod +x "${GAME_DIR}/drumrom_handheld"

for d in configs kits presets samples; do
  if [[ -d "${ROOT_DIR}/${d}" ]]; then
    rm -rf "${GAME_DIR:?}/${d}"
    cp -a "${ROOT_DIR}/${d}" "${GAME_DIR}/${d}"
  fi
done

cat > "${GAME_DIR}/launch.sh" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail

GAMEDIR="$(cd "$(dirname "$0")" && pwd)"
cd "$GAMEDIR"

export HOME="$GAMEDIR"
export XDG_CONFIG_HOME="$GAMEDIR/.config"
export SDL_AUDIODRIVER=alsa

mkdir -p "$GAMEDIR/.config"
mkdir -p "$GAMEDIR/roms"
mkdir -p "$GAMEDIR/settings"

./drumrom_handheld > "$GAMEDIR/run.log" 2>&1
EOF

chmod +x "${GAME_DIR}/launch.sh"

cat > "${STAGE_DIR}/DrumRomDesigner.sh" << 'EOF'
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/drumrom"
exec ./launch.sh
EOF

chmod +x "${STAGE_DIR}/DrumRomDesigner.sh"

echo "Staged PortMaster layout at: ${STAGE_DIR}"
echo "Main launcher: ${STAGE_DIR}/DrumRomDesigner.sh"
