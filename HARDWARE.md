# Hardware

The development target is a Linux box that can run QEMU with KVM acceleration
*and* nest another QEMU inside a guest (for VFIO passthrough). macOS hosts were
ruled out — Lima does not expose nested KVM cleanly — so the target is cloud.

## The constraint that picks the machine

Octopus needs **KVM in the L1 VM**, which means the cloud VM must support
**nested virtualization**. On GCP that rules out E2, the memory-optimized
M-series, the AMD-based families (N2D/C3D/C4D), and the Arm families — none
support nested virt. C4, N2, and N4 do; C4 is fastest in current benchmarks.

## The pick

**`c4-highcpu-8` in `asia-northeast1-b` (Tokyo), 50 GB Hyperdisk Balanced,
Ubuntu 24.04 LTS** — created with `--enable-nested-virtualization`.

- **C4 over N2**: ~2.3× faster on a nested-VM build workload at similar price;
  optimize wall clock.
- **Intel over AMD**: VFIO/IOMMU tooling and `-device intel-iommu` are
  Intel-canonical, and AMD VMs do not support nested virt on GCP anyway.
- **`highcpu-8`**: 16 GB RAM is enough for QEMU + a guest + a build shell.

## Spin-up

```sh
gcloud compute instances create octopus-dev \
  --zone=asia-northeast1-b \
  --machine-type=c4-highcpu-8 \
  --image-family=ubuntu-2404-lts-amd64 --image-project=ubuntu-os-cloud \
  --boot-disk-size=50GB --boot-disk-type=hyperdisk-balanced \
  --enable-nested-virtualization
```

Then `gcloud compute ssh octopus-dev --zone=asia-northeast1-b`. Add the user to
the `kvm` group (`sudo usermod -aG kvm $USER`, then reconnect) — Ubuntu's
`/dev/kvm` is mode 660 owned `root:kvm`. Install build deps:
`build-essential ninja-build pkg-config libglib2.0-dev libpixman-1-dev
libslirp-dev python3-venv git linux-headers-$(uname -r)`.

## Lifecycle

- **Stop when walking away** — a stopped instance bills only for disk (~$5/mo at
  50 GB), not compute:
  `gcloud compute instances stop octopus-dev --zone=asia-northeast1-b`.
- **Delete when done** —
  `gcloud compute instances delete octopus-dev --zone=asia-northeast1-b`.

## Cost ballpark

`c4-highcpu-8` on-demand is ~$0.34/hr in `asia-northeast1`; Hyperdisk Balanced
50 GB ~$6/mo. Active development at a few hours a day is ~$30/mo compute + disk.

## Dependency cooldown

Never install a package version published less than 7 days ago; take security
patches immediately.

## scripts/verify.sh

Ships the working-tree `qemu-device/` to the dev VM (`OCTOPUS_VM`, default
`octopus-dev`), rebuilds QEMU with the device wired in, and verifies it
registers and that `realize()` does not crash. One build-and-check cycle, no
commit required.
