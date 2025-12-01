#include "storage.h"

#include <string.h>

#include <sys/errno.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/clock.h>
#include <zephyr/toolchain.h>

LOG_MODULE_REGISTER(storage);

#define STORAGE_INIT_PRIORITY 99

#define STORAGE_PARTITION        storage_partition
#define STORAGE_PARTITION_DEVICE FIXED_PARTITION_DEVICE(STORAGE_PARTITION)
#define STORAGE_PARTITION_OFFSET FIXED_PARTITION_OFFSET(STORAGE_PARTITION)

BUILD_ASSERT(sizeof(CONFIG_APP_INITIAL_SSID) <= STORAGE_MAX_SSID_SIZE, "SSID max size exceeded");
BUILD_ASSERT(sizeof(CONFIG_APP_INITIAL_PASS) >= STORAGE_MIN_PASS_SIZE,
	     "Password needs to be at least 8 char long");
BUILD_ASSERT(sizeof(CONFIG_APP_INITIAL_PASS) <= STORAGE_MAX_PASS_SIZE,
	     "Password max size exceeded");

enum storage_id {
	STORAGE_ID_SSID,
	STORAGE_ID_PASS,
};

static struct nvs_fs fs;

ssize_t storage_ssid_get(char *data, size_t len)
{
	return nvs_read(&fs, STORAGE_ID_SSID, data, len);
}

ssize_t storage_ssid_set(const char *data, size_t len)
{
	if (len > STORAGE_MAX_SSID_SIZE) {
		return -EINVAL;
	}
	return nvs_write(&fs, STORAGE_ID_SSID, data, len);
}

ssize_t storage_pass_get(char *data, size_t len)
{
	return nvs_read(&fs, STORAGE_ID_PASS, data, len);
}

ssize_t storage_pass_set(const char *data, size_t len)
{
	if (len > STORAGE_MAX_PASS_SIZE) {
		return -EINVAL;
	}
	if (len < STORAGE_MIN_PASS_SIZE) {
		return -EINVAL;
	}
	return nvs_write(&fs, STORAGE_ID_PASS, data, len);
}

static void storage_setup_defaults()
{
	ssize_t rc;

	rc = storage_ssid_get(NULL, 0);
	if (rc == -ENOENT) {
		LOG_INF("%s: ssid entry enpty, setting default", fs.flash_device->name);

		rc = storage_ssid_set(CONFIG_APP_INITIAL_SSID, strlen(CONFIG_APP_INITIAL_SSID));
		if (rc != strlen(CONFIG_APP_INITIAL_SSID)) {
			LOG_ERR("%s: failed to set default ssid (err %zd)", fs.flash_device->name,
				rc);
		}
	}

	rc = storage_pass_get(NULL, 0);
	if (rc == -ENOENT) {
		LOG_INF("%s: pass entry enpty, setting default", fs.flash_device->name);

		rc = storage_pass_set(CONFIG_APP_INITIAL_PASS, strlen(CONFIG_APP_INITIAL_PASS));
		if (rc != strlen(CONFIG_APP_INITIAL_PASS)) {
			LOG_ERR("%s: failed to set default password (err %zd)",
				fs.flash_device->name, rc);
		}
	}
}

static int storage_init()
{
	int rc;
	struct flash_pages_info info;

	fs.flash_device = STORAGE_PARTITION_DEVICE;
	if (!device_is_ready(fs.flash_device)) {
		LOG_ERR("%s: device not ready", fs.flash_device->name);
		return -ENODEV;
	}

	fs.offset = STORAGE_PARTITION_OFFSET;
	rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
	if (rc) {
		LOG_ERR("%s: unable to get page info (err %d)", fs.flash_device->name, rc);
		return rc;
	}
	fs.sector_size = info.size;
	fs.sector_count = 3U;

	rc = nvs_mount(&fs);
	if (rc) {
		LOG_ERR("%s: failed to mount storage partition (err %d)", fs.flash_device->name,
			rc);
		return rc;
	}

	LOG_INF("mounted flash storage");

	storage_setup_defaults();

	return 0;
}

SYS_INIT(storage_init, POST_KERNEL, STORAGE_INIT_PRIORITY);
