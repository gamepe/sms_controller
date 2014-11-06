

#ifndef EXTRAS_H
#define EXTRAS
#include <stdio.h>

int dump2tmpfile(char*,void *pdata,size_t size);
void router_reboot();

/* removes all ctrl chars */
char *cut_ctrl(char* message);

char *cut_crlf(char *st);

/* Is a character a space or tab? */
int is_blank(char c);

int line_is_blank(char *line);

/* Moves a file into another directory. Returns 1 if success. */
int movefile(char* filename, char* directory);


/* removes ctrl chars at the beginning and the end of the text and removes */
/* \r in the text. Returns text.*/
char *cutspaces(char *text);

/* removes all empty lines */
char *cut_emptylines(char *text);

/* Checks if the text contains only numbers. */
int is_number(char* text);







/* Parse validity value string */
int parse_validity(char *value, int defaultvalue);
int report_validity(char *buffer, int validity_period);

/* Return a random number between 1 and toprange */
int getrand(int toprange);

/* Check permissions of filename */
int is_executable(char *filename);
int check_access(char *filename);

int value_in(int value, int arg_count, ...);

// t_sleep returns 1 if terminate is set to 1 while sleeping:
int t_sleep(int seconds);

int usleep_until(unsigned long long target_time);

unsigned long long time_usec();

int make_datetime_string(char *dest, size_t dest_size, char *a_date, char *a_time, char *a_format);

void strcat_realloc(char **buffer, char *str, char *delimiter);

char *strcpyo(char *dest, const char *src);

void getfield(char* line, int field, char* result, int size);

#endif
