// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 */

#include <sys/types.h>
#include <libudev.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <netdb.h>
#include <unistd.h>

#include <dirent.h>

#include <linux/usb/ch9.h>

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip.h"

static const char usbip_list_usage_string[] =
	"usbip list [-p|--parsable] <args>\n"
	"    -p, --parsable         Parsable list format\n"
	"    -r, --remote=<host>    List the exportable USB devices on <host>\n"
	"    -l, --local            List the local USB devices\n"
	"    -d, --device           List the local USB gadgets bound to usbip-vudc\n";

void usbip_list_usage(void)
{
	printf("usage: %s", usbip_list_usage_string);
}

// list of exported USB devices from a remote host
static int get_exported_devices(char *host, int sockfd)
{
	char product_name[100];
	char class_name[100];
	struct op_devlist_reply reply;
	uint16_t code = OP_REP_DEVLIST;
	struct usbip_usb_device udev;
	struct usbip_usb_interface uintf;
	unsigned int i;
	int rc, j;
	int status;

	// This sends a request to the remote host to list its exported USB devices.
	rc = usbip_net_send_op_common(sockfd, OP_REQ_DEVLIST, 0);
	if (rc < 0) {
		dbg("usbip_net_send_op_common failed");
		return -1;
	}

	//  receive the reply to the above request.
	rc = usbip_net_recv_op_common(sockfd, &code, &status);
	if (rc < 0) {
		err("Exported Device List Request failed - %s\n",
		    usbip_op_common_status_string(status));
		return -1;
	}
	// initialize reply - op_devlist_reply  to 0
	memset(&reply, 0, sizeof(reply));

	// receive the data of the reply variable from the remote host.
	rc = usbip_net_recv(sockfd, &reply, sizeof(reply));
	if (rc < 0) {
		dbg("usbip_net_recv_op_devlist failed");
		return -1;
	}

	// convert the received data from network byte order to host byte order.
	PACK_OP_DEVLIST_REPLY(0, &reply);
	dbg("exportable devices: %d\n", reply.ndev);

	// checks the number of devices in the reply
	if (reply.ndev == 0) {
		info("no exportable devices found on %s", host);
		return 0;
	}

	printf("Exportable USB devices\n");
	printf("======================\n");
	printf(" - %s\n", host);

	// loop over devices
	for (i = 0; i < reply.ndev; i++) {
		// initializes a udev - usbip_usb_device to 0.
		memset(&udev, 0, sizeof(udev));

		// to receive the data of the udev from the remote host.
		rc = usbip_net_recv(sockfd, &udev, sizeof(udev));
		if (rc < 0) {
			dbg("usbip_net_recv failed: usbip_usb_device[%d]", i);
			return -1;
		}

		// to convert from network byte order to host byte order.
		usbip_net_pack_usb_device(0, &udev);

		// to get the product name of the device
		usbip_names_get_product(product_name, sizeof(product_name),
					udev.idVendor, udev.idProduct);
		// to get the class name of the device.
		usbip_names_get_class(class_name, sizeof(class_name),
				      udev.bDeviceClass, udev.bDeviceSubClass,
				      udev.bDeviceProtocol);
		printf("%11s: %s\n", udev.busid, product_name);
		printf("%11s: %s\n", "", udev.path);
		printf("%11s: %s\n", "", class_name);

		// loop for each interface in each device
		for (j = 0; j < udev.bNumInterfaces; j++) {
			// to receive the data of the uintf from the remote host.
			rc = usbip_net_recv(sockfd, &uintf, sizeof(uintf));
			if (rc < 0) {
				err("usbip_net_recv failed: usbip_usb_intf[%d]",
				    j);

				return -1;
			}

			// to convert from network byte order to host byte order.
			usbip_net_pack_usb_interface(0, &uintf);
			// to get the class name of the device.
			usbip_names_get_class(class_name, sizeof(class_name),
					      uintf.bInterfaceClass,
					      uintf.bInterfaceSubClass,
					      uintf.bInterfaceProtocol);
			printf("%11s: %2d - %s\n", "", j, class_name);
		}

		printf("\n");
	}

	return 0;
}

// list the exported USB devices on a remote host -> it calls get_exported_devices (above)
static int list_exported_devices(char *host)
{
	int rc;
	int sockfd;
	// creating a socket connection to the remote host
	sockfd = usbip_net_tcp_connect(host, usbip_port_string);
	if (sockfd < 0) {
		err("could not connect to %s:%s: %s", host, usbip_port_string,
		    gai_strerror(sockfd));
		return -1;
	}
	dbg("connected to %s:%s", host, usbip_port_string);

	// if connection successful
	rc = get_exported_devices(host, sockfd);
	if (rc < 0) {
		err("failed to get device list from %s", host);
		return -1;
	}

	close(sockfd);

	return 0;
}

static void print_device(const char *busid, const char *vendor,
			 const char *product, bool parsable)
{
	if (parsable)
		printf("busid=%s#usbid=%.4s:%.4s#", busid, vendor, product);
	else
		printf(" - busid %s (%.4s:%.4s)\n", busid, vendor, product);
}

static void print_product_name(char *product_name, bool parsable)
{
	if (!parsable)
		printf("   %s\n", product_name);
}

// list local USB devices that are currently connected to the system.
static int list_devices(bool parsable)
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	const char *path;
	const char *idVendor;
	const char *idProduct;
	const char *bConfValue;
	const char *bNumIntfs;
	const char *busid;
	char product_name[128];
	int ret = -1;
	const char *devpath;

	/* Create libudev context. */
	// initializing a new libudev context
	udev = udev_new();

	/* Create libudev device enumeration. */
	// creating a new device enumeration using udev
	enumerate = udev_enumerate_new(udev);

	/* Take only USB devices that are not hubs and do not have
	 * the bInterfaceNumber attribute, i.e. are not interfaces.
	 */
	// sets up filters on the enumeration
	udev_enumerate_add_match_subsystem(enumerate, "usb");
	udev_enumerate_add_nomatch_sysattr(enumerate, "bDeviceClass", "09");
	udev_enumerate_add_nomatch_sysattr(enumerate, "bInterfaceNumber", NULL);
	udev_enumerate_scan_devices(enumerate);

	// scans for devices
	devices = udev_enumerate_get_list_entry(enumerate);

	/* Show information about each device. */
	udev_list_entry_foreach(dev_list_entry, devices)
	{
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);

		/* Ignore devices attached to vhci_hcd */
		devpath = udev_device_get_devpath(dev);
		if (strstr(devpath, USBIP_VHCI_DRV_NAME)) {
			dbg("Skip the device %s already attached to %s\n",
			    devpath, USBIP_VHCI_DRV_NAME);
			continue;
		}

		/* Get device information. */
		idVendor = udev_device_get_sysattr_value(dev, "idVendor");
		idProduct = udev_device_get_sysattr_value(dev, "idProduct");
		bConfValue = udev_device_get_sysattr_value(
			dev, "bConfigurationValue");
		bNumIntfs =
			udev_device_get_sysattr_value(dev, "bNumInterfaces");
		busid = udev_device_get_sysname(dev);
		if (!idVendor || !idProduct || !bConfValue || !bNumIntfs) {
			err("problem getting device attributes: %s",
			    strerror(errno));
			goto err_out;
		}

		/* Get product name. */
		// looks up the product name in a database based on the idVendor and idProduct attributes.
		usbip_names_get_product(product_name, sizeof(product_name),
					strtol(idVendor, NULL, 16),
					strtol(idProduct, NULL, 16));

		/* Print information. */
		print_device(busid, idVendor, idProduct, parsable);
		print_product_name(product_name, parsable);

		printf("\n");

		udev_device_unref(dev);
	}

	ret = 0;

err_out:
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	return ret;
}

// uses libudev library to enumerate the local USB devices that are bound to the usbip-vudc driver.
static int list_gadget_devices(bool parsable)
{
	int ret = -1;
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	const char *path;
	const char *driver;

	const struct usb_device_descriptor *d_desc;
	const char *descriptors;
	char product_name[128];

	uint16_t idVendor;
	char idVendor_buf[8];
	uint16_t idProduct;
	char idProduct_buf[8];
	const char *busid;

	// initializing a new libudev context
	udev = udev_new();

	// creating a new device enumeration using udev
	enumerate = udev_enumerate_new(udev);

	// filter
	udev_enumerate_add_match_subsystem(enumerate, "platform");

	// scan
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);

	udev_list_entry_foreach(dev_list_entry, devices)
	{
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);

		driver = udev_device_get_driver(dev);
		/* We only have mechanism to enumerate gadgets bound to vudc */
		// checks if the device is bound to the "usbip-vudc" driver
		if (driver == NULL || strcmp(driver, USBIP_DEVICE_DRV_NAME))
			continue;

		/* Get device information. */
		descriptors = udev_device_get_sysattr_value(
			dev, VUDC_DEVICE_DESCR_FILE);

		if (!descriptors) {
			err("problem getting device attributes: %s",
			    strerror(errno));
			goto err_out;
		}

		d_desc = (const struct usb_device_descriptor *)descriptors;

		idVendor = le16toh(d_desc->idVendor);
		sprintf(idVendor_buf, "0x%4x", idVendor);
		idProduct = le16toh(d_desc->idProduct);
		sprintf(idProduct_buf, "0x%4x", idVendor);
		busid = udev_device_get_sysname(dev);

		/* Get product name. */
		usbip_names_get_product(product_name, sizeof(product_name),
					le16toh(idVendor), le16toh(idProduct));

		/* Print information. */
		print_device(busid, idVendor_buf, idProduct_buf, parsable);
		print_product_name(product_name, parsable);

		printf("\n");

		udev_device_unref(dev);
	}
	ret = 0;

err_out:
	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	return ret;
}

int usbip_list(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "parsable", no_argument, NULL, 'p' },
		{ "remote", required_argument, NULL, 'r' },
		{ "local", no_argument, NULL, 'l' },
		{ "device", no_argument, NULL, 'd' },
		{ NULL, 0, NULL, 0 }
	};

	bool parsable = false;
	int opt;
	int ret = -1;

	if (usbip_names_init(USBIDS_FILE))
		err("failed to open %s", USBIDS_FILE);

	for (;;) {
		opt = getopt_long(argc, argv, "pr:ld", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'p':
			parsable = true;
			break;
		case 'r':
			ret = list_exported_devices(optarg);
			goto out;
		case 'l':
			ret = list_devices(parsable);
			goto out;
		case 'd':
			ret = list_gadget_devices(parsable);
			goto out;
		default:
			goto err_out;
		}
	}

err_out:
	usbip_list_usage();
out:
	usbip_names_free();

	return ret;
}
