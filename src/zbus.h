#ifndef _ZBUS_H
#define _ZBUS_H

#include "sensor_map.h"

#include <stdint.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/dsp/types.h>

struct sensor_reading {
	const char *sensor;
	const char *type;
	q31_t value;
	int8_t shift;
};

struct device_sensor_msg {
	struct sensor_reading readings[SENSOR_READINGS_MAX];
	size_t count;
	struct k_sem *sem;
};

ZBUS_CHAN_DECLARE(timer_chan);
ZBUS_CHAN_DECLARE(environment_chan);

#endif /* _ZBUS_H */
