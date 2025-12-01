#include "net.h"
#include "storage.h"

#include <errno.h>
#include <stdint.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/clock.h>
#include <zephyr/toolchain.h>

LOG_MODULE_REGISTER(net, CONFIG_APP_LOG_LEVEL);

static K_SEM_DEFINE(network_connected, 0, 1);

static void l4_event_handler(uint64_t mgmt_event, struct net_if *iface, void *info,
			     size_t info_length, void *user_data)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(info_length);
	ARG_UNUSED(user_data);

	switch (mgmt_event) {
	case NET_EVENT_DNS_SERVER_ADD:
		LOG_DBG("network connected");
		k_sem_give(&network_connected);
		break;
	case NET_EVENT_L4_DISCONNECTED:
		LOG_DBG("network disconnected");
		k_sem_take(&network_connected, K_NO_WAIT);
		break;
	}
}

static NET_MGMT_REGISTER_EVENT_HANDLER(net_connection_mgmt_handler,
				       NET_EVENT_DNS_SERVER_ADD | NET_EVENT_L4_DISCONNECTED,
				       l4_event_handler, NULL);

static int dns_lookup_server(struct zsock_addrinfo **addr)
{
	int rc;

	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};

	rc = zsock_getaddrinfo(CONFIG_APP_SERVER_HOSTNAME, NULL, &hints, addr);
	if (rc) {
		LOG_ERR("failed to resolve '" CONFIG_APP_SERVER_HOSTNAME "' hostname (err %d)", rc);
		return rc;
	}

	return 0;
}

static void net_disconnect()
{
	struct net_if *iface = net_if_get_default();

	net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
}

static int net_connect()
{
	int rc;
	char ssid[STORAGE_MAX_SSID_SIZE] = {0};
	char pass[STORAGE_MAX_PASS_SIZE] = {0};
	struct wifi_connect_req_params params = {0};
	struct net_if *iface = net_if_get_default();

	rc = storage_ssid_get(ssid, STORAGE_MAX_SSID_SIZE - 1);
	if (rc < 0) {
		LOG_ERR("failed to load wifi SSID (err %d)", rc);
		return rc;
	}

	rc = storage_pass_get(pass, STORAGE_MAX_PASS_SIZE - 1);
	if (rc < 0) {
		LOG_ERR("failed to load wifi password (err %d)", rc);
		return rc;
	}

	params.ssid = ssid;
	params.ssid_length = strlen(params.ssid);
	params.psk = pass;
	params.psk_length = strlen(params.psk);
	params.channel = WIFI_CHANNEL_ANY;
	params.security = WIFI_SECURITY_TYPE_PSK;
	params.band = WIFI_FREQ_BAND_2_4_GHZ;
	params.mfp = WIFI_MFP_OPTIONAL;

	rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params,
		      sizeof(struct wifi_connect_req_params));
	if (rc) {
		LOG_ERR("failed to request network connection (err %d)", rc);
		return rc;
	}

	rc = k_sem_take(&network_connected, K_SECONDS(CONFIG_APP_NETWORK_TIMEOUT));
	if (rc) {
		net_disconnect();
	}

	return rc;
}

int server_connect()
{
	int rc;
	int sock;
	struct zsock_addrinfo *addr;
	struct sockaddr_in *sa;

	rc = net_connect();
	if (rc) {
		goto _err_net_disconnect;
	}
	k_sleep(K_SECONDS(1));

	rc = dns_lookup_server(&addr);
	if (rc) {
		goto _err_net_disconnect;
	}

	rc = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (rc < 0) {
		LOG_ERR("failed to create socket (err %d)", rc);
		goto _err_net_disconnect;
	}
	sock = rc;

	for (; addr != NULL; addr = addr->ai_next) {
		if (addr->ai_addr->sa_family == AF_INET) {
			sa = (struct sockaddr_in *)addr->ai_addr;
			sa->sin_port = htons(CONFIG_APP_SERVER_PORT);

			rc = zsock_connect(sock, (struct sockaddr *)sa, sizeof(struct sockaddr_in));
			if (rc == 0) {
				LOG_DBG("server connected");
				return sock;
			}

			zsock_close(sock);

			if (addr->ai_next) {
				LOG_WRN("server connection failed, trying next address (err %d)",
					-errno);
			}
		}
	}

	LOG_ERR("server connection failed (err %d)", -errno);
_err_net_disconnect:
	net_disconnect();
	return rc;
}

void server_disconnect(int sock)
{
	zsock_close(sock);
	LOG_DBG("server disconnected");

	net_disconnect();
}
