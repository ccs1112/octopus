/*
 * mxgpu-mini synthetic PCIe device for QEMU.
 *
 * Phase 1, E1.1: empty PCIDevice that probes.
 * Subsequent exercises extend this file in place.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "qemu/module.h"
#include "qom/object.h"

#define TYPE_MXGPU_MINI "mxgpu-mini"
OBJECT_DECLARE_SIMPLE_TYPE(MxgpuMiniState, MXGPU_MINI)

#define MXGPU_VENDOR_ID  0x1b36   /* Red Hat / QEMU virtual */
#define MXGPU_DEVICE_ID  0x00e0

/* BAR0 register layout */
#define MXGPU_REG_MAGIC    0x00
#define MXGPU_REG_VERSION  0x04
#define MXGPU_REG_COUNTER  0x08

#define MXGPU_MAGIC    0x6d786770    /* "mxgp" */
#define MXGPU_VERSION  0x00000001

struct MxgpuMiniState {
    PCIDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion scratch;

    uint32_t read_count;
};

static uint64_t mxgpu_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    MxgpuMiniState *s = opaque;

    switch (addr) {
    case MXGPU_REG_MAGIC:
        return MXGPU_MAGIC;
    case MXGPU_REG_VERSION:
        return MXGPU_VERSION;
    case MXGPU_REG_COUNTER:
        return s->read_count++;
    default:
        return 0;
    }
}

static void mxgpu_mmio_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
}

static const MemoryRegionOps mxgpu_mmio_ops = {
    .read       = mxgpu_mmio_read,
    .write      = mxgpu_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void mxgpu_realize(PCIDevice *pdev, Error **errp)
{
    MxgpuMiniState *s = MXGPU_MINI(pdev);

    memory_region_init_io(&s->mmio, OBJECT(s), &mxgpu_mmio_ops, s,
                          "mxgpu-mmio", 4 * KiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    memory_region_init_ram(&s->scratch, OBJECT(s), "mxgpu-scratch",
                           1 * MiB, errp);
    if (*errp) {
        return;
    }
    pci_register_bar(pdev, 2,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->scratch);
}

static void mxgpu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize   = mxgpu_realize;
    k->vendor_id = MXGPU_VENDOR_ID;
    k->device_id = MXGPU_DEVICE_ID;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_OTHERS;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "mxgpu-mini toy SR-IOV GPU";
}

static const TypeInfo mxgpu_info = {
    .name          = TYPE_MXGPU_MINI,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MxgpuMiniState),
    .class_init    = mxgpu_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void mxgpu_register_types(void)
{
    type_register_static(&mxgpu_info);
}

type_init(mxgpu_register_types)
