/*
 *  Generic Loongson processor based LAPTOP/ALL-IN-ONE driver
 *
 *  Jianmin Lv <lvjianmin@loongson.cn>
 *  Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>
#include <linux/input/sparse-keymap.h>
#include <linux/device.h>
#include <linux/backlight.h>
#include <acpi/video.h>

/* ACPI HIDs */
#define LOONGSON_ACPI_HKEY_HID	"LOON0000"
#define LOONGSON_ACPI_EC_HID	"PNP0C09"

/* Main driver */

#define ACPI_LAPTOP_VERSION "1.0"
#define ACPI_LAPTOP_NAME "loongson-laptop"
#define ACPI_LAPTOP_DESC "Loongson Laptop/all-in-one ACPI Driver"
#define ACPI_LAPTOP_FILE ACPI_LAPTOP_NAME "_acpi"
#define ACPI_LAPTOP_DRVR_NAME ACPI_LAPTOP_FILE
#define ACPI_LAPTOP_ACPI_EVENT_PREFIX "loongson"

/* Driver-wide structs and misc. variables */

struct generic_struct;

struct generic_acpi_drv_struct {
	u32 type;
	acpi_handle *handle;
	const struct acpi_device_id *hid;
	struct acpi_device *device;
	struct acpi_driver *driver;
	void (*notify)(struct generic_struct *, u32);
};

struct generic_struct {
	char *name;

	int (*init)(struct generic_struct *);

	struct generic_acpi_drv_struct *acpi;

	struct {
		u8 acpi_driver_registered;
		u8 acpi_notify_installed;
	} flags;
};


static struct {
	u32 input_device_registered:1;
} generic_features;

static int hotkey_status_get(int *status);
static int loongson_laptop_backlight_update(struct backlight_device *bd);

/* 1. ACPI Helpers and device model */

/* ACPI basic handles */

static acpi_handle ec_handle;

#define GENERIC_HANDLE(object, parent, paths...)			\
	static acpi_handle  object##_handle;			\
	static const acpi_handle * const object##_parent __initconst =	\
						&parent##_handle; \
	static char *object##_paths[] __initdata = { paths }

GENERIC_HANDLE(hkey, ec, "\\_SB.HKEY", "^HKEY", "HKEY",);

/* ACPI device model */

#define GENERIC_ACPIHANDLE_INIT(object) \
	drv_acpi_handle_init(#object, &object##_handle, *object##_parent, \
		object##_paths, ARRAY_SIZE(object##_paths))

static void __init drv_acpi_handle_init(const char *name,
			   acpi_handle *handle, const acpi_handle parent,
			   char **paths, const int num_paths)
{
	int i;
	acpi_status status;

	for (i = 0; i < num_paths; i++) {
		status = acpi_get_handle(parent, paths[i], handle);
		if (ACPI_SUCCESS(status))
			return;
	}

	*handle = NULL;
}
static acpi_status __init generic_acpi_handle_locate_callback(acpi_handle handle,
					u32 level, void *context, void **return_value)
{
	*(acpi_handle *)return_value = handle;

	return AE_CTRL_TERMINATE;
}

static void __init generic_acpi_handle_locate(const char *name,
		const char *hid, acpi_handle *handle)
{
	acpi_status status;
	acpi_handle device_found;

	BUG_ON(!name || !hid || !handle);

	*handle = NULL;

	memset(&device_found, 0, sizeof(device_found));
	status = acpi_get_devices(hid, generic_acpi_handle_locate_callback,
				  (void *)name, &device_found);

	if (ACPI_SUCCESS(status))
		*handle = device_found;
}

static void dispatch_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct generic_struct *sub_driver = data;

	if (!sub_driver || !sub_driver->acpi || !sub_driver->acpi->notify)
		return;
	sub_driver->acpi->notify(sub_driver, event);
}

static int __init setup_acpi_notify(struct generic_struct *sub_driver)
{
	acpi_status status;

	BUG_ON(!sub_driver->acpi);

	if (!*sub_driver->acpi->handle)
		return 0;

	sub_driver->acpi->device = acpi_fetch_acpi_dev(*sub_driver->acpi->handle);
	if (!sub_driver->acpi->device) {
		pr_err("acpi_fetch_acpi_dev(%s) failed\n", sub_driver->name);
		return -ENODEV;
	}

	sub_driver->acpi->device->driver_data = sub_driver;
	sprintf(acpi_device_class(sub_driver->acpi->device), "%s/%s",
		ACPI_LAPTOP_ACPI_EVENT_PREFIX,
		sub_driver->name);

	status = acpi_install_notify_handler(*sub_driver->acpi->handle,
			sub_driver->acpi->type, dispatch_acpi_notify, sub_driver);
	if (ACPI_FAILURE(status)) {
		if (status == AE_ALREADY_EXISTS) {
			pr_notice("Another device driver is already "
				  "handling %s events\n", sub_driver->name);
		} else {
			pr_err("acpi_install_notify_handler(%s) failed: %s\n",
			       sub_driver->name, acpi_format_exception(status));
		}
		return -ENODEV;
	}
	sub_driver->flags.acpi_notify_installed = 1;
	return 0;
}

static int __init tpacpi_device_add(struct acpi_device *device)
{
	return 0;
}

static struct input_dev *generic_inputdev;

static int loongson_generic_suspend(struct device *dev)
{
	return 0;
}
static int loongson_generic_resume(struct device *dev)
{
	int status = 0;
	struct key_entry ke;
	struct backlight_device *bd;

	/*
	 * Only if the firmware supports SW_LID event model, we can handle the
	 * event. This is for the consideration of development board without
	 * EC.
	 */
	if (test_bit(SW_LID, generic_inputdev->swbit)) {
		if (hotkey_status_get(&status))
			return -EIO;
		/*
		 * The input device sw element records the last lid status.
		 * When the system is awakened by other wake-up sources,
		 * the lid event will also be reported. The judgment of
		 * adding SW_LID bit which in sw element can avoid this
		 * case.
		 *
		 * Input system will drop lid event when current lid event
		 * value and last lid status in the same. So laptop driver
		 * doesn't report repeated events.
		 *
		 * Lid status is generally 0, but hardware exception is
		 * considered. So add lid status confirmation.
		 */
		if (test_bit(SW_LID, generic_inputdev->sw) && !(status & (1 << SW_LID))) {
			ke.type = KE_SW;
			ke.sw.value = (u8)status;
			ke.sw.code = SW_LID;
			sparse_keymap_report_entry(generic_inputdev, &ke,
					1, true);
		}
	}

	bd = backlight_device_get_by_type(BACKLIGHT_PLATFORM);
	if (bd) {
		loongson_laptop_backlight_update(bd) ?
		pr_warn("Loongson_backlight: resume brightness failed") :
		pr_info("Loongson_backlight: resume brightness %d\n", bd->props.brightness);
	}

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(loongson_generic_pm,
		loongson_generic_suspend, loongson_generic_resume);

static int __init register_generic_subdriver(struct generic_struct *sub_driver)
{
	int rc;

	BUG_ON(!sub_driver->acpi);

	sub_driver->acpi->driver = kzalloc(sizeof(struct acpi_driver), GFP_KERNEL);
	if (!sub_driver->acpi->driver) {
		pr_err("Failed to allocate memory for ibm->acpi->driver\n");
		return -ENOMEM;
	}

	sprintf(sub_driver->acpi->driver->name, "%s_%s", ACPI_LAPTOP_NAME, sub_driver->name);
	sub_driver->acpi->driver->ids = sub_driver->acpi->hid;
	sub_driver->acpi->driver->ops.add = &tpacpi_device_add;
	sub_driver->acpi->driver->drv.pm = pm_ptr(&loongson_generic_pm);
	rc = acpi_bus_register_driver(sub_driver->acpi->driver);
	if (rc < 0) {
		pr_err("acpi_bus_register_driver(%s) failed: %d\n", sub_driver->name, rc);
		kfree(sub_driver->acpi->driver);
		sub_driver->acpi->driver = NULL;
	} else if (!rc)
		sub_driver->flags.acpi_driver_registered = 1;

	return rc;
}

#define MAX_ACPI_ARGS 3

static int acpi_evalf(acpi_handle handle,
		      int *res, char *method, char *fmt, ...)
{
	char res_type;
	char *fmt0 = fmt;
	va_list ap;
	int success, quiet;
	acpi_status status;
	struct acpi_object_list params;
	struct acpi_buffer result, *resultp;
	union acpi_object in_objs[MAX_ACPI_ARGS], out_obj;

	if (!*fmt) {
		pr_err("acpi_evalf() called with empty format\n");
		return 0;
	}

	if (*fmt == 'q') {
		quiet = 1;
		fmt++;
	} else
		quiet = 0;

	res_type = *(fmt++);

	params.count = 0;
	params.pointer = &in_objs[0];

	va_start(ap, fmt);
	while (*fmt) {
		char c = *(fmt++);
		switch (c) {
		case 'd':	/* int */
			in_objs[params.count].integer.value = va_arg(ap, int);
			in_objs[params.count++].type = ACPI_TYPE_INTEGER;
			break;
			/* add more types as needed */
		default:
			pr_err("acpi_evalf() called with invalid format character '%c'\n",
			       c);
			va_end(ap);
			return 0;
		}
	}
	va_end(ap);

	if (res_type != 'v') {
		result.length = sizeof(out_obj);
		result.pointer = &out_obj;
		resultp = &result;
	} else
		resultp = NULL;

	status = acpi_evaluate_object(handle, method, &params, resultp);

	switch (res_type) {
	case 'd':		/* int */
		success = (status == AE_OK &&
			   out_obj.type == ACPI_TYPE_INTEGER);
		if (success && res)
			*res = out_obj.integer.value;
		break;
	case 'v':		/* void */
		success = status == AE_OK;
		break;
		/* add more types as needed */
	default:
		pr_err("acpi_evalf() called with invalid format character '%c'\n",
		       res_type);
		return 0;
	}

	if (!success && !quiet)
		pr_err("acpi_evalf(%s, %s, ...) failed: %s\n",
		       method, fmt0, acpi_format_exception(status));

	return success;
}

/* Loongson generic laptop firmware event model */

#define GENERIC_HOTKEY_MAP_MAX	64
#define METHOD_NAME__KMAP	"KMAP"
static struct key_entry hotkey_keycode_map[GENERIC_HOTKEY_MAP_MAX];

static int hotkey_map(void)
{
	u32 index;
	acpi_status status;
	struct acpi_buffer buf;
	union acpi_object *pack;

	buf.length = ACPI_ALLOCATE_BUFFER;
	status = acpi_evaluate_object_typed(hkey_handle, METHOD_NAME__KMAP, NULL, &buf, ACPI_TYPE_PACKAGE);
	if (status != AE_OK) {
		pr_err("ACPI exception: %s\n", acpi_format_exception(status));
		return -1;
	}
	pack = buf.pointer;
	for (index = 0; index < pack->package.count; index++) {
		union acpi_object *sub_pack = &pack->package.elements[index];
		union acpi_object *element = &sub_pack->package.elements[0];

		hotkey_keycode_map[index].type = element->integer.value;
		element = &sub_pack->package.elements[1];
		hotkey_keycode_map[index].code = element->integer.value;
		element = &sub_pack->package.elements[2];
		hotkey_keycode_map[index].keycode = element->integer.value;
	}

	return 0;
}

static int hotkey_backlight_set(bool enable)
{
	if (!acpi_evalf(hkey_handle, NULL, "VCBL", "vd", enable ? 1 : 0))
		return -EIO;

	return 0;
}

static int __init event_init(struct generic_struct *sub_driver)
{
	int ret;

	GENERIC_ACPIHANDLE_INIT(hkey);
	ret = hotkey_map();
	if (ret < 0) {
		pr_err("Failed to parse keymap from DSDT\n");
		return ret;
	}

	ret = sparse_keymap_setup(generic_inputdev, hotkey_keycode_map, NULL);
	if (ret < 0) {
		pr_err("Failed to setup input device keymap\n");
		input_free_device(generic_inputdev);

		return ret;
	}

	/*
	 * This hotkey driver handle backlight event when
	 * acpi_video_get_backlight_type() gets acpi_backlight_vendor
	 */
	if (acpi_video_get_backlight_type() != acpi_backlight_vendor)
		hotkey_backlight_set(false);
	else
		hotkey_backlight_set(true);

	pr_info("ACPI: enabling firmware HKEY event interface...\n");

	return ret;
}

#define GENERIC_EVENT_TYPE_OFF		12
#define GENERIC_EVENT_MASK		0xFFF

static int ec_get_brightness(void)
{
	int status = 0;

	if (!hkey_handle)
		return -ENXIO;

	if (!acpi_evalf(hkey_handle, &status, "ECBG", "d"))
		return -EIO;

	if (status < 0)
		return status;

	return status;
}

static int ec_set_brightness(int level)
{

	int ret = 0;

	if (!hkey_handle)
		return -ENXIO;

	if (!acpi_evalf(hkey_handle, NULL, "ECBS", "vd", level))
		ret = -EIO;

	return ret;
}

static int ec_backlight_level(u8 level)
{
	int status = 0;

	if (!hkey_handle)
		return -ENXIO;

	if (!acpi_evalf(hkey_handle, &status, "ECLL", "d"))
		return -EIO;

	if ((status < 0) || (level > status))
		return status;

	if (!acpi_evalf(hkey_handle, &status, "ECSL", "d"))
		return -EIO;

	if ((status < 0) || (level < status))
		return status;

	return level;
}

static int loongson_laptop_backlight_update(struct backlight_device *bd)
{
	int lvl = ec_backlight_level(bd->props.brightness);

	if (lvl < 0)
		return -EIO;
	if (ec_set_brightness(lvl))
		return -EIO;

	return 0;
}

static int loongson_laptop_get_brightness(struct backlight_device *bd)
{
	u8 level;

	level = ec_get_brightness();
	if (level < 0)
		return -EIO;

	return level;
}

static const struct backlight_ops backlight_laptop_ops = {
	.update_status = loongson_laptop_backlight_update,
	.get_brightness = loongson_laptop_get_brightness,
};

static int laptop_backlight_register(void)
{
	int status = 0;
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_PLATFORM;

	if (!acpi_evalf(hkey_handle, &status, "ECLL", "d"))
		return -EIO;

	props.brightness = 1;
	props.max_brightness = status;

	backlight_device_register("loongson_laptop",
				NULL, NULL, &backlight_laptop_ops, &props);

	return 0;
}

int loongson_laptop_turn_on_backlight(void)
{
	int status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	arg0.integer.value = 1;
	status = acpi_evaluate_object(NULL, "\\BLSW", &args, NULL);
	if (ACPI_FAILURE(status)) {
		pr_info("Loongson lvds error: 0x%x\n", status);
		return -ENODEV;
	}

	return 0;
}

int loongson_laptop_turn_off_backlight(void)
{
	int status;
	union acpi_object arg0 = { ACPI_TYPE_INTEGER };
	struct acpi_object_list args = { 1, &arg0 };

	arg0.integer.value = 0;
	status = acpi_evaluate_object(NULL, "\\BLSW", &args, NULL);
	if (ACPI_FAILURE(status)) {
		pr_info("Loongson lvds error: 0x%x\n", status);
		return -ENODEV;
	}

	return 0;
}

static int hotkey_status_get(int *status)
{
	if (!acpi_evalf(hkey_handle, status, "GSWS", "d"))
		return -EIO;

	return 0;
}

static void event_notify(struct generic_struct *sub_driver, u32 event)
{
	struct key_entry *ke = NULL;
	int scan_code = event & GENERIC_EVENT_MASK;
	int type = (event >> GENERIC_EVENT_TYPE_OFF) & 0xF;

	ke = sparse_keymap_entry_from_scancode(generic_inputdev, scan_code);
	if (ke) {
		if (type == KE_SW) {
			int status = 0;

			if (hotkey_status_get(&status))
				return;

			ke->sw.value = !!(status & (1 << ke->sw.code));
		}
		sparse_keymap_report_entry(generic_inputdev, ke, 1, true);
	}
}

static const struct acpi_device_id loongson_htk_device_ids[] = {
	{LOONGSON_ACPI_HKEY_HID, 0},
	{"", 0},
};

static struct generic_acpi_drv_struct ec_event_acpidriver = {
	.hid = loongson_htk_device_ids,
	.notify = event_notify,
	.handle = &hkey_handle,
	.type = ACPI_DEVICE_NOTIFY,
};

/* 2. Infrastructure */

static int __init probe_for_generic(void)
{
	if (acpi_disabled)
		return -ENODEV;

	/* The EC handler is required */
	generic_acpi_handle_locate("ec", LOONGSON_ACPI_EC_HID, &ec_handle);
	if (!ec_handle)
		return -ENODEV;

	return 0;
}

static void generic_exit(struct generic_struct *sub_driver)
{

	if (sub_driver->flags.acpi_notify_installed) {
		BUG_ON(!sub_driver->acpi);
		acpi_remove_notify_handler(*sub_driver->acpi->handle,
					   sub_driver->acpi->type,
					   dispatch_acpi_notify);
		sub_driver->flags.acpi_notify_installed = 0;
	}

	if (sub_driver->flags.acpi_driver_registered) {
		BUG_ON(!sub_driver->acpi);
		acpi_bus_unregister_driver(sub_driver->acpi->driver);
		kfree(sub_driver->acpi->driver);
		sub_driver->acpi->driver = NULL;
		sub_driver->flags.acpi_driver_registered = 0;
	}

}

static int __init generic_subdriver_init(struct generic_struct *sub_driver)
{
	int ret;

	BUG_ON(sub_driver == NULL);

	if (sub_driver->init)
		sub_driver->init(sub_driver);

	if (sub_driver->acpi) {
		if (sub_driver->acpi->hid) {
			ret = register_generic_subdriver(sub_driver);
			if (ret)
				goto err_out;
		}

		if (sub_driver->acpi->notify) {
			ret = setup_acpi_notify(sub_driver);
			if (ret == -ENODEV) {
				ret = 0;
				goto err_out;
			}
			if (ret < 0)
				goto err_out;
		}
	}

	return 0;

err_out:
	generic_exit(sub_driver);
	return (ret < 0) ? ret : 0;
}

/* Module init, exit, parameters */
static struct generic_struct generic_sub_drivers[] __initdata = {
	{
		.name = "EC Event",
		.init = event_init,
		.acpi = &ec_event_acpidriver,
	},
};

static void generic_acpi_laptop_exit(void);

static int __init generic_acpi_laptop_init(void)
{
	int i, ret, status;

	ret = probe_for_generic();
	if (ret < 0)
		return ret;

	generic_inputdev = input_allocate_device();
	if (!generic_inputdev) {
		pr_err("Unable to allocate input device\n");
		return -ENOMEM;
	}

	/* Prepare input device, but don't register */
	generic_inputdev->name =
		"Loongson Generic Laptop/All-in-one Extra Buttons";
	generic_inputdev->phys = ACPI_LAPTOP_DRVR_NAME "/input0";
	generic_inputdev->id.bustype = BUS_HOST;
	generic_inputdev->dev.parent = NULL;

	/* Init subdrivers */
	for (i = 0; i < ARRAY_SIZE(generic_sub_drivers); i++) {
		ret = generic_subdriver_init(&generic_sub_drivers[i]);
		if (ret < 0) {
			generic_acpi_laptop_exit();
			return ret;
		}
	}

	ret = input_register_device(generic_inputdev);
	if (ret < 0) {
		pr_err("Unable to register input device\n");
		generic_acpi_laptop_exit();
		return ret;
	}

	generic_features.input_device_registered = 1;

	if (acpi_evalf(hkey_handle, &status, "ECBG", "d")) {
		pr_info("Loongson Laptop used, init brightness is 0x%x\n", status);
		ret = laptop_backlight_register();
		if (ret < 0)
			pr_err("Loongson Laptop: laptop-backlight device register failed\n");
	}

	return 0;
}

static void __exit generic_acpi_laptop_exit(void)
{
	if (generic_inputdev) {
		if (generic_features.input_device_registered)
			input_unregister_device(generic_inputdev);
		else
			input_free_device(generic_inputdev);
	}
}

module_init(generic_acpi_laptop_init);
module_exit(generic_acpi_laptop_exit);

MODULE_ALIAS("platform:acpi-laptop");
MODULE_AUTHOR("Jianmin Lv <lvjianmin@loongson.cn>");
MODULE_AUTHOR("Huacai Chen <chenhuacai@loongson.cn>");
MODULE_DESCRIPTION(ACPI_LAPTOP_DESC);
MODULE_VERSION(ACPI_LAPTOP_VERSION);
MODULE_LICENSE("GPL");
