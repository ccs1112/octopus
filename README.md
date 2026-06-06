# Octopus

> An end-to-end virtual GPU.

An SR-IOV GPU virtualization stack: a synthetic PCIe device for QEMU and the
Linux PF/VF drivers that drive it. Implements the same surface as AMD's
MxGPU/GIM stack — PCIe, SR-IOV, BAR partitioning, world-switch scheduling,
DMA, IOMMU, VFIO passthrough.

One physical function drives many virtual functions that share the device, the
way one octopus brain drives many semi-autonomous arms: the PF time-shares the
GPU across VFs through a world-switch scheduler.

## Components

- `qemu-device/` — the synthetic Octopus PCIe device, built into a QEMU tree
- `pf-driver/` — the Linux Physical Function driver, an out-of-tree kernel module
- `guest/` — userspace exercisers that drive the device from inside the guest

See [DESIGN.md](DESIGN.md) for the architecture and roadmap, and
[HARDWARE.md](HARDWARE.md) for the development substrate.

## Scope

| Phase | Component | Status |
|------:|-----------|--------|
| 1 | QEMU device: BAR0 (MMIO) + BAR2 (scratch RAM), MSI-X, command ring | in progress |
| 2 | SR-IOV capability; VF spawn on `NumVFs` write; VF BAR partitioning | planned |
| 3 | PF driver: probe, BAR remap, MSI-X, world-switch scheduler | planned |
| 4 | DMA descriptor engine; IOMMU mapping via the streaming DMA API | planned |
| 5 | VFIO passthrough into a nested guest; guest VF driver | planned |
| 6 | A GEMM workload — the device runs a matmul, not just a memset | planned |

## Building

PF driver (against the running kernel's headers):

```sh
cd pf-driver && make
sudo insmod octopus_pf.ko
```

QEMU device: drop `qemu-device/octopus.c` into a QEMU source tree under
`hw/misc/`, add a `config OCTOPUS` entry to `hw/misc/Kconfig`, append the source
to `hw/misc/meson.build`, build QEMU, then run with `-device octopus`.

## License

GPL-2.0. The kernel module and QEMU device are GPL-bound by their headers; the
rest of the repo follows for consistency. See [LICENSE](LICENSE).
