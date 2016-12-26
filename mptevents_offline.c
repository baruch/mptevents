#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>

#include "mpt.h"

static void my_syslog_wrapper(int priority, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');

	(void)priority; // unused
}

static void usage(const char *name)
{
	fprintf(stderr, "\nmptevents_offline %s\n", VERSION);
	fprintf(stderr, "Usage:\n\t%s <dev>\n\tFor example %s %s\n\n", name, name, MPT_EVENTS_LOG);
}

int main(int argc, char **argv)
{
	struct mpt_events events;
	int fd;
	uint32_t size;
	int rc = -1;
	int first_read = 1;
	uint32_t last_context = 0;
	int ret;

    if (argc == 1) {
        usage(argv[0]);
        return 1;
    }

	my_syslog = my_syslog_wrapper;

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("Failed to open debug file");
		return 1;
	}

	while (1) {
		ret = read(fd, &size, sizeof(size));
		if (ret == 0) {
			printf("EOF\n");
			break;
		}
		if (ret != sizeof(size)) {
			perror("Error reading size from file");
			goto Exit;
		}

		if (size != sizeof(struct mpt_events)) {
			fprintf(stderr, "Sizes don't match, got %u expected %lu\n", size, sizeof(events));
			goto Exit;
		}

		ret = read(fd, &events, sizeof(events));
		if (ret != sizeof(events)) {
			perror("Error reading data from dump");
			goto Exit;
		}

		dump_all_events(&events, &last_context, first_read);
		first_read = 0;
	}

	rc = 0;
Exit:
	close(fd);
	return rc;
}
