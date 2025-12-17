#ifndef _SENSOR_MAP_H
#define _SENSOR_MAP_H

#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

#define SENSOR_CHAN_BY_COMPAT(phandle, compat, ...)                                                \
	COND_CODE_1(DT_NODE_HAS_COMPAT(phandle, compat), (__VA_ARGS__), ())

#define SENSOR_CHAN_MAP(phandle)                                                                   \
	SENSOR_CHAN_BY_COMPAT(phandle, rohm_bh1750, {SENSOR_CHAN_LIGHT, 0})                        \
	SENSOR_CHAN_BY_COMPAT(phandle, sensirion_sht4x, {SENSOR_CHAN_AMBIENT_TEMP, 0},             \
			      {SENSOR_CHAN_HUMIDITY, 0})                                           \
	SENSOR_CHAN_BY_COMPAT(phandle, maxim_ds18b20, {SENSOR_CHAN_AMBIENT_TEMP, 0})               \
	SENSOR_CHAN_BY_COMPAT(phandle, aosong_dht, {SENSOR_CHAN_AMBIENT_TEMP, 0},                  \
			      {SENSOR_CHAN_HUMIDITY, 0})                                           \
	SENSOR_CHAN_BY_COMPAT(phandle, aosong_dht20, {SENSOR_CHAN_AMBIENT_TEMP, 0},                \
			      {SENSOR_CHAN_HUMIDITY, 0})                                           \
	SENSOR_CHAN_BY_COMPAT(phandle, bosch_bmp180, {SENSOR_CHAN_PRESS, 0},                       \
			      {SENSOR_CHAN_DIE_TEMP, 0})                                           \
	SENSOR_CHAN_BY_COMPAT(phandle, bosch_bme280, {SENSOR_CHAN_AMBIENT_TEMP, 0},                \
			      {SENSOR_CHAN_PRESS, 0}, {SENSOR_CHAN_HUMIDITY, 0})                   \
	SENSOR_CHAN_BY_COMPAT(phandle, bosch_bmp280, {SENSOR_CHAN_AMBIENT_TEMP, 0},                \
			      {SENSOR_CHAN_PRESS, 0})                                              \
	SENSOR_CHAN_BY_COMPAT(phandle, bosch_bme680, {SENSOR_CHAN_AMBIENT_TEMP, 0},                \
			      {SENSOR_CHAN_PRESS, 0}, {SENSOR_CHAN_HUMIDITY, 0})                   \
	SENSOR_CHAN_BY_COMPAT(phandle, avago_apds9960, {SENSOR_CHAN_LIGHT, 0},                     \
			      {SENSOR_CHAN_RED, 0}, {SENSOR_CHAN_GREEN, 0},                        \
			      {SENSOR_CHAN_GAS_BLUE, 0}, {SENSOR_CHAN_PROX, 0})                    \
	SENSOR_CHAN_BY_COMPAT(phandle, vishay_vcnl4040, {SENSOR_CHAN_PROX, 0},                     \
			      {SENSOR_CHAN_LIGHT, 0})                                              \
	SENSOR_CHAN_BY_COMPAT(phandle, aosong_dht, {SENSOR_CHAN_AMBIENT_TEMP, 0},                  \
			      {SENSOR_CHAN_HUMIDITY, 0})

#define SENSOR_CHAN_COUNT(node_id, prop, idx)                                                      \
	ARRAY_SIZE(((struct sensor_chan_spec[]){                                                   \
		SENSOR_CHAN_MAP(DT_PHANDLE_BY_IDX(node_id, prop, idx))}))

#define SENSOR_READINGS_MAX                                                                        \
	DT_FOREACH_PROP_ELEM_SEP(ZEPHYR_USER_NODE, env_sensors, SENSOR_CHAN_COUNT, (+))

#endif // _SENSOR_MAP_H
