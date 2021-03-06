/*
 * This file is part of linux-driver-management.
 *
 * Copyright © 2016-2018 Ikey Doherty
 *
 * linux-driver-management is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include "device.h"
#include "ldm-enums.h"
#include "ldm-private.h"
#include "util.h"

/* Supported device types */
#include "bluetooth-device.h"
#include "dmi-device.h"
#include "hid-device.h"
#include "pci-device.h"
#include "usb-device.h"
#include "wifi-device.h"

static void ldm_device_set_property(GObject *object, guint id, const GValue *value,
                                    GParamSpec *spec);
static void ldm_device_get_property(GObject *object, guint id, GValue *value, GParamSpec *spec);

G_DEFINE_TYPE(LdmDevice, ldm_device, G_TYPE_INITIALLY_UNOWNED)

/**
 * SECTION:device
 * @Short_description: Abstract device encapsulation
 * @see_also: #LdmManager, #LdmModalias
 * @Title: LdmDevice
 *
 * An LdmDevice is not directly created, it is owned and constructed by an
 * #LdmManager instance. Each #LdmDevice may either be a PCI or USB device,
 * and is in reality an abstraction of an underlying udev device.
 *
 * This object provides easy access to system devices, allowing the end
 * user to easily introspect basic properties such as the #LdmDevice:name,
 * #LdmDevice:vendor-id or even the #LdmDevice:modalias.
 *
 * Each device returned by the #LdmManager is a composite toplevel device,
 * that is to say, it is the sum of all of its properties. This is particularly
 * helpful when dealing with #LdmUSBDevice, whereby the root level properties
 * of the device are the sum of all child parts. This allows users of the
 * library to ignore USB interface internals and directly query an #LdmDevice
 * to determine what capabilities and classes it supports, such as a composite
 * device with #LDM_DEVICE_TYPE_VIDEO | #LDM_DEVICE_TYPE_AUDIO capabilities.
 */

/* Property IDs */
enum { PROP_PARENT = 1,
       PROP_PATH,
       PROP_MODALIAS,
       PROP_PRODUCT_ID,
       PROP_NAME,
       PROP_VENDOR,
       PROP_VENDOR_ID,
       PROP_DEV_TYPE,
       PROP_ATTRIBUTES,
       N_PROPS };

static GParamSpec *obj_properties[N_PROPS] = {
        NULL,
};

/**
 * ldm_device_dispose:
 *
 * Clean up a LdmDevice instance
 */
static void ldm_device_dispose(GObject *obj)
{
        LdmDevice *self = LDM_DEVICE(obj);

        g_clear_pointer(&self->tree.kids, g_hash_table_unref);
        g_clear_pointer(&self->os.hwdb_info, g_hash_table_unref);
        g_clear_pointer(&self->os.sysfs_path, g_free);
        g_clear_pointer(&self->os.modalias, g_free);
        g_clear_pointer(&self->id.name, g_free);
        g_clear_pointer(&self->id.vendor, g_free);

        G_OBJECT_CLASS(ldm_device_parent_class)->dispose(obj);
}

/**
 * ldm_device_class_init:
 *
 * Handle class initialisation
 */
static void ldm_device_class_init(LdmDeviceClass *klazz)
{
        GObjectClass *obj_class = G_OBJECT_CLASS(klazz);

        /* gobject vtable hookup */
        obj_class->dispose = ldm_device_dispose;
        obj_class->get_property = ldm_device_get_property;
        obj_class->set_property = ldm_device_set_property;

        /**
         * LdmDevice:parent: (type LdmDevice) (transfer none)
         *
         * Parent device for this device instance
         */
        obj_properties[PROP_PARENT] =
            g_param_spec_pointer("parent",
                                 "Parent device",
                                 "Parent device for this device",
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
        /**
         * LdmDevice:path
         *
         * The system path for this device. On Linux this is the sysfs path.
         */
        obj_properties[PROP_PATH] = g_param_spec_string("path",
                                                        "The device path",
                                                        "System path for this device",
                                                        NULL,
                                                        G_PARAM_READABLE);

        /**
         * LdmDevice:modalias
         *
         * The modalias reported by the kernel for this device.
         */
        obj_properties[PROP_MODALIAS] = g_param_spec_string("modalias",
                                                            "The device modalias",
                                                            "System modalias for this device",
                                                            NULL,
                                                            G_PARAM_READABLE);

        /**
         * LdmDevice:name
         *
         * The name used to display this device to users, i.e. the model.
         */
        obj_properties[PROP_NAME] = g_param_spec_string("name",
                                                        "The device name",
                                                        "Display name (model) for this device",
                                                        NULL,
                                                        G_PARAM_READABLE);

        /**
         * LdmDevice:vendor
         *
         * The vendor string to display the users, i.e. the manufacturer.
         */
        obj_properties[PROP_VENDOR] =
            g_param_spec_string("vendor",
                                "The device vendor",
                                "The vendor (manufacturer) for this device",
                                NULL,
                                G_PARAM_READABLE);

        /**
         * LdmDevice:product-id
         *
         * The product ID for this device.
         */
        obj_properties[PROP_PRODUCT_ID] = g_param_spec_int("product-id",
                                                           "Product ID",
                                                           "Hardware product ID",
                                                           0,
                                                           0,
                                                           0,
                                                           G_PARAM_READABLE);

        /**
         * LdmDevice:vendor-id
         *
         * The vendor ID for this device.
         */
        obj_properties[PROP_VENDOR_ID] = g_param_spec_int("vendor-id",
                                                          "Vendor ID",
                                                          "Hardware vendor ID",
                                                          0,
                                                          0,
                                                          0,
                                                          G_PARAM_READABLE);

        /**
         * LdmDevice:device-type
         *
         * The composite type of this device, which is a bitwise combination
         * of multiple device types (such as PCI|GPU)
         */
        obj_properties[PROP_DEV_TYPE] = g_param_spec_flags("device-type",
                                                           "Device type",
                                                           "Composite type for this device",
                                                           LDM_TYPE_DEVICE_TYPE,
                                                           LDM_DEVICE_TYPE_ANY,
                                                           G_PARAM_READABLE);

        /**
         * LdmDevice:attributes
         *
         * The composite attributes of this device, which is a bitwise combination
         * of multiple attribute (such as BOOT_VGA)
         */
        obj_properties[PROP_ATTRIBUTES] = g_param_spec_flags("attributes",
                                                             "Device attributes",
                                                             "Composite attributes for this device",
                                                             LDM_TYPE_DEVICE_ATTRIBUTE,
                                                             LDM_DEVICE_ATTRIBUTE_ANY,
                                                             G_PARAM_READABLE);

        g_object_class_install_properties(obj_class, N_PROPS, obj_properties);
}

static void ldm_device_set_property(GObject *object, guint id, const GValue *value,
                                    GParamSpec *spec)
{
        LdmDevice *self = LDM_DEVICE(object);

        switch (id) {
        case PROP_PARENT:
                self->tree.parent = g_value_get_pointer(value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
                break;
        }
}

static void ldm_device_get_property(GObject *object, guint id, GValue *value, GParamSpec *spec)
{
        LdmDevice *self = LDM_DEVICE(object);

        switch (id) {
        case PROP_PARENT:
                g_value_set_pointer(value, self->tree.parent);
                break;
        case PROP_PATH:
                g_value_set_string(value, self->os.sysfs_path);
                break;
        case PROP_MODALIAS:
                g_value_set_string(value, self->os.modalias);
                break;
        case PROP_NAME:
                g_value_set_string(value, self->id.name);
                break;
        case PROP_PRODUCT_ID:
                g_value_set_int(value, self->id.product_id);
                break;
        case PROP_VENDOR:
                g_value_set_string(value, self->id.vendor);
                break;
        case PROP_VENDOR_ID:
                g_value_set_int(value, self->id.vendor_id);
                break;
        case PROP_DEV_TYPE:
                g_value_set_flags(value, self->os.devtype);
                break;
        case PROP_ATTRIBUTES:
                g_value_set_flags(value, self->os.attributes);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID(object, id, spec);
                break;
        }
}

/**
 * ldm_device_init:
 *
 * Handle construction of the LdmDevice
 */
static void ldm_device_init(LdmDevice *self)
{
        /* Just set up the table for our properties */
        self->os.hwdb_info = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

        /* We have sysfs ID to child mapping and own the child */
        self->tree.kids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_object_unref);
}

/**
 * ldm_device_get_modalias:
 *
 * The modalias is unique to the device and is used in identifying potential
 * driver candidates.
 *
 * The Linux kernel will assign each device (or interface) a modalias, which can
 * be matched via #LdmModalias and #LdmModaliasPlugin to determine which package
 * provides the drivers required to enable this device.
 *
 * Returns: (transfer none): The modalias of the device
 */
const gchar *ldm_device_get_modalias(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, NULL);
        return (const gchar *)self->os.modalias;
}

/**
 * ldm_device_get_name:
 *
 * This function will return the name (model) of this device, suitable
 * for presentation to a user.
 *
 * Returns: (transfer none): The name of the device
 */
const gchar *ldm_device_get_name(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, NULL);
        return (const gchar *)self->id.name;
}

/**
 * ldm_device_get_path:
 *
 * This function will return the system-specific path for this device.
 * This is the fully qualified `/sys` path.
 *
 * Returns: (transfer none): The path of the device
 */
const gchar *ldm_device_get_path(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, NULL);
        return (const gchar *)self->os.sysfs_path;
}

/**
 * ldm_device_get_product_id:
 *
 * This function will return the product ID (model) of this device,
 * suitable for comparison with known models.
 *
 * Returns: The product ID of the device
 */
gint ldm_device_get_product_id(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, 0);
        return self->id.product_id;
}

/**
 * ldm_device_get_vendor:
 *
 * This function will return the vendor (manufacturer) of this device,
 * suitable for presentation to a user.
 *
 * Returns: (transfer none): The vendor of the device
 */
const gchar *ldm_device_get_vendor(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, NULL);
        return (const gchar *)self->id.vendor;
}

/**
 * ldm_device_get_vendor_id:
 *
 * This function will return the vendor ID (manufacturer) of this device,
 * suitable for comparison with known vendors.
 *
 * This is especially useful with the predefined #LdmPCIVendorID IDs
 *
 * C example:
 *
 * |[<!-- language="C" -->
 *      if (ldm_device_get_vendor_id(device) == LDM_PCI_VENDOR_ID_NVIDIA) {
 *              g_message("Found an NVIDIA device!");
 *      }
 * ]|
 * Returns: The vendor ID of the device
 */
gint ldm_device_get_vendor_id(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, 0);
        return self->id.vendor_id;
}

/**
 * ldm_device_new_from_udev:
 * @parent: (nullable): Parent device, if any.
 * @device: Associated udev device
 * @hwinfo: If set, the hwdb entry for this device.
 *
 * Construct a new LdmDevice from the given udev device and hwdb information.
 * This is private API between the manager and the device.
 */
LdmDevice *ldm_device_new_from_udev(LdmDevice *parent, udev_device *device, udev_list *properties)
{
        udev_list *entry = NULL;
        LdmDevice *self = NULL;
        gchar *lookup = NULL;
        const char *subsystem = NULL;
        GType special_type = 0;
        const char *sysattr = NULL;

        /* Specialise the gtype here */
        subsystem = udev_device_get_subsystem(device);
        if (g_str_equal(subsystem, "usb")) {
                special_type = LDM_TYPE_USB_DEVICE;
        } else if (g_str_equal(subsystem, "pci")) {
                special_type = LDM_TYPE_PCI_DEVICE;
        } else if (g_str_equal(subsystem, "dmi")) {
                special_type = LDM_TYPE_DMI_DEVICE;
        } else if (g_str_equal(subsystem, "hid")) {
                special_type = LDM_TYPE_HID_DEVICE;
        } else if (g_str_equal(subsystem, "bluetooth")) {
                special_type = LDM_TYPE_BLUETOOTH_DEVICE;
        } else if (g_str_equal(subsystem, "ieee80211")) {
                special_type = LDM_TYPE_WIFI_DEVICE;
        } else {
                special_type = LDM_TYPE_DEVICE;
        }

        self = g_object_new(special_type, "parent", parent, NULL);

        /* Set the absolute basics */
        self->os.sysfs_path = g_strdup(udev_device_get_syspath(device));
        sysattr = udev_device_get_sysattr_value(device, "modalias");
        if (sysattr) {
                self->os.modalias = g_strdup(sysattr);
        }

        /* Shouldn't happen, but is definitely possible.. */
        if (!properties) {
                goto post_hwdb;
        }

        /* Duplicate the hardware data into a private table */
        udev_list_entry_foreach(entry, properties)
        {
                const char *prop_id = NULL;
                const char *value = NULL;

                prop_id = udev_list_entry_get_name(entry);
                value = udev_list_entry_get_value(entry);

                g_hash_table_insert(self->os.hwdb_info, g_strdup(prop_id), g_strdup(value));
        }

        /* Set vendor from hwdb information */
        lookup = g_hash_table_lookup(self->os.hwdb_info, "ID_VENDOR_FROM_DATABASE");
        if (!lookup) {
                lookup = g_hash_table_lookup(self->os.hwdb_info, "ID_VENDOR");
        }
        if (lookup) {
                self->id.vendor = g_strdup(lookup);
                lookup = NULL;
        }

        /* Set name from hwdb information. TODO: Add fallback name! */
        lookup = g_hash_table_lookup(self->os.hwdb_info, "ID_MODEL_FROM_DATABASE");
        if (!lookup) {
                lookup = g_hash_table_lookup(self->os.hwdb_info, "ID_MODEL");
        }
        if (lookup) {
                self->id.name = g_strdup(lookup);
                lookup = NULL;
        }

post_hwdb:

        if (special_type == LDM_TYPE_PCI_DEVICE) {
                ldm_pci_device_init_private(self, device);
        } else if (special_type == LDM_TYPE_USB_DEVICE) {
                ldm_usb_device_init_private(self, device);
        } else if (special_type == LDM_TYPE_DMI_DEVICE) {
                ldm_dmi_device_init_private(self, device);
        } else if (special_type == LDM_TYPE_BLUETOOTH_DEVICE) {
                ldm_bluetooth_device_init_private(self, device);
        }

        if (!self->id.name) {
                self->id.name = g_strdup_printf("Device %x", self->id.product_id);
        }

        return self;
}

/**
 * ldm_device_get_device_type:
 *
 * Return the device type (bitwise field)
 */
LdmDeviceType ldm_device_get_device_type(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, LDM_DEVICE_TYPE_ANY);
        return self->os.devtype;
}

/**
 * ldm_device_has_type:
 * @mask: Bitwise OR combination of #LdmDeviceType
 *
 * Test whether this device has the given type(s) by testing the mask against
 * our known types.
 *
 * C example:
 *
 * |[<!-- language="C" -->
 *
 *      if (ldm_device_has_type(device, LDM_DEVICE_TYPE_USB | LDM_DEVICE_TYPE_PRINTER) {
 *              g_message("Found a USB printer!");
 *      }
 *  ]|
 */
gboolean ldm_device_has_type(LdmDevice *self, LdmDeviceType mask)
{
        GHashTableIter iter = { 0 };
        __ldm_unused__ void *key = NULL;
        LdmDevice *value = NULL;

        g_return_val_if_fail(self != NULL, FALSE);

        /* Do we match? */
        if ((self->os.devtype & mask) == mask) {
                return TRUE;
        }

        /* Walk children */
        g_hash_table_iter_init(&iter, self->tree.kids);
        while (g_hash_table_iter_next(&iter, (void **)&key, (void **)&value)) {
                if (ldm_device_has_type(value, mask)) {
                        return TRUE;
                }
        }

        /* No match */
        return FALSE;
}

/**
 * ldm_device_get_attributes:
 *
 * Return the device type (bitwise field)
 */
LdmDeviceAttribute ldm_device_get_attributes(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, LDM_DEVICE_ATTRIBUTE_ANY);
        return self->os.attributes;
}

/**
 * ldm_device_has_attribute:
 * @mask: Bitwise OR combination of #LdmDeviceAttribute
 *
 * Test whether this device has the given attribute(s) by testing the mask against
 * our known attributes
 *
 * C example:
 *
 * |[<!-- language="C" -->
 *      if (ldm_device_has_attribute(device, LDM_DEVICE_ATTRIBUTE_BOOT_VGA)) {
 *              g_message("User booted with this GPU: %s", ldm_device_get_name(device));
 *      }
 * ]|
 */
gboolean ldm_device_has_attribute(LdmDevice *self, LdmDeviceAttribute mask)
{
        GHashTableIter iter = { 0 };
        __ldm_unused__ void *key = NULL;
        LdmDevice *value = NULL;

        g_return_val_if_fail(self != NULL, FALSE);

        /* Do we match? */
        if ((self->os.attributes & mask) == mask) {
                return TRUE;
        }

        /* Walk children */
        g_hash_table_iter_init(&iter, self->tree.kids);
        while (g_hash_table_iter_next(&iter, (void **)&key, (void **)&value)) {
                if (ldm_device_has_type(value, mask)) {
                        return TRUE;
                }
        }

        /* No match */
        return FALSE;
}

/**
 * ldm_device_get_parent:
 *
 * Get the parent device, if any
 *
 * Returns: (transfer none) (nullable): The parent device if it exists
 */
LdmDevice *ldm_device_get_parent(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, NULL);

        return self->tree.parent;
}

/**
 * ldm_device_get_children:
 *
 * Return any child devices, if any
 *
 * Returns: (element-type Ldm.Device) (transfer container): a list of all child devices
 */
GList *ldm_device_get_children(LdmDevice *self)
{
        g_return_val_if_fail(self != NULL, NULL);

        return g_hash_table_get_values(self->tree.kids);
}

/**
 * ldm_device_add_child:
 * @child: (transfer full): Child to add to this device
 *
 * Add a new child to this device, with this device now taking ownership of it.
 * Note: Children must be constructed with the parent explicitly being this
 * instance, it is not possible to reparent a child, instead they must
 * be destroyed.
 */
void ldm_device_add_child(LdmDevice *self, LdmDevice *child)
{
        const gchar *id = NULL;
        g_return_if_fail(self != NULL);

        id = ldm_device_get_path(child);
        if (!g_hash_table_replace(self->tree.kids, g_strdup(id), g_object_ref_sink(child))) {
                return;
        }
}

/**
 * ldm_device_remove_child:
 * @child: The child to remove from this device
 *
 * This is a wrapper around #ldm_device_remove_child_by_path
 */
void ldm_device_remove_child(LdmDevice *self, LdmDevice *child)
{
        const gchar *id = NULL;
        g_return_if_fail(self != NULL);

        id = ldm_device_get_path(child);
        ldm_device_remove_child_by_path(self, id);
}

/**
 * ldm_device_remove_child_by_path:
 * @path: Sysfs path for the child to be removed
 *
 * Remove a child from this device if we own it, and unset the parent on
 * that device.
 */
void ldm_device_remove_child_by_path(LdmDevice *self, const gchar *path)
{
        g_return_if_fail(self != NULL);

        if (!g_hash_table_remove(self->tree.kids, path)) {
                return;
        }
}

/**
 * ldm_device_get_child_by_path:
 * @path: Path of the child
 *
 * Return a pointer ot the child referenced by path, if it exists.
 */
LdmDevice *ldm_device_get_child_by_path(LdmDevice *self, const gchar *path)
{
        g_return_val_if_fail(self != NULL, NULL);

        return g_hash_table_lookup(self->tree.kids, path);
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
