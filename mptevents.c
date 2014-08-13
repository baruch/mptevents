#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <memory.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

#include "mpt.h"

static int opt_debug;

static int usage(const char *name)
{
	fprintf(stderr, "\nmptevents %s\n", VERSION);
	fprintf(stderr, "Usage:\n\t%s <dev>\n\tf.ex. %s /dev/mptctl\n\n", name, name);
	return 1;
}

static const char *parse_opts(int argc, char **argv)
{
	int c;
	int opt_help = 0;

	while (1) {
		//int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		static struct option long_options[] = {
			{"debug",   no_argument,       0,  'd' },
			{"help",    no_argument,       0,  'h' },
			{0,         0,                 0,  0 }
		};

		c = getopt_long(argc, argv, "dh",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
			case 0:
				printf("long opt?\n");
				break;

			case 'd':
				opt_debug = 1;
				break;

			case 'h':
				opt_help = 1;
				break;

			default:
				printf("?? getopt returned character code 0%o ??\n", c);
		}
	}

	if (opt_help) {
		usage(argv[0]);
		return NULL;
	}

	if (optind == argc) {
		fprintf(stderr, "Missing device name argument\n");
		usage(argv[0]);
		return NULL;
	}

	if (optind != argc-1) {
		fprintf(stderr, "Too many devices given, can only monitor once!\n");
		usage(argv[0]);
		return NULL;
	}

	return argv[optind];
}

static int enable_events(int fd, int port)
{
	struct mpt2_ioctl_eventenable cmd;
	int i;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.ioc_number = port;
	cmd.hdr.port_number = 0;

	// We want all the events
	for (i = 0; i < 4; i++)
		cmd.event_types[i] = 0xFFFFFFFF;

	ret = ioctl(fd, MPT2EVENTENABLE, &cmd);
	if (ret < 0) {
		syslog(LOG_ERR, "Failed to set the events on mpt device, this might not be a real mpt device: %d (%m)", errno);
	}

	return ret;
}

/* We have to read all the events and figure out which of them is new and which isn't */
static int handle_events(int fd, int port, uint32_t *highest_context, int first_read)
{
	struct mpt_events events;
	int ret;

	memset(&events, 0, sizeof(events));
    events.hdr.ioc_number = port;
	events.hdr.port_number = 0;
    events.hdr.max_data_size = sizeof(events);

	ret = ioctl(fd, MPT2EVENTREPORT, &events);
	if (ret < 0) {
		if (errno == EINTR)
			return 0;
		if (errno == EAGAIN) {
			// mpt2sas returns EAGAIN when the controller is busy, avoid a busy loop in that case
			sleep(1);
			return 0;
		}
		syslog(LOG_ERR, "Error while reading mpt events: %d (%m)", errno);
		return -1;
	}

	if (opt_debug) {
		int debug_fd = open("/tmp/mptevents.debug.log", O_WRONLY|O_CREAT|O_APPEND, 0600);
		if (debug_fd >= 0) {
			uint32_t sz = sizeof(events);
			write(debug_fd, &sz, sizeof(sz));
			write(debug_fd, &events, sz);
			close(debug_fd);
		}
	}

	dump_all_events(&events, highest_context, first_read);
	return 0;
}

static void monitor_mpt(int fd, int port)
{
	int ret;
	int poll_fd;
	struct epoll_event event;
	uint32_t last_context = 0;

	ret = enable_events(fd, port);
	if (ret < 0)
		return;

	poll_fd = epoll_create(1);
	if (poll_fd < 0) {
		syslog(LOG_ERR, "Error creating epoll to wait for events: %d (%m)", errno);
		return;
	}

	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = fd;

	ret = epoll_ctl(poll_fd, EPOLL_CTL_ADD, fd, &event);
	if (ret < 0) {
		syslog(LOG_ERR, "Error adding fd to epoll: %d (%m)", errno);
		close(poll_fd);
		return;
	}

	// First run to get the context
	ret = handle_events(fd, port, &last_context, 1);
	if (ret < 0) {
		syslog(LOG_ERR, "Error while waiting for first mpt events: %d (%m)", errno);
		close(poll_fd);
		return;
	}

	// Now we run the normal loop with the received context
	do {
		ret = epoll_wait(poll_fd, &event, 1, -1);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				syslog(LOG_ERR, "Error while waiting for mpt events: %d (%m)", errno);
				break;
			}
		} else if (ret == 0) {
			continue;
		}

		ret = handle_events(fd, port, &last_context, 0);
	} while (ret == 0);

	close(poll_fd);
}

int main(int argc, char **argv)
{
	int attempts;
	int port = 0;
	const char *devname;

	devname = parse_opts(argc, argv);
	if (devname == NULL)
		return 1;

	openlog("mptevents", LOG_PERROR, LOG_DAEMON);
	syslog(LOG_INFO, "mptevents starting for device %s", devname);

	attempts = 10;

	do {
		int fd = open(devname, O_RDWR);
		if (fd >= 0) {
			monitor_mpt(fd, port);
			close(fd);
			sleep(30);
		} else {
			syslog(LOG_INFO, "Failed to open mpt device %s: %d (%m)", devname, errno);
			sleep(30);
			attempts--;
		}
	} while (attempts > 0);

	syslog(LOG_INFO, "mptevents stopping");
	closelog();
	return 0;
}
