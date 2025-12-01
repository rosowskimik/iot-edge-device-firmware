#include "sensor_map.h"
#include "timer.h"
#include "net.h"
#include "zbus.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/toolchain.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/http/method.h>
#include <zephyr/net/http/status.h>
#include <zephyr/logging/log.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/data/json.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

#include <mbedtls/sha1.h>
#include <sys/errno.h>

LOG_MODULE_REGISTER(http, CONFIG_APP_LOG_LEVEL);

#define NET_ID_INIT_PRIORITY 99

#define APP_HTTP_AUTHORIZE_URL    "/devices/register"
#define APP_HTTP_POST_READING_URL "/devices/send_data"
#define APP_HTTP_PROTOCOL         "HTTP/1.1"
#define APP_HTTP_DEV_ID_HEADER    "X-DEVICE-ID"

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
#define SENSOR_COUNT     DT_PROP_LEN(ZEPHYR_USER_NODE, env_sensors)

#define DT_NODE_FULL_NAME_BY_IDX(node_id, prop, idx)                                               \
	DT_NODE_FULL_NAME(DT_PHANDLE_BY_IDX(node_id, prop, idx))

#define SHA1_BYTES 20
#define SHA1_HEX   (SHA1_BYTES * 2)

static char net_id[SHA1_HEX + 1] = {0};
static char net_id_header[SHA1_HEX + sizeof(APP_HTTP_DEV_ID_HEADER) + 4] = {0};

static uint8_t recv_buf[128];
static char json_buf[CONFIG_APP_MAX_JSON_PAYLOAD] = {0};

static K_SEM_DEFINE(http_done, 0, 1);

static bool authorized = false;

const static char *sensors[] = {
	DT_FOREACH_PROP_ELEM_SEP(ZEPHYR_USER_NODE, env_sensors, DT_NODE_FULL_NAME_BY_IDX, (,)) };

static struct json_obj_descr sensor_reading_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct sensor_reading, sensor, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct sensor_reading, type, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct sensor_reading, value, JSON_TOK_NUMBER),
	JSON_OBJ_DESCR_PRIM(struct sensor_reading, shift, JSON_TOK_NUMBER),
};

static struct json_obj_descr sensor_reading_arr[] = {
	JSON_OBJ_DESCR_OBJ_ARRAY(struct device_sensor_msg, readings, SENSOR_READINGS_MAX, count,
				 sensor_reading_descr, ARRAY_SIZE(sensor_reading_descr)),
};

static int net_id_init()
{
	int rc;
	uint8_t dev_id[16];
	uint8_t sha1_bytes[SHA1_BYTES];
	mbedtls_sha1_context sha1_ctx;

	rc = hwinfo_get_device_id(dev_id, sizeof(dev_id));
	if (rc < 0) {
		LOG_ERR("failed to get device id (err %d)", rc);
		return rc;
	}

	mbedtls_sha1_init(&sha1_ctx);

	rc = mbedtls_sha1_starts(&sha1_ctx);
	if (rc) {
		LOG_ERR("failed to start network id calculation (err %d)", rc);
		return rc;
	}

	rc = mbedtls_sha1_update(&sha1_ctx, dev_id, sizeof(dev_id));
	if (rc) {
		LOG_ERR("failed to add device id to network id (err %d)", rc);
		return rc;
	}

	ARRAY_FOR_EACH(sensors, idx) {
		rc = mbedtls_sha1_update(&sha1_ctx, sensors[idx], strlen(sensors[idx]));
		if (rc) {
			LOG_ERR("failed to add sensor '%s' to network id (err %d)", sensors[idx],
				rc);
			return rc;
		}
	}

	rc = mbedtls_sha1_finish(&sha1_ctx, sha1_bytes);
	if (rc) {
		LOG_ERR("failed to calculate network id (err %d)", rc);
		return rc;
	}

	mbedtls_sha1_free(&sha1_ctx);

	if (bin2hex(sha1_bytes, sizeof(sha1_bytes), net_id, sizeof(net_id)) != SHA1_HEX) {
		LOG_ERR("failed to set network id");
		return -ENOSPC;
	}

	snprintf(net_id_header, sizeof(net_id_header), "%s: %s\r\n", APP_HTTP_DEV_ID_HEADER,
		 net_id);

	LOG_INF("device id: %s", net_id);

	return 0;
}

SYS_INIT(net_id_init, POST_KERNEL, NET_ID_INIT_PRIORITY);

ZBUS_SUBSCRIBER_DEFINE(http_subscriber, 1);
ZBUS_CHAN_ADD_OBS(environment_chan, http_subscriber, 0);

static int authorize_response_cb(struct http_response *rsp, enum http_final_call final_data,
				 void *user_data)
{
	if (final_data != HTTP_DATA_FINAL) {
		return 0;
	}

	switch (rsp->http_status_code) {
	case HTTP_200_OK:
		authorized = true;
		sensor_timer_start();
		LOG_INF("device authorized");
		break;
	case HTTP_401_UNAUTHORIZED:
		sensor_timer_stop();
		LOG_INF("device not authorized");
		break;
	default:
		LOG_WRN("unexpected response status %d", rsp->http_status_code);
		break;
	}

	k_sem_give(&http_done);
	return 0;
}

static int authorize_device()
{
	int rc;
	int sock;
	bool success;
	int delay = CONFIG_APP_NETWORK_RETRY_DELAY;

	const static char *headers[] = {"Transfer-Encoding: chunked\r\n", NULL};

	struct http_request req = {
		.method = HTTP_POST,
		.url = APP_HTTP_AUTHORIZE_URL,
		.protocol = APP_HTTP_PROTOCOL,
		.header_fields = headers,
		.content_type_value = "text/plain",
		.payload = net_id,
		.payload_len = strlen(net_id),
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
		.response = authorize_response_cb,
	};

	for (;;) {
		sock = server_connect();
		if (sock < 0) {
			goto _err_delay;
		}

		rc = http_client_req(sock, &req, CONFIG_APP_NETWORK_TIMEOUT, &success);
		if (rc < 0) {
			LOG_ERR("authorize request failed (err %d)", rc);
			goto _err_delay;
		}

		k_sem_take(&http_done, K_FOREVER);
		server_disconnect(sock);

		if (authorized) {
			return 0;
		}

_err_delay:
		LOG_WRN("next attempt in %d minute(s)", delay);
		k_sleep(K_MINUTES(delay));

		delay = MIN(2 * delay, CONFIG_APP_NETWORK_RETRY_DELAY_MAX);
	}
}

static int publish_response_cb(struct http_response *rsp, enum http_final_call final_data,
			       void *user_data)
{
	if (final_data != HTTP_DATA_FINAL) {
		return 0;
	}

	switch (rsp->http_status_code) {
	case HTTP_200_OK:
		LOG_INF("data published");
		break;
	case HTTP_401_UNAUTHORIZED:
		sensor_timer_stop();
		authorized = false;
		LOG_INF("device unauthorized");
		break;
	default:
		LOG_WRN("unexpected response status %d", rsp->http_status_code);
		break;
	}

	k_sem_give(&http_done);
	return 0;
}

static int sensor_server_push()
{
	int rc;
	int sock;
	bool success;

	const static char *headers[] = {"Transfer-Encoding: chunked\r\n", net_id_header, NULL};

	struct http_request req = {
		.method = HTTP_POST,
		.url = APP_HTTP_POST_READING_URL,
		.protocol = APP_HTTP_PROTOCOL,
		.header_fields = headers,
		.content_type_value = "application/json",
		.payload = json_buf,
		.payload_len = strlen(json_buf),
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
		.response = publish_response_cb,
	};

	sock = server_connect();
	if (sock < 0) {
		return sock;
	}

	rc = http_client_req(sock, &req, CONFIG_APP_NETWORK_TIMEOUT, &success);
	if (rc < 0) {
		LOG_ERR("publish request failed (err %d)", rc);
		goto _err_disc;
	}

	k_sem_take(&http_done, K_FOREVER);

_err_disc:
	server_disconnect(sock);
	return rc;
}

static int sensor_publish()
{
	const struct zbus_channel *chan;
	struct device_sensor_msg msg;
	int rc;

	rc = zbus_sub_wait(&http_subscriber, &chan, K_FOREVER);
	if (rc) {
		LOG_ERR("waiting for channel notification failed (err %d)", rc);
		return rc;
	}

	rc = zbus_chan_read(chan, &msg, K_NO_WAIT);
	if (rc) {
		LOG_ERR("failed to read channel message (err %d)", rc);
		return rc;
	}

	rc = k_sem_take(msg.sem, K_SECONDS(1));
	if (rc) {
		LOG_ERR("failed to lock sensor buffer (err %d)", rc);
		return rc;
	}

	rc = json_arr_encode_buf(sensor_reading_arr, &msg, json_buf, ARRAY_SIZE(json_buf));

	k_sem_give(msg.sem);

	if (rc) {
		LOG_ERR("failed to encode json message (err %d)", rc);
		return rc;
	}

	rc = sensor_server_push();

	return rc;
}

static void http_thrd(void *a1, void *a2, void *a3)
{
	LOG_INF("network thread ready");

	for (;;) {
		if (!authorized) {
			authorize_device();
			k_sleep(K_SECONDS(1));
		}
		sensor_publish();
		k_sleep(K_SECONDS(1));
	}
}

K_THREAD_DEFINE(http_thrd_id, CONFIG_APP_NET_STACK_SIZE, http_thrd, NULL, NULL, NULL,
		CONFIG_APP_NET_THREAD_PRIORITY, 0, 1500);
