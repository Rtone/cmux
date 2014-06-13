/**
*	Cmux
*	Enables GSM 0710 multiplex using n_gsm
*
*	Copyright (C) 2013 - Rtone - Nicolas Le Manchet <nicolaslm@rtone.fr>
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

/* serial port of the modem */
#define SERIAL_PORT	"/dev/ttyAPP0"

/* line speed */
#define LINE_SPEED	B115200

/* maximum transfert unit (MTU), value in bytes */
#define MTU	255

/**
* whether or not to create virtual TTYs for the multiplex
*	0 : do not create
*	1 : create
*/
#define CREATE_NODES	1

/* number of virtual TTYs to create (most modems can handle up to 4) */
#define NUM_NODES	4

/* name of the virtual TTYs to create */
#define BASENAME_NODES	"/dev/ttyGSM"

/* name of the driver, used to get the major number */
#define DRIVER_NAME	"gsmtty"

/**
* whether or not to print debug messages to stderr
*	0 : debug off
*	1 : debug on
*/
#define DEBUG	1

/**
* whether or not to detach the program from the terminal
*	0 : do not daemonize
*	1 : daemonize
*/
#define DAEMONIZE	1

 /* size of the reception buffer which gets data from the serial line */
#define SIZE_BUF	256


/**
*	Prints debug messages to stderr if debug is wanted
*/
static void dbg(char *fmt, ...) {
	
	va_list args;

	if (DEBUG) {
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
		err(EXIT_FAILURE, "Cannot write to %s", SERIAL_PORT);
	
	/* wait a bit to allow the modem to rest */
	sleep(1);

	/* read the result of the command from the modem */
	memset(buf, 0, sizeof(buf));
	r = read(serial_fd, buf, sizeof(buf));
	if (r == -1)
		err(EXIT_FAILURE, "Cannot read %s", SERIAL_PORT);
	
	/* if there is no result from the modem, return failure */
	if (r == 0) {
		dbg("%s\t: No response", command);
		return -1;
	}

	/* if we have a result and want debug info, strip CR & LF out from the output */
	if (DEBUG) {
		int i;
		char bufp[SIZE_BUF];
		memcpy(bufp, buf, sizeof(buf));
		for(i=0; i<strlen(bufp); i++) {
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

	int node;
	char node_name[15];

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

int main(void) {

	int serial_fd, major;
	struct termios tio;
	int ldisc = N_GSM0710;
	struct gsm_config gsm;
	char atcommand[40];

	/* print global parameters */
	dbg("SERIAL_PORT = %s", SERIAL_PORT);

	/* open the serial port */
	serial_fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NDELAY);
	if (serial_fd == -1)
		err(EXIT_FAILURE, "Cannot open %s", SERIAL_PORT);
	
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
	if (cfsetospeed(&tio, LINE_SPEED) < 0 || cfsetispeed(&tio, LINE_SPEED) < 0)
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
	if (send_at_command(serial_fd, "AAAT\r") == -1)
		errx(EXIT_FAILURE, "AAAAT: bad response");

	if (send_at_command(serial_fd, "AT+IFC=2,2\r") == -1)
		errx(EXIT_FAILURE, "AT+IFC=2,2: bad response");	
	if (send_at_command(serial_fd, "AT+GMM\r") == -1)
		warnx("AT+GMM: bad response");
	if (send_at_command(serial_fd, "AT\r") == -1)
		warnx("AT: bad response");
//	if (send_at_command(serial_fd, "AT+IPR=115200&w\r") == -1)
//		errx(EXIT_FAILURE, "AT+IPR=115200&w: bad response");
//	sprintf(atcommand, "AT+CMUX=0,0,5,%d,10,3,30,10,2\r", MTU);
	sprintf(atcommand, "AT+CMUX=0\r");
	if (send_at_command(serial_fd, atcommand) == -1)
		errx(EXIT_FAILURE, "Cannot enable modem CMUX");

	/* use n_gsm line discipline */
	sleep(0.1);
	if (ioctl(serial_fd, TIOCSETD, &ldisc) < 0)
		err(EXIT_FAILURE, "Cannot set line dicipline. Is 'n_gsm' module registred?");

	/* get n_gsm configuration */
	if (ioctl(serial_fd, GSMIOC_GETCONF, &gsm) < 0)
		err(EXIT_FAILURE, "Cannot get GSM multiplex parameters");

	/* set and write new attributes */
	gsm.initiator = 1;
	gsm.encapsulation = 0;
	gsm.mru = MTU;
	gsm.mtu = MTU;
	gsm.t1 = 10;
	gsm.n2 = 3;
	gsm.t2 = 30;
	gsm.t3 = 10;

	if (ioctl(serial_fd, GSMIOC_SETCONF, &gsm) < 0)
		err(EXIT_FAILURE, "Cannot set GSM multiplex parameters");
	dbg("Line dicipline set");
	
	/* create the virtual TTYs */
	if (CREATE_NODES) {
		int created;
		if ((major = get_major(DRIVER_NAME)) < 0)
			errx(EXIT_FAILURE, "Cannot get major number");
		if ((created = make_nodes(major, BASENAME_NODES, NUM_NODES)) < NUM_NODES)
			warnx("Cannot create all nodes, only %d/%d have been created.", created, NUM_NODES);
	}

	/* detach from the terminal if needed */
	if (DAEMONIZE) {
		dbg("Going to background");
		if (daemon(0,0) != 0)
			err(EXIT_FAILURE, "Cannot daemonize");
	}

	/* wait to keep the line discipline enabled, wake it up with a signal */
	signal(SIGINT, signal_callback_handler);
	signal(SIGTERM, signal_callback_handler);
	pause();
	
	/* remove the created virtual TTYs */
	if (CREATE_NODES) {
		remove_nodes(BASENAME_NODES, NUM_NODES);
	}

	/* close the serial line */
	close(serial_fd);

	return EXIT_SUCCESS;
}
