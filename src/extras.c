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

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/wait.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>
#include "extras.h"

#include "smsd_cfg.h"



char *cut_ctrl(char* message) /* removes all ctrl chars */
{
	// 3.0.9: use dynamic buffer to avoid overflow:
	//char tmp[500];iconv_ucs2utf
	char *tmp;
	int posdest=0;
	int possource;
	int count;

	count=strlen(message);
	if ((tmp = (char *)malloc(count +1)))
	{
		for (possource=0; possource<=count; possource++)
		{
			// 3.1beta7: added unsigned test:
			if (((unsigned char)message[possource] >= (unsigned char)' ') || (message[possource]==0))
				tmp[posdest++]=message[possource];
		}
		strcpy(message,tmp);
		free(tmp);
	}
	return message;
}

char *cut_crlf(char *st)
{

	while (*st && strchr("\r\n", st[strlen(st) -1]))
		st[strlen(st) -1] = 0;

	return st;
}

int is_blank(char c)
{
	return (c==9) || (c==32);
}

int line_is_blank(char *line)
{
	int i = 0;

	while (line[i])
		if (strchr("\t \r\n", line[i]))
			i++;
		else
			break;

	return(line[i] == 0);
}


// 3.1beta7: Return values:
// 0 = OK.
// 1 = lockfile cannot be created. It exists.
// 2 = file copying failed.
// 3 = lockfile removing failed.


char *cutspaces(char *text)
{
	int count;
	int Length;
	int i;
	int omitted;

	/* count ctrl chars and spaces at the beginning */
	count=0;
	while ((text[count]!=0) && ((is_blank(text[count])) || (iscntrl((int)text[count]))) )
		count++;
	/* remove ctrl chars at the beginning and \r within the text */
	omitted=0;
	Length=strlen(text);
	for (i=0; i<=(Length-count); i++)
		if (text[i+count]=='\r')
			omitted++;
		else
			text[i-omitted]=text[i+count];
	Length=strlen(text);
	while ((Length>0) && ((is_blank(text[Length-1])) || (iscntrl((int)text[Length-1]))))
	{
		text[Length-1]=0;
		Length--;
	}

	return text;
}

char *cut_emptylines(char *text)
{
	char* posi;
	char* found;

	posi=text;
	while (posi[0] && (found=strchr(posi,'\n')))
	{
		if ((found[1]=='\n') || (found==text))
			memmove(found,found+1,strlen(found));
		else
			posi++;
	}
	return text;
}

int is_number( char*  text)
{
	int i;
	int Length;

	Length=strlen(text);
	for (i=0; i<Length; i++)
		if (((text[i]>'9') || (text[i]<'0')) && (text[i]!='-'))
			return 0;
	return 1;
}


int file_is_writable(char *filename)
{
	int result = 0;
	FILE *fp;
	struct stat statbuf;

	// 3.1.12: First check that the file exists:
	if (stat(filename, &statbuf) == 0)
	{
		if (S_ISDIR(statbuf.st_mode) == 0)
		{
			if ((fp = fopen(filename, "a")))
			{
				result = 1;
				fclose(fp);
			}
		}
	}

	return result;
}




int parse_validity(char *value, int defaultvalue)
{
	int result = defaultvalue;
	char buffer[100];
	int i;
	char tmp[100];
	int got_numbers = 0;
	int got_letters = 0;
	int idx;
	char *p;

	if (value && *value)
	{
		// n min, hour, day, week, month, year
		// 3.0.9: if only keyword is given, insert number 1.
		// Fixed number without keyword handling.
		// Convert to lowercase so upcase is also accepted.

		*buffer = 0;
		snprintf(tmp, sizeof(tmp), "%s", value);
		cutspaces(tmp);
		for (idx = 0; tmp[idx]; idx++)
		{
			tmp[idx] = tolower((int)tmp[idx]);
			if (tmp[idx] == '\t')
				tmp[idx] = ' ';
			if (isdigitc(tmp[idx]))
				got_numbers = 1;
			else
				got_letters = 1;
		}

		if (got_numbers && !got_letters)
		{
			i = atoi(tmp);
			if (i >= 0 && i <= 255)
				result = i;
			return result;
		}

		if ((p = strchr(tmp, ' ')))
			*p = 0;

		if (strstr("min hour day week month year", tmp))
			sprintf(buffer, "1 %.*s", (int)sizeof(buffer) -3, tmp);
		else
			sprintf(buffer, "%.*s", (int)sizeof(buffer) -1, value);

		while ((i = atoi(buffer)) > 0)
		{
			// 0 ... 143     (value + 1) * 5 minutes (i.e. 5 minutes intervals up to 12 hours)
			if (strstr(buffer, "min"))
			{
				if (i <= 720)
				{
					result = (i < 5)? 0 : i /5 -1;
					break;
				}
				sprintf(buffer, "%i hour", i /= 60);
			}

			// 144 ... 167   12 hours + ((value - 143) * 30 minutes) (i.e. 30 min intervals up to 24 hours)
			if (strstr(buffer, "hour"))
			{
				if (i <= 12)
				{
					sprintf(buffer, "%i min", i *60);
					continue;
				}
				if (i <= 24)
				{
					result = (i -12) *2 +143;
					break;
				}
				sprintf(buffer, "%i day", i /= 24);
			}

			// 168 ... 196   (value - 166) * 1 day (i.e. 1 day intervals up to 30 days)
			if (strstr(buffer, "day"))
			{
				if (i < 2)
				{
					sprintf(buffer, "24 hour");
					continue;
				}
				if (i <= 34)
				{
					result = (i <= 30)? i +166 : 30 +166;
					break;
				}
				sprintf(buffer, "%i week", i /= 7);
			}

			// 197 ... 255   (value - 192) * 1 week (i.e. 1 week intervals up to 63 weeks)
			if (strstr(buffer, "week"))
			{
				if (i < 5)
				{
					sprintf(buffer, "%i day", i *7);
					continue;
				}
				result = (i <= 63)? i +192 : 255;
				break;
			}

			if (strstr(buffer, "month"))
			{
				sprintf(buffer, "%i day", (i == 12)? 365 : i *30);
				continue;
			}

			if (strstr(buffer, "year"))
			{
				if (i == 1)
				{
					sprintf(buffer, "52 week");
					continue;
				}
				result = 255;
			}

			break;
		}
	}

	return result;
}

// 0=invalid, 1=valid
int report_validity(char *buffer, int validity_period)
{
	int result = 0;
	int n;
	char *p;

	if (validity_period < 0 || validity_period > 255)
		sprintf(buffer, "invalid (%i)", validity_period);
	else
	{
		if (validity_period <= 143)
		{
			// 0 ... 143    (value + 1) * 5 minutes (i.e. 5 minutes intervals up to 12 hours)
			n = (validity_period +1) *5;
			p = "min";
		}
		else if (validity_period <= 167)
		{
			// 144 ... 167  12 hours + ((value - 143) * 30 minutes) (i.e. 30 min intervals up to 24 hours)
			n =  12 +(validity_period -143) /2;
			p = "hour";
		}
		else if (validity_period <= 196)
		{
			// 168 ... 196  (value - 166) * 1 day (i.e. 1 day intervals up to 30 days)
			n = validity_period -166;
			p = "day";
		}
		else
		{
			// 197 ... 255  (value - 192) * 1 week (i.e. 1 week intervals up to 63 weeks)
			n = validity_period -192;
			p = "week";
		}

		sprintf(buffer, "%i %s%s (%i)", n, p, (n > 1)? "s" : "", validity_period);
		result = 1;
	}

	return result;
}

int getrand(int toprange)
{
	srand((int)(time(NULL) * getpid()));
	return (rand() % toprange) +1;
}

int is_executable(char *filename)
{
	// access() migth do this easier, but in Gygwin it returns 0 even when requested permissions are NOT granted.
	int result = 0;
	struct stat statbuf;
	mode_t mode;
	int n, i;
	gid_t *g;

	if (stat(filename, &statbuf) >= 0)
	{
		mode = statbuf.st_mode & 0755;

		if (getuid())
		{
			if (statbuf.st_uid != getuid())
			{
				if ((n = getgroups(0, NULL)) > 0)
				{
					if ((g = (gid_t *)malloc(n * sizeof(gid_t))))
					{
						if ((n = getgroups(n, g)) > 0)
						{
							for (i = 0; (i < n) & (!result); i++)
								if (g[i] == statbuf.st_gid)
									result = 1;
						}
						free(g);
					}
				}

				if (result)
				{
					if ((mode & 050) != 050)
						result = 0;
				}
				else if ((mode & 05) == 05)
					result = 1;
			}
			else if ((mode & 0500) == 0500)
				result = 1;
		}
		else if ((mode & 0100) || (mode & 010) || (mode & 01))
			result = 1;
	}

	return result;
}

int check_access(char *filename)
{
	// access() migth do this easier, but in Gygwin it returns 0 even when requested permissions are NOT granted.
	int result = 0;
	struct stat statbuf;
	mode_t mode;
	int n, i;
	gid_t *g;

	if (stat(filename, &statbuf) >= 0)
	{
		mode = statbuf.st_mode; // & 0777;

		if (getuid())
		{
			if (statbuf.st_uid != getuid())
			{
				if ((n = getgroups(0, NULL)) > 0)
				{
					if ((g = (gid_t *)malloc(n * sizeof(gid_t))))
					{
						if ((n = getgroups(n, g)) > 0)
						{
							for (i = 0; (i < n) & (!result); i++)
								if (g[i] == statbuf.st_gid)
									result = 1;
						}
						free(g);
					}
				}

				if (result)
				{
					if ((mode & 060) != 060)
						result = 0;
				}
				else if ((mode & 06) == 06)
					result = 1;
			}
			else if ((mode & 0600) == 0600)
				result = 1;
		}
		else if ((mode & 0200) || (mode & 020) || (mode & 02))
			result = 1;
	}

	return result;
}

int value_in(int value, int arg_count, ...)
{
	int result = 0;
	va_list ap;

	va_start(ap, arg_count);
	for (; arg_count > 0; arg_count--)
		if (value == va_arg(ap, int))
			result = 1;

	va_end(ap);

	return result;
}

int t_sleep(int seconds)
{
	// 3.1.12: When a signal handler is installed, receiving of any singal causes
	// that functions sleep() and usleep() will return immediately.
	//int i;
	time_t t;

	t = time(0);
	//for (i = 0; i < seconds; i++)
	while (time(0) - t < seconds)
	{
		if (terminate)
			return 1;

		sleep(1);
	}

	return 0;
}

int usleep_until(unsigned long long target_time)
{
	struct timeval tv;
	struct timezone tz;
	unsigned long long now;

	do
	{
		gettimeofday(&tv, &tz);
		now = (unsigned long long)tv.tv_sec *1000000 +tv.tv_usec;

		if (terminate == 1)
			return 1;

		if (now < target_time)
			usleep(100);
	}
	while (now < target_time);

	return 0;
}

unsigned long long time_usec()
{
	struct timeval tv;
	struct timezone tz;


	gettimeofday(&tv, &tz);


	return (unsigned long long)tv.tv_sec *1000000 +tv.tv_usec;
}

int make_datetime_string(char *dest, size_t dest_size, char *a_date, char *a_time, char *a_format)
{
	int result = 0;
	time_t rawtime;
	struct tm *timeinfo;


	if (!a_date && !a_time)
	{
		struct timeval tv;
		struct timezone tz;
		char *p;
		char buffer[7];

		gettimeofday(&tv, &tz);
		rawtime = tv.tv_sec;
		timeinfo = localtime(&rawtime);
		result = strftime(dest, dest_size, (a_format)? a_format : datetime_format, timeinfo);

		if ((p = strstr(dest, "timeus")))
		{
			snprintf(buffer, sizeof(buffer), "%06d", (int)tv.tv_usec);
			strncpy(p, buffer, strlen(buffer));
		}
		else if ((p = strstr(dest, "timems")))
		{
			snprintf(buffer, sizeof(buffer), "%03d", (int)tv.tv_usec / 1000);
			strncpy(p, buffer, strlen(buffer));
			memmove(p + 3, p + 6, strlen(p + 6) + 1);
		}

		return result;
	}

	if (a_date && strlen(a_date) >= 8 && a_time && strlen(a_time) >= 8)
	{
		time(&rawtime);

		timeinfo = localtime(&rawtime);
		timeinfo->tm_year = atoi(a_date) + 100;
		timeinfo->tm_mon = atoi(a_date + 3) - 1;
		timeinfo->tm_mday = atoi(a_date + 6);
		timeinfo->tm_hour = atoi(a_time);
		timeinfo->tm_min = atoi(a_time + 3);
		timeinfo->tm_sec = atoi(a_time + 6);
		// ?? mktime(timeinfo);
		result = strftime(dest, dest_size, (a_format)? a_format : datetime_format, timeinfo);
	}

	return result;
}

void strcat_realloc(char **buffer, char *str, char *delimiter)
{
	int delimiter_length = 0;

	if (delimiter)
		delimiter_length = strlen(delimiter);

	if (*buffer == 0)
	{
		if ((*buffer = (char *) malloc(strlen(str) + delimiter_length + 1)))
			**buffer = 0;
	}
	else
		*buffer = (char *) realloc((void *) *buffer, strlen(*buffer) + strlen(str) + delimiter_length + 1);

	if (*buffer)
		sprintf(strchr(*buffer, 0), "%s%s", str, (delimiter) ? delimiter : "");
}

char *strcpyo(char *dest, const char *src)
{
	size_t i;

	for (i = 0; src[i] != '\0'; i++)
		dest[i] = src[i];

	dest[i] = '\0';

	return dest;
}

void getfield(char* line, int field, char* result, int size)
{
	char* start;
	char* end;
	int i;
	int length;

#ifdef DEBUGMSG
	printf("!! getfield(line=%s, field=%i, ...)\n",line,field);
#endif
	if (size < 1)
		return;

	*result=0;
	start=strstr(line,":");
	if (start==0)
		return;
	for (i=1; i<field; i++)
	{
		start=strchr(start+1,',');
		if (start==0)
			return;
	}
	start++;
	while (start[0]=='\"' || start[0]==' ')
		start++;
	if (start[0]==0)
		return;
	end=strstr(start,",");
	if (end==0)
		end=start+strlen(start)-1;
	while ((end[0]=='\"' || end[0]=='\"' || end[0]==',') && (end>=start))
		end--;
	length=end-start+1;
	if (length >= size)
		return;
	strncpy(result,start,length);
	result[length]=0;
#ifdef DEBUGMSG
	printf("!! result=%s\n",result);
#endif
}
