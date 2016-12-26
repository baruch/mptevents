#ifndef MPTEVENTS_MPT_H
#define MPTEVENTS_MPT_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;

#define __user

#include "mpi/mpi2_type.h"
#include "mpi/mpi2.h"
#include "mpi/mpi2_cnfg.h"
#include "mpi/mpi2_ioc.h"
#include "mpt2sas_ctl.h"
#include "mpt3sas_ctl.h"

#define MPT_EVENTS_LOG "/var/log/mptevents.log"

struct mpt_events {
	struct mpt2_ioctl_header hdr;
	struct MPT2_IOCTL_EVENTS event_data[MPT2SAS_CTL_EVENT_LOG_SIZE];
};

void dump_all_events(struct mpt_events *events, uint32_t *highest_context, int first_read);

extern void (*my_syslog)(int priority, const char *format, ...);

#endif
