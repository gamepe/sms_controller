/*
SMS Server Tools 3
Copyright (C) 2006- Keijo Kasvi
http://smstools3.kekekasvi.com/

Based on SMS Server Tools 2 from Stefan Frings
http://www.meinemullemaus.de/
SMS Server Tools version 2 and below are Copyright (C) Stefan Frings.

This program is free software unless you got it under another license directly
from the author. You can redistribute it and/or modify it under the terms of
the GNU General Public License as published by the Free Software Foundation.
Either version 2 of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <grp.h>
#include "extras.h"

#include "smsd_cfg.h"


#define strcpy2(dest, value) copyvalue(dest, sizeof(dest) -1, value, name)

char *tb_sprintf(char* format, ...)
{
	va_list argp;

	va_start(argp, format);
	vsnprintf(tb, sizeof(tb), format, argp);
	va_end(argp);

	return tb;
}


void initcfg()
{

	int i;
	autosplit=3;
	receive_before_send=0;
	store_received_pdu=1;
	store_sent_pdu = 1;
	validity_period=255;
	delaytime=10;
	delaytime_mainprocess = -1;
	blocktime=60*60;
	blockafter = 3;
	errorsleeptime=10;

	eventhandler[0]=0;
	checkhandler[0]=0;
	alarmhandler[0]=0;




	decode_unicode_text=0;
	internal_combine = 1;
	internal_combine_binary = -1;
	keep_filename = 1;
	store_original_filename = 1;




	incoming_utf8 = 0;
	outgoing_utf8 = 1;
	log_charconv = 0;
	log_read_from_modem = 0;
	log_single_lines = 1;
	executable_check = 1;
	keep_messages = 0;

	ic_purge_hours = 24;
	ic_purge_minutes = 0;
	ic_purge_read = 1;
	ic_purge_interval = 30;
	strcpy(shell, "/bin/sh");


	status_signal_quality = 1;
	status_include_counters = 1;

	max_continuous_sending = 5 *60;

	trust_outgoing = 0;
	ignore_outgoing_priority = 0;


	trim_text = 1;

	message_count = 0;

	username[0] = 0;
	groupname[0] = 0;



	terminal = 0;


	*international_prefixes = 0;
	*national_prefixes = 0;


	for (i = 0; i < 1; i++)
	{
		devices[i].name[0]=0;
		devices[i].number[0]=0;
		devices[i].device[0]=0;
		devices[i].device_open_retries = 1;
		devices[i].device_open_errorsleeptime = 3;

		devices[i].identity[0] = 0;
		devices[i].conf_identity[0] = 0;
		//devices[i].identity_header[0] = 0;
		devices[i].incoming=1;
		devices[i].outgoing=1;
		devices[i].pin[0]=0;
		devices[i].pinsleeptime=0;
		devices[i].smsc[0]=0;
		devices[i].baudrate=115200;
		devices[i].send_delay=0;

		devices[i].cs_convert=1;
		devices[i].initstring[0]=0;
		devices[i].initstring2[0]=0;

		devices[i].rtscts=1;
		//devices[i].read_memory_start=1;
		strcpy(devices[i].mode,"new");

		devices[i].primary_memory[0]=0;
		devices[i].secondary_memory[0]=0;
		devices[i].secondary_memory_max=-1;
		devices[i].sending_disabled=0;
		devices[i].modem_disabled=0;
		devices[i].decode_unicode_text=-1;
		devices[i].internal_combine=-1;
		devices[i].internal_combine_binary=-1;
		devices[i].pre_init = 1;
		devices[i].check_network=1;

		devices[i].message_limit=0;
		devices[i].message_count_clear=0;
		devices[i].keep_open = 1; // 0;

		devices[i].dev_rr_interval = 5 * 60;

		devices[i].messageids = 2;

		// devices[i].check_memory_method = CM_CPMS;
		strcpy(devices[i].cmgl_value, "4");

		devices[i].read_timeout = 10;
		devices[i].ms_purge_hours = 6;
		devices[i].ms_purge_minutes = 0;
		devices[i].ms_purge_read = 1;
		devices[i].detect_message_routing = 1;
		devices[i].detect_unexpected_input = 1;
		devices[i].unexpected_input_is_trouble = 1;


		devices[i].status_signal_quality = -1;
		devices[i].status_include_counters = -1;
		devices[i].communication_delay = 0;
		devices[i].hangup_incoming_call = -1;
		devices[i].max_continuous_sending = -1;
		devices[i].startstring[0] = 0;
		devices[i].startsleeptime = 3;
		devices[i].stopstring[0] = 0;

		// devices[i].report_device_details = (strstr(smsd_version, "beta"))? 1 : 0;
		devices[i].using_routed_status_report = 0;
		devices[i].routed_status_report_cnma = 1;
		devices[i].needs_wakeup_at = 0;
		devices[i].keep_messages = 0;
		devices[i].trust_spool = 1;
		devices[i].smsc_pdu = 0;
		devices[i].signal_quality_ber_ignore = 0;
		devices[i].verify_pdu = 0;

	}



	translate_incoming = 1;

	*no_chars = 0;



	*logtime_format = 0;


	enable_smsd_debug = 0;
	ignore_exec_output = 0;
	conf_umask = 0;



	// 3.1.14:
	logtime_us = 0;
	logtime_ms = 0;


}


int getdevice(char* name)
{
	int i=0;

	while (devices[i].name[0] && (i < 1))
	{
		if (!strcmp(name,devices[i].name))
			return i;
		i++;
	}
	return -1;
}



