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
#include "mpi/mpi2_cnfg.h"
#include "mpi/mpi2_ioc.h"
#include "mpt2sas_ctl.h"

static int usage(const char *name)
{
	fprintf(stderr, "Missing MPT device name to open.\n\n");
	fprintf(stderr, "mptevents %s", VERSION);
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
		hexbuf[j++] = nibble_to_hex(buf[i] >> 4);
		hexbuf[j++] = nibble_to_hex(buf[i]);
		hexbuf[j++] = ' ';
	}
	hexbuf[j] = 0;
}

static const char *reason_code_to_text(unsigned rc)
{
	switch (rc) {
		case MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA:
			return "SMART_DATA";
		case MPI2_EVENT_SAS_DEV_STAT_RC_UNSUPPORTED:
			return "UNSUPPORTED";
		case MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET:
			return "INTERNAL_DEVICE_RESET";
		case MPI2_EVENT_SAS_DEV_STAT_RC_TASK_ABORT_INTERNAL:
			return "TASK_ABORT_INTERNAL";
		case MPI2_EVENT_SAS_DEV_STAT_RC_ABORT_TASK_SET_INTERNAL:
			return "ABORT_TASK_SET_INTERNAL";
		case MPI2_EVENT_SAS_DEV_STAT_RC_CLEAR_TASK_SET_INTERNAL:
			return "CLEAR_TASK_SET_INTERNAL";
		case MPI2_EVENT_SAS_DEV_STAT_RC_QUERY_TASK_INTERNAL:
			return "QUERY_TASK_INTERNAL";
		case MPI2_EVENT_SAS_DEV_STAT_RC_ASYNC_NOTIFICATION:
			return "ASYNC_NOTIFICATION";
		case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET:
			return "CMP_INTERNAL_DEV_RESET";
		case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_TASK_ABORT_INTERNAL:
			return "CMP_TASK_ABORT_INTERNAL";
		case MPI2_EVENT_SAS_DEV_STAT_RC_SATA_INIT_FAILURE:
			return "SATA_INIT_FAILURE";
		case MPI2_EVENT_SAS_DEV_STAT_RC_EXPANDER_REDUCED_FUNCTIONALITY:
			return "EXPANDER_REDUCED_FUNCTIONALITY";
		case MPI2_EVENT_SAS_DEV_STAT_RC_CMP_EXPANDER_REDUCED_FUNCTIONALITY:
			return "CMP_EXPANDER_REDUCED_FUNCTIONALITY";
	}

	return "UNKNOWN";
}

static void dump_sas_device_status_change(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Device Status Change: context=%u tag=%04x rc=%u(%s) port=%u asc=%02X ascq=%02X handle=%04x reserved2=%u SASAddress=%"PRIx64, event->context, evt->TaskTag, evt->ReasonCode, reason_code_to_text(evt->ReasonCode), evt->PhysicalPort, evt->ASC, evt->ASCQ, evt->DevHandle, evt->Reserved2, evt->SASAddress);
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

static void dump_name_only(const char *name, struct MPT2_IOCTL_EVENTS *event)
{
	char hexbuf[512];

	buf2hex((char *)event->data, sizeof(event->data), hexbuf, sizeof(hexbuf));
	syslog(LOG_INFO, "%s: event=%u context=%u buf=%s", name, event->event, event->context, hexbuf);
}

static void dump_temperature_threshold(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_TEMPERATURE *evt = (void*)&event->data;

	syslog(LOG_INFO, "Temperature Threshold: context=%u status=%04x sensornum=%u current_temp=%u reversed1=%u reserved2=%u reserved3=%u reserved4=%u", event->context, evt->Status, evt->SensorNum, evt->CurrentTemperature, evt->Reserved1, evt->Reserved2, evt->Reserved3, evt->Reserved4);
}

static void dump_hard_reset_received(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_HARD_RESET_RECEIVED *evt = (void*)&event->data;

	syslog(LOG_INFO, "Hard Reset Received: context=%u port=%u reserved1=%u reserved2=%u",
			event->context,
			evt->Port,
			evt->Reserved1,
			evt->Reserved2);
}

static void dump_task_set_full(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_TASK_SET_FULL *evt = (void*)&event->data;

	syslog(LOG_INFO, "Task Set Full: context=%u dev_handle=%hx current_depth=%hu", event->context, evt->DevHandle, evt->CurrentDepth);
}

static const char *raid_op_to_text(uint8_t raid_op)
{
	switch (raid_op) {
		case MPI2_EVENT_IR_RAIDOP_RESYNC:
			return "RESYNC";
		case MPI2_EVENT_IR_RAIDOP_ONLINE_CAP_EXPANSION:
			return "ONLINE_CAPACITY_EXPANSION";
		case MPI2_EVENT_IR_RAIDOP_CONSISTENCY_CHECK:
			return "CONSISTENCY_CHECK";
		case MPI2_EVENT_IR_RAIDOP_BACKGROUND_INIT:
			return "BACKGROUND_INIT";
		case MPI2_EVENT_IR_RAIDOP_MAKE_DATA_CONSISTENT:
			return "MAKE_DATA_CONSISTENT";
	}

	return "UNKNOWN";
}

static void dump_ir_operation_status(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_IR_OPERATION_STATUS *evt = (void*)&event->data;

	syslog(LOG_INFO, "IR Operation Status: context=%u vol_dev_handle=%hx raid_op=%hhu(%s) percent=%hhu elapsed_sec=%u reserved1=%hu reserved2=%hu",
			event->context,
			evt->VolDevHandle,
			evt->RAIDOperation, raid_op_to_text(evt->RAIDOperation),
			evt->PercentComplete,
			evt->ElapsedSeconds,
			evt->Reserved1,
			evt->Reserved2);
}

static const char *ir_volume_code_to_text(uint8_t rc)
{
	switch (rc) {
		case MPI2_EVENT_IR_VOLUME_RC_SETTINGS_CHANGED: return "SETTINGS_CHANGED";
		case MPI2_EVENT_IR_VOLUME_RC_STATUS_FLAGS_CHANGED: return "STATUS_FLAGS_CHANGED";
		case MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED: return "STATE_CHANGED";
	}

	return "UNKNOWN";
}

static void dump_ir_volume(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_IR_VOLUME *evt = (void*)&event->data;

	syslog(LOG_INFO, "IR Volume: context=%u vol_dev_handle=%hx reason=%hhu(%s) new_value=%u prev_value=%u reserved1=%hhu",
			event->context,
			evt->VolDevHandle,
			evt->ReasonCode, ir_volume_code_to_text(evt->ReasonCode),
			evt->NewValue,
			evt->PreviousValue,
			evt->Reserved1);
}

static const char *ir_physical_disk_rc_to_text(uint8_t rc)
{
	switch (rc) {
		case MPI2_EVENT_IR_PHYSDISK_RC_SETTINGS_CHANGED: return "SETTINGS_CHANGED";
		case MPI2_EVENT_IR_PHYSDISK_RC_STATUS_FLAGS_CHANGED: return "STATUS_FLAGS_CHANGED";
		case MPI2_EVENT_IR_PHYSDISK_RC_STATE_CHANGED: return "STATE_CHANGED";
	}

	return "UNKNOWN";
}

static void dump_ir_physical_disk(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_IR_PHYSICAL_DISK *evt = (void*)&event->data;

	syslog(LOG_INFO, "IR Physical Disk: context=%u reason=%hhu(%s) phys_disk_num=%hhu phys_disk_dev_handle=%hx slot=%hu enclosure_handle=%hu new_value=%u prev_value=%u reserved1=%hu reserved2=%hu",
			event->context,
			evt->ReasonCode, ir_physical_disk_rc_to_text(evt->ReasonCode),
			evt->PhysDiskNum,
			evt->PhysDiskDevHandle,
			evt->Slot,
			evt->EnclosureHandle,
			evt->NewValue,
			evt->PreviousValue,
			evt->Reserved1,
			evt->Reserved2);
}

static const char *ir_config_element_flag_to_text(uint16_t flags)
{
	switch (flags & MPI2_EVENT_IR_CHANGE_EFLAGS_ELEMENT_TYPE_MASK) {
		case MPI2_EVENT_IR_CHANGE_EFLAGS_VOLUME_ELEMENT: return "VOLUME_ELEMENT";
		case MPI2_EVENT_IR_CHANGE_EFLAGS_VOLPHYSDISK_ELEMENT: return "VOLPHYSDISK_ELEMENT";
		case MPI2_EVENT_IR_CHANGE_EFLAGS_HOTSPARE_ELEMENT: return "HOTSPARE_ELEMENT";
	}

	return "UNKNOWN";
}

static const char *ir_config_element_reason_to_text(uint8_t rc)
{
	switch (rc) {
		case MPI2_EVENT_IR_CHANGE_RC_ADDED: return "ADDED";
		case MPI2_EVENT_IR_CHANGE_RC_REMOVED: return "REMOVED";
		case MPI2_EVENT_IR_CHANGE_RC_NO_CHANGE: return "NO_CHANGE";
		case MPI2_EVENT_IR_CHANGE_RC_HIDE: return "HIDE";
		case MPI2_EVENT_IR_CHANGE_RC_UNHIDE: return "UNHIDE";
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED: return "VOLUME_CREATED";
		case MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED: return "VOLUME_DELETED";
		case MPI2_EVENT_IR_CHANGE_RC_PD_CREATED: return "PD_CREATED";
		case MPI2_EVENT_IR_CHANGE_RC_PD_DELETED: return "PD_DELETED";
	}

	return "UNKNOWN";
}

static void dump_ir_config_change_list(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_IR_CONFIG_CHANGE_LIST *evt = (void*)&event->data;

	syslog(LOG_INFO, "IR Config Change List: context=%u num_elements=%hhu config_num=%hhu flags=%x reserved1=%hhu reserved2=%hhu",
			event->context,
			evt->NumElements,
			evt->ConfigNum,
			evt->Flags,
			evt->Reserved1,
			evt->Reserved2);

	int i;
	for (i = 0; i < evt->NumElements; i++) {
		MPI2_EVENT_IR_CONFIG_ELEMENT *elem = &evt->ConfigElement[i];
		syslog(LOG_INFO, "IR Config Change List Element (%d/%d): flags=%hx(%s) vol_dev_handle=%hx reason=%hhu(%s) phys_disk_num=%hhu phys_disk_dev_handle=%hx",
				i+1, evt->NumElements,
				elem->ElementFlags, ir_config_element_flag_to_text(elem->ElementFlags),
				elem->VolDevHandle,
				elem->ReasonCode, ir_config_element_reason_to_text(elem->ReasonCode),
				elem->PhysDiskNum,
				elem->PhysDiskDevHandle);
	}
}

static const char *sas_discovery_flags_to_text(uint8_t flags)
{
	static char text[128];
	int i = 0;

	if (flags & MPI2_EVENT_SAS_DISC_IN_PROGRESS)
		i += sprintf(text, "%s", "IN_PROGRESS");
	if (flags & MPI2_EVENT_SAS_DISC_DEVICE_CHANGE) {
		if (i > 0)
			text[i++] = ',';
		sprintf(text + i, "%s", "DEVICE_CHANGE");
	}
	return text;
}

static const char *sas_discovery_reason_to_text(uint8_t reason)
{
	if (reason == MPI2_EVENT_SAS_DISC_RC_STARTED)
		return "STARTED";
	else if (reason == MPI2_EVENT_SAS_DISC_RC_COMPLETED)
		return "COMPLETED";
	else
		return "UNKNOWN";
}

static const char *sas_discovery_status_to_text(uint32_t status)
{
	static char text[256];
	int i = 0;

#define OUTPUT_FLAG(val, name) \
	do { \
		if (status & val) { \
			if (i > 0) \
				text[i++] = ','; \
			i += sprintf(text+i, "%s", name); \
		} \
	} while (0)
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_MAX_ENCLOSURES_EXCEED, "MAX_ENCLOSURES_EXCEED");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_MAX_EXPANDERS_EXCEED, "MAX_EXPANDERS_EXCEED");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_MAX_DEVICES_EXCEED, "MAX_DEVICES_EXCEED");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_MAX_TOPO_PHYS_EXCEED, "MAX_TOPO_PHYS_EXCEED");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_DOWNSTREAM_INITIATOR, "DOWNSTREAM_INITIATOR");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_MULTI_SUBTRACTIVE_SUBTRACTIVE, "MULTI_SUBTRACTIVE_SUBTRACTIVE");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_EXP_MULTI_SUBTRACTIVE, "EXP_MULTI_SUBTRACTIVE");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_MULTI_PORT_DOMAIN, "MULTI_PORT_DOMAIN");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_TABLE_TO_SUBTRACTIVE_LINK, "TABLE_TO_SUBTRACTIVE_LINK");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_UNSUPPORTED_DEVICE, "UNSUPPORTED_DEVICE");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_TABLE_LINK, "TABLE_LINK");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_SUBTRACTIVE_LINK, "SUBTRACTIVE_LINK");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_SMP_CRC_ERROR, "SMP_CRC_ERROR");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_SMP_FUNCTION_FAILED, "SMP_FUNCTION_FAILED");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_INDEX_NOT_EXIST, "INDEX_NOT_EXIST");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_OUT_ROUTE_ENTRIES, "OUT_ROUTE_ENTRIES");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_SMP_TIMEOUT, "SMP_TIMEOUT");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_MULTIPLE_PORTS, "MULTIPLE_PORTS");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_UNADDRESSABLE_DEVICE, "UNADDRESSABLE_DEVICE");
	OUTPUT_FLAG(MPI2_EVENT_SAS_DISC_DS_LOOP_DETECTED, "LOOP_DETECTED");
#undef OUTPUT_FLAG

	return text;
}


static void dump_sas_discovery(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_DISCOVERY *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Discovery: context=%u flags=%02hhx(%s) reason=%hhx(%s) physical_port=%hhx discovery_status=%x(%s) reserved1=%hhx",
			event->context,
			evt->Flags, sas_discovery_flags_to_text(evt->Flags),
			evt->ReasonCode, sas_discovery_reason_to_text(evt->ReasonCode),
			evt->PhysicalPort,
			evt->DiscoveryStatus, sas_discovery_status_to_text(evt->DiscoveryStatus),
			evt->Reserved1);
}

static const char *sas_broadcast_primitive_to_text(uint8_t primitive)
{
	switch (primitive) {
		case MPI2_EVENT_PRIMITIVE_CHANGE: return "CHANGE";
		case MPI2_EVENT_PRIMITIVE_SES: return "SES";
		case MPI2_EVENT_PRIMITIVE_EXPANDER: return "EXPANDER";
		case MPI2_EVENT_PRIMITIVE_ASYNCHRONOUS_EVENT: return "ASYNCHRONOUS_EVENT";
		case MPI2_EVENT_PRIMITIVE_RESERVED3: return "RESERVED3";
		case MPI2_EVENT_PRIMITIVE_RESERVED4: return "RESERVED4";
		case MPI2_EVENT_PRIMITIVE_CHANGE0_RESERVED: return "CHANGE0_RESERVED";
		case MPI2_EVENT_PRIMITIVE_CHANGE1_RESERVED: return "CHANGE1_RESERVED";
	}

	return "UNKNOWN";
}

static void dump_sas_broadcast_primitive(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_BROADCAST_PRIMITIVE *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Broadcast Primitive: context=%u phy_num=%hhu port=%hhu port_width=%hhu primitive=%hhu(%s)",
			event->context,
			evt->PhyNum,
			evt->Port,
			evt->PortWidth,
			evt->Primitive, sas_broadcast_primitive_to_text(evt->Primitive));
}

static const char *sas_notify_primitive_to_text(uint8_t primitive)
{
	switch (primitive) {
		case MPI2_EVENT_NOTIFY_ENABLE_SPINUP: return "ENABLE_SPINUP";
		case MPI2_EVENT_NOTIFY_POWER_LOSS_EXPECTED: return "POWER_LOSS_EXPECTED";
		case MPI2_EVENT_NOTIFY_RESERVED1: return "RESERVED1";
		case MPI2_EVENT_NOTIFY_RESERVED2: return "RESERVED2";
	}

	return "UNKNOWN";
}

static void dump_sas_notify_primitive(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_NOTIFY_PRIMITIVE *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Notify Primitive: context=%u phy_num=%hhu port=%hhu primitive=%hhu(%s) reserved1=%hhx",
			event->context,
			evt->PhyNum,
			evt->Port,
			evt->Primitive, sas_notify_primitive_to_text(evt->Primitive),
			evt->Reserved1);
}

static const char *sas_init_dev_status_reason_to_text(uint8_t reason)
{
	switch (reason) {
		case MPI2_EVENT_SAS_INIT_RC_ADDED: return "ADDED";
		case MPI2_EVENT_SAS_INIT_RC_NOT_RESPONDING: return "NOT_RESPONDING";
	}

	return "UNKNOWN";
}

static void dump_sas_init_dev_status_change(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_INIT_DEV_STATUS_CHANGE *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Init Dev Status Change: context=%u reason=%hhd(%s) phys_port=%hhu dev_handle=%hu sas_address=%"PRIx64,
			event->context,
			evt->ReasonCode, sas_init_dev_status_reason_to_text(evt->ReasonCode),
			evt->PhysicalPort,
			evt->DevHandle,
			evt->SASAddress);
}

static void dump_sas_init_table_overflow(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_INIT_TABLE_OVERFLOW *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Init Table Overflow: context=%u max_init=%hu current_init=%hu sas_address=%"PRIx64,
			event->context,
			evt->MaxInit,
			evt->CurrentInit,
			evt->SASAddress);
}

static const char *sas_topology_change_list_status_to_text(uint8_t status)
{
	switch (status) {
		case MPI2_EVENT_SAS_TOPO_ES_NO_EXPANDER: return "NO_EXPANDER";
		case MPI2_EVENT_SAS_TOPO_ES_ADDED: return "ADDED";
		case MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING: return "NOT_RESPONDING";
		case MPI2_EVENT_SAS_TOPO_ES_RESPONDING: return "RESPONDING";
		case MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING: return "DELAY_NOT_RESPONDING";
	}

	return "UNKNOWN";
}

static const char *sas_topo_link_rate_to_text(uint8_t link_rate)
{
	switch (link_rate) {
		case MPI2_EVENT_SAS_TOPO_LR_UNKNOWN_LINK_RATE: return "UNKNOWN_LINK_RATE";
		case MPI2_EVENT_SAS_TOPO_LR_PHY_DISABLED: return "PHY_DISABLED";
		case MPI2_EVENT_SAS_TOPO_LR_NEGOTIATION_FAILED: return "NEGOTIATION_FAILED";
		case MPI2_EVENT_SAS_TOPO_LR_SATA_OOB_COMPLETE: return "SATA_OOB_COMPLETE";
		case MPI2_EVENT_SAS_TOPO_LR_PORT_SELECTOR: return "PORT_SELECTOR";
		case MPI2_EVENT_SAS_TOPO_LR_SMP_RESET_IN_PROGRESS: return "SMP_RESET_IN_PROGRESS";
		case MPI2_EVENT_SAS_TOPO_LR_UNSUPPORTED_PHY: return "UNSUPPORTED_PHY";
		case MPI2_EVENT_SAS_TOPO_LR_RATE_1_5: return "RATE_1_5";
		case MPI2_EVENT_SAS_TOPO_LR_RATE_3_0: return "RATE_3_0";
		case MPI2_EVENT_SAS_TOPO_LR_RATE_6_0: return "RATE_6_0";
	}

	return "UNKNOWN";
}

static const char *sas_topo_phy_status_to_text(uint8_t status)
{
	static char text[256];
	int i = 0;

#define OUTPUT_FLAG(val, name) \
	do { \
		if (status & val) { \
			if (i > 0) \
				text[i++] = ','; \
			i += sprintf(text+i, "%s", name); \
		} \
	} while (0)
	OUTPUT_FLAG(MPI2_EVENT_SAS_TOPO_PHYSTATUS_VACANT, "PHYSTATUS_VACANT");
	OUTPUT_FLAG(0x40, "UNKNOWN_40");
	OUTPUT_FLAG(0x20, "UNKNOWN_20");
	OUTPUT_FLAG(MPI2_EVENT_SAS_TOPO_PS_MULTIPLEX_CHANGE, "PS_MULTIPLEX_CHANGE");
#undef OUTPUT_FLAG

	const char *rc = "UNKNOWN";
	switch (status & MPI2_EVENT_SAS_TOPO_RC_MASK) {
		case MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED: rc = "TARG_ADDED";
		case MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING: rc = "TARG_NOT_RESPONDING";
		case MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED: rc = "PHY_CHANGED";
		case MPI2_EVENT_SAS_TOPO_RC_NO_CHANGE: rc = "NO_CHANGE";
		case MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING: rc = "DELAY_NOT_RESPONDING";
	}

	if (i > 0)
		text[i++] = ',';
	sprintf(text+i, "%s", rc);

	return text;
}

static void dump_sas_topology_change_list(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_TOPOLOGY_CHANGE_LIST *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Topology Change List: context=%u enclosure_handle=%hx expander_dev_handle=%hx num_phys=%hhu num_entries=%hhu start_phy_num=%hhu exp_status=%hhu(%s) physical_port=%hhu reserved1=%hhu reserved2=%hu",
			event->context,
			evt->EnclosureHandle,
			evt->ExpanderDevHandle,
			evt->NumPhys,
			evt->NumEntries,
			evt->StartPhyNum,
			evt->ExpStatus, sas_topology_change_list_status_to_text(evt->ExpStatus),
			evt->PhysicalPort,
			evt->Reserved1,
			evt->Reserved2);

	int i;
	for (i = 0; i < evt->NumEntries; i++) {
		MPI2_EVENT_SAS_TOPO_PHY_ENTRY *entry = &evt->PHY[i];
		syslog(LOG_INFO, "SAS Topology Change List Entry (%d/%d): attached_dev_handle=%hx link_rate=%hhu(prev=%s,next=%s) phy_status=%hhu(%s)",
				i+1, evt->NumEntries,
				entry->AttachedDevHandle,
				entry->LinkRate,
				sas_topo_link_rate_to_text((entry->LinkRate & MPI2_EVENT_SAS_TOPO_LR_CURRENT_MASK) >> MPI2_EVENT_SAS_TOPO_LR_CURRENT_SHIFT),
				sas_topo_link_rate_to_text((entry->LinkRate & MPI2_EVENT_SAS_TOPO_LR_PREV_MASK) >> MPI2_EVENT_SAS_TOPO_LR_PREV_SHIFT),
				entry->PhyStatus, sas_topo_phy_status_to_text(entry->PhyStatus));
	}
}

static const char *sas_enclosure_dev_status_change_reason_to_text(uint8_t reason)
{
	switch (reason) {
		case MPI2_EVENT_SAS_ENCL_RC_ADDED: return "ADDED";
		case MPI2_EVENT_SAS_ENCL_RC_NOT_RESPONDING: return "NOT_RESPONDING";
	}

	return "UNKNOWN";
}

static void dump_sas_enclosure_device_status_change(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_ENCL_DEV_STATUS_CHANGE *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Enclosure Device Status Change: context=%u enclosure_handle=%hx reason=%hhu(%s) enclosure_logical_id=%"PRIx64" num_slots=%hu start_slot=%hu phy_bits=%x",
			event->context,
			evt->EnclosureHandle,
			evt->ReasonCode, sas_enclosure_dev_status_change_reason_to_text(evt->ReasonCode),
			evt->EnclosureLogicalID,
			evt->NumSlots,
			evt->StartSlot,
			evt->PhyBits);
}

static const char *sas_quiesce_reason_to_text(uint8_t reason)
{
	switch (reason) {
		case MPI2_EVENT_SAS_QUIESCE_RC_STARTED: return "STARTED";
		case MPI2_EVENT_SAS_QUIESCE_RC_COMPLETED: return "COMPLETED";
	}

	return "UNKNOWN";
}

static void dump_sas_quiesce(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_QUIESCE *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Quiesce: context=%u reason=%hhu(%s) reserved1=%hhu reserved2=%hu reserved3=%u",
			event->context,
			evt->ReasonCode, sas_quiesce_reason_to_text(evt->ReasonCode),
			evt->Reserved1,
			evt->Reserved2,
			evt->Reserved3);
}

static const char *phy_event_code_to_text(uint8_t code)
{
	switch (code) {
		case MPI2_SASPHY3_EVENT_CODE_NO_EVENT: return "NO_EVENT";
		case MPI2_SASPHY3_EVENT_CODE_INVALID_DWORD: return "INVALID_DWORD";
		case MPI2_SASPHY3_EVENT_CODE_RUNNING_DISPARITY_ERROR: return "RUNNING_DISPARITY_ERROR";
		case MPI2_SASPHY3_EVENT_CODE_LOSS_DWORD_SYNC: return "LOSS_DWORD_SYNC";
		case MPI2_SASPHY3_EVENT_CODE_PHY_RESET_PROBLEM: return "PHY_RESET_PROBLEM";
		case MPI2_SASPHY3_EVENT_CODE_ELASTICITY_BUF_OVERFLOW: return "ELASTICITY_BUF_OVERFLOW";
		case MPI2_SASPHY3_EVENT_CODE_RX_ERROR: return "RX_ERROR";
		case MPI2_SASPHY3_EVENT_CODE_RX_ADDR_FRAME_ERROR: return "RX_ADDR_FRAME_ERROR";
		case MPI2_SASPHY3_EVENT_CODE_TX_AC_OPEN_REJECT: return "TX_AC_OPEN_REJECT";
		case MPI2_SASPHY3_EVENT_CODE_RX_AC_OPEN_REJECT: return "RX_AC_OPEN_REJECT";
		case MPI2_SASPHY3_EVENT_CODE_TX_RC_OPEN_REJECT: return "TX_RC_OPEN_REJECT";
		case MPI2_SASPHY3_EVENT_CODE_RX_RC_OPEN_REJECT: return "RX_RC_OPEN_REJECT";
		case MPI2_SASPHY3_EVENT_CODE_RX_AIP_PARTIAL_WAITING_ON: return "RX_AIP_PARTIAL_WAITING_ON";
		case MPI2_SASPHY3_EVENT_CODE_RX_AIP_CONNECT_WAITING_ON: return "RX_AIP_CONNECT_WAITING_ON";
		case MPI2_SASPHY3_EVENT_CODE_TX_BREAK: return "TX_BREAK";
		case MPI2_SASPHY3_EVENT_CODE_RX_BREAK: return "RX_BREAK";
		case MPI2_SASPHY3_EVENT_CODE_BREAK_TIMEOUT: return "BREAK_TIMEOUT";
		case MPI2_SASPHY3_EVENT_CODE_CONNECTION: return "CONNECTION";
		case MPI2_SASPHY3_EVENT_CODE_PEAKTX_PATHWAY_BLOCKED: return "PEAKTX_PATHWAY_BLOCKED";
		case MPI2_SASPHY3_EVENT_CODE_PEAKTX_ARB_WAIT_TIME: return "PEAKTX_ARB_WAIT_TIME";
		case MPI2_SASPHY3_EVENT_CODE_PEAK_ARB_WAIT_TIME: return "PEAK_ARB_WAIT_TIME";
		case MPI2_SASPHY3_EVENT_CODE_PEAK_CONNECT_TIME: return "PEAK_CONNECT_TIME";
		case MPI2_SASPHY3_EVENT_CODE_TX_SSP_FRAMES: return "TX_SSP_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_RX_SSP_FRAMES: return "RX_SSP_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_TX_SSP_ERROR_FRAMES: return "TX_SSP_ERROR_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_RX_SSP_ERROR_FRAMES: return "RX_SSP_ERROR_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_TX_CREDIT_BLOCKED: return "TX_CREDIT_BLOCKED";
		case MPI2_SASPHY3_EVENT_CODE_RX_CREDIT_BLOCKED: return "RX_CREDIT_BLOCKED";
		case MPI2_SASPHY3_EVENT_CODE_TX_SATA_FRAMES: return "TX_SATA_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_RX_SATA_FRAMES: return "RX_SATA_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_SATA_OVERFLOW: return "SATA_OVERFLOW";
		case MPI2_SASPHY3_EVENT_CODE_TX_SMP_FRAMES: return "TX_SMP_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_RX_SMP_FRAMES: return "RX_SMP_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_RX_SMP_ERROR_FRAMES: return "RX_SMP_ERROR_FRAMES";
		case MPI2_SASPHY3_EVENT_CODE_HOTPLUG_TIMEOUT: return "HOTPLUG_TIMEOUT";
		case MPI2_SASPHY3_EVENT_CODE_MISALIGNED_MUX_PRIMITIVE: return "MISALIGNED_MUX_PRIMITIVE";
		case MPI2_SASPHY3_EVENT_CODE_RX_AIP: return "RX_AIP";
	}

	return "UNKNOWN";
}

static const char *counter_type_to_text(uint8_t type)
{
	switch (type) {
		case MPI2_SASPHY3_COUNTER_TYPE_WRAPPING: return "WRAPPING";
		case MPI2_SASPHY3_COUNTER_TYPE_SATURATING: return "SATURATING";
		case MPI2_SASPHY3_COUNTER_TYPE_PEAK_VALUE: return "PEAK_VALUE";
	}

	return "UNKNOWN";
}

static const char *time_units_to_text(uint8_t unit)
{
	switch (unit) {
		case MPI2_SASPHY3_TIME_UNITS_10_MICROSECONDS: return "10_MICROSECONDS";
		case MPI2_SASPHY3_TIME_UNITS_100_MICROSECONDS: return "100_MICROSECONDS";
		case MPI2_SASPHY3_TIME_UNITS_1_MILLISECOND: return "1_MILLISECOND";
		case MPI2_SASPHY3_TIME_UNITS_10_MILLISECONDS: return "10_MILLISECONDS";
	}

	return "UNKNOWN";
}

static const char *threshold_flags_to_text(uint8_t flags)
{
	switch (flags) {
		case MPI2_SASPHY3_TFLAGS_PHY_RESET: return "PHY_RESET";
		case MPI2_SASPHY3_TFLAGS_EVENT_NOTIFY: return "EVENT_NOTIFY";
		case MPI2_SASPHY3_TFLAGS_EVENT_NOTIFY|MPI2_SASPHY3_TFLAGS_PHY_RESET: return "PHY_RESET,EVENT_NOTIFY";
	}

	return "UNKNOWN";
}

static void dump_sas_phy_counter(struct MPT2_IOCTL_EVENTS *event)
{
	MPI2_EVENT_DATA_SAS_PHY_COUNTER *evt = (void*)&event->data;

	syslog(LOG_INFO, "SAS Phy Counter: context=%u timestamp=%"PRIu64" phy_event_code=%hhu(%s) phy_num=%hhu phy_event_info=%x counter_type=%hhu(%s) threshold_window=%hhu time_units=%hhu(%s) event_threshold=%u threshold_flags=%hx(%s) reserved1=%u reserved2=%hu reserved3=%hhu reserved4=%hu",
			event->context,
			evt->TimeStamp,
			evt->PhyEventCode, phy_event_code_to_text(evt->PhyEventCode),
			evt->PhyNum,
			evt->PhyEventInfo,
			evt->CounterType, counter_type_to_text(evt->CounterType),
			evt->ThresholdWindow,
			evt->TimeUnits, time_units_to_text(evt->TimeUnits),
			evt->EventThreshold,
			evt->ThresholdFlags, threshold_flags_to_text(evt->ThresholdFlags),
			evt->Reserved1,
			evt->Reserved2,
			evt->Reserved3,
			evt->Reserved4);
}

static void dump_event(struct MPT2_IOCTL_EVENTS *event)
{
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
			dump_name_only("State Change", event);
			break;

		case MPI2_EVENT_HARD_RESET_RECEIVED:
			dump_hard_reset_received(event);
			break;

		case MPI2_EVENT_EVENT_CHANGE:
			dump_name_only("Event Change", event);
			break;

		case MPI2_EVENT_TASK_SET_FULL:
			dump_task_set_full(event);
			break;

		case MPI2_EVENT_IR_OPERATION_STATUS:
			dump_ir_operation_status(event);
			break;

		case MPI2_EVENT_SAS_DISCOVERY:
			dump_sas_discovery(event);
			break;

		case MPI2_EVENT_SAS_BROADCAST_PRIMITIVE:
			dump_sas_broadcast_primitive(event);
			break;

		case MPI2_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE:
			dump_sas_init_dev_status_change(event);
			break;

		case MPI2_EVENT_SAS_INIT_TABLE_OVERFLOW:
			dump_sas_init_table_overflow(event);
			break;

		case MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST:
			dump_sas_topology_change_list(event);
			break;

		case MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE:
			dump_sas_enclosure_device_status_change(event);
			break;

		case MPI2_EVENT_IR_VOLUME:
			dump_ir_volume(event);
			break;

		case MPI2_EVENT_IR_PHYSICAL_DISK:
			dump_ir_physical_disk(event);
			break;

		case MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST:
			dump_ir_config_change_list(event);
			break;

		case MPI2_EVENT_LOG_ENTRY_ADDED:
			dump_name_only("Log Entry Added", event);
			break;

		case MPI2_EVENT_SAS_PHY_COUNTER:
			dump_sas_phy_counter(event);
			break;

		case MPI2_EVENT_HOST_BASED_DISCOVERY_PHY:
			dump_name_only("Host Based Discovery Phy", event);
			break;

		case MPI2_EVENT_SAS_QUIESCE:
			dump_sas_quiesce(event);
			break;

		case MPI2_EVENT_SAS_NOTIFY_PRIMITIVE:
			dump_sas_notify_primitive(event);
			break;

		case MPI2_EVENT_TEMP_THRESHOLD:
			dump_temperature_threshold(event);
			break;

		case MPI2_EVENT_HOST_MESSAGE:
			dump_name_only("Host Message", event);
			break;

		default:
			dump_name_only("Unknown Event", event);
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

/* We have to read all the events and figure out which of them is new and which isn't */
static int handle_events(int fd, int port, uint32_t *highest_context, int first_read)
{
	struct {
		struct mpt2_ioctl_header hdr;
		struct MPT2_IOCTL_EVENTS event_data[MPT2SAS_CTL_EVENT_LOG_SIZE];
	} events;
	int ret;
	int i;
	uint32_t new_context = *highest_context;

	memset(&events, 0, sizeof(events));
    events.hdr.ioc_number = port;
	events.hdr.port_number = 0;
    events.hdr.max_data_size = sizeof(events);

	ret = ioctl(fd, MPT2EVENTREPORT, &events);
	if (ret < 0) {
		syslog(LOG_ERR, "Error while reading mpt events: %d (%m)", errno);
		return -1;
	}

	for (i = 0; i < MPT2SAS_CTL_EVENT_LOG_SIZE; i++) {
		struct MPT2_IOCTL_EVENTS *event = &events.event_data[i];

		if (!event->event)
			continue;

		/* Because we read the entire circular buffer we can get the highest
		 * context before the older contexts that we didn't read yet. As such
		 * we need to compare the context to the old context we had and only
		 * replace it once we finished the read
		 */
		if (first_read || (int)(event->context - *highest_context) > 0) {
			dump_event(event);
			new_context = event->context;
		}
	}

	*highest_context = new_context;
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

	if (argc != 2 || argv[1][0] == '-') {
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
