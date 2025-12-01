#include "timer.h"
#include "zbus.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(timer, CONFIG_APP_LOG_LEVEL);

/* clang-format off */
ZBUS_CHAN_DEFINE(timer_chan,	/* Name */
	 void*,						/* Message type */
	 NULL,						/* Validator */
	 NULL,						/* User data */
	 ZBUS_OBSERVERS_EMPTY,		/* Observers */
	 ZBUS_MSG_INIT(NULL)		/* Initial value */
);
/* clang-format on */

static void sensor_timer_expiry_cb(struct k_timer *timer)
{
	int rc;

	LOG_DBG("timer expired");

	rc = zbus_chan_pub(&timer_chan, timer, K_NO_WAIT);
	if (rc) {
		LOG_ERR("failed to publish timer event (err %d)", rc);
	}
}

static K_TIMER_DEFINE(sensor_timer, sensor_timer_expiry_cb, NULL);

void sensor_timer_start()
{
	LOG_INF("sensor timer started");
	k_timer_start(&sensor_timer, K_NO_WAIT, K_MINUTES(CONFIG_APP_SENSOR_INTERVAL));
};

void sensor_timer_stop()
{
	LOG_INF("sensor timer stopped");
	k_timer_stop(&sensor_timer);
}
