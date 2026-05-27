# mxgpu-mini exercises

Learning log for the device and drivers built in this repo.

**Method:** see `METHOD.md` for the transcription-interrogation loop that
governs how exercises are worked. Read it before starting.

---

## Artifact sequence

**P1 — Building a fake GPU in QEMU**
A QEMU device falls out of one `TypeInfo` struct. We walk from an empty
PCIDevice parented to `TYPE_PCI_DEVICE` to a working device with two BARs
(4 KiB MMIO with read/write handlers, 1 MiB host-RAM-backed), MSI-X
interrupts, and a one-entry command ring that executes a memset op when
the guest writes a doorbell register. The guest can `mmap` the BARs,
submit descriptors, ring the doorbell, and receive completions. Every
primitive a real GPU exposes for command submission, in roughly 200 lines
of C. Phase 1, exercises E1.1–E1.7.

**P2 — SR-IOV: spawning VFs and why the BAR layout matters**
SR-IOV is a PCIe extended capability that turns one physical function into
N virtual functions, each appearing to the OS as an independent device. We
add the capability via `pcie_sriov_pf_init`, wire VF spawn through
`sriov_numvfs`, and carve the PF's scratch region into per-VF BAR slices
so VF[k] sees its own window into shared physical state. The post argues
that the BAR layout — which VF sees what offset — decides everything
downstream: scheduling shape, isolation guarantees, and the host-side
driver's shape. Phase 2, exercises E2.1–E2.4.

**P3 — From pci_register_driver to a working PF**
The host-side kernel module binds to the device. We walk through
`pci_driver` registration with a matching `.id_table`, `probe`/`remove`
callbacks, BAR remapping via `pcim_iomap_regions`, `pci_alloc_irq_vectors`
and an interrupt handler that confirms the path from guest doorbell to
kernel ISR, and finally a `.sriov_configure` callback that routes
`echo N > sriov_numvfs` through code we wrote. The kernel module now owns
the PF and decides, in code, who lives and what slice they get. Phase 3
first half, exercises E3.1–E3.5.

**P4 — World-switch scheduling: how one GPU pretends to be eight**
"World-switch" stops being marketing and becomes a timer callback. A
kernel timer fires every few milliseconds and rotates which VF owns the
doorbell — the smallest possible time-multiplexed scheduler over a
contended physical resource. The post shows why this same shape appears
at three granularities in production (NVIDIA MPS, time-sliced vGPU,
vLLM's continuous-batching scheduler) and what changes when you add
priority, preemption cost, and request-shape heterogeneity. The reader
has now written the minimum scheduler that demonstrates the pattern.
Phase 3 second half, exercise E3.6.

**P5 — DMA, IOMMU, and the lie of device memory**
"Device memory" is a story the kernel tells; the truth is IOMMU page
tables. We trace `dma_map_single` end to end — IOVA allocation, page-table
walk, IOMMU group enforcement — and compare it to `dma_alloc_coherent`
for long-lived rings. The post introduces IOMMU groups as the kernel's
unit of passthrough isolation, explains why DMA-after-FLR is the classic
accelerator use-after-free, and demonstrates the bug with a planted
regression. The reader can now read an `iommu_group/*/devices` dump and
predict which devices safely pass through together. Phase 4, including
the planted bug E4.B.

**P6 — Passthrough end-to-end**
A VF takes the final step out of the host: bind to `vfio-pci`, hand it to
a nested QEMU, write a guest driver that submits a command. We trace one
doorbell write through the full chain — nested guest → nested QEMU →
vfio-pci → outer kernel → real PCIe write to your QEMU device → executed
op → MSI-X → routed back into the nested guest. This is the exact
mechanism behind every cloud GPU instance: AWS p5, GCP A3, Azure
ND-series. The reader can now read a cloud provider's accelerator-
passthrough docs and see what each step is doing. Phase 5.

**P7 — The bug that ate my weekend**
Three planted bugs, three tools, three root-cause walks: a probe/remove
race against the scheduler timer found with `lockdep`; a DMA-after-FLR
use-after-free found with `KASAN` + `dma-debug`; a missed interrupt under
live VF rebind found with `ftrace`. Each section walks from symptom to
root cause to fix, with the actual command sequences and trace excerpts
that surfaced the bug. The post proves the toy is real enough to break in
production-shaped ways — and that the author can debug across the
user/kernel/hypervisor/device boundary. Consolidated from E3.B, E4.B, E5.B.

**P8 — From a cloud GPU instance to the toy you built**
Reframes the entire series outside-in for readers who think in
`p5.48xlarge` instances rather than `TypeInfo` structs. Each mechanism in
mxgpu-mini — VFIO passthrough, SR-IOV and MIG-style partitioning,
world-switch scheduling, DMA/IOMMU isolation, host-side mediation — maps
to a named production system: AWS Nitro, NVIDIA vGPU and MIG, vLLM's
continuous-batching scheduler, H100 confidential computing, the kernel's
`vfio-mdev` framework. The post grounds the toy in the inference stack
that runs Claude, ChatGPT, and Gemini today, and is honest about what the
toy is missing. ~1500 words, citation-heavy.

**P9 — A toy GEMM: making the costume earn its name**
The device has executed `memset` for eight posts. Now it runs a matmul. We
extend the descriptor with M, N, K and three BAR-relative offsets, add a
MATMUL opcode whose handler is a scalar triple loop against the scratch
region, and submit the same workload every accelerator on Earth was built
to run. The guest tool writes A and B into BAR2, rings the doorbell, waits
for MSI-X, and diffs against a precomputed reference. The post is honest
about the gap from this to a real GEMM — no SIMD, no tiling, one
descriptor at a time — but the shape is correct: parallel multiply-add
over device-owned memory. Optional E7.4 dispatches the same op from four
VFs under the world-switch scheduler, demonstrating the smallest possible
production-shaped story: multiple tenants, contended accelerator,
time-multiplexed dispatch, real workload. Phase 7, exercises E7.1-E7.4.

Status:

```
P1: in progress
P2: planned
P3: planned
P4: planned
P5: planned
P6: planned
P7: planned
P8: planned
P9: planned
```

---

## Phase 1 — QEMU synthetic device

**Phase goal:** `-device mxgpu-mini` produces a PCIe device with two BARs,
MSI-X interrupts, a doorbell, and a one-entry command ring that executes a
trivial op (memset on the scratch BAR).

**You will need:** a local QEMU source tree (build it modified), a guest
Linux image (Alpine standard ISO works), root in the guest for `lspci -vv`
and `/sys/bus/pci/...` access.

---

### E1.1 — Empty PCIDevice that probes

- [x] Done

**Goal:** QEMU launches with `-device mxgpu-mini` without error. `info pci`
in the monitor lists vendor 1b36 device 00e0. Guest `lspci` sees it.

**Predict:**
- What does `type_init(...)` expand to, and when does the registration
  function actually run relative to `main()`?
  - _Prediction:_ shape of the pcie bus, the registration runs immediately
- If you set `.instance_size` smaller than `sizeof(MxgpuMiniState)`, what
  specific crash mode results, and where does the symptom surface?
  - _Prediction:_ fatal within the device, inside the device
- Will the device show up in the guest's `lspci` if `realize` is completely
  empty? What if `class_init` is empty?
  - _Prediction:_ no, yes

**Build:**
- `qemu-device/mxgpu_mini.c` with a `TypeInfo` whose parent is
  `TYPE_PCI_DEVICE`, an empty `realize`, and a `class_init` setting
  vendor 0x1b36, device 0x00e0, class `PCI_CLASS_OTHERS`.
- Wire into a QEMU source tree: copy/symlink under `hw/misc/`, add a
  `config MXGPU_MINI` entry in `hw/misc/Kconfig`, append the source in
  `hw/misc/meson.build`.

**Verify:**
- `qemu-system-x86_64 -nographic -monitor stdio -device mxgpu-mini` launches.
- Monitor: `info pci` shows the device.
- Guest: `lspci -nn -d 1b36:00e0` lists it.

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- QEMU `hw/misc/edu.c` — the canonical pedagogical PCI device.
- `docs/devel/qom.rst` in the QEMU tree.

---

### E1.2 — BAR0: MMIO register window

- [x] Done

**Goal:** Device exposes a 4 KiB MMIO BAR. Guest `lspci -v` reports it;
`od` against `/sys/bus/pci/devices/.../resource0` reads without OOPS.

**Predict:**
- When the guest BIOS assigns a physical address to your BAR, what address
  is that — host physical, guest physical, IOVA, something else?
- Your stub `.read` returns 0 unconditionally. What hex does `od` show on
  the BAR? What would change if you returned `addr` instead?
- What's the minimum and maximum access size, and what enforces it — your
  code, QEMU's dispatch, or the simulated bus?

**Build:**
- Add `MemoryRegion mmio` to your device state struct.
- In `realize`, `memory_region_init_io(&s->mmio, OBJECT(s), &ops, s,
  "mxgpu-mmio", 4 * KiB)` with stub `.read`/`.write` ops.
- `pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio)`.

**Verify:**
- Guest `lspci -v -d 1b36:00e0` shows `Region 0: Memory at ... [size=4K]`.
- `sudo od -Ax -t x4 -N 16 /sys/bus/pci/devices/.../resource0` reads zeros,
  `dmesg` shows no errors.

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- `hw/misc/edu.c` — its MMIO setup, especially the ops struct.
- `docs/devel/memory.rst` in the QEMU tree.

---

### E1.3 — BAR2: scratch RAM

- [x] Done

**Goal:** A 1 MiB RAM BAR backed by host memory. Guest writes persist;
reads return what was written.

**Predict:**
- Why `memory_region_init_ram` here vs `_io`? What does each translate to
  in the guest's page tables?
- Will reads return 0 before the guest has written anything? Why?
- This BAR is 64-bit; BAR0 was 32-bit. What changes in config-space
  layout, and what's the consequence of getting flags wrong?

**Build:**
- `memory_region_init_ram(&s->scratch, OBJECT(s), "mxgpu-scratch", 1 * MiB,
  errp)`.
- Register at BAR **2** with
  `PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64`.
- Note: 64-bit BARs consume two slots — next BAR you add is BAR4.

**Verify:**
- Guest `lspci -v` shows `Region 2: Memory at ... 64-bit [size=1M]`.
- `dd if=/dev/urandom of=/sys/bus/pci/devices/.../resource2 bs=1 count=1
  conv=notrunc` writes a byte; re-reading returns the same byte.

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- PCIe base spec §7.5.1.2 (Base Address Registers).

---

### E1.4 — Guest userspace BAR test

- [ ] Done

**Goal:** A small C program inside the guest that opens BAR0 and BAR2, prints
their first 4 dwords, writes a pattern to BAR2, and verifies on a re-run.

**Predict:**
- What permission/cap is needed to open the `resource*` sysfs files? Why?
- If you `mmap` with `MAP_SHARED` and write a 4-byte value to BAR0 offset 0,
  trace the path from store instruction to your QEMU `.write` handler.
- What happens if you do a 1-byte write to BAR0 where `.min_access_size = 4`?
  Where does the size mismatch get handled?

**Build:**
- Plain C, compile inside the guest. `pread`/`pwrite` on `resource0` /
  `resource2`, or `mmap` for the full experience.

**Verify:**
- Pattern in BAR2 survives across invocations (it's real RAM).
- Pattern in BAR0 reads back as zero (your stub write discards).

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- `Documentation/PCI/sysfs-pci.rst` in the kernel tree.

---

### E1.5 — Live MMIO registers

- [ ] Done

**Goal:** Reads from BAR0 return real values. Layout:
- `[0x00]` = magic `0x6d786770` ("mxgp")
- `[0x04]` = version `0x00000001`
- `[0x08]` = a counter that increments on every read

**Predict:**
- Where does the magic at offset 0 logically live in QEMU — on the device
  state, in a `.rodata` constant, in the scratch region?
- If the counter is a non-atomic field and two vCPUs read concurrently,
  what specific race shows up?
- Should the magic be LE or BE on the wire? What determines the answer?

**Build:**
- `.read` switches on `addr`. Return the right value for each offset.
- Counter as a field on `MxgpuMiniState`. Increment naively.

**Verify:**
- Guest tool from E1.4: read offset 0 prints `0x6d786770`. Read offset 8
  twice in a row, get different values.

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- `hw/misc/edu.c` register-layout switch.

---

### E1.6 — MSI-X capability + raise an interrupt

- [ ] Done

**Goal:** Guest writes to BAR0 offset 0x10 ("fire" register); device raises
MSI-X vector 0; a throwaway test (driver stub or userspace VFIO) sees it.

**Predict:**
- INTx vs MSI vs MSI-X: why does MSI-X exist when MSI already supports
  multiple vectors?
- The MSI-X table lives in BAR memory. Whose memory — yours, the host
  kernel's, the guest kernel's, a third region?
- When the device "raises" MSI-X, what does it actually do on the simulated
  bus?

**Build:**
- `msix_init_exclusive_bar(pdev, num_vectors=4, bar_nr=4, errp)` (uses
  BAR4, since BAR2 is 64-bit and consumes 2+3).
- BAR0 offset 0x10 `.write` handler: `msix_notify(pdev, 0)`.

**Verify:**
- `lspci -vv` PF shows `Capabilities: ... MSI-X: Enable+ Count=4`.
- Throwaway driver or VFIO-based test sees the IRQ when you poke 0x10.

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- `hw/misc/edu.c` MSI section.
- `Documentation/PCI/msi-howto.rst`.

---

### E1.7 — Doorbell + one-entry command ring

- [ ] Done

**Goal:** Guest writes a descriptor (op, target offset in BAR2, length, value)
into BAR2 at a fixed location, then writes the descriptor's BAR2 offset to a
doorbell register in BAR0. Device executes the command (start with a memset
op) and raises MSI-X.

**Predict:**
- Why is the descriptor in BAR2 (RAM) and the doorbell in BAR0 (MMIO)?
  What breaks if you reverse it?
- The descriptor contains an "address." From whose perspective is that
  address — guest physical, guest virtual, DMA address?
- What's the ordering race if guest writes the descriptor and the doorbell
  with insufficient memory barriers between?

**Build:**
- Define `struct mxgpu_cmd { uint32_t op; uint32_t offset; uint32_t len;
  uint32_t value; }`. Document in a header you'll share with the driver.
- Doorbell at BAR0 offset 0x20: write value is the descriptor's BAR2 offset.
- Doorbell `.write` handler: read descriptor, execute against scratch
  region, `msix_notify(pdev, 0)`.

**Verify:**
- Guest tool: write a "fill BAR2 at offset 0x1000 with 0xAB for 256 bytes"
  descriptor; ring the doorbell; brief sleep; read BAR2 back — bytes are 0xAB.
- Interrupt path fires (verify via signal handler or driver stub).

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- AMD GIM driver — its mailbox/command ring vocabulary.

---

## Phase 2 — SR-IOV capability + VF spawn

**Phase goal:** Writing `N` to PF's `sriov_numvfs` spawns N VFs visible in
`lspci`, each exposing a sliced view of PF state.

---

### E2.1 — Predict the SR-IOV surface before reading the spec

- [ ] Done

**Goal:** No code. Write down what config space needs to expose for SR-IOV
before opening the spec.

**Predict:**
- What capability ID covers SR-IOV?
- What field tells the OS the maximum VF count?
- Where does the "spawn VFs" trigger come from — sysfs, ioctl, ACPI, BIOS?

**Reference:**
- PCIe base spec §9 (SR-IOV) — skim, don't memorize.
- `Documentation/PCI/pci-iov-howto.rst`.

**Reflect:**
- _________________
- _________________

---

### E2.2 — Add SR-IOV capability to QEMU device

- [ ] Done

**Goal:** PF in guest `lspci -vv` shows `Capabilities: ... SR-IOV` with
`TotalVFs: 4`.

**Predict:**
- Where does the SR-IOV cap sit — standard config space, extended config,
  PCIe capability list? What byte range?
- Will adding the cap alone make VFs appear, or just advertise that they're
  possible? What gates actual spawn?

**Build:**
- `pcie_sriov_pf_init(pdev, offset, "mxgpu-mini-vf", vf_dev_id, total_vfs=4,
  ...)`.
- Define a second TypeInfo `mxgpu-mini-vf` as a stub PCI device for now.

**Verify:**
- PF `lspci -vv` shows the SR-IOV cap, `Initial VFs: 4 Total VFs: 4
  Number of VFs: 0`.
- No VFs visible yet (NumVFs == 0).

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- QEMU `hw/net/igb*.c` — a real SR-IOV device.

---

### E2.3 — Spawn VFs on NumVFs write

- [ ] Done

**Goal:** `echo 4 > /sys/bus/pci/devices/.../sriov_numvfs` on the host
(after your phase-3 driver binds the PF) makes 4 VFs appear in `lspci`.

**Predict:**
- Who actually instantiates the VF `PCIDevice` objects — QEMU when
  NumVFs is written, the host driver via sysfs, or your VF realize?
- What changes for the guest if VFs are spawned at runtime vs at boot?

**Build:**
- `pcie_sriov_pf_init` from E2.2 wires this up. Confirm VF `realize` is
  non-crashing.
- Until phase 3 lands, drive sriov_numvfs from the host. Or temporarily
  hardcode NumVFs in the PF's class_init for a smoke test.

**Verify:**
- Host `lspci -nn` shows PF + 4 VFs with your VF device ID.

**Reflect:**
- _________________
- _________________
- _________________

---

### E2.4 — VF BAR partitioning

- [ ] Done

**Goal:** VF[k] sees a slice of the PF's scratch region: VF[k]'s BAR2
starts at PF scratch offset `k * (BAR_SIZE / N)`.

**Predict:**
- Two architectures: (a) each VF holds independent register state,
  scheduled in by PF; (b) each VF's BAR is a window into shared PF state.
  Which does real MxGPU use? Which is easier to implement here?
- If VFs share state via windowed BARs, what enforces isolation between
  them?

**Build:**
- In VF `realize`, register BARs as `MemoryRegion` aliases / subregions of
  the PF's scratch region at the appropriate offset.
- MMIO: either route VF MMIO reads through the PF handler with a
  transformed address, or give VFs a small private MMIO area.

**Verify:**
- VF[0] writes to its BAR2 offset 0; PF reads its BAR2 offset 0 — sees it.
- VF[1] writes to its BAR2 offset 0; PF reads at offset `BAR_SIZE/4` — sees it.

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- AMD GIM driver — BAR partitioning logic.

---

## Phase 3 — PF kernel driver (sketch, flesh out when you arrive)

**Phase goal:** Out-of-tree Linux kernel module binds the PF, maps BARs,
services MSI-X, exposes `sriov_numvfs`, and time-slices doorbell ownership
across VFs.

- [ ] **E3.1** — Empty kernel module that loads/unloads. Build infra; lsmod;
  dmesg. (Use `.solutions/pf-driver/Makefile` as the build floor if stuck.)
- [ ] **E3.2** — `pci_driver` with probe/remove matching device ID 0x1b36:0x00e0.
- [ ] **E3.3** — BAR remap via `pcim_iomap_regions`. Read the magic word from
  E1.5; verify it matches.
- [ ] **E3.4** — `pci_alloc_irq_vectors` + handler. Confirm IRQ fires when
  guest userspace pokes BAR0 offset 0x10.
- [ ] **E3.5** — `.sriov_configure` callback wiring sysfs writes through to
  the device.
- [ ] **E3.6** — World-switch scheduler: timer-driven round-robin of doorbell
  ownership across VFs.
- [ ] **E3.B** (bug-injection) — Inject a probe/remove race with the
  scheduler timer. Find with `lockdep`.

Reference: `Documentation/PCI/pci.rst`, AMD GIM `gim_main.c`, LWN's
"Writing a PCI driver" series.

---

## Phase 4 — DMA + IOMMU (sketch)

**Phase goal:** Device executes commands whose addresses are DMA addresses
mapped through the streaming DMA API; behaves correctly under IOMMU.

Concept gates:
- [ ] Guest physical vs DMA address: what's the difference, what creates
  the mapping?
- [ ] `dma_map_single` vs `dma_alloc_coherent` — when each is right.
- [ ] IOMMU groups: what enforces them, what's in your device's group?

- [ ] **E4.B** (bug-injection) — DMA-after-FLR use-after-free. Find with
  `KASAN` + `dma-debug`.

Reference: `Documentation/core-api/dma-api.rst`,
`Documentation/userspace-api/iommu.rst`.

---

## Phase 5 — VFIO passthrough (sketch)

**Phase goal:** Bind a VF to `vfio-pci`, pass it into a nested QEMU guest,
write a guest VF driver that submits a command end-to-end.

Concept gates:
- [ ] What does `vfio-pci` take over from the host's driver model?
- [ ] Why does VFIO require IOMMU groups? What's in your VF's group?
- [ ] Userspace VFIO vs kernel passthrough — which path are you using and why?

- [ ] **E5.B** (bug-injection) — Missed interrupt under live VF re-bind.
  Find with `ftrace`.

Reference: `Documentation/driver-api/vfio.rst`, QEMU VFIO docs.

---

## Phase 6 — Consolidated debug post

- [ ] After phases 3-5 complete: write the consolidated debug post
  pulling each `*.B` cell's notes together. Title:
  **"the bug that ate my weekend."**

---

## Phase 7 — Toy GEMM workload

**Phase goal:** Guest submits a matmul descriptor; device executes the
multiply against the scratch BAR; output matches a numpy reference within
floating-point tolerance.

**You will need:** Phase 1 complete (descriptor + doorbell + MSI-X path).
Optionally Phases 2-3 for the multi-VF stretch in E7.4.

**Scope discipline:** one new opcode, scalar C loop, single descriptor at
a time. No SIMD, no tiling, no perf chasing. The point is shape, not
throughput — if you find yourself reaching for AVX intrinsics, stop and
write the post instead.

---

### E7.1 — Extend the descriptor with matmul fields

- [ ] Done

**Goal:** A second descriptor shape — `{op=MATMUL, m, n, k, a_offset,
b_offset, out_offset}` — coexists with the memset descriptor from E1.7.
Layout is documented in the shared header.

**Predict:**
- Does a longer descriptor break the doorbell protocol from E1.7? If so,
  what's the fix — fixed-size with a discriminant, variable length with a
  size field, or separate doorbell registers per op?
- The three offsets are BAR2-relative. Why not absolute guest physical?
  What would change if they were?
- What alignment does the matmul handler want for A, B, out? What
  enforces it — the guest, the device, or both?

**Build:**
- Extend the header shared with the eventual guest driver. New op tag
  `MXGPU_OP_MATMUL`, struct laid out for natural alignment.
- The doorbell handler in `mxgpu_mini.c` switches on `op` and dispatches.

**Verify:**
- Old memset path from E1.7 still works.
- New descriptor parses; an unimplemented `MATMUL` returns/logs without
  crashing the device.

**Reflect:**
- _________________
- _________________
- _________________

---

### E7.2 — Scalar matmul against the scratch BAR

- [ ] Done

**Goal:** The MATMUL opcode actually multiplies. A scalar triple loop
reads A and B from the scratch region, writes C to it, and the doorbell
handler raises MSI-X on completion.

**Predict:**
- Where does the multiply run — QEMU's main thread, a vCPU thread, an
  iothread? Does it block the guest's progress, or not?
- What's the upper bound on M*N*K before the guest's doorbell write
  visibly stalls? Does it matter for correctness, or only ergonomics?
- Floats or ints? What does the choice change about reference comparison?

**Build:**
- C triple loop in the doorbell handler. Default to float32; M=N=K=8 is
  enough for a first end-to-end run.
- Bounds-check offsets against scratch size.

**Verify:**
- Hand-crafted 2x2 matrices: write A=[[1,2],[3,4]], B=[[5,6],[7,8]],
  expect C=[[19,22],[43,50]]. Read back and confirm.

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- Any first-week CUDA tutorial — note what they call "the slow version."

---

### E7.3 — Guest tool with reference comparison

- [ ] Done

**Goal:** A guest C program writes random A and B, submits the matmul,
waits for the IRQ (or polls a completion flag), reads C back, and
compares against a precomputed reference within tolerance.

**Predict:**
- Where do reference values come from in a minimal Alpine guest — ship
  precomputed bytes from the host, compute on the guest with a tiny C
  routine, or link numpy?
- What's the right interrupt-wait primitive from userspace — VFIO's
  eventfd, a `/dev/uio` file, a signal? What does each cost?
- For float32, what tolerance? What sets it — the order of summation,
  the host's FPU, both?

**Build:**
- A guest binary that takes M N K on the command line, generates inputs
  with a fixed seed, writes them to BAR2, drops a descriptor, rings the
  doorbell, waits for completion, reads C back, diffs.

**Verify:**
- Sizes 8x8, 64x64, 128x128 all pass within tolerance.
- A deliberately wrong reference (off-by-one in summation) fails the
  diff — confirms the comparator isn't just rubber-stamping.

**Reflect:**
- _________________
- _________________
- _________________

---

### E7.4 — Multi-VF dispatch under world-switch (stretch)

- [ ] Done

**Goal:** Four VFs each submit an independent matmul. The Phase 3
world-switch timer rotates doorbell ownership. All four jobs complete
correctly; per-job latency reflects the time-slicing.

**Predict:**
- With four VFs, a 5ms timer slice, and a single job that takes 20ms
  scalar, what's the wall-clock latency for each VF's job? What changes
  if jobs are heterogeneously sized?
- What happens if a VF rings the doorbell during another VF's slice —
  queued, dropped, error?
- Where would head-of-line blocking show up first?

**Build:**
- Run the E7.3 guest tool from four VFs simultaneously (or four threads,
  each bound to a VF's BAR slice).
- Instrument the scheduler timer callback from E3.6 to log which VF owns
  the doorbell at each tick.

**Verify:**
- All four outputs correct.
- Log shows round-robin ownership.
- Per-job wall clock approximately `4 * single_job_time` for equal-sized
  jobs; explain any deviation.

**Reflect:**
- _________________
- _________________
- _________________

**Reference:**
- vLLM's continuous-batching scheduler — same shape, different layer.

---

## Glossary — fill as you encounter

- **BAR** — _________________
- **MSI-X** — _________________
- **PF / VF** — _________________
- **World switch** — _________________
- **IOMMU group** — _________________
- **vfio-pci** — _________________
- **Streaming vs coherent DMA** — _________________
- **FLR** — _________________
