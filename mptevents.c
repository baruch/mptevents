#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <stdint.h>
#include <memory.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <inttypes.h>

typedef uint8_t u8;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;

#define __user

#include "mpi/mpi2_type.h"
#include "mpi/mpi2.h"
#include "mpi/mpi2_ioc.h"
#include "mpt2sas_ctl.h"

static int usage(const char *name)
{
	fprintf(stderr, "Missing MPT device name to open.\n\n");
	fprintf(stderr, "Usage:\n\t%s <dev>\n\tf.ex. %s /dev/mptctl\n\n", name, name);
	return 1;
}

static inline char nibble_to_hex(char nibble)
{
	nibble &= 0xF;
	if (nibble < 10)
		return '0' + nibble;
	else
		return 'A' + nibble - 10;
}

static void buf2hex(char *buf, int buf_len, char *hexbuf, int hexbuf_sz)
{
	int i, j;

	for (i = 0, j = 0; i < buf_len && j < hexbuf_sz - 4; i++) {
		hexbuf[j] = nibble_to_hex(buf[i] >> 4);
		hexbuf[j++] = nibble_to_hex(buf[i]);
		hexbuf[j++] = ' ';
	}
	hexbuf[j] = 0;
}

static void dump_sas_device_status_change(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Device Status Change: context=%u tag=%04x rc=%u port=%u asc=%02X ascq=%02X handle=%04x reserved2=%u SASAddress=%"PRIx64, event->context, evt->TaskTag, evt->ReasonCode, evt->PhysicalPort, evt->ASC, evt->ASCQ, evt->DevHandle, evt->Reserved2, evt->SASAddress);
}

static void dump_log_data(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_LOG_ENTRY_ADDED *evt = (void*)&event->data;

	syslog(LOG_INFO, "Log Entry Added: context=%u timestamp=%"PRIu64" reserved1=%u seq=%u entry_qualifier=%u vp_id=%u vf_id=%u reserved2=%u", event->context, evt->TimeStamp, evt->Reserved1, evt->LogSequence, evt->LogEntryQualifier, evt->VP_ID, evt->VF_ID, evt->Reserved2);
	// TODO: Parse or at least hexdump the LogData
}

static void dump_gpio_interrupt(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_GPIO_INTERRUPT *evt = (void*)&event->data;

	syslog(LOG_INFO, "GPIO Interrupt: context=%u gpionum=%u reserved1=%u reserved2=%u", event->context, evt->GPIONum, evt->Reserved1, evt->Reserved2);
}

static void dump_event(struct MPT2_IOCTL_EVENTS *event)
{
	char hexbuf[512];

	switch (event->event) {
		case MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE:
			dump_sas_device_status_change(event);
			break;

		case MPI2_EVENT_LOG_DATA:
			dump_log_data(event);
			break;

		case MPI2_EVENT_GPIO_INTERRUPT:
			dump_gpio_interrupt(event);
			break;

		case MPI2_EVENT_STATE_CHANGE:
		case MPI2_EVENT_HARD_RESET_RECEIVED:
		case MPI2_EVENT_EVENT_CHANGE:
		case MPI2_EVENT_TASK_SET_FULL:
		case MPI2_EVENT_IR_OPERATION_STATUS:
		case MPI2_EVENT_SAS_DISCOVERY:
		case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
		case MPI2_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE:
		case MPI2_EVENT_SAS_INIT_TABLE_OVERFLOW:
		case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
		case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
		case MPI2_EVENT_IR_VOLUME:
		case MPI2_EVENT_IR_PHYSICAL_DISK:
		case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
		case MPI2_EVENT_LOG_ENTRY_ADDED:
		case MPI2_EVENT_SAS_PHY_COUNTER:
		case MPI2_EVENT_HOST_BASED_DISCOVERY_PHY:
		case MPI2_EVENT_SAS_QUIESCE:
		case MPI2_EVENT_SAS_NOTIFY_PRIMITIVE:
		case MPI2_EVENT_TEMP_THRESHOLD:
		case MPI2_EVENT_HOST_MESSAGE:

		default:
			buf2hex((char *)event->data, sizeof(event->data), hexbuf, sizeof(hexbuf));
			syslog(LOG_INFO, "mpt event: event=%u context=%u buf=%s", event->event, event->context, hexbuf);
			break;
	}
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

static int handle_events(int fd, int port)
{
	int ret;
	struct mpt2_ioctl_eventreport events;

	memset(&events, 0, sizeof(events));
    events.hdr.ioc_number = port;
	events.hdr.port_number = 0;
    events.hdr.max_data_size = sizeof(events);

	ret = ioctl(fd, MPT2EVENTREPORT, &events);
	if (ret < 0) {
		syslog(LOG_ERR, "Error while reading mpt events: %d (%m)", errno);
		return -1;
	}

	if (events.event_data[0].event)
		dump_event(&events.event_data[0]);
	return 0;
}

static void monitor_mpt(int fd, int port)
{
	int ret;
	int poll_fd;
	struct epoll_event event;

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

		ret = handle_events(fd, port);
	} while (ret == 0);

	close(poll_fd);
}

int main(int argc, char **argv)
{
	int attempts;
	int port = 0;

	if (argc != 2) {
		return usage(argv[0]);
	}

	openlog("mptevents", LOG_PERROR, LOG_DAEMON);
	syslog(LOG_INFO, "mptevents starting");

	attempts = 10;

	do {
		int fd = open(argv[1], O_RDWR);
		if (fd >= 0) {
			monitor_mpt(fd, port);
			close(fd);
		} else {
			syslog(LOG_INFO, "Failed to open mpt device %s: %d (%m)", argv[1], errno);
			sleep(30);
			attempts--;
		}
	} while (attempts > 0);

	syslog(LOG_INFO, "mptevents stopping");
	closelog();
	return 0;
}
