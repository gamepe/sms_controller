#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/reboot.h>

#include "spdu.h"
#include "modeminit.h"
#include "smsd_cfg.h"
#include "extras.h"
#include "charset.h"
#include "fec.h"


int break_workless_delay; // To break the delay when SIGCONT is received.
int workless_delay;
int break_suspend; // To break suspend when SIGUSR2 is received.
int concatenated_id = 0;
//toolchain-mipsel_24kec+dsp_gcc-4.8-linaro_uClibc-0.9.33.2/mipsel-openwrt-linux

typedef struct {

	unsigned short magic;
	unsigned char fec_hdr[8]; //on the chunk data
	unsigned short type;
	unsigned short indx; //  index + 32 indx of chunk
	unsigned short total_indx;
	unsigned short size; //  size +32      //sizeof chunk

} XCHUNK_HDR;

char g_sztmpdatafilename[256];
char g_szencodedpackage_filename[256];
char g_szdecodedpackage_filename[256];

void delete_tmp_files()
{

	remove(g_szdecodedpackage_filename);
	remove(g_szencodedpackage_filename);
	remove(g_sztmpdatafilename);

}


void router_reboot()
{

	sync();
	reboot(RB_POWER_OFF);

}

int printsms(char *line1, char *line2, char *filename, int *stored_concatenated,
		int incomplete);

int create_dump_sms_files();

int  dump2tmpfile(char *input_file, void *pdata, size_t size)
{

	FILE *pfile = fopen(input_file, "ab");
	if (! pfile) return 0;
	fseek(pfile, 0, SEEK_END);
	fwrite(pdata, sizeof(unsigned char), size, pfile);
	fclose(pfile);
	return 1;


}

/* =======================================================================
 Send a whole message, this can have many parts
 ======================================================================= */

int send1sms(char *recvier_number, char *msg, int* quick, int* errorcounter)
// Search the queues for SMS to send and send one of them.
// Returns 0 if queues are empty
// Returns -1 if sending failed (v 3.0.1 because of a modem)
// v 3.0.1 returns -2 if sending failed because of a message file, in this case
// there is no reason to block a modem.
// Returns 1 if successful
{

	char to[SIZE_TO] = { 0 };
	char from[SIZE_FROM] = { 0 };
	char smsc[SIZE_SMSC] = { 0 };

	char text[MAXTEXT];
	int with_udh = -1;
	int had_udh = 0; // for binary message handling.
	char udh_data[SIZE_UDH_DATA];
	int textlen;
	char part_text[maxsms_pdu + 1];
	int part_text_length;

	int part;
	int parts = 0;
	int maxpartlen;
	int eachpartlen;
	int alphabet;
	int success = 0;
	int flash;
	int report;
	int split = 0;
	int tocopy;
	int reserved;
	char messageids[SIZE_MESSAGEIDS] = { 0 };

	int validity;

	int hex;
	int system_msg;
	int to_type;

	int replace_msg = 0;

	int i;
	char *fail_text = 0;
	char error_text[2048];

	strcpy(to, recvier_number);

	alphabet = 0;
	with_udh = 0;
	to_type = 0;
	flash = 0;

	// SMSC setting is allowed only if there is smsc set in the config file:
	if (DEVICE.smsc[0] == 0 && !DEVICE.smsc_pdu)
		smsc[0] = 0;

	// If the checkhandler has moved this message, some things are probably not checked:
	if (to[0] == 0) {
		fail_text = "No destination";
		success = -2;
	}
#ifdef USE_ICONV
	else if (alphabet>3)
#else
	else if (alphabet > 2)
#endif
			{
		fail_text = "Invalid alphabet";
		success = -2;
	} else if (to_type == -2) {

		fail_text = "Invalid number type";
		success = -2;
	}

	if (success == 0) {

		// Set a default for with_udh if it is not set in the message file.
		if (with_udh == -1) {
			if ((alphabet == 1 || udh_data[0]) && !system_msg)
				with_udh = 1;
			else
				with_udh = 0;
		}

		// Save the udh bit, with binary concatenated messages we need to know if
		// there is user pdu in the begin of a message.
		had_udh = with_udh;

		// If the header includes udh-data then enforce udh flag even if it is not 1.
		if (udh_data[0])
			with_udh = 1;

		split = autosplit;

		// disable splitting if udh flag or udh_data is 1
		// 3.1beta7: binary message can have an udh:
		//if (with_udh && split)
		// 3.1:
		//    if (*udh_data && split)
		if (*udh_data && split && alphabet != 1) {
			split = 0;
			// Keke: very old and possible wrong message, if there is no need to do splitting? Autosplit=0 prevents this message.
			printf("Cannot split this message because it has an UDH.");
		}

		// If this is a text message, then read also the text
		if (alphabet < 1 || alphabet >= 2) {

			printf("!! This is %stext message\n",
					(alphabet >= 2) ? "unicode " : "");

			maxpartlen = (alphabet >= 2) ? maxsms_ucs2 : maxsms_pdu; // ucs2 = 140, pdu = 160

			strcpy(text, msg);

			textlen = strlen(text);

			if (textlen == 0) {

				fail_text = "No text";
				parts = 0;
				success = -2;
			} else {

				if (alphabet == 3) {
					if (is_ascii_gsm(text, textlen))
						alphabet = -1;
					else {
						alphabet = 2;
						textlen = (int) iconv_utf2ucs(text, textlen,
								sizeof(text));
					}
				}

				// In how many parts do we need to split the text?
				if (split > 0) {
					// 3.1beta7: Unicode part numbering now supported.
					//if (alphabet == 2) // With unicode messages
					//  if (split == 2)  // part numbering with text is not yet supported,
					//    split = 3;     // using udh numbering instead.

					// if it fits into 1 SM, then we need 1 part
					if (textlen <= maxpartlen) {
						parts = 1;
						reserved = 0;
						eachpartlen = maxpartlen;
					} else if (split == 2) // number with text
							{
						reserved = 4; // 1/9_
						if (alphabet == 2)
							reserved *= 2;
						eachpartlen = maxpartlen - reserved;
						parts = (textlen + eachpartlen - 1) / eachpartlen;
						// If we have more than 9 parts, we need to reserve 6 chars for the numbers
						// And recalculate the number of parts.
						if (parts > 9) {
							reserved = 6; // 11/99_
							if (alphabet == 2)
								reserved *= 2;
							eachpartlen = maxpartlen - reserved;
							parts = (textlen + eachpartlen - 1) / eachpartlen;
							// 3.1beta7: there can be more than 99 parts:
							if (parts > 99) {
								reserved = 8; // 111/255_
								if (alphabet == 2)
									reserved *= 2;
								eachpartlen = maxpartlen - reserved;
								parts = (textlen + eachpartlen - 1)
										/ eachpartlen;

								// 3.1.1:
								if (parts > 255) {

									fail_text = "Too long text";
									parts = 0;
									success = -2;
								}
							}
						}
					} else if (split == 3) // number with udh
							{
						// reserve 7 chars for the UDH
						reserved = 7;
						if (alphabet == 2) // Only six with unicode
							reserved = 6;
						eachpartlen = maxpartlen - reserved;
						parts = (textlen + eachpartlen - 1) / eachpartlen;
						concatenated_id++;
						if (concatenated_id > 255)
							concatenated_id = 0;
					} else {
						// no numbering, each part can have the full size
						eachpartlen = maxpartlen;
						reserved = 0;
						parts = (textlen + eachpartlen - 1) / eachpartlen;
					}
				} else {
					// split is 0, too long message is just cutted.
					eachpartlen = maxpartlen;
					reserved = 0;
					parts = 1;
				}

				if (parts > 1)
					printf(
							"Splitting this message into %i parts of max %i characters%s.",
							parts,
							(alphabet == 2) ? eachpartlen / 2 : eachpartlen,
							(alphabet == 2) ? " (unicode)" : "");
			}
		} else {
#ifdef DEBUGMSG
			printf("!! This is a binary message.\n");
#endif
			maxpartlen = maxsms_binary;
			if (hex == 1) {

				printf("readSMShex \n");

			} else {
				printf("readSMStext \n");

			}
			// 3.1:
			if (*udh_data) {
				int bytes = (strlen(udh_data) + 1) / 3;
				int i;

				if (textlen <= (ssize_t) sizeof(text) - bytes) {
					memmove(text + bytes, text, textlen);
					for (i = 0; i < bytes; i++)
						text[i] = octet2bin(udh_data + i * 3);
					textlen += bytes;
				}

				*udh_data = 0;
			}

			eachpartlen = maxpartlen;
			reserved = 0;
			parts = 1;
			// Is the message empty?
			if (textlen == 0) {

				fail_text = "No data";
				parts = 0;
				success = -2;
			}

			// 3.1beta7: Is the message too long?:
			if (textlen > maxpartlen) {
				if (system_msg) {
					fail_text = "Too long data for system message";
					parts = 0;
					success = -2;
				} else if (!split) {
					fail_text = "Too long data for single part sending";
					parts = 0;
					success = -2;
				} else {
					// Always use UDH numbering.
					split = 3;
					reserved = 6;
					eachpartlen = maxpartlen - reserved;
					parts = (textlen + eachpartlen - 1) / eachpartlen;
					concatenated_id++;
					if (concatenated_id > 255)
						concatenated_id = 0;
				}
			}
		}
	} // success was ok after initial checks.

	// parts can now be 0 if there is some problems,
	// fail_text and success is also set.

	// Try to send each part
	if (parts > 0)

		// If sending concatenated message, replace_msg should not be used (otherwise previously
		// received part is replaced with a next one...
		if (parts > 1)
			replace_msg = 0;

	for (part = 0; part < parts; part++) {
		if (split == 2 && parts > 1) // If more than 1 part and text numbering
				{
			sprintf(part_text, "%i/%i     ", part + 1, parts);

			if (alphabet == 2) {
				for (i = reserved; i > 0; i--) {
					part_text[i * 2 - 1] = part_text[i - 1];
					part_text[i * 2 - 2] = 0;
				}
			}

			tocopy = textlen - (part * eachpartlen);
			if (tocopy > eachpartlen)
				tocopy = eachpartlen;
#ifdef DEBUGMSG
			printf("!! tocopy=%i, part=%i, eachpartlen=%i, reserved=%i\n",tocopy,part,eachpartlen,reserved);
#endif
			memcpy(part_text + reserved, text + (eachpartlen * part), tocopy);
			part_text_length = tocopy + reserved;
		} else if (split == 3 && parts > 1) // If more than 1 part and UDH numbering
				{
			// in this case the numbers are not part of the text, but UDH instead
			tocopy = textlen - (part * eachpartlen);
			if (tocopy > eachpartlen)
				tocopy = eachpartlen;
#ifdef DEBUGMSG
			printf("!! tocopy=%i, part=%i, eachpartlen=%i, reserved=%i\n",tocopy,part,eachpartlen,reserved);
#endif
			memcpy(part_text, text + (eachpartlen * part), tocopy);
			part_text_length = tocopy;
			sprintf(udh_data, "05 00 03 %02X %02X %02X", concatenated_id, parts,
					part + 1);
			with_udh = 1;
		} else // No part numbers
		{
			tocopy = textlen - (part * eachpartlen);
			if (tocopy > eachpartlen)
				tocopy = eachpartlen;
#ifdef DEBUGMSG
			printf("!! tocopy=%i, part=%i, eachpartlen=%i\n",tocopy,part,eachpartlen);
#endif
			memcpy(part_text, text + (eachpartlen * part), tocopy);
			part_text_length = tocopy;
		}

	}

	{
		// Try to send the sms

		// If there is no user made udh (message header say so), the normal
		// concatenation header can be used. With user made udh the concatenation
		// information of a first part is inserted to the existing udh. Other but
		// first message part can be processed as usual.

		if (alphabet == 1 && part == 0 && parts > 1 && had_udh) {
			int n;

			*udh_data = 0;
			n = part_text[0];

			// Check if length byte has too high value:
			if (n >= part_text_length) {

				fail_text = "Incorrect first byte of UDH";
				success = -2;

			}

			for (i = part_text_length - 1; i > n; i--)
				part_text[i + 5] = part_text[i];

			part_text[n + 1] = 0;
			part_text[n + 2] = 3;
			part_text[n + 3] = concatenated_id;
			part_text[n + 4] = parts;
			part_text[n + 5] = part + 1;
			part_text[0] = n + 5;
			part_text_length += 5;
		}

		i = send_part((from[0]) ? from : DEVICE.number, to, part_text,
				part_text_length, alphabet, with_udh, udh_data, *quick, flash,
				messageids, smsc, report, validity, part, parts, replace_msg, 0,
				to_type, error_text);

		if (i == 0) {
			printf("Sending was successful.\n");
			*quick = 1;
			success = 1;

			// Possible previous errors are ignored because now the modem worked well:
			*errorcounter = 0;
		} else {
			printf("Sending failed...\n");
			*quick = 0;
			success = -1;

			if (i == 1)
				fail_text = "Modem initialization failed";
			else if (*error_text)
				fail_text = error_text;
			else
				fail_text = "Unknown";

		}
	}

	if (*messageids && DEVICE.messageids == 3)
		strcat(messageids, " .");

	if (success < 0) {
		printf("Sending failed......\n");

		if (success == -1) {
			// Check how often this modem failed and block it if it seems to be broken
			(*errorcounter)++;
			if (*errorcounter >= blockafter) {

				t_sleep(blocktime);
				*errorcounter = 0;
			}
		}

		if (outgoing_pdu_store) {
			free(outgoing_pdu_store);
			outgoing_pdu_store = NULL;
		}

	}

	return success;
}

/* ==========================================================================================
 Send a part of a message, this is physically one SM with max. 160 characters or 140 bytes
 ========================================================================================== */

int send_part(char* from, char* to, char* text, int textlen, int alphabet,
		int with_udh, char* udh_data, int quick, int flash, char* messageids,
		char* smsc, int report, int validity, int part, int parts,
		int replace_msg, int system_msg, int to_type, char *error_text)
// alphabet can be -1=GSM 0=ISO 1=binary 2=UCS2
// with_udh can be 0=off or 1=on or -1=auto (auto = 1 for binary messages and text message with udh_data)
// udh_data is the User Data Header, only used when alphabet= -1 or 0.
// With alphabet=1 or 2, the User Data Header should be included in the text argument.
// smsc is optional. Can be used to override config file setting.
// Output: messageids
// 3.1beta7: return value changed:
// 0 = OK.
// 1 = Modem initialization failed.
// 2 = Cancelled because of too many retries.
// 3 = Cancelled because shutdown request while retrying.
// error_text: latest modem response. Might have a value even if return value is ok (when retry helped).
{
	char pdu[1024];
	int retries;
	char command[128];
	char command2[1024];
	char answer[1024];
	char* posi1;
	char* posi2;
	char partstr[41];
	char replacestr[41];
	time_t start_time;
	char tmpid[10] = { 0 };

#ifdef DEBUGMSG
	printf("!! send_part(from=%s, to=%s, text=..., textlen=%i, alphabet=%i, with_udh=%i, udh_data=%s, quick=%i, flash=%i, messageids=..., smsc=%s, report=%i, validity=%i, part=%i, parts=%i, replace_msg=%i, system_msg=%i, to_type=%i)\n",
			from, to, textlen, alphabet, with_udh, udh_data, quick, flash, smsc, report, validity, part, parts, replace_msg, system_msg, to_type);
#endif
	if (error_text)
		*error_text = 0;
	start_time = time(0);
	// Mark modem as sending

	*partstr = 0;
	if (parts > 1)
		sprintf(partstr, " (part %i/%i)", part + 1, parts);
	printf("Sending SMS%s from %s to %s\n", partstr, from, to);

	// 3.1beta7: Now logged only if a message file contained Report:yes.
	if (report == 1 && !DEVICE.incoming)
		printf("Cannot receive status report because receiving is disabled");

	if ((quick == 0 || (*smsc && !DEVICE.smsc_pdu))
			&& DEVICE.sending_disabled == 0) {
		int i;

		i = initialize_modem_sending(smsc);
		if (i) {

			return (i == 7) ? 3 : 1;
		}
	} else {
		// 3.1:
		if (DEVICE.sending_disabled == 0 && DEVICE.check_network) {
			switch (wait_network_registration(1, 100)) {
			case -1:

				return 1;

			case -2:

				return 3;
			}
		}
	}

	// Compose the modem command
	make_pdu(to, text, textlen, alphabet, flash, report, with_udh, udh_data,
			DEVICE.mode, pdu, validity, replace_msg, system_msg, to_type, smsc);
	if (strcasecmp(DEVICE.mode, "old") == 0)
		sprintf(command, "AT+CMGS=%i\r", (int) strlen(pdu) / 2);
	else
		sprintf(command, "AT%s+CMGS=%i\r", (DEVICE.verify_pdu) ? "E1" : "",
				(int) strlen(pdu) / 2 - 1); // 3.1.4: verify_pdu

	sprintf(command2, "%s\x1A", pdu);

	if (store_sent_pdu) {
		char *title = "PDU: ";

		if (!outgoing_pdu_store) {
			if ((outgoing_pdu_store = (char *) malloc(
					strlen(title) + strlen(pdu) + 2)))
				*outgoing_pdu_store = 0;
		} else
			outgoing_pdu_store = (char *) realloc((void *) outgoing_pdu_store,
					strlen(outgoing_pdu_store) + strlen(title) + strlen(pdu)
							+ 2);

		if (outgoing_pdu_store)
			sprintf(strchr(outgoing_pdu_store, 0), "%s%s\n", title, pdu);
	}

	// 3.1.5: DEBUGGING:
	//if (DEVICE.sending_disabled == 1)
	if (DEVICE.sending_disabled == 1
			|| (enable_smsd_debug && parts > 1 && part == 0
					&& strstr(smsd_debug, "drop1"))
			|| (enable_smsd_debug && parts > 2 && part == 1
					&& strstr(smsd_debug, "drop2"))) {
		printf("Test run, NO actual sending:%s from %s to %s", partstr, from,
				to);
		printf("PDU to %s: %s %s", to, command, pdu);

		// 3.1.12: Simulate sending time
		//sleep(1);
		t_sleep(4 + getrand(10));

		strcpy(messageids, "1");

		printf("SMS sent, Message_id: %s, To: %s, sending time %i sec.",
				messageids, to, time(0) - start_time);

		return 0;
	} else {
		retries = 0;
		while (1) {
			// Send modem command
			// 3.1.5: initial timeout changed 1 --> 2 and for PDU 6 --> 12.
			put_command(command, answer, sizeof(answer), 2, "(>)|(ERROR)");
			// Send message if command was successful
			if (!strstr(answer, "ERROR")) {
				put_command(command2, answer, sizeof(answer), 12,
						EXPECT_OK_ERROR);

				// 3.1.14:
				if (DEVICE.verify_pdu) {
					char *buffer;

					if ((buffer = strdup(command2))) {
						int i;

						i = strlen(buffer);
						if (i > 1) {
							buffer[i - 1] = 0;

							if (strstr(answer, buffer))
								printf("Verify PDU: OK");
							else {
								int src = 0;
								char *p = answer;

								while (buffer[src]) {
									if (*p && (p = strchr(p, buffer[src]))) {
										p++;
										src++;
									} else {

										printf("Verify PDU: -> %s", buffer);
										printf("Verify PDU: <- %s", answer);
										break;
									}
								}

								if (buffer[src] == 0)
									printf("Verify PDU: OK (not exact)");
							}
						}

						free(buffer);
					}
				}
			}

			// 3.1.14:
			if (DEVICE.verify_pdu) {
				char answer2[1024];

				put_command("ATE0\r", answer2, sizeof(answer2), 1,
						EXPECT_OK_ERROR);
			}

			// Check answer
			if (strstr(answer, "OK")) {
				// If the modem answered with an ID number then copy it into the messageid variable.
				posi1 = strstr(answer, "CMGS: ");
				if (posi1) {
					posi1 += 6;
					posi2 = strchr(posi1, '\r');
					if (!posi2)
						posi2 = strchr(posi1, '\n');
					if (posi2)
						posi2[0] = 0;

					// 3.1:
					//strcpy(messageid,posi1);
					strcpy(tmpid, posi1);
					while (*tmpid == ' ')
						strcpyo(tmpid, tmpid + 1);

					// 3.1.1:
					switch (DEVICE.messageids) {
					case 1:
						if (!(*messageids))
							strcpy(messageids, tmpid);
						break;

					case 2:
						strcpy(messageids, tmpid);
						break;

					case 3:
						if (*messageids)
							sprintf(strchr(messageids, 0), " %s", tmpid);
						else
							strcpy(messageids, tmpid);
						break;
					}

#ifdef DEBUGMSG
					printf("!! messageid=%s\n", tmpid);
#endif
				}

				*replacestr = 0;
				if (replace_msg)
					sprintf(replacestr, ", Replace_msg: %i", replace_msg);

				*answer = 0;
				if (retries > 0)
					snprintf(answer, sizeof(answer), " Retries: %i.", retries);

				printf(
						"SMS sent%s, Message_id: %s%s, To: %s, sending time %i sec.%s",
						partstr, tmpid, replacestr, to, time(0) - start_time,
						answer);

				return 0;
			} else {
				// 3.1.14: show retries and trying time when stopping:
				int result = 0;

				// Set the error text:
				if (error_text) {
					strcpy(error_text, answer);
					cut_ctrl(error_text);
					cutspaces(error_text);
				}

				// 3.1.5:
				//writelogfile0(LOG_ERR, tb_sprintf("The modem said ERROR or did not answer."));
				if (!(*answer))
					printf("The modem did not answer (expected OK).");
				else
					printf("The modem answer was not OK: %s", cut_ctrl(answer));

				if (++retries <= 2) {

					if (t_sleep(errorsleeptime))
						result = 3; // Cancel if terminating
					else if (initialize_modem_sending("")) // Initialize modem after error
						result = 1; // Cancel if initializing failed
				} else
					result = 2; // Cancel if too many retries

				if (result) {

					printf(
							"Sending SMS%s to %s failed, trying time %i sec. Retries: %i.",
							partstr, to, time(0) - start_time, retries - 1);

					return result;
				}
			}
		}
	}
}
/* =======================================================================
 Device-Spooler (one for each modem)
 ======================================================================= */

int cmd_to_modem(
//
//
//
		char *command, int cmd_number) {
	int result = 1;
	char *cmd;
	char *p;
	char answer[500];
	char buffer[600];

	int is_ussd = 0; // 3.1.7

	if (!command || !(*command))
		return 1;

	if (!try_openmodem())
		return 0;

	if (cmd_number == 1 && DEVICE.needs_wakeup_at) {
		put_command("AT\r", 0, 0, 1, 0);
		usleep_until(time_usec() + 100000);
		read_from_modem(answer, sizeof(answer), 2);
	}

	if ((cmd = malloc(strlen(command) + 2))) {
		sprintf(cmd, "%s\r", command);
		// 3.1.5: Special case: AT+CUSD, wait USSD message:
		//put_command(*modem, device, cmd, answer, sizeof(answer), 1, EXPECT_OK_ERROR);
		if (!strncasecmp(command, "AT+CUSD", 7) && strlen(command) > 9) {
			is_ussd++;
			put_command(cmd, answer, sizeof(answer), 3, "(\\+CUSD:)|(ERROR)");
		} else
		// 3.1.12:
		if (*cmd == '[' && strchr(cmd, ']')) {
			char *expect;

			if ((expect = strdup(cmd + 1))) {
				*(strchr(expect, ']')) = 0;
				put_command(strchr(cmd, ']') + 1, answer, sizeof(answer), 1,
						expect);
				free(expect);
			}
		} else
			// -------
			put_command(cmd, answer, sizeof(answer), 1, EXPECT_OK_ERROR);

		if (*answer) {
			char timestamp[81];

			make_datetime_string(timestamp, sizeof(timestamp), 0, 0,
					logtime_format);

			while ((p = strchr(answer, '\r')))
				*p = ' ';
			while ((p = strchr(answer, '\n')))
				*p = ' ';
			while ((p = strstr(answer, "  ")))
				strcpyo(p, p + 1);
			if (*answer == ' ')
				strcpyo(answer, answer + 1);
			p = answer + strlen(answer);
			while (p > answer && p[-1] == ' ')
				--p;
			*p = 0;

		}

		free(cmd);
	}

	return result;
}

/* =======================================================================
 Read a memory space from SIM card
 ======================================================================= */

int readsim(int sim, char* line1, char* line2)
/* reads a SMS from the given SIM-memory */
/* returns number of SIM memory if successful, otherwise -1 */
/* 3.1.5: In case of timeout return value is -2. */
/* line1 contains the first line of the modem answer */
/* line2 contains the pdu string */
{
	char command[500];
	char answer[1024];
	char* begin1;
	char* begin2;
	char* end1;
	char* end2;

	line2[0] = 0;
	line1[0] = 0;

	if (DEVICE.modem_disabled == 1) {
		printf("Cannot try to get stored message %i, MODEM IS DISABLED", sim);
		return 0;
	}

	printf("Trying to get stored message %i\n", sim);
	sprintf(command, "AT+CMGR=%i\r", sim);
	// 3.1beta3: Some modems answer OK in case of empty memory space (added "|(OK)")
	if (put_command(command, answer, sizeof(answer), 1,
			"(\\+CMGR:.*OK)|(ERROR)|(OK)") == -2)
		return -2;

	// 3.1.7:
	while ((begin1 = strchr(answer, '\n')))
		*begin1 = '\r';

	while ((begin1 = strstr(answer, "\r\r")))
		strcpyo(begin1, begin1 + 1);
	// ------

	if (strstr(answer, ",,0\r")) // No SMS,  because Modem answered with +CMGR: 0,,0
		return -1;
	if (strstr(answer, "ERROR")) // No SMS,  because Modem answered with ERROR
		return -1;
	begin1 = strstr(answer, "+CMGR:");
	if (begin1 == 0)
		return -1;
	end1 = strstr(begin1, "\r");
	if (end1 == 0)
		return -1;
	begin2 = end1 + 1;
	end2 = strstr(begin2 + 1, "\r");
	if (end2 == 0)
		return -1;
	strncpy(line1, begin1, end1 - begin1);
	line1[end1 - begin1] = 0;
	strncpy(line2, begin2, end2 - begin2);
	line2[end2 - begin2] = 0;
	cutspaces(line1);
	cut_ctrl(line1);
	cutspaces(line2);
	cut_ctrl(line2);
	if (strlen(line2) == 0)
		return -1;

	printf("!! line1=%s, line2=%s\n", line1, line2);

	return sim;
}
/* =======================================================================
 Delete message on the SIM card
 ======================================================================= */
void hexDump(char *desc, void *addr, int len) {
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char*) addr;

	// Output description if given.
	if (desc != NULL)
		printf("%s:\n", desc);

	// Process every byte in the data.
	for (i = 0; i < len; i++) {
		// Multiple of 16 means new line (with line offset).

		if ((i % 16) == 0) {
			// Just don't print ASCII for the zeroth line.
			if (i != 0)
				printf("  %s\n", buff);

			// Output the offset.
			printf("  %04x ", i);
		}

		// Now the hex code for the specific character.
		printf(" %02x", pc[i]);

		// And store a printable ASCII character for later.
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];
		buff[(i % 16) + 1] = '\0';
	}

	// Pad out last line if not exactly 16 characters.
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}

	// And print the final ASCII bit.
	printf("  %s\n", buff);
}
void deletesms(int sim) /* deletes the selected sms from the sim card */
{
	char command[100];
	char answer[500];

	if (keep_messages || DEVICE.keep_messages)
		printf("Keeping message %i\n", sim);
	else {
		printf("Deleting message %i\n", sim);
		sprintf(command, "AT+CMGD=%i\r", sim);
		put_command(command, answer, sizeof(answer), 1, EXPECT_OK_ERROR);
	}
}

void deletesms_list(char *memory_list) {
	char command[100];
	char answer[500];
	int sim;
	char *p;

	while (*memory_list) {
		sim = atoi(memory_list);

		if ((p = strchr(memory_list, ',')))
			strcpyo(memory_list, p + 1);
		else
			*memory_list = 0;

		if (keep_messages || DEVICE.keep_messages)
			printf("Keeping message %i\n", sim);
		else {
			printf("Deleting message %i\n", sim);
			sprintf(command, "AT+CMGD=%i\r", sim);
			put_command(command, answer, sizeof(answer), 1, EXPECT_OK_ERROR);
		}

	}
}

/* =======================================================================
 Check size of SIM card
 ======================================================================= */

// Return value: 1 = OK, 0 = check failed.
int check_memory(int *used_memory, int *max_memory, char *memory_list,
		size_t memory_list_size, char *delete_list, int delete_list_size) {
	// 3.1.5: GMGL list needs much more space: using global buffer.
	//char answer[500];
	char *answer = check_memory_buffer;
	// 3.1.12: Use allocated memory:
	//int size_answer = SIZE_CHECK_MEMORY_BUFFER;
	int size_answer = (int) check_memory_buffer_size;

	char* start;
	char* end;
	char tmp[100];
	char *p;
	char *pos;
	int i;

	if (!answer)
		return 0;

	(void) delete_list_size; // 3.1.7: remove warning.
	// Set default values in case that the modem does not support the +CPMS command
	*used_memory = 1;
	*max_memory = 10;

	*answer = 0;

	if (DEVICE.modem_disabled == 1) {
		*used_memory = 0;
		return 1;
	}

	printf("Checking memory size\n");

	// 3.1.5:
	if (DEVICE.check_memory_method == CM_CMGD) {
		*used_memory = 0;
		*memory_list = 0;
		put_command("AT+CMGD=?\r", answer, size_answer, 1,
				"(\\+CMGD:.*OK)|(ERROR)");
		// +CMGD: (1,22,23,28),(0-4) \r OK
		// +CMGD: (),(0-4) \r OK

		if (strstr(answer, "()"))
			return 1;

		if ((p = strstr(answer, " ("))) {
			strcpyo(answer, p + 2);
			if ((p = strstr(answer, "),"))) {
				*p = 0;
				if (strlen(answer) < memory_list_size) {
					*used_memory = 1;
					strcpy(memory_list, answer);
					p = memory_list;
					while ((*p) && (p = strstr(p + 1, ",")))
						(*used_memory)++;
				}
			} else
				printf(
						"Incomplete answer for AT+CMGD=?, feature not supported?");
		}
		printf("Used memory is %i%s%s", *used_memory,
				(*memory_list) ? ", list: " : "", memory_list);
		return 1;
	} else if (value_in(DEVICE.check_memory_method, 5, CM_CMGL,
			CM_CMGL_DEL_LAST, CM_CMGL_CHECK, CM_CMGL_DEL_LAST_CHECK,
			CM_CMGL_SIMCOM)) {
		// 3.1.5: Check CMGL result, it can be incorrect or broken if bad baudrate is used:
		char *errorstr = 0;
		char *term;
		char *p2;
		int mnumber;
		int mlength;
		int pdu1;
		int save_log_single_lines = log_single_lines;
		char buffer[256 * LENGTH_PDU_DETAIL_REC + 1];

		*used_memory = 0;
		*memory_list = 0;
		*buffer = 0;

		sprintf(tmp, "AT+CMGL=%s\r", DEVICE.cmgl_value);

		// 3.1.5: Empty list gives OK answer without +CMGL prefix:
		log_single_lines = 0;

		// 3.1.12: With large number of messages and slow modem, much longer timeout than "1" is required.
		put_command(tmp, answer, size_answer, 10, EXPECT_OK_ERROR);

		log_single_lines = save_log_single_lines;

		pos = answer;
		while ((p = strstr(pos, "+CMGL:"))) {
			mnumber = 0; // initial value for error message
			if (!(term = strchr(p, '\r'))) {
				errorstr = "Line end not found (fields)";
				break;
			}

			// 3.1.6: Message number can be zero:
			//if ((mnumber = atoi(p + 6)) <= 0)
			mnumber = (int) strtol(p + 6, NULL, 0);
			if (errno == EINVAL || mnumber < 0) {
				errorstr = "Invalid message number";
				break;
			}

			if (value_in(DEVICE.check_memory_method, 3, CM_CMGL_CHECK,
					CM_CMGL_DEL_LAST_CHECK, CM_CMGL_SIMCOM)) {
				p2 = term;
				while (*p2 != ',') {
					if (p2 <= p) {
						errorstr = "Message length field not found";
						break;
					}
					p2--;
				}

				if (errorstr)
					break;

				if ((mlength = atoi(p2 + 1)) <= 0) {
					errorstr = "Invalid length information";
					break;
				}

				p = term + 1;
				if (*p == '\n')
					p++;
				if ((pdu1 = octet2bin_check(p)) <= 0) {
					errorstr = "Invalid first byte of PDU";
					break;
				}

				if (!(term = strchr(p, '\r'))) {
					errorstr = "Line end not found (PDU)";
					break;
				}

				// Some devices give PDU total length, some give length of TPDU:
				if (pdu1 * 2 + 2 + mlength * 2 != (int) (term - p)
						&& mlength * 2 != (int) (term - p)) {
					errorstr = "PDU does not match with length information";
					break;
				}

				i = get_pdu_details(buffer, sizeof(buffer), p, mnumber);
				if (i > 1) {
					errorstr = "Fatal error";
					break;
				}
			}

			// By default this is the result:
			sprintf(tmp, "%s%i", (*memory_list) ? "," : "", mnumber);
			if (strlen(memory_list) + strlen(tmp) < memory_list_size) {
				(*used_memory)++;
				strcat(memory_list, tmp);
			} else {
				errorstr = "Too many messages";
				break;
			}

			pos = term + 1;
		}

		if (errorstr) {

			printf("CMGL handling error: message %i, %s", mnumber, errorstr);

			return 0;
		}

		if (*buffer) {
			sort_pdu_details(buffer);

			// Show even if there is only one message, timestamp may be interesting.
			//if (strlen(buffer) > LENGTH_PDU_DETAIL_REC)

		}

		// If there is at least one message and SIMCOM SIM600 style handling is required.
		// ( one because delete_list is always needed ).
		if (*used_memory > 0
				&& value_in(DEVICE.check_memory_method, 1, CM_CMGL_SIMCOM)) {
			/*
			 001 A 358401234567____________________111 001/001 r 00-00-00 00-00-00n

			 3.1.7:
			 001 A 00-00-00 00-00-00 358401234567____________________111 001/001 rn

			 */
			int pos_sender = 24; //6;
			int pos_messageid = 56; //38;
			int length_messagedetail = 11;
			int pos_partnr = 60; //42;
			int pos_partcount = 64; //46;
			int pos_timestamp = 6; //52;
			int offset_messageid = 32; // from sender_id

			// NOTE:
			// Exact size for buffers:
			char sender[32 + 1];
			char sender_id[35 + 1];

			int p_count;
			int read_all_parts;

			*used_memory = 0;
			*memory_list = 0;
			*delete_list = 0;
			pos = buffer;
			while (*pos) {
				if (!strncmp(&pos[pos_messageid], "999 001/001",
						length_messagedetail)) {
					// Single part message. Always taken and fits to the list.
					mnumber = atoi(pos);

					sprintf(strchr(memory_list, 0), "%s%i",
							(*memory_list) ? "," : "", mnumber);
					(*used_memory)++;

					sprintf(strchr(delete_list, 0), "%s%i",
							(*delete_list) ? "," : "", mnumber);

					pos += LENGTH_PDU_DETAIL_REC;
					continue;
				}

				// Multipart message, first part which is seen.
				strncpy(sender, &pos[pos_sender], sizeof(sender) - 1);
				sender[sizeof(sender) - 1] = 0;
				while (sender[strlen(sender) - 1] == ' ')
					sender[strlen(sender) - 1] = 0;

				strncpy(sender_id, &pos[pos_sender], sizeof(sender_id) - 1);
				sender_id[sizeof(sender_id) - 1] = 0;

				p_count = atoi(&pos[pos_partcount]);
				p = pos;
				for (i = 1; *p && i <= p_count; i++) {
					if (strncmp(&p[pos_sender], sender_id,
							sizeof(sender_id) - 1) || atoi(&p[pos_partnr]) != i)
						break;
					p += LENGTH_PDU_DETAIL_REC;
				}

				read_all_parts = 1;

				if (i <= p_count) {
					// Some part(s) missing.
					// With SIM600: only the first _available_ part can be deleted and all
					// other parts of message are deleted by the modem.
					int ms_purge;
					int remaining = -1;

					read_all_parts = 0;

					if ((ms_purge = DEVICE.ms_purge_hours * 60
							+ DEVICE.ms_purge_minutes) > 0) {
						time_t rawtime;
						struct tm *timeinfo;
						time_t now;
						time_t msgtime;

						time(&rawtime);
						timeinfo = localtime(&rawtime);
						now = mktime(timeinfo);

						p = pos + pos_timestamp;
						timeinfo->tm_year = atoi(p) + 100;
						timeinfo->tm_mon = atoi(p + 3) - 1;
						timeinfo->tm_mday = atoi(p + 6);
						timeinfo->tm_hour = atoi(p + 9);
						timeinfo->tm_min = atoi(p + 12);
						timeinfo->tm_sec = atoi(p + 15);
						msgtime = mktime(timeinfo);

						if (ms_purge * 60 > now - msgtime) {
							remaining = ms_purge - (now - msgtime) / 60;
							ms_purge = 0;
						}
					}

					if (ms_purge > 0) {
						if (DEVICE.ms_purge_read) {
							read_all_parts = 1;
							printf(
									"Reading message %s from %s partially, all parts not found and timeout expired",
									sender_id + offset_messageid, sender);
						} else {
							printf(
									"Deleting message %s from %s, all parts not found and timeout expired",
									sender_id + offset_messageid, sender);
							deletesms(atoi(pos));
						}
					} else {
						printf(
								"Skipping message %s from %s, all parts not found",
								sender_id + offset_messageid, sender);
						if (remaining > 0) {
							printf(
									("Message %s from %s will be %s after %i minutes unless remaining parts are received", sender_id
											+ offset_messageid, sender,
											(DEVICE.ms_purge_read) ?
													"read partially" :
													"deleted", remaining));
						}
					}
				}

				if (read_all_parts)
					sprintf(strchr(delete_list, 0), "%s%i",
							(*delete_list) ? "," : "", atoi(pos));

				while (*pos) {
					if (strncmp(&pos[pos_sender], sender_id,
							sizeof(sender_id) - 1))
						break;

					if (read_all_parts) {
						sprintf(strchr(memory_list, 0), "%s%i",
								(*memory_list) ? "," : "", atoi(pos));
						(*used_memory)++;
					}

					pos += LENGTH_PDU_DETAIL_REC;
				}
			}
		} else if (*buffer) {
			// Re-create memory_list because messages are sorted.
			*used_memory = 0;
			*memory_list = 0;

			pos = buffer;
			while (*pos) {
				sprintf(strchr(memory_list, 0), "%s%i",
						(*memory_list) ? "," : "", atoi(pos));
				(*used_memory)++;
				pos += LENGTH_PDU_DETAIL_REC;
			}
		}

		printf("Used memory is %i%s%s", *used_memory,
				(*memory_list) ? ", list: " : "", memory_list);

		if (*delete_list
				&& value_in(DEVICE.check_memory_method, 1, CM_CMGL_SIMCOM))
			printf("Will later delete messages: %s", delete_list);

		return 1;
	} else {
		put_command("AT+CPMS?\r", answer, size_answer, 1,
				"(\\+CPMS:.*OK)|(ERROR)");
		if ((start = strstr(answer, "+CPMS:"))) {
			// 3.1.5: Check if reading of messages is not supported:
			if (strstr(answer, "+CPMS: ,,,,,,,,"))
				printf("Reading of messages is not supported?");
			else {
				end = strchr(start, '\r');
				if (end) {
					*end = 0;
					getfield(start, 2, tmp, sizeof(tmp));
					if (tmp[0])
						*used_memory = atoi(tmp);
					getfield(start, 3, tmp, sizeof(tmp));
					if (tmp[0])
						*max_memory = atoi(tmp);
					printf("Used memory is %i of %i", *used_memory,
							*max_memory);
					return 1;
				}
			}
		}
	}

	// 3.1.5: If CPMS did not work but it should work:
	if (DEVICE.check_memory_method == CM_CPMS) {
		printf("Command failed.");
		return 0;
	}

	printf("Command failed, using defaults.");
	return 1;
}

/* =======================================================================
 Receive one SMS
 ======================================================================= */

int receivesms(int* quick, int only1st)
// receive one SMS or as many as the modem holds in memory
// if quick=1 then no initstring
// if only1st=1 then checks only 1st memory space
// Returns 1 if successful
// Return 0 if no SM available
// Returns -1 on error
{
	int max_memory, used_memory;

	int found;

	int sim = 0;
	char line1[1024];
	char line2[1024];

	int memories;
	char *p;
	char *p2;
	char memory_list[1024];
	char delete_list[1024];
	size_t i;

	if (terminate == 1)
		return 0;

	used_memory = 0;

	// Dual-memory handler:
	for (memories = 0; memories <= 2; memories++) {
		if (terminate == 1)
			break;

		*delete_list = 0;

		if (DEVICE.primary_memory[0] && DEVICE.secondary_memory[0]
				&& DEVICE.modem_disabled == 0) {
			char command[128];
			char answer[1024];
			char *memory;

			memory = 0;
			if (memories == 1) {
				if (only1st)
					break;
				memory = DEVICE.secondary_memory;
			} else if (memories == 2)
				memory = DEVICE.primary_memory;

			if (memory) {
				sprintf(command, "AT+CPMS=\"%s\"\r", memory);

				// 3.1beta7: initially the value was giwen as ME or "ME" without multiple memories defined.
				// If there is ME,ME,ME or "ME","ME,"ME" given, all "'s are removed while reading the setup.
				// Now, if there is a comma in the final command string, "'s are added back:
				p = command;
				while (*p && (p = strchr(p, ','))) {
					if (strlen(command) > sizeof(command) - 3)
						break;
					for (p2 = strchr(command, 0); p2 > p; p2--)
						*(p2 + 2) = *p2;
					strncpy(p, "\",\"", 3);
					p += 3;
				}

				printf("Changing memory");
				put_command(command, answer, sizeof(answer), 1,
						EXPECT_OK_ERROR);

				// 3.1.9:
				if (strstr(answer, "ERROR")) {
					printf(
							"The modem said ERROR while trying to change memory to %s",
							memory);
					return -1;
				}
			}

			if (memories == 2)
				break;
		} else if (memories > 0)
			break;

		*check_memory_buffer = 0;

		printf("[*]check how many memory spaces we really can read\n");
		// Check how many memory spaces we really can read
		if (check_memory(&used_memory, &max_memory, memory_list,
				sizeof(memory_list), delete_list, sizeof(delete_list)) == 0)
			break;

		found = 0;
		if (used_memory > 0) {
			if (max_memory == 0 && memories == 1)
				max_memory = DEVICE.secondary_memory_max;

			// 3.1.5: memory list handling if method 2 or 3 is used:
			//for (sim=DEVICE.read_memory_start; sim<=DEVICE.read_memory_start+max_memory-1; sim++)
			if (!value_in(DEVICE.check_memory_method, 6, CM_CMGD, CM_CMGL,
					CM_CMGL_DEL_LAST, CM_CMGL_CHECK, CM_CMGL_DEL_LAST_CHECK,
					CM_CMGL_SIMCOM))
				sim = DEVICE.read_memory_start;

			for (;;) {
				if (!(*delete_list))
					if (terminate == 1)
						break;

				if (value_in(DEVICE.check_memory_method, 6, CM_CMGD, CM_CMGL,
						CM_CMGL_DEL_LAST, CM_CMGL_CHECK, CM_CMGL_DEL_LAST_CHECK,
						CM_CMGL_SIMCOM)) {
					if (!(*memory_list))
						break;

					sim = atoi(memory_list);

					if ((p = strchr(memory_list, ',')))
						strcpyo(memory_list, p + 1);
					else
						*memory_list = 0;
				} else {
					if (sim > DEVICE.read_memory_start + max_memory - 1)
						break;
				}

				*line2 = 0;
				if (value_in(DEVICE.check_memory_method, 3, CM_CMGL_CHECK,
						CM_CMGL_DEL_LAST_CHECK, CM_CMGL_SIMCOM)) {
					if (*check_memory_buffer) {
						// There can be more than one space before number, "+CMGL: %i" will not work.
						p = check_memory_buffer;
						while ((p = strstr(p, "+CMGL:"))) {
							if (atoi(p + 6) == sim)
								break;
							p += 6;
						}

						if (p) {
							if ((p = strchr(p, '\r'))) {
								p++;
								if (*p == '\n')
									p++;
								if ((p2 = strchr(p, '\r'))) {
									i = p2 - p;
									if (i < sizeof(line2)) {
										strncpy(line2, p, i);
										line2[i] = 0;
										found = sim;
									}
								}
							}
						}
					}

					if (!(*line2))
						printf(
								"CMGL PDU read failed with message %i, using CMGR.",
								sim);
				}

				if (!(*line2)) {

					found = readsim(sim, line1, line2);

				}

				// 3.1.5: Should stop if there was timeout:
				if (found == -2) {

					printf("should stop if there was timeout:\n");
					*quick = 0;
					break;
				}

				if (found >= 0) {

					int _quick = 1;

					int errorcounter = 0;

					printf("calling send1sms....\n");

					char filename[200];
					char d_incoming[2000];
					int stored_concatenated = 0;
					printf("create tmp  file for message recvied\n");
					//Create a temp file for received message
					//3.1beta3: Moved to the received2file function, filename is now a return value:
					sprintf(filename, "%s/%s.XXXXXX", d_incoming, DEVICE.name);
					close(mkstemp(filename));
					int statusreport = printsms(line1, line2, filename,
							&stored_concatenated, 0);

					//send1sms("972545807087","system up.",&_quick, &errorcounter);

					*quick = 1;

					if (value_in(DEVICE.check_memory_method, 2,
							CM_CMGL_DEL_LAST, CM_CMGL_DEL_LAST_CHECK)) {
						char tmp[32];

						sprintf(tmp, "%s%u", (*delete_list) ? "," : "", found);
						if (strlen(delete_list) + strlen(tmp)
								< sizeof(delete_list))
							strcat(delete_list, tmp);
					} else if (!value_in(DEVICE.check_memory_method, 1,
							CM_CMGL_SIMCOM))
						deletesms(found);

					used_memory--;
					if (used_memory < 1)
						break; // Stop reading memory if we got everything
				}
				if (only1st)
					break;

				//if (!value_in(DEVICE.check_memory_method, 6, CM_CMGD, CM_CMGL, CM_CMGL_DEL_LAST, CM_CMGL_CHECK, CM_CMGL_DEL_LAST_CHECK, CM_CMGL_SIMCOM))
				sim++;
			}
		}

		if (*delete_list) {
			deletesms_list(delete_list);

		}
	}

	return 0;
}

// 3.1.7:
int send_startstring() {

	if (DEVICE.modem_disabled)
		return 0;

	if (DEVICE.startstring[0]) {
		char answer[500];
		int retries = 0;
		char *p;

		printf("Sending start string to the modem");

		do {
			retries++;
			put_command(DEVICE.startstring, answer, sizeof(answer), 2,
					EXPECT_OK_ERROR);
			if (strstr(answer, "ERROR"))
				if (retries < 2)
					t_sleep(1);
		} while (retries < 2 && !strstr(answer, "OK"));
		if (strstr(answer, "OK") == 0) {
			p = get_gsm_error(answer);
			printf("Modem did not accept the start string%s%s",
					(*p) ? ", " : "", p);
			// alarm_handler0(LOG_ERR, tb);
		}

		if (DEVICE.startsleeptime > 0) {
			printf("Spending sleep time after starting (%i sec)",
					DEVICE.startsleeptime);
			if (t_sleep(DEVICE.startsleeptime))
				return 1;
		}
	}

	return 0;
}

// 3.1.7:
int send_stopstring() {

	// 3.1.12:
	if (DEVICE.modem_disabled)
		return 0;

	if (DEVICE.stopstring[0]) {
		char answer[500];
		int retries = 0;
		char *p;

		if (!try_openmodem())
			printf("Cannot send stop string to the modem");
		else {
			printf("Sending stop string to the modem");

			do {
				retries++;
				put_command(DEVICE.stopstring, answer, sizeof(answer), 2,
						EXPECT_OK_ERROR);
				if (strstr(answer, "ERROR"))
					if (retries < 2)
						t_sleep(1);
			} while (retries < 2 && !strstr(answer, "OK"));
			if (strstr(answer, "OK") == 0) {
				p = get_gsm_error(answer);
				printf("Modem did not accept the stop string%s%s",
						(*p) ? ", " : "", p);
				// alarm_handler0(LOG_ERR, tb);
			}

		}
	}

	return 0;
}

// Used to select an appropriate header:
char *get_header(const char *header, char *header2) {
	if (header2 && *header2 && strcmp(header2, "-")) {
		if (*header2 == '-')
			return header2 + 1;
		return header2;
	}
	return (char *) header;
}

char *get_header_incoming(const char *header, char *header2) {
	if (!translate_incoming)
		return (char *) header;
	return get_header(header, header2);
}

unsigned char g_sms_reference_number = 0;
unsigned short g_total_expected = 0;
unsigned int g_total_recived_sms = 0;

#define SSID_TYPE    33
#define CHUNK_TYPE   32
typedef struct {
	unsigned short magic;
	unsigned char fec_hdr[8];
	unsigned short type;
	unsigned short length;

} XSSID_HDR;
void handle_ucs2_sms(void *pbuffer, size_t size) {

	printf("enter handle ucs2 sms size %d\n",size);

	if (size <= sizeof(XCHUNK_HDR) && size <= sizeof(XSSID_HDR)) {

		printf("the ucs2_sms message is  too small !");
		hexDump("UCS2 MSG", pbuffer, size);
		return;
	}

	if (size%2) {

		printf("terror he ucs2_sms message must be divded by 2 !\n");
		hexDump("UCS2 MSG", pbuffer, size);
		return;
	}

	XCHUNK_HDR *pchunkhdr = (XCHUNK_HDR*) (unsigned char*) pbuffer;

	unsigned short total_sms = htons(pchunkhdr->total_indx);
	unsigned short length = htons(pchunkhdr->size);
	unsigned char  ref_number = ((pchunkhdr->magic) & 0xff);
	unsigned short sms_indx = htons(pchunkhdr->indx);
	unsigned short type = htons(pchunkhdr->type);

	if (type != SSID_TYPE && type != CHUNK_TYPE) {
		printf(
				"the  type of ucs2_sms message   header is not uupported [type=%d]!\n",
				type);
		hexDump("UCS2 MSG", pbuffer, size);
		return;

	}

	if (type == SSID_TYPE) {

		printf("[8]parsing ssid sms \n");

		XSSID_HDR *pssid_hdr = (XSSID_HDR*) (unsigned char*) pbuffer;
		unsigned short length = htons(pssid_hdr->length);
		printf("length =%d size =%d  should be equal %d sizeof(XSSID_HDR)=%d\n",
				length, size, pssid_hdr->length, sizeof(XSSID_HDR));
		char *ssid_buffer = (char*) pbuffer + sizeof(XSSID_HDR);
		hexDump("SSID MSG", pssid_hdr, length);
		printf("SSID NAME %s\n", ssid_buffer);
		return;
	}

	if (sms_indx > total_sms) {

		printf("we recived bad header when sms_index >total_sms drop sms!!");
		hexDump("DROPPRD UCS2 MSG", pbuffer, size);
		return;
	}
	if ((g_sms_reference_number != ref_number)
			|| (g_total_expected != total_sms)) {

		printf("this is a start of a new package re create tmp files\n");
		g_total_expected = total_sms;
		g_sms_reference_number = ref_number;
		create_dump_sms_files();
		g_total_recived_sms = 0;
	}

	hexDump("PARSING UCS2 MSG", pbuffer, size);

	dump2tmpfile(g_sztmpdatafilename, (void*) pbuffer, size);
	if (size==132 && length==4){

		g_total_recived_sms+=6;

	}else if (size==126 && length==108) {
		 g_total_recived_sms++;

	}else if (size==66 && length==4 ){
		g_total_recived_sms+=3;
	}else{

		g_total_recived_sms++;

	}

	printf("we expect %d of total sms (we got %d)  reference_number %02x\n",
			total_sms, g_total_recived_sms, g_sms_reference_number);

	if (total_sms == g_total_recived_sms) {

		g_total_recived_sms = 0;
		g_sms_reference_number = 0;
		g_total_expected = 0;
		//take the tmp sms file defrag it and create final output of the package file
		if (!defrag_file(g_sztmpdatafilename, g_szencodedpackage_filename)) {

			printf("[!]defrag_file error !\n");
			delete_tmp_files();
			return;
		}

		printf("start decoding %s output %s\n", g_szencodedpackage_filename,
				g_szdecodedpackage_filename);
		if (!decode_file(g_szencodedpackage_filename,
				g_szdecodedpackage_filename)) {

			printf("fail to decode file ....\n");
			delete_tmp_files();
			return;
		}
		printf("success to decode file ....\n");


			printf("moving file to webserver <www> folder \n");
			char move_buf[0x256];

			sprintf(move_buf, "mv %s %s", g_szdecodedpackage_filename,
					"/www/final_package.bin");

			if (system(move_buf) == -1) {

				printf("error rename file %s\n", strerror(errno));

			} else {
				printf("[*]success to move package file to webserver \n");
				printf("please wait we unzip2 the package....\n");

				/*char unzip2buf[0x256];
				sprintf(unzip2buf, "bunzip2 /www/final_package.bin");
				if (system(unzip2buf)==-1){

					printf("error unzip2 the  file %s\n", strerror(errno));
				}else{

					printf("[*] success to unzip2 the package.\n");
				}*/

			}

		delete_tmp_files();
	}

}

// returns 1 if this was a status report
int printsms(char *line1, char *line2, char *filename, int *stored_concatenated,
		int incomplete) {
	int userdatalength;
	char ascii[MAXTEXT] = { };
	char sendr[100] = { };
	int with_udh = 0;
	char udh_data[SIZE_UDH_DATA] = { };
	char udh_type[SIZE_UDH_TYPE] = { };
	char smsc[31] = { };
	char name[64] = { };
	char date[9] = { };
	char Time[9] = { };
	char warning_headers[SIZE_WARNING_HEADERS] = { };
	//char status[40]={}; not used
	int alphabet = 0;
	int is_statusreport = 0;

	int do_decode_unicode_text = 0;
	int do_internal_combine = 0;
	int do_internal_combine_binary;
	int is_unsupported_pdu = 0;

	int result = 1;
	char from_toa[51] = { };
	int report;
	int replace;
	int flash;
	int p_count = 1;
	int p_number;

	if (DEVICE.decode_unicode_text == 1
			|| (DEVICE.decode_unicode_text == -1 && decode_unicode_text == 1))
		do_decode_unicode_text = 1;

	if (!incomplete)
		if (DEVICE.internal_combine == 1
				|| (DEVICE.internal_combine == -1 && internal_combine == 1))
			do_internal_combine = 1;

	do_internal_combine_binary = do_internal_combine;
	if (do_internal_combine_binary)
		if (DEVICE.internal_combine_binary == 0
				|| (DEVICE.internal_combine_binary == -1
						&& internal_combine_binary == 0))
			do_internal_combine_binary = 0;

#ifdef DEBUGMSG
	printf("!! received2file: line1=%s, line2=%s, decode_unicode_text=%i, internal_combine=%i, internal_combine_binary=%i\n",
			line1, line2, do_decode_unicode_text, do_internal_combine, do_internal_combine_binary);
#endif

	//getfield(line1,1,status, sizeof(status)); not usedg_reference_number
	getfield(line1, 2, name, sizeof(name));

	// Check if field 2 was a number instead of a name
	if (atoi(name) > 0)
		name[0] = 0; //Delete the name because it is missing

	userdatalength = splitpdu(line2, DEVICE.mode, &alphabet, sendr, date, Time,
			ascii, smsc, &with_udh, udh_data, udh_type, &is_statusreport,
			&is_unsupported_pdu, from_toa, &report, &replace, warning_headers,
			&flash, do_internal_combine_binary);
	if (alphabet == -1 && DEVICE.cs_convert == 1)
		userdatalength = gsm2iso(ascii, userdatalength, ascii, sizeof(ascii));
	else if (alphabet == 2 && do_decode_unicode_text == 1) {
		if (with_udh) {
			char *tmp;
			int m_id, p_count, p_number;

			if ((tmp = strdup(udh_data))) {
				if (get_remove_concatenation(tmp, &m_id, &p_count, &p_number)
						> 0) {
					if (p_count == 1 && p_number == 1) {
						strcpy(udh_data, tmp);
						if (!(*udh_data)) {
							with_udh = 0;
							*udh_type = 0;
						} else {
							if (explain_udh(udh_type, udh_data) < 0)
								if (strlen(udh_type) + 7 < SIZE_UDH_TYPE)
									sprintf(strchr(udh_type, 0), "%sERROR",
											(*udh_type) ? ", " : "");
						}

					}
				}
				free(tmp);
			}
		}

		// 3.1beta7, 3.0.9: decoding is always done:
		userdatalength = decode_ucs2(ascii, userdatalength);
		alphabet = 0;

		//userdatalength = iconv_ucs2utf(ascii, userdatalength, sizeof(ascii));
		printf("!! iconv_ucs2utf=%i\n", userdatalength);
		//alphabet = 4;

	}

	printf("!! userdatalength=%i\n", userdatalength);
	printf("!! name=%s\n", name);
	printf("!! sendr=%s\n", sendr);
	printf("!! date=%s\n", date);
	printf("!! Time=%s\n", Time);
	if ((alphabet == -1 && DEVICE.cs_convert == 1) || (alphabet == 0)
			|| (alphabet == 4))
		printf("!! ascii=%s\n", ascii);
	printf("!! smsc=%s\n", smsc);
	printf("!! with_udh=%i\n", with_udh);
	printf("!! udh_data=%s\n", udh_data);
	printf("!! udh_type=%s\n", udh_type);
	printf("!! is_statusreport=%i\n", is_statusreport);
	printf("!! is_unsupported_pdu=%i\n", is_unsupported_pdu);
	printf("!! from_toa=%s\n", from_toa);
	printf("!! report=%i\n", report);
	printf("!! replace=%i\n", replace);

	*stored_concatenated = 0;
	if (do_internal_combine == 1) {
		int offset = 0; // points to the part count byte.
		int m_id;
		char storage_udh[SIZE_UDH_DATA] = { };
		int a_type;

		if ((a_type = get_concatenation(udh_data, &m_id, &p_count, &p_number))
				> 0) {
			if (p_count > 1) {
				if (a_type == 1)
					sprintf(storage_udh, "05 00 03 ");
				else
					sprintf(storage_udh, "06 08 04 %02X ",
							(m_id & 0xFF00) >> 8);
				sprintf(strchr(storage_udh, 0), "%02X ", m_id & 0xFF);
				sprintf(strchr(storage_udh, 0), "%02X %02X ", p_count,
						p_number);
				offset = (a_type == 1) ? 12 : 15;
			}
		}

	} // do_internal_combine ends.

	if (p_count == 1) {
		if (!is_statusreport)
			printf("SMS received, From: %s\n", sendr);
	} else
		printf("SMS received (part %i/%i), From: %s\n", p_number, p_count,
				sendr);

	if (result) {
		if (*stored_concatenated)
			result = 0;
		else {

			char *p = 0;

			p = "";
			if (alphabet == -1) {
				if (DEVICE.cs_convert)
					p = "ISO";
				else
					p = "GSM";
			} else if (alphabet == 0)
				p = "ISO";
			else if (alphabet == 1)
				p = "binary";
			else if (alphabet == 2)
				p = "UCS2";
			else if (alphabet == 4)
				p = "UTF-8";
			else if (alphabet == 3)
				p = "reserved";

			printf("alphabet=%s\n", p);
			// 3.1.5:
			if (incoming_utf8 == 1 && alphabet <= 0)
				p = "UTF-8";

			// 3.1beta7: This header is only necessary with binary messages. With other message types
			// there is UDH-DATA header included if UDH is presented. "true" / "false" is now
			// presented as "yes" / "no" which may be translated to some other language.

			// 3.1beta7, 3.0.9: with value 2 unsupported pdu's were not stored.
			if (store_received_pdu == 3
					|| (store_received_pdu == 2
							&& (alphabet == 1 || alphabet == 2))
					|| (store_received_pdu >= 1 && is_unsupported_pdu == 1)) {
				if (incoming_pdu_store)
					printf("%s", incoming_pdu_store);
				else
					printf("PDU: alphabet%s\n", line2);
			}

			// Show the error position (first) if possible:
			if (store_received_pdu >= 1 && is_unsupported_pdu == 1) {
				char *p;
				char *p2;
				int pos;
				int len = 1;
				int i = 0;

				if ((p = strstr(ascii, "Position "))) {
					if ((pos = atoi(p + 9)) > 0) {
						if ((p = strchr(p + 9, ',')))
							if ((p2 = strchr(p, ':')) && p2 > p + 1)
								len = atoi(p + 1);
						printf("Pos: ");
						while (i++ < pos - 1)
							if (i % 10) {
								if (i % 5)
									printf(".");
								else
									printf("-");
							} else
								printf("*");

						for (i = 0; i < len; i++)
							printf("^");
						printf("~here(%i)\n", pos);
					}
				}
			}
			// --------------------------------------------

			printf("\n");

			// UTF-8 conversion if necessary:
			if (incoming_utf8 == 1 && alphabet <= 0) {
				// 3.1beta7, 3.0.9: GSM alphabet is first converted to ISO
				if (alphabet == -1 && DEVICE.cs_convert == 0) {
					userdatalength = gsm2iso(ascii, userdatalength, ascii,
							sizeof(ascii));
					alphabet = 0; // filename_preview will need this information.
				}

				printf("iso2utf8 file !!");

			} else {

				printf("parsing incoming sms alphabet=%d ...", alphabet);
				if (alphabet == 2 && with_udh == 0) {

					handle_ucs2_sms((void*) ascii, userdatalength);

				} else if (alphabet == 2 && with_udh == 1) {

					printf("we recived UC2 message with UDH data we drop it !");
					hexDump("ucs2 sms", (void*) ascii, userdatalength);

				} else if (alphabet == -1 || alphabet == 0) {
					printf("we recived ISO message with UDH data we drop it !");
					hexDump("ucs2 sms", (void*) ascii, userdatalength);

				}

				/*	char *putf8 = malloc(userdatalength * 5);
				 int utf8_len = 0;
				 int i;
				 for (i = 0; i < userdatalength / 2; i++) {
				 char utf8[8] = { 0 };

				 unsigned short ucs2 = *(unsigned short*) &ascii[i * 2];
				 int len = ucs2_to_utf8(ucs2, utf8);

				 if (len != -1) {

				 memcpy(putf8 + utf8_len, utf8, len);
				 utf8_len += len;

				 } else {
				 utf8_len = -1;
				 break;

				 }
				 }

				 free(putf8);*/
			}

		}
	}

	if (incoming_pdu_store) {
		free(incoming_pdu_store);
		incoming_pdu_store = NULL;
	}
	return result;
}

int run_rr() {

	int modem_was_open;

	modem_was_open = modem_handle >= 0;

	if (modem_was_open)
		try_openmodem();
	else
		try_closemodem(0);

	return 1;
}

void devicespooler() {
	int workless;
	int quick = 0;

	int i;
	time_t now;

	time_t last_rr;
	char *p = "";

	*smsd_debug = 0;

	if (DEVICE.outgoing && !DEVICE.incoming)
		p = " Will only send messages.";
	else if (!DEVICE.outgoing && DEVICE.incoming)
		p = " Will only receive messages.";

	else if (!DEVICE.outgoing && !DEVICE.incoming) {
		p =
				" Nothing to do with a modem: sending and receiving are both disabled!";

	}

	// 3.1beta7: This message is printed to stderr while reading setup. Now also
	// log it and use the alarmhandler. Setting is cleared. Later this kind of
	// message is only given if there is Report:yes in the message file.

	concatenated_id = getrand(255);

	//if (check_suspend(0))
	//return;

	// Open serial port or return if not successful
	if (!try_openmodem())
		return;

	if (DEVICE.sending_disabled == 1 && DEVICE.modem_disabled == 0) {
		printf("%s: Modem handler %i is in testing mode, SENDING IS DISABLED\n",
				process_title, 0);
		printf("Modem handler %i is in testing mode, SENDING IS DISABLED", 0);
	}

	if (DEVICE.modem_disabled == 1) {
		printf("%s: Modem handler %i is in testing mode, MODEM IS DISABLED\n",
				process_title, 0);

		printf("Modem handler %i is in testing mode, MODEM IS DISABLED", 0);

		DEVICE.sending_disabled = 1;
	}

	if (DEVICE.read_timeout != 5)
		printf("Using read_timeout %i seconds.", DEVICE.read_timeout);

	if (DEVICE.communication_delay > 0)
		printf(
				"Using communication_delay between new commands %i milliseconds.",
				DEVICE.communication_delay);

	printf("[*]Entering endless send/receive loop\n");

	last_rr = 0;
	// last_ic_purge = 0;

	// 3.1.12: Allocate memory for check_memory_buffer:
	if (DEVICE.incoming) {
		check_memory_buffer_size = select_check_memory_buffer_size();
		if (!(check_memory_buffer = (char *) malloc(check_memory_buffer_size))) {
			printf(
					"Did not get memory for check_memory_buffer (%i). Stopping.\n",
					check_memory_buffer_size);
			//alarm_handler0(LOG_CRIT, tb);
			return;
		}
	}

	if (send_startstring()) {

		printf("[error] send_startsting");
		return;
	}

	// 3.1.1: If a modem is used for sending only, it's first initialized.
	// 3.1.12: Check if a modem is disabled.
	if (DEVICE.outgoing && !DEVICE.incoming && !DEVICE.modem_disabled) {
		if (initialize_modem_sending("")) {
			printf("Failed to initialize modem %s. Stopping.", DEVICE.name);
			//alarm_handler0(LOG_CRIT, tb);
			return;
		} else
			printf("Waiting for messages to send...");
	}

	// Copy device value to global value:
	if (DEVICE.max_continuous_sending != -1)
		max_continuous_sending = DEVICE.max_continuous_sending;

	if (max_continuous_sending < 0)
		max_continuous_sending = 0;

	while (terminate == 0) /* endless loop */
	{
		workless = 1;
		break_workless_delay = 0;
		workless_delay = 0;

		//continuous_sent = 0;

		/*while (!terminate && DEVICE.outgoing)
		 {
		 // if (check_suspend(1))
		 // return;

		 if (DEVICE.message_count_clear > 0)
		 {
		 now = time(0);
		 if (now >= last_msgc_clear + DEVICE.message_count_clear)
		 {
		 if (message_count > 0)
		 printf( "Message limit counter cleared, it was %i.", message_count);
		 last_msgc_clear = now;
		 message_count = 0;
		 }
		 }

		 if (DEVICE.message_limit > 0)
		 if (message_count >= DEVICE.message_limit)
		 break;


		 if (!try_openmodem())
		 return;receivesms

		 i = send1sms(&quick, &errorcounter);

		 if (i > 0)
		 {
		 if (!message_count)
		 last_msgc_clear = time(0);

		 message_count++;
		 continuous_sent++;

		 if (DEVICE.message_limit > 0 &&
		 message_count == DEVICE.message_limit)
		 {
		 char msg[100];

		 printf("Message limit %i is reached.", DEVICE.message_limit);


		 // sprintf(msg, "Smsd3: %s: Message limit %i is reached.", process_title, DEVICE.message_limit);
		 //send_admin_message(&quick, &errorcounter, msg);
		 }

		 if (max_continuous_sending > 0)
		 {
		 if (time(0) >= started_sending +max_continuous_sending)
		 {
		 printf( "Max continuous sending time reached, will do other tasks and then continue.");
		 workless = 0;

		 if (continuous_sent)
		 {
		 time_t seconds;

		 seconds = time(0) - started_sending;
		 // printf( "Sent %d messages in %d sec. Average time for one message: %.1f sec.", continuous_sent, seconds, (double)seconds / continuous_sent);
		 }

		 break;
		 }
		 }
		 }
		 else
		 if (i != -2) // If there was a failed messsage, do not break.
		 break;

		 workless=0;

		 if (DEVICE.incoming == 2) // repeat only if receiving has low priority
		 break;

		 if (terminate == 1)
		 return;

		 printf("???\n");

		 }
		 */

		if (terminate == 1)
			return;

		//printf(" Receive SM [%d]....\n",DEVICE.incoming);
		if (DEVICE.incoming) {
			//if (check_suspend(1))
			//return;

			if (!try_openmodem())
				return;

			// In case of (fatal or permanent) error return value is < 0:

			int ret = -1;
			if ((ret = receivesms(&quick, 0)) > 0)
				workless = 0;

			if (ret == 0) {
				printf("no SM available %d\n", ret);
			}

			if (routed_pdu_store) {
				char *term;
				char filename[PATH_MAX];
				int stored_concatenated;

				printf("Handling saved routed messages / status reports");

				p = routed_pdu_store;
				while (*p) {
					if (!(term = strchr(p, '\n')))
						break;

					*term = 0;

					printsms("", p, filename, &stored_concatenated, 0);

					p = term + 1;
				}

				free(routed_pdu_store);
				routed_pdu_store = NULL;
			}

			if (terminate == 1)
				return;
		}

		if (DEVICE.dev_rr_interval > 0 && DEVICE.modem_disabled == 0) {
			now = time(0);
			if (now >= last_rr + DEVICE.dev_rr_interval) {
				last_rr = now;
				if (!run_rr())
					return;
			}
		}

		if (DEVICE.incoming && keep_messages) {
			printf("Messages are kept, stopping.");
			try_closemodem(0);
			kill((int) getppid(), SIGTERM);
			return;
		}

		break_suspend = 0;
		//if (check_suspend(1))
		//return;

		if (workless == 1) // wait a little bit if there was no SM to send or receive to save CPU usage
				{
			try_closemodem(0);

			// Disable quick mode if modem was workless
			quick = 0;

			workless_delay = 1;
			for (i = 0; i < delaytime; i++) {
				if (terminate == 1)
					return;
				if (break_workless_delay)
					break;

				if (DEVICE.dev_rr_interval > 0 && !DEVICE.modem_disabled) {
					now = time(0);
					if (now >= last_rr + DEVICE.dev_rr_interval) {
						last_rr = now;
						if (!run_rr())
							return;
					}
				}

				t_sleep(1);
			}
			workless_delay = 0;
		}

	}

}

void sendsignal2devices(int signum) {
	int i;

	for (i = 0; i < 1; i++)
		if (device_pids[i] > 0)
			kill(device_pids[i], signum);
}

/* =============================================receivesms==========================
 Termination handler
 ======================================================================= */

// Stores termination request when termination signal has been received
void soft_termination_handler(int signum) {

	printf("enter soft_termination_handler\n");

	(void) signum; // 3.1.7: remove warning.

	signal(SIGTERM, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	signal(SIGUSR1, SIG_IGN);
	// process_id has always the same value like device when it is larger than -1

	terminate = 1;
}

void abnormal_termination(int all) {

	printf("enter abnormal_termination\n");

	if (process_id >= 0) {

		exit(EXIT_FAILURE);
	}
}

void signal_handler(int signum) {

	printf("enter signal_handler\n");
	signal(SIGCONT, SIG_IGN);
	signal(SIGUSR2, SIG_IGN);

	if (signum == SIGCONT) {

		printf("Modem handler %i received SIGCONT, will continue %s. PID: %i.",
				process_id, (workless_delay) ? "immediately" : "without delays",
				(int) getpid());

		break_workless_delay = 1;

	} else if (signum == SIGUSR2)
		break_suspend = 1;

	signal(SIGCONT, signal_handler);
	signal(SIGUSR2, signal_handler);
}

unsigned get_file_size(const char * file_name) {
	struct stat sb;
	if (stat(file_name, &sb) != 0) {

		return 0;
	}
	return sb.st_size;
}

/* This routine reads the entire file into memory. */

unsigned char *
read_whole_file(const char * file_name, unsigned int* plength) {
	unsigned int s;
	unsigned char * contents;
	FILE * f;
	size_t bytes_read;
	int status;

	s = get_file_size(file_name);

	contents = (unsigned char*) malloc(s + 1);
	if (!contents) {
		printf("Not enough memory.\n");

		return 0;
	}

	*plength = s;
	f = fopen(file_name, "rb");
	if (!f) {

		free(contents);
		printf("Could not open '%s': %s.\n", file_name, strerror(errno));

		return 0;
	}

	bytes_read = fread(contents, sizeof(unsigned char), s, f);

	if (bytes_read != s) {

		printf("Short read of '%s': expected %d bytes "
				"but got %d: %s.\n", file_name, s, bytes_read, strerror(errno));

		free(contents);
		return 0;
	}

	status = fclose(f);
	if (status != 0) {
		printf("Error closing '%s': %s.\n", file_name, strerror(errno));

		free(contents);
		return 0;
	}
	return contents;
}
//g_sztmpdatafilename

int decode_file(char *input_file, char *output_file) {

	unsigned char * file_contents = 0;
	unsigned int file_length = -1;

	file_contents = read_whole_file(input_file, &file_length);

	if (!file_contents)
		return 0;
	printf("enter decode_file %s:length =%d\n", input_file, file_length);
	hexDump("decode file dump", file_contents, file_length);
	unsigned int total_parsed_length = 0;

	do {

		XCHUNK_HDR *pchunkhdr = (XCHUNK_HDR*) ((unsigned char*) file_contents
				+ total_parsed_length);

		unsigned char *data_ptr = ((unsigned char *) pchunkhdr
				+ sizeof(XCHUNK_HDR));
		unsigned short data_len = htons(pchunkhdr->size);
		hexDump("decode chunk dump", (void*) pchunkhdr,
				data_len + sizeof(XCHUNK_HDR));
		printf("data len %d pchunkhdr->magic=%02x\n", data_len,
				pchunkhdr->magic);

		//xmagic
		unsigned char xmagic = htons(pchunkhdr->magic)&0xFF;
		printf("fec hdr magic value=%02X\n",xmagic);

		hexDump("decode_file:hdr_fec", pchunkhdr,
							sizeof(XCHUNK_HDR));
		if (xmagic != 0) {

			if (xmagic & 0x01) {
				pchunkhdr->fec_hdr[0] ^= 0xD0;
			}
			if (xmagic & 0x02) {
				pchunkhdr->fec_hdr[2] ^= 0xD0;
			}
			if (xmagic & 0x04) {
				pchunkhdr->fec_hdr[4] ^= 0xD0;
			}
			if (xmagic & 0x08) {
				pchunkhdr->fec_hdr[6] ^= 0xD0;
			}

		}

		fec_init(8);
		uint8_t result = fec_decode(data_ptr, data_len, pchunkhdr->fec_hdr);
		if (FEC_UNCORRECTABLE_ERRORS == result) {

			hexDump("decode_file:chunk error", pchunkhdr,
					sizeof(XCHUNK_HDR) + data_len);
			printf("fec_decode cannot fix all errors offset %02X!!\n",
					total_parsed_length);
			free(file_contents);
			return 0;
		}
		if (FEC_CORRECTED_ERRORS == result) {

			printf("fec_decode chunk corrected chunk size %d \n", data_len);
		}

		printf("flashing to %s\n", output_file);
		dump2tmpfile(output_file, (unsigned char*) data_ptr, data_len);

		total_parsed_length += data_len + sizeof(XCHUNK_HDR);

		if (data_len % 2)
			break;
	} while (total_parsed_length != file_length);

	printf("leave decode_file ");

	free(file_contents);

	return 1;
}

int defrag_file(char *input_file, char *outputfile) {

	printf("enter defrag_file [%s] .....\n", input_file);

	unsigned char * file_contents = 0;
	unsigned int file_length = -1;

	unsigned short *pchunk_index_table=0;
	file_contents = read_whole_file(input_file, &file_length);

	if (!file_contents)
		return 0;

	printf("defrag_file:length =%d ptr contents %p\n", file_length,file_contents);

	hexDump("defrag content ", file_contents, file_length);

	XCHUNK_HDR *pchunkhdr = (XCHUNK_HDR*) (unsigned char*) file_contents;

	printf("start to defrag the chunks ...");
	printf("chunk size =%02X total chunks=%02X", htons(pchunkhdr->size),
			htons(pchunkhdr->total_indx));

	unsigned short total_chunks = htons(pchunkhdr->total_indx);
	unsigned short chunk_size = htons(pchunkhdr->size);
	unsigned char **ptr = NULL;


	if (chunk_size>160||total_chunks>1024){

		printf("the recived file is courrputed  !!\n");
		return 0;
	}


	int chunk_index_check=0;
	if (pchunk_index_table==0){

		pchunk_index_table=malloc(sizeof(unsigned short)*total_chunks);

		  memset(pchunk_index_table,0xFF,sizeof(unsigned short)*total_chunks);

	}



	ptr = (unsigned char**) malloc(sizeof(char *) * total_chunks);
	unsigned int total_parsed_length = 0;

	if (ptr) {

		do {

			XCHUNK_HDR *pchunkhdr =
					(XCHUNK_HDR*) ((unsigned char*) file_contents
							+ total_parsed_length);

			unsigned char *hdr_ptr = (unsigned char *) pchunkhdr;
			unsigned char *data_ptr = (unsigned char *) hdr_ptr
					+ sizeof(XCHUNK_HDR);
			unsigned short data_len = htons(pchunkhdr->size);
			unsigned short chunk_index = htons(pchunkhdr->indx);



			if (data_len % 2)
				data_len++;
			//last chunk can be size ord
			ptr[chunk_index - 1] = (unsigned char*) malloc(
					(data_len) + sizeof(XCHUNK_HDR));
			memcpy(ptr[chunk_index - 1], hdr_ptr,
					(data_len) + sizeof(XCHUNK_HDR));

			total_parsed_length += data_len + sizeof(XCHUNK_HDR);
			chunk_index++;
			//last chunk is ord because file size is ord
		} while (total_parsed_length != file_length);

		int i;
		for (i = 0; i < total_chunks; i++) {

			XCHUNK_HDR *pchunkhdr = (XCHUNK_HDR*) ptr[i];
			unsigned short data_len = htons(pchunkhdr->size);

			printf("save chunk index %d of chunk size %d\n", i, data_len);
			hexDump("defraged chunk", ptr[i], data_len + sizeof(XCHUNK_HDR));

			if (data_len % 2) {

				printf("we dump last ord sms we should flip it content");
				ptr[i][data_len + sizeof(XCHUNK_HDR) - 2] = ptr[i][data_len
						+ sizeof(XCHUNK_HDR) - 1];
			}


			unsigned short chunk_index=htons(pchunkhdr->indx);


			int j;
			for (j=0; j<total_chunks; j++){


				if (chunk_index==pchunk_index_table[j]){

					printf("the file is courruputed same chuck already in file ! ");

					chunk_index_check=1;
					break;
				}

			}

			pchunk_index_table[i]=chunk_index;

			dump2tmpfile(outputfile, (unsigned char*) ptr[i],
					data_len + sizeof(XCHUNK_HDR));
			free(ptr[i]);

		}

		free(ptr);
	}

	free(pchunk_index_table);
	free(file_contents);

	printf("leave defrag_file .\n");

	if (chunk_index_check==1) {
		remove(outputfile);
		return 0;
	}

	return 1;
}

int create_dump_sms_files() {

	if (!tmpnam_r(g_sztmpdatafilename)) {

		printf("error:failed to create tmp file for sms incoming data!\n");
		return -1;

	}
	if (!tmpnam_r(g_szencodedpackage_filename)) {

		printf("error:failed to create tmp file for sms incoming data!\n");
		return -1;
	}
	if (!tmpnam_r(g_szdecodedpackage_filename)) {

		printf("error:failed to create tmp file for sms incoming data!\n");
		return -1;

	}
	printf("create sms chunks file = %s\n", g_sztmpdatafilename);
	printf("create tmp encoded file = %s\n", g_szencodedpackage_filename);
	printf("create tmp decoded file = %s\n", g_szdecodedpackage_filename);
	return 0;
}

int main(int argc, char* argv[]) {

	terminate = 0;
	process_id = 0;
	initcfg();
	strcpy(process_title, "sms controller");

	create_dump_sms_files();

	//fclose(fopen(g_szdecodedpackage_filename,"a"));

	signal(SIGTERM, soft_termination_handler);
	signal(SIGINT, soft_termination_handler);
	signal(SIGHUP, soft_termination_handler);
	signal(SIGUSR1, soft_termination_handler);
	signal(SIGUSR2, signal_handler);
	signal(SIGCONT, signal_handler);

	// TODO: Some more signals should be ignored or handled too?

	incoming_pdu_store = NULL;
	outgoing_pdu_store = NULL;
	routed_pdu_store = NULL;

	check_memory_buffer = NULL;
	check_memory_buffer_size = 0;

	modem_handle = -1;

	char szusb[0x100];
	int i = 0;
	for (i = 0; i < 5; i++) {

		sprintf(szusb, "/dev/ttyUSB%d", i);
		strcpy(DEVICE.device, szusb);

		if (try_openmodem() == 1)
			break;
	}

	if (i == 5) {

		printf("fail to open modem connection !\n");
		exit(1);
	}

	char answer[500];
	char *p = 0;
	int retries = 0;

	printf("Checking if modem in %s is ready \n", DEVICE.device);

	do {
		retries++;
		put_command("AT\r", answer, sizeof(answer), 1, EXPECT_OK_ERROR);
		if (!strstr(answer, "OK") && !strstr(answer, "ERROR")) {
			if (terminate)
				break;

			// if Modem does not answer, try to send a PDU termination character
			put_command("\x1A\r", answer, sizeof(answer), 1, EXPECT_OK_ERROR);

			if (terminate)
				break;
		}
	} while (retries <= 5 && !strstr(answer, "OK"));
	if (!strstr(answer, "OK")) {
		p = get_gsm_error(answer);
		printf("Modem is not ready to answer commands%s%s. Stopping.\n",
				(*p) ? ", " : "", p);
		//alarm_handler0(LOG_ERR, tb);
		router_reboot();
		exit(127);
	}

	put_command("AT+CIMI\r", answer, SIZE_IDENTITY, 1, EXPECT_OK_ERROR);

	while (*answer && !isdigitc(*answer))
		strcpyo(answer, answer + 1);

	if (strstr(answer, "ERROR")) {
		put_command("AT+CGSN\r", answer, SIZE_IDENTITY, 1, EXPECT_OK_ERROR);

		while (*answer && !isdigitc(*answer))
			strcpyo(answer, answer + 1);
	}

	try_closemodem(1);

	if (!strstr(answer, "ERROR")) {
		if ((p = strstr(answer, "OK")))
			*p = 0;
		cut_ctrl(answer);
		cutspaces(answer);

		if (!strcmp(DEVICE.conf_identity, answer))
			printf("Checking identity: OK\n.");
		else {

			printf("\nChecking identity: No match, searching new settings.\n");

		}
	}

	printf("enter device spooler...\n");
	devicespooler();
	send_stopstring();
	try_closemodem(1);

	return 0;
}

