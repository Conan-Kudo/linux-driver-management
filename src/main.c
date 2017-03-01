/*
 * This file is part of linux-driver-management.
 *
 * Copyright © 2016-2017 Ikey Doherty
 *
 * linux-driver-management is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <pci/pci.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * Interesting vendors (to LDM)
 */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_VENDOR_ID_NVIDIA 0x10DE
#define PCI_VENDOR_ID_AMD 0x1002

/**
 * Return PCI id in format appropriate to X.Org (decimal, prefixed)
 */
static char *get_xorg_pci_id(struct pci_dev *dev)
{
        char *p = NULL;
        int ret = asprintf(&p, "PCI:%d:%d:%d", dev->domain, dev->dev, dev->func);
        if (ret < 0) {
                return NULL;
        }
        return p;
}

/**
 * Demo code follows
 */
static void discover_devices(void)
{
        struct pci_access *ac = NULL;

        /* Init PCI lookup */
        ac = pci_alloc();
        if (!ac) {
                abort();
        }
        pci_init(ac);
        pci_scan_bus(ac);

        /* Iterate devices looking for something interesting. */
        for (struct pci_dev *dev = ac->devices; dev != NULL; dev = dev->next) {
                pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_CLASS);
                const char *vendor = NULL;
                char *pci_id = NULL;

                if (dev->device_class < PCI_CLASS_DISPLAY_VGA ||
                    dev->device_class > PCI_CLASS_DISPLAY_3D) {
                        continue;
                }

                switch (dev->vendor_id) {
                case PCI_VENDOR_ID_INTEL:
                        vendor = "Intel";
                        break;
                case PCI_VENDOR_ID_NVIDIA:
                        vendor = "NVIDIA";
                        break;
                case PCI_VENDOR_ID_AMD:
                        vendor = "AMD";
                        break;
                default:
                        vendor = "<unknown";
                        break;
                }
                fprintf(stderr,
                        " %02x:%02x.%x: Discovered VGA device\n",
                        dev->domain,
                        dev->dev,
                        dev->func);
                fprintf(stderr, " \u251C Vendor: %s\n", vendor);
                pci_id = get_xorg_pci_id(dev);
                fprintf(stderr, " \u2514 X.Org ID: %s\n", pci_id ? pci_id : "<unknown>");

                if (pci_id) {
                        free(pci_id);
                }
        }

        pci_cleanup(ac);
}

int main(int argc, char **argv)
{
        discover_devices();
        return EXIT_SUCCESS;
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */