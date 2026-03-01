#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <binary_path> <output_dir>"
  exit 1
fi

BIN_PATH="$1"
OUT_DIR="$2"
APP_NAME="av_service"
BUNDLE_DIR="${OUT_DIR}/av-service-full"
LIB_DIR="${BUNDLE_DIR}/lib"

mkdir -p "${LIB_DIR}"
cp "${BIN_PATH}" "${BUNDLE_DIR}/${APP_NAME}"
chmod +x "${BUNDLE_DIR}/${APP_NAME}"

# copy dependent shared libraries for direct run on clean Linux hosts
ldd "${BIN_PATH}" | awk '/=> \/.* \(/ {print $3}' | while read -r lib; do
  [ -f "$lib" ] || continue
  cp -L "$lib" "${LIB_DIR}/"
done

cat > "${BUNDLE_DIR}/run.sh" <<'RUNEOF'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="${SCRIPT_DIR}/lib:${LD_LIBRARY_PATH:-}"
exec "${SCRIPT_DIR}/av_service" "$@"
RUNEOF
chmod +x "${BUNDLE_DIR}/run.sh"

( cd "${OUT_DIR}" && tar -czf av-service-full.tar.gz av-service-full )

echo "bundle generated: ${OUT_DIR}/av-service-full.tar.gz"
