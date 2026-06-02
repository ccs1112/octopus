/*
 * octopus synthetic PCIe device for QEMU.
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

#define TYPE_OCTOPUS "octopus"
OBJECT_DECLARE_SIMPLE_TYPE(OctopusState, OCTOPUS)

#define OCTOPUS_VENDOR_ID  0x1b36   /* Red Hat / QEMU virtual */
#define OCTOPUS_DEVICE_ID  0x00e0

/* BAR0 register layout */
#define OCTOPUS_REG_MAGIC    0x00
#define OCTOPUS_REG_VERSION  0x04
#define OCTOPUS_REG_COUNTER  0x08

#define OCTOPUS_MAGIC    0x6d786770    /* "mxgp" */
#define OCTOPUS_VERSION  0x00000001

struct OctopusState {
    PCIDevice parent_obj;

    MemoryRegion mmio;
    MemoryRegion scratch;

    uint32_t read_count;
};

static uint64_t octopus_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    OctopusState *s = opaque;

    switch (addr) {
    case OCTOPUS_REG_MAGIC:
        return OCTOPUS_MAGIC;
    case OCTOPUS_REG_VERSION:
        return OCTOPUS_VERSION;
    case OCTOPUS_REG_COUNTER:
        return s->read_count++;
    default:
        return 0;
    }
}

static void octopus_mmio_write(void *opaque, hwaddr addr,
                             uint64_t val, unsigned size)
{
}

static const MemoryRegionOps octopus_mmio_ops = {
    .read       = octopus_mmio_read,
    .write      = octopus_mmio_write,
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

static void octopus_realize(PCIDevice *pdev, Error **errp)
{
    OctopusState *s = OCTOPUS(pdev);

    memory_region_init_io(&s->mmio, OBJECT(s), &octopus_mmio_ops, s,
                          "octopus-mmio", 4 * KiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    memory_region_init_ram(&s->scratch, OBJECT(s), "octopus-scratch",
                           1 * MiB, errp);
    if (*errp) {
        return;
    }
    pci_register_bar(pdev, 2,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->scratch);
}

static void octopus_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize   = octopus_realize;
    k->vendor_id = OCTOPUS_VENDOR_ID;
    k->device_id = OCTOPUS_DEVICE_ID;
    k->revision  = 0x00;
    k->class_id  = PCI_CLASS_OTHERS;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->desc = "octopus synthetic SR-IOV GPU";
}

static const TypeInfo octopus_info = {
    .name          = TYPE_OCTOPUS,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(OctopusState),
    .class_init    = octopus_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void octopus_register_types(void)
{
    type_register_static(&octopus_info);
}

type_init(octopus_register_types)
