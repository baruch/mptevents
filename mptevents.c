#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <memory.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdarg.h>
#include <time.h>
#include <dirent.h>

#include "mpt.h"

#define DEV_DIR "/dev"
#define MPT2_DIR "/dev/mpt2ctl"
#define MPT3_DIR "/dev/mpt3ctl"
#define SCSIHOST_DIR "/sys/class/scsi_host"
#define MISC_MAJOR_NUM 10
#define MPT2SAS_MINOR_NUM 221
#define MPT3SAS_MINOR_NUM 222

typedef enum mpt_type {
    MPT2SAS,
    MPT3SAS
}mpt_type_e;

typedef struct mpt_ioc {
    int ioc_id;
    uint32_t ioc_last_context;
    mpt_type_e ioc_type;
    int ioc_enabled;
}mpt_ioc_t;

static int opt_debug;
static int opt_stdout;
static int opt_skip_old;

static void syslog_none(int priority, const char *format, ...)
{
}

static void syslog_stdout(int priority, const char *format, ...)
{
        time_t now;
        struct tm *tm;
        char timestr[32];
        va_list ap;

        now = time(NULL);
        tm = localtime(&now);
        strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tm);
        printf("%s ", timestr);

        va_start(ap, format);
        vprintf(format, ap);
        va_end(ap);

        putchar('\n');
        fflush(stdout);
}

static int usage(const char *name)
{
	fprintf(stderr, "\nmptevents [options] %s\n", VERSION);
	fprintf(stderr, "Usage:\n\t%s <dev>\n\tFor example %s /dev/mpt3ctl\n\n", name, name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -h  --help          Display this usage information.\n"
	                "  -d  --debug         Save raw data to a debug file for later re-parsing with mptevents_offline.\n"
	                "  -o  --stdout        Output the logs to stdout with a timestamp (else, output to syslog without timestamps).\n"
	                "  -k  --skip-old      Skip the old events in case of a restart.\n"
	                "\n"
	       );
	return 1;
}

static int find_mpt_host(mpt_ioc_t **ioc_ids, int *ioc_ids_nr)
{
    DIR *dir;
	struct dirent *dirent;
    int fd = -1;
    ssize_t ret = -1;
    mpt_ioc_t *ids = NULL;
    int ids_idx = 0;
    int ids_sz = 10;

    ids = malloc(sizeof(*ids) * ids_sz);
    if (!ids)
        return -1;

    memset(ids, 0x0, sizeof(*ids) * ids_sz);

	dir = opendir(SCSIHOST_DIR);
	if (!dir) {
        free(ids);
		return -1;
	}

    while ( (dirent = readdir(dir)) != NULL ) {
		char filename[512];
		char procname[8];

		snprintf(filename, sizeof(filename), "%s/%s/proc_name", SCSIHOST_DIR, dirent->d_name);

        fd = open(filename, O_RDONLY);
		if (fd < 0)
			continue;

		ret = read(fd, procname, 8);
		if (ret < 0)
			continue;

        close(fd);

        procname[7] = '\0';

        if (strncmp("mpt3sas", procname, 7) == 0) {
            ids[ids_idx].ioc_type = MPT3SAS;
        } else if (strncmp("mpt2sas", procname, 7) == 0) {
            ids[ids_idx].ioc_type = MPT2SAS;
        } else {
            continue;
        }

        snprintf(filename, sizeof(filename), "%s/%s/unique_id", SCSIHOST_DIR, dirent->d_name);

        fd = open(filename, O_RDONLY);
        if (fd < 0)
            continue;

        ret = read(fd, procname, 8);
        if (ret < 0)
            continue;

        close(fd);

        if (ids_idx == ids_sz)
        {
            ids_sz *= 2;
            ids = realloc(ids, sizeof(*ids) * ids_sz);
            if (!ids)
                break;

            ids[ids_idx].ioc_id = atoi(procname);
            ids[ids_idx].ioc_last_context = 0;
            my_syslog(LOG_INFO, "Found MPT ioc %d type %d",
                      ids[ids_idx].ioc_id, ids[ids_idx].ioc_type);
            ids_idx += 1;
        }
        else
        {
            ids[ids_idx].ioc_id = atoi(procname);
            ids[ids_idx].ioc_last_context = 0;
            my_syslog(LOG_INFO, "Found MPT ioc %d type %d",
                      ids[ids_idx].ioc_id, ids[ids_idx].ioc_type);
            ids_idx += 1;
        }
	}

	closedir(dir);

    *ioc_ids = ids;
    *ioc_ids_nr = ids_idx;

    return 0;
}

static const char *find_mptctl_device(void)
{
	DIR *dir;
	struct dirent *dirent;
	static char matching_filename[256];
	int count = 0;

	matching_filename[0] = 0;

	dir = opendir(DEV_DIR);
	if (!dir)
		return NULL;

	while ( (dirent = readdir(dir)) != NULL ) {
		char filename[512];
		struct stat stbuf;

		snprintf(filename, sizeof(filename), "%s/%s", DEV_DIR, dirent->d_name);
		int ret = stat(filename, &stbuf);

		if (ret < 0)
			continue;

		if (!S_ISCHR(stbuf.st_mode))
			continue;

		if (major(stbuf.st_rdev) == MISC_MAJOR_NUM &&
				(minor(stbuf.st_rdev) == MPT2SAS_MINOR_NUM ||
				 minor(stbuf.st_rdev) == MPT3SAS_MINOR_NUM)) {
			printf("Found control device: %s\n", filename);
			count++;

			strcpy(matching_filename, filename);
		}
	}

	closedir(dir);

	if (count > 1) {
		my_syslog(LOG_CRIT, "More than one control file found, cannot auto-select one!");
		matching_filename[0] = 0; // Empty selection;
	}

	// If we have something in the buffer, return it, otherwise we failed
	return matching_filename[0] ? matching_filename : NULL;
}

static const char *parse_opts(int argc, char **argv)
{
	int c;
	int opt_help = 0;
	const char *mptctl_dev = NULL;

	while (1) {
		//int this_option_optind = optind ? optind : 1;
		int option_index = 0;
		static struct option long_options[] = {
			{"debug",   no_argument,       0,  'd' },
			{"stdout",  no_argument,       0,  'o' },
			{"skip-old", no_argument,      0,  'k' },
			{"help",    no_argument,       0,  'h' },
			{0,         0,                 0,  0 }
		};

		c = getopt_long(argc, argv, "dhok",
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

			case 'o':
				opt_stdout = 1;
				break;

			case 'k':
				opt_skip_old = 1;
				break;

			default:
				return NULL;
		}
	}

	if (opt_help) {
		usage(argv[0]);
		return NULL;
	}

	if (optind == argc) {
		// Try to autodetect a device
		mptctl_dev = find_mptctl_device();
		if (!mptctl_dev) {
			fprintf(stderr, "Missing device name argument (auto-detection failed)\n");
			usage(argv[0]);
			return NULL;
		}
	} else if (optind != argc-1) {
		fprintf(stderr, "Too many devices given, can only monitor one!\n");
		usage(argv[0]);
		return NULL;
	} else {
		mptctl_dev = argv[optind];
        if ((strncmp(MPT2_DIR, mptctl_dev, strlen(MPT2_DIR)) != 0)
            && (strncmp(MPT3_DIR, mptctl_dev, strlen(MPT3_DIR)) != 0)) {
            fprintf(stderr, "Unsupported device %s.\n", mptctl_dev);
			usage(argv[0]);
            return NULL;
        }
    }

	return mptctl_dev;
}

static int enable_events(int fd, int port, mpt_type_e type)
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

	ret = ioctl(fd, (type == MPT2SAS) ? MPT2EVENTENABLE : MPT3EVENTENABLE, &cmd);
	if (ret < 0) {
		my_syslog(LOG_ERR, "Failed to set the events on mpt device, this might not be a real mpt device: %d (%m)", errno);
	}

    my_syslog(LOG_INFO, "Enable the events on ioc %d", port);

	return ret;
}

/* We have to read all the events and figure out which of them is new and which isn't */
static int handle_events(int fd, int port, mpt_type_e type,
                         uint32_t *highest_context, int first_read)
{
	struct mpt_events events;
	int ret;

	memset(&events, 0, sizeof(events));
	events.hdr.ioc_number = port;
	events.hdr.port_number = 0;
	events.hdr.max_data_size = sizeof(events);

	ret = ioctl(fd, (type == MPT2SAS) ? MPT2EVENTREPORT : MPT3EVENTREPORT, &events);
	if (ret < 0) {
		if (errno == EINTR)
			return 0;
		if (errno == EAGAIN) {
			// mpt2sas returns EAGAIN when the controller is busy, avoid a busy loop in that case
			sleep(1);
			return 0;
		}
		my_syslog(LOG_ERR, "Error while reading mpt events: %d (%m)", errno);
		return -1;
	}

	if (opt_debug) {
		int debug_fd = open(MPT_EVENTS_LOG, O_WRONLY|O_CREAT|O_APPEND, 0600);
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

static void monitor_mpt(int fd)
{
	int ret;
	int poll_fd;
	struct epoll_event event;
    mpt_ioc_t *ids = NULL;
    int ids_nr = 0;
    int idx = 0;
	void (*temp_syslog)(int priority, const char *format, ...);

    ret = find_mpt_host(&ids, &ids_nr);
    if (ret < 0)
        return;

    if (ids_nr == 0) {
        free(ids);
        my_syslog(LOG_ERR, "Not found any supported MPT ioc");
        return;
    }

	for (idx = 0; idx < ids_nr; idx++) {
		ret = enable_events(fd, idx, ids[idx].ioc_type);
		if (ret < 0) {
			ids[idx].ioc_enabled = 0;
			continue;
		}

		ids[idx].ioc_enabled = 1;
	}

	poll_fd = epoll_create(1);
	if (poll_fd < 0) {
		my_syslog(LOG_ERR, "Error creating epoll to wait for events: %d (%m)", errno);
        free(ids);
        return;
	}

	memset(&event, 0, sizeof(event));
	event.events = EPOLLIN;
	event.data.fd = fd;

	ret = epoll_ctl(poll_fd, EPOLL_CTL_ADD, fd, &event);
	if (ret < 0) {
		my_syslog(LOG_ERR, "Error adding fd to epoll: %d (%m)", errno);
		close(poll_fd);
        free(ids);
		return;
	}

	// First run to get the context
	if (opt_skip_old) {
		temp_syslog = my_syslog;
		my_syslog = syslog_none;
	}

    for (idx = 0; idx < ids_nr; idx++) {
        if (!ids[idx].ioc_enabled)
            continue;

		ret = handle_events(fd, idx, ids[idx].ioc_type,
		                    &(ids[idx].ioc_last_context), 1);
		if (ret < 0) {
			my_syslog(LOG_ERR, "Error while waiting for first mpt events: %d (%m) ioc %d", errno, ids[idx].ioc_id);
		}
	}

	if (opt_skip_old) {
		my_syslog = temp_syslog;
	}

	// Now we run the normal loop with the received context
	do {
		ret = epoll_wait(poll_fd, &event, 1, -1);
		if (ret < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				my_syslog(LOG_ERR, "Error while waiting for mpt events: %d (%m)", errno);
				break;
			}
		} else if (ret == 0) {
			continue;
		}

        for (idx = 0; idx < ids_nr; idx++) {
            if (!ids[idx].ioc_enabled)
                continue;

		    ret = handle_events(fd, idx, ids[idx].ioc_type,
                                &(ids[idx].ioc_last_context), 0);
        }
	} while (1);

	close(poll_fd);
    free(ids);
}

int main(int argc, char **argv)
{
	int attempts;
	const char *devname;

	my_syslog = syslog_stdout;

	devname = parse_opts(argc, argv);
	if (devname == NULL)
		return 1;

	if (opt_stdout) {
		my_syslog = syslog_stdout;
	} else {
		openlog("mptevents", LOG_PERROR, LOG_USER);
		my_syslog = syslog;
	}
	my_syslog(LOG_INFO, "mptevents starting for device %s", devname);

	attempts = 10;

	do {
		int fd = open(devname, O_RDWR);
		if (fd >= 0) {
			monitor_mpt(fd);
			close(fd);
		} else {
			my_syslog(LOG_INFO, "Failed to open mpt device %s: %d (%m)", devname, errno);
			attempts--;
		}
		sleep(30);
	} while (attempts > 0);

	my_syslog(LOG_INFO, "mptevents stopping");

	if (!opt_stdout)
		closelog();
	return 0;
}
