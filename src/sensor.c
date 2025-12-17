#include "zbus.h"
#include "sensor_map.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/logging/log.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(sensor, CONFIG_APP_LOG_LEVEL);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define SENSOR_COUNT     DT_PROP_LEN(ZEPHYR_USER_NODE, env_sensors)

#define SENSOR_IODEV_SYM(idx)      CONCAT(_sens_iodev_, idx)
#define SENSOR_IODEV_PTR(idx, ...) &SENSOR_IODEV_SYM(idx)

/* clang-format off */
#define SENSOR_IODEV_DEFINE(node_id, prop, idx)		\
		SENSOR_DT_READ_IODEV(						\
			SENSOR_IODEV_SYM(idx),					\
			DT_PHANDLE_BY_IDX(node_id, prop, idx),	\
			SENSOR_CHAN_MAP(						\
				DT_PHANDLE_BY_IDX(node_id, prop, idx)))
/* clang-format on */

DT_FOREACH_PROP_ELEM_SEP(ZEPHYR_USER_NODE, env_sensors, SENSOR_IODEV_DEFINE, (;));

RTIO_DEFINE_WITH_MEMPOOL(sensor_ctx, SENSOR_COUNT, SENSOR_COUNT, SENSOR_COUNT, 64, sizeof(void *));

static struct rtio_iodev *iodevs[SENSOR_COUNT] = {LISTIFY(SENSOR_COUNT, SENSOR_IODEV_PTR, (,))};

K_SEM_DEFINE(reading_sem, 1, 1);

static struct device_sensor_msg zbus_msg = {
	.readings = {},
	.count = 0,
	.sem = &reading_sem,
};

ZBUS_SUBSCRIBER_DEFINE(env_subscriber, 1);
ZBUS_CHAN_ADD_OBS(timer_chan, env_subscriber, 0);

/* clang-format off */
ZBUS_CHAN_DEFINE(environment_chan,	/* Name */
	 struct device_sensor_msg,		/* Message */
	 NULL,							/* Validator */
	 NULL,					 		/* User data */
	 ZBUS_OBSERVERS_EMPTY,	 		/* Observers */
	 ZBUS_MSG_INIT(0)		 		/* Initial value */
);
/* clang-format on */

static const char *chan_type_str(int16_t chan)
{
	switch (chan) {
	case SENSOR_CHAN_DIE_TEMP:
	case SENSOR_CHAN_AMBIENT_TEMP:
		return "temp";
	case SENSOR_CHAN_PRESS:
		return "press";
	case SENSOR_CHAN_PROX:
		return "prox";
	case SENSOR_CHAN_HUMIDITY:
		return "humid";
	case SENSOR_CHAN_LIGHT:
	case SENSOR_CHAN_AMBIENT_LIGHT:
		return "light";
	case SENSOR_CHAN_IR:
		return "ir";
	case SENSOR_CHAN_RED:
		return "red";
	case SENSOR_CHAN_GREEN:
		return "green";
	case SENSOR_CHAN_BLUE:
		return "blue";
	case SENSOR_CHAN_ALTITUDE:
		return "alt";
	case SENSOR_CHAN_PM_1_0:
		return "pm1.0";
	case SENSOR_CHAN_PM_2_5:
		return "pm2.5";
	case SENSOR_CHAN_PM_10:
		return "pm10";
	case SENSOR_CHAN_DISTANCE:
		return "dist";
	case SENSOR_CHAN_CO2:
		return "co2";
	case SENSOR_CHAN_O2:
		return "o2";
	case SENSOR_CHAN_GAS_RES:
		return "gas_res";
	case SENSOR_CHAN_VOC:
		return "voc";
	case SENSOR_CHAN_VOLTAGE:
		return "volt";
	default:
		return "unknown";
	}
}

static void sensor_init()
{
	const struct device *dev;
	ARRAY_FOR_EACH(iodevs, idx) {
		dev = ((struct sensor_read_config *)(iodevs[idx]->data))->sensor;

		if (!device_is_ready(dev)) {
			LOG_ERR("%s: device not ready", dev->name);
			continue;
		}
	}

	LOG_INF("sensor thread ready");
}

static void sensor_loop()
{
	const struct sensor_decoder_api *decoder;
	const struct zbus_channel *chan;
	const char *chan_type;
	struct sensor_q31_data data;
	struct sensor_read_config *cfg;
	struct rtio_cqe *cqe;
	uint8_t *buf;
	uint32_t buf_len;
	uint32_t fit;
	int rc;

	for (;;) {
		rc = zbus_sub_wait(&env_subscriber, &chan, K_FOREVER);
		if (rc) {
			LOG_WRN("waiting for channel notification failed (err %d)", rc);
			continue;
		}

		ARRAY_FOR_EACH(iodevs, idx) {
			cfg = (struct sensor_read_config *)(iodevs[idx]->data);

			rc = sensor_read_async_mempool(iodevs[idx], &sensor_ctx, iodevs[idx]);
			if (rc) {
				LOG_WRN("%s: failed to init sensor read (err %d)",
					cfg->sensor->name, rc);
				continue;
			}
		}

		rc = k_sem_take(&reading_sem, K_FOREVER);
		if (rc) {
			LOG_ERR("failed to lock readings message (err %d)", rc);
			return;
		}

		zbus_msg.count = 0;
		ARRAY_FOR_EACH(iodevs, _) {
			cqe = rtio_cqe_consume_block(&sensor_ctx);
			if (cqe->result) {
				LOG_WRN("%s: async read failed (err %d)", cfg->sensor->name,
					cqe->result);
				continue;
			}

			cfg = (struct sensor_read_config *)((struct rtio_iodev *)cqe->userdata)
				      ->data;
			rtio_cqe_release(&sensor_ctx, cqe);

			rc = rtio_cqe_get_mempool_buffer(&sensor_ctx, cqe, &buf, &buf_len);
			if (rc) {
				LOG_WRN("%s: failed to get memory buffer (err %d)",
					cfg->sensor->name, rc);
				continue;
			}

			rc = sensor_get_decoder(cfg->sensor, &decoder);
			if (rc) {
				LOG_WRN("%s: failed to get decoder (err %d)", cfg->sensor->name,
					rc);
				continue;
			}

			for (size_t i = 0; i < cfg->count; ++i) {
				fit = 0;
				decoder->decode(buf, cfg->channels[i], &fit, 1, &data);
				chan_type = chan_type_str(cfg->channels[i].chan_type);

				LOG_DBG("%s: %s = %s%d.%02d", cfg->sensor->name, chan_type,
					PRIq_arg(data.readings[0].value, 2, data.shift));

				zbus_msg.readings[zbus_msg.count++] = (struct sensor_reading){
					.sensor = cfg->sensor->name,
					.type = chan_type,
					.value = data.readings[0].value,
					.shift = data.shift,
				};
			}

			rtio_release_buffer(&sensor_ctx, buf, buf_len);
		}

		rc = zbus_chan_pub(&environment_chan, &zbus_msg, K_FOREVER);
		if (rc) {
			LOG_WRN("failed to publish environment data (err %d)", rc);
		}

		k_sem_give(&reading_sem);
	}
}

static void sensor_thrd(void *a1, void *a2, void *a3)
{
	sensor_init();
	sensor_loop();
}

K_THREAD_DEFINE(sensor_thrd_id, CONFIG_APP_SENSOR_STACK_SIZE, sensor_thrd, NULL, NULL, NULL,
		CONFIG_APP_SENSOR_THREAD_PRIORITY, 0, 1000);
