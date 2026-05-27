#!/usr/bin/env bash
# verify.sh — one cycle of METHOD.md step 2c.
#
# Ships the current state of qemu-device/ to the dev VM, rebuilds QEMU
# with the device wired in, and verifies the device loads. Uses
# git ls-files so untracked junk doesn't leak. No commit required —
# picks up working-tree state.
#
# Env overrides: MXGPU_VM (default mxgpu-dev), MXGPU_ZONE (default
# asia-northeast1-b).

set -euo pipefail

VM="${MXGPU_VM:-mxgpu-dev}"
ZONE="${MXGPU_ZONE:-asia-northeast1-b}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# --- 1. Build tarball of qemu-device/ (tracked + untracked-not-ignored).
TARBALL=$(mktemp -t mxgpu-push.XXXXXX).tar
trap 'rm -f "$TARBALL"' EXIT

FILES=$(git ls-files -co --exclude-standard -- qemu-device)
if [[ -z "$FILES" ]]; then
  echo "verify.sh: no files under qemu-device/ (gitignored or missing?)" >&2
  exit 1
fi
echo "$FILES" | tar -cf "$TARBALL" --files-from=-

# --- 2. Ship to VM.
gcloud compute scp "$TARBALL" "$VM:/tmp/mxgpu-push.tar" \
  --zone="$ZONE" --quiet

# --- 3. Apply, register, build, verify (one SSH round-trip).
gcloud compute ssh "$VM" --zone="$ZONE" --quiet --command='
set -euo pipefail

# Unpack into a clean staging dir, then mirror mxgpu_mini.c into QEMU.
rm -rf /tmp/mxgpu-staging && mkdir /tmp/mxgpu-staging
tar -xf /tmp/mxgpu-push.tar -C /tmp/mxgpu-staging
cp /tmp/mxgpu-staging/qemu-device/mxgpu_mini.c ~/qemu/hw/misc/mxgpu_mini.c

# Idempotent registration in QEMU build system.
if ! grep -q MXGPU_MINI ~/qemu/hw/misc/Kconfig; then
  printf "\nconfig MXGPU_MINI\n    bool\n    default y if PCI_DEVICES\n    depends on PCI\n" \
    >> ~/qemu/hw/misc/Kconfig
  echo "registered MXGPU_MINI in hw/misc/Kconfig"
fi
if ! grep -q mxgpu_mini.c ~/qemu/hw/misc/meson.build; then
  echo "system_ss.add(when: '"'"'CONFIG_MXGPU_MINI'"'"', if_true: files('"'"'mxgpu_mini.c'"'"'))" \
    >> ~/qemu/hw/misc/meson.build
  echo "registered mxgpu_mini.c in hw/misc/meson.build"
fi

# Build (incremental). If meson config drifted, ninja re-runs meson itself.
cd ~/qemu/build
ninja qemu-system-x86_64 2>&1 | tail -5

# Verify 1: device type is registered.
if ! ./qemu-system-x86_64 -device help 2>&1 | grep -q mxgpu-mini; then
  echo "FAIL: mxgpu-mini not in -device help" >&2
  exit 1
fi
echo "ok: mxgpu-mini registered with qemu-system-x86_64"

# Verify 2: realize() does not crash on bare instantiation.
set +e
timeout 2 ./qemu-system-x86_64 -nographic -display none -monitor null -serial null \
  -machine pc -m 128 -accel kvm -device mxgpu-mini > /tmp/qemu-verify.log 2>&1
RC=$?
set -e
# 124 = timeout (good: still running), 0 = clean exit (also good).
if [[ $RC -ne 124 && $RC -ne 0 ]]; then
  echo "FAIL: realize() exited $RC" >&2
  tail -20 /tmp/qemu-verify.log >&2
  exit 1
fi
echo "ok: realize() does not crash"
echo "=== verify.sh OK ==="
'
