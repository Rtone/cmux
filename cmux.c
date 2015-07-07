/**
*	Cmux
*	Enables GSM 0710 multiplex using n_gsm
*
*	Copyright (C) 2013 - Rtone - Nicolas Le Manchet <nicolaslm@rtone.fr> and others
*
*	This program is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <net/if.h>
#include <linux/types.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <signal.h>
#include <ctype.h>
/**
*	gsmmux.h provides n_gsm line dicipline structures and functions.
*	It should be kept in sync with your kernel release.
*/
#include "gsmmux.h"

/* n_gsm ioctl */
#ifndef N_GSM0710
# define N_GSM0710	21
#endif

/* attach a line discipline ioctl */
#ifndef TIOCSETD
# define TIOCSETD	0x5423
#endif

 /* size of the reception buffer which gets data from the serial line */
#define SIZE_BUF	256

char *g_gsm = "default";

/* number of virtual TTYs to create (most modems can handle up to 4) */
int g_nodes = 1;

/* name of the virtual TTYs to create */
char *g_base = "/dev/ttyGSM";

/* name of the driver, used to get the major number */
char *g_driver = "gsmtty";

/**
* whether or not to detach the program from the terminal
*	0 : do not daemonize
*	1 : daemonize
*/
int g_daemon = 1;

/**
* whether or not to print debug messages to stderr
*	0 : debug off
*	1 : debug on
*/
int g_debug = 1;

/* serial port of the modem */
char *g_device = "/dev/ttyUSB0";

/* line speed */
int g_speed = 115200;

/* maximum transfert unit (MTU), value in bytes */
int g_mtu = 512;

/**
*	Prints debug messages to stderr if debug is wanted
*/
static void dbg(char *fmt, ...) {
	va_list args;

	if (g_debug) {
		fflush(NULL);
		va_start(args, fmt);
		vfprintf(stderr, fmt, args);
		va_end(args);
		fprintf(stderr, "\n");
		fflush(NULL);
	}
	return;
}

/**
*	Sends an AT command to the specified line and gets its result
*	Returns  0 on success
*			-1 on failure
*/
int send_at_command(int serial_fd, char *command) {
	char buf[SIZE_BUF];
	int r;

	/* write the AT command to the serial line */
	if (write(serial_fd, command, strlen(command)) <= 0)
		err(EXIT_FAILURE, "Cannot write to %s", g_device);

	/* wait a bit to allow the modem to rest */
	sleep(1);

	/* read the result of the command from the modem */
	memset(buf, 0, sizeof(buf));
	r = read(serial_fd, buf, sizeof(buf));
	if (r == -1)
		err(EXIT_FAILURE, "Cannot read %s", g_device);

	/* if there is no result from the modem, return failure */
	if (r == 0) {
		dbg("%s\t: No response", command);
		return -1;
	}

	/* if we have a result and want debug info, strip CR & LF out from the output */
	if (g_debug) {
		int i;
		char bufp[SIZE_BUF];
		memcpy(bufp, buf, sizeof(buf));
		for (i=0; i<strlen(bufp); i++) {
			if (bufp[i] == '\r' || bufp[i] == '\n')
				bufp[i] = ' ';
		}
		dbg("%s\t: %s", command, bufp);
	}

	/* if the output shows "OK" return success */
	if (strstr(buf, "OK\r") != NULL) {
		return 0;
	}

	return -1;
}

/**
*	Function raised by signal catching
*/
void signal_callback_handler(int signum) {
	return;
}

/**
*	Gets the major number of the driver device
*	Returns  the major number on success
*			-1 on failure
*/
int get_major(char *driver) {
	FILE *fp;
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	char device[20];
	int major = -1;

	/* open /proc/devices file */
	if ((fp = fopen("/proc/devices", "r")) == NULL)
		err(EXIT_FAILURE, "Cannot open /proc/devices");

	/* read the file line by line */
	while ((major == -1) && (read = getline(&line, &len, fp)) != -1) {

		/* if the driver name string is found in the line, try to get the major */
		if (strstr(line, driver) != NULL) {
			if (sscanf(line,"%d %s\n", &major, device) != 2)
				major = -1;
		}

		/* free the line before getting a new one */
		if (line) {
			free(line);
			line = NULL;
		}
	}

	/* close /proc/devices file */
	fclose(fp);

	return major;
}

/**
*	Creates nodes for the virtual TTYs
*	Returns the number of nodes created
*/
int make_nodes(int major, char *basename, int number_nodes) {
	int minor, created = 0;
	dev_t device;
	char node_name[15];
	mode_t oldmask;

	/* set a new mask to get 666 mode and stores the old one */
	oldmask = umask(0);

	for (minor=1; minor<number_nodes+1; minor++) {

		/* append the minor number to the base name */
		sprintf(node_name, "%s%d", basename, minor);

		/* store a device info with major and minor */
		device = makedev(major, minor);

		/* create the actual character node */
		if (mknod(node_name, S_IFCHR | 0666, device) != 0) {
			warn("Cannot create %s", node_name);
		} else {
			created++;
			dbg("Created %s", node_name);
		}
	}

	/* revert the mask to the old one */
	umask(oldmask);

	return created;
}

/**
*	Removes previously created TTY nodes
*	Returns nothing, it doesn't really matter if it fails
*/
void remove_nodes(char *basename, int number_nodes) {
	char node_name[15];
	int node;

	for (node=1; node<number_nodes+1; node++) {

		/* append the minor number to the base name */
		sprintf(node_name, "%s%d", basename, node);

		/* unlink the actual character node */
		dbg("Removing %s", node_name);
		if (unlink(node_name) == -1)
			warn("Cannot remove %s", node_name);
	}

	return;
}

int match(const char *arg, const char *opt) {
	return (arg == opt) || (arg && (strcmp(arg, opt) == 0));
}

int parse_num(char *str, const char *opt) {
	char* end = NULL;
	int n = strtol(str, &end, 10);
	if (*end || n < 0)
		errx(EXIT_FAILURE, "Invalid number for option %s: %s", opt, str);

	return n;
}

char* parse_string(char *str, const char *opt) {
	if (str == NULL)
		errx(EXIT_FAILURE, "Argument missing for option %s", opt);

	return str;
}

int handle_string_arg(char **args, char**val, const char *opt) {
	if (match(args[0], opt)) {
		*val = parse_string(args[1], opt);
		return 1;
	}
	return 0;
}

int handle_number_arg(char **args, int *val, const char *opt) {
	if (match(args[0], opt)) {
		char *str = parse_string(args[1], opt);
		*val = parse_num(str, opt);
		return 1;
	}
	return 0;
}

void print_help() {
	printf(
		"Usage: cmux --device /dev/ttyUSB0 --speed 115200\n\n"
		"--gsm <type>	SIM900, TELIT or default. (Default: %s)\n"
		"--device <name>	Serial device name. (Default: %s)\n"
		"--speed <rate>	Serial device line speed. (Default: %d)\n"
		"--mtu <number>	MTU size. (Default: %d)\n"
		"--debug [1|0]	Enable debugging. (Default: %d)\n"
		"--daemon [1|0]	Fork into background. (Default: %d)\n"
		"--driver <name>	Driver to use. (Default: %s)\n"
		"--base <name>	Base name for the nodes. (Default: %s)\n"
		"--nodes [0-4]	Number of nodes to create. (Default: %d)\n"
		"\n",
		g_gsm, g_device, g_speed, g_mtu, g_debug,
		g_daemon, g_driver, g_base, g_nodes
	);
}

int to_line_speed(int speed) {
	switch(speed) {
		case 2400: return B2400;
		case 4800: return B4800;
		case 9600: return B9600;
		case 19200: return B19200;
		case 38400: return B38400;
		case 57600: return B57600;
		case 115200: return B115200;
		default:
			errx(EXIT_FAILURE, "Invalid value for speed: %d", speed);
	}
}

// string lower case
char *to_lower(const char *str) {
	int i;

	if (str == NULL)
		return NULL;

	char *s = strdup(str);
	for (i = 0; i < strlen(s); ++i) {
		s[i] = tolower(s[i]);
	}

	return s;
}

int main(int argc, char **argv) {
	int serial_fd, major, speed, i;
	struct termios tio;
	int ldisc = N_GSM0710;
	struct gsm_config gsm;
	char atcommand[40];

	for (i = 1; i < argc; ++i) {
		char **args = &argv[i];

		if (match(args[0], "-h")) {
			print_help();
			return 0;
		}

		if (handle_string_arg(args, &g_gsm, "--gsm")
			|| handle_string_arg(args, &g_device, "--device")
			|| handle_number_arg(args, &g_speed, "--speed")
			|| handle_number_arg(args, &g_mtu, "--mtu")
			|| handle_number_arg(args, &g_debug, "--debug")
			|| handle_number_arg(args, &g_daemon, "--daemon")
			|| handle_number_arg(args, &g_nodes, "--nodes")
			|| handle_string_arg(args, &g_driver, "--driver")
			|| handle_string_arg(args, &g_base, "--base")) {
				i++;
		} else {
			errx(EXIT_FAILURE, "Unknown argument: %s", args[0]);
		}
	};

	speed = to_line_speed(g_speed);
	g_gsm = to_lower(g_gsm);

	if (strcmp(g_gsm, "default") && strcmp(g_gsm, "sim900") && strcmp(g_gsm, "telit"))
		errx(EXIT_FAILURE, "Invalid value for --gsm: %s", g_gsm);

	if (g_daemon != 0 && g_daemon != 1)
		errx(EXIT_FAILURE, "Invalid value for --daemon: %d", g_daemon);

	if (g_debug != 0 && g_debug != 1)
		errx(EXIT_FAILURE, "Invalid value for --debug: %d", g_debug);

	if (g_nodes > 4)
		errx(EXIT_FAILURE, "Invalid value for --nodes: %d", g_nodes);

	if (match(g_gsm, "sim900")) {
		g_mtu = 255;
	} else {
		g_mtu = 512;
	}

	/* print global parameters */
	dbg(
		"gsm: %s\n"
		"device: %s\n"
		"speed: %d\n"
		"mtu: %d\n"
		"debug: %d\n"
		"daemon: %d\n"
		"driver: %s\n"
		"base: %s\n"
		"nodes: %d\n",
		g_gsm, g_device, g_speed, g_mtu, g_debug,
		g_daemon, g_driver, g_nodes ? g_base : "disabled", g_nodes
	);

	/* open the serial port */
	serial_fd = open(g_device, O_RDWR | O_NOCTTY | O_NDELAY);
	if (serial_fd == -1)
		err(EXIT_FAILURE, "Cannot open %s", g_device);

	/* get the current attributes of the serial port */
	if (tcgetattr(serial_fd, &tio) == -1)
		err(EXIT_FAILURE, "Cannot get line attributes");

	/* set the new attrbiutes */
	tio.c_iflag = 0;
	tio.c_oflag = 0;
	tio.c_cflag = CS8 | CREAD | CLOCAL;
	tio.c_cflag |= CRTSCTS;
	tio.c_lflag = 0;
	tio.c_cc[VMIN] = 1;
	tio.c_cc[VTIME] = 0;

	/* write the speed of the serial line */
	if (cfsetospeed(&tio, speed) < 0 || cfsetispeed(&tio, speed) < 0)
		err(EXIT_FAILURE, "Cannot set line speed");

	/* write the attributes */
	if (tcsetattr(serial_fd, TCSANOW, &tio) == -1)
		err(EXIT_FAILURE, "Cannot set line attributes");

	/**
	*	Send AT commands to put the modem in CMUX mode.
	*	This is vendor specific and should be changed
	*	to fit your modem needs.
	*	The following matches Quectel M95.
	*/

	if (match(g_gsm, "sim900")) {
		if (send_at_command(serial_fd, "AAAT\r") == -1)
			errx(EXIT_FAILURE, "AAAAT: bad response");
	}

	if (match(g_gsm, "telit")) {
		if (send_at_command(serial_fd, "AT#SELINT=2\r") == -1)
			errx(EXIT_FAILURE, "AT#SELINT=2: bad response");

		if (send_at_command(serial_fd, "ATE0V1&K3&D2\r") == -1)
			errx(EXIT_FAILURE, "ATE0V1&K3&D2: bad response");

		sprintf(atcommand, "AT+IPR=%d\r", g_speed);
		if (send_at_command(serial_fd, atcommand) == -1)
			errx(EXIT_FAILURE, "AT+IPR=%d: bad response", g_speed);

		if (send_at_command(serial_fd, "AT#CMUXMODE=0\r") == -1)
			errx(EXIT_FAILURE, "AT#CMUXMODE=0: bad response");

		(void)send_at_command(serial_fd, "AT+CMUX=0\r");
	} else {
		if (send_at_command(serial_fd, "AT+IFC=2,2\r") == -1)
			errx(EXIT_FAILURE, "AT+IFC=2,2: bad response");

		if (send_at_command(serial_fd, "AT+GMM\r") == -1)
			warnx("AT+GMM: bad response");

		if (send_at_command(serial_fd, "AT\r") == -1)
			warnx("AT: bad response");

		if (!match(g_gsm, "sim900")) {
			sprintf(atcommand, "AT+IPR=%d&w\r", g_speed);
			if (send_at_command(serial_fd, atcommand) == -1)
				errx(EXIT_FAILURE, "AT+IPR=%d&w: bad response", g_speed);
		}

		sprintf(atcommand, "AT+CMUX=0,0,5,%d,10,3,30,10,2\r", g_mtu);
		if (send_at_command(serial_fd, atcommand) == -1)
			errx(EXIT_FAILURE, "Cannot enable modem CMUX");
	}

	/* use n_gsm line discipline */
	if (match(g_gsm, "sim900")) {
		sleep(0.1);
	} else {
		sleep(2);
	}

	if (ioctl(serial_fd, TIOCSETD, &ldisc) < 0)
		err(EXIT_FAILURE, "Cannot set line dicipline. Is 'n_gsm' module registred?");

	/* get n_gsm configuration */
	if (ioctl(serial_fd, GSMIOC_GETCONF, &gsm) < 0)
		err(EXIT_FAILURE, "Cannot get GSM multiplex parameters");

	/* set and write new attributes */
	gsm.initiator = 1;
	gsm.encapsulation = 0;
	gsm.mru = g_mtu;
	gsm.mtu = g_mtu;
	gsm.t1 = 10;
	gsm.n2 = 3;
	gsm.t2 = 30;
	gsm.t3 = 10;

	if (ioctl(serial_fd, GSMIOC_SETCONF, &gsm) < 0)
		err(EXIT_FAILURE, "Cannot set GSM multiplex parameters");
	dbg("Line dicipline set");

	/* create the virtual TTYs */
	if (g_nodes > 0) {
		int created;
		if ((major = get_major(g_driver)) < 0)
			errx(EXIT_FAILURE, "Cannot get major number");
		if ((created = make_nodes(major, g_base, g_nodes)) < g_nodes)
			warnx("Cannot create all nodes, only %d/%d have been created.", created, g_nodes);
	}

	/* detach from the terminal if needed */
	if (g_daemon) {
		dbg("Going to background");
		if (daemon(0,0) != 0)
			err(EXIT_FAILURE, "Cannot daemonize");
	}

	/* wait to keep the line discipline enabled, wake it up with a signal */
	signal(SIGINT, signal_callback_handler);
	signal(SIGTERM, signal_callback_handler);
	pause();

	/* remove the created virtual TTYs */
	if (g_nodes > 0) {
		remove_nodes(g_base, g_nodes);
	}

	/* close the serial line */
	close(serial_fd);

	return EXIT_SUCCESS;
}
