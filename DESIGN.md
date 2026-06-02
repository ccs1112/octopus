# Octopus — design

Octopus is an SR-IOV GPU virtualization stack built from the substrate up: a
synthetic PCIe device for QEMU and the Linux PF/VF drivers that drive it. It
walks the same surface as AMD's MxGPU/GIM stack — PCIe, SR-IOV, BAR
partitioning, world-switch scheduling, DMA, IOMMU, and VFIO passthrough.

The name: one physical function drives many virtual functions that share the
device, the way one octopus brain drives many semi-autonomous arms. The PF
time-shares the GPU across VFs through a world-switch scheduler.

## Architecture

### Synthetic PCIe device (`qemu-device/octopus.c`)

A QEMU device falls out of one `TypeInfo` struct: a `PCIDevice` with two BARs
(a 4 KiB MMIO register window and a 1 MiB host-RAM-backed scratch region),
MSI-X interrupts, and a one-entry command ring that executes an op when the
guest rings a doorbell register. Every primitive a real GPU exposes for command
submission, in a few hundred lines of C. SR-IOV is then a PCIe extended
capability layered on top: `pcie_sriov_pf_init` advertises N virtual functions,
each carved a slice of the PF's scratch region so the BAR layout decides
isolation and scheduling shape downstream.

### PF kernel driver (`pf-driver/`)

An out-of-tree Linux module binds the physical function: `pci_driver`
registration with a matching `id_table`, BAR remap via `pcim_iomap_regions`,
`pci_alloc_irq_vectors` and an interrupt handler, a `.sriov_configure` callback
that routes `echo N > sriov_numvfs` through code we own, and a timer-driven
world-switch scheduler that rotates doorbell ownership across VFs — the
smallest possible time-multiplexed scheduler over a contended accelerator.

### Guest exercisers (`guest/`)

Userspace programs that `mmap` the BARs, submit descriptors, ring the doorbell,
and verify completions — the functional check at each phase.

## Roadmap

| Milestone | Scope | Status |
|---|---|---|
| Device | BARs, live MMIO registers, MSI-X, doorbell + command ring | in progress |
| SR-IOV | Capability, VF spawn on `NumVFs`, per-VF BAR partitioning | planned |
| PF driver | Probe, BAR remap, MSI-X, `sriov_configure`, world-switch scheduler | planned |
| DMA / IOMMU | Descriptor addresses as DMA addresses; behavior under the IOMMU | planned |
| Passthrough | Bind a VF to `vfio-pci`, pass into a nested guest, guest VF driver | planned |
| GEMM | A MATMUL opcode: a scalar matmul against the scratch region, diffed vs a reference | planned |
| Debugging notes | Probe/remove race, DMA-after-FLR use-after-free, missed interrupt — symptom to root cause | planned |

## Relation to production systems

Each mechanism maps to a named production system, which the design notes draw on
as prior art: VFIO passthrough underlies every cloud GPU instance (AWS p5, GCP
A3, Azure ND); SR-IOV and BAR partitioning underlie NVIDIA vGPU and MIG;
world-switch scheduling is the same shape as NVIDIA MPS, time-sliced vGPU, and a
serving scheduler's time-multiplexing of a contended accelerator.

## Substrate

The development target needs nested virtualization (KVM in an L1 VM, to nest a
guest for VFIO passthrough). See [HARDWARE.md](HARDWARE.md).
