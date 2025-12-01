#include "storage.h"

#include <stddef.h>
#include <string.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/printk.h>

static int cmd_set_ssid(const struct shell *shell, size_t argc, char *argv[])
{
	const char *arg_ssid;
	int rc;

	if (argc != 2) {
		shell_error(shell, "Usage: set_ssid <ssid>");
		return -EINVAL;
	}
	arg_ssid = argv[1];

	rc = storage_ssid_set(arg_ssid, strlen(arg_ssid));
	if (rc < 0) {
		shell_error(shell, "bad ssid (err %d)", rc);
		return rc;
	}

	shell_print(shell, "ssid updated");
	return 0;
}

static int cmd_set_pass(const struct shell *shell, size_t argc, char *argv[])
{
	const char *arg_pass;
	int rc;

	if (argc != 2) {
		shell_error(shell, "Usage: set_pass <ssid>");
		return -EINVAL;
	}
	arg_pass = argv[1];

	rc = storage_pass_set(arg_pass, strlen(arg_pass));
	if (rc < 0) {
		shell_error(shell, "bad password (err %d)", rc);
		return rc;
	}

	shell_print(shell, "password updated");
	return 0;
}

SHELL_CMD_ARG_REGISTER(set_ssid, NULL, "Set WiFi SSID", cmd_set_ssid, 2, 0);
SHELL_CMD_ARG_REGISTER(set_pass, NULL, "Set WiFi password", cmd_set_pass, 2, 0);
