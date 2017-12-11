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

#pragma once

#include <stdbool.h>

#include "device.h"

/**
 * LdmUSBDevice represents a PCI device on the system
 */
typedef struct LdmUSBDevice {
        LdmDevice device;    /**<Extend LdmDevice */
        char *sysfs_address; /**</sys address on the host */
} LdmUSBDevice;

/**
 * Free USB specific fields
 */
void ldm_usb_device_free(LdmUSBDevice *device);

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
