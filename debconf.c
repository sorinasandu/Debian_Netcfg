/*
 * Debconf communication routines.
 */

#include "debconf.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* Holds the textual return code of the last command. */
char *text;

/* Returns the last command's textual return code. */
char *debconf_ret (void) {
	return text;
}

/* 
 * Talks to debconf and returns the numeric return code.
 * Unfortunatly, you need to use a NULL - terminated list of commands.
 */
int debconf_command (const char *command, ...) {
	char buf[DEBCONF_BUFSIZE];
	va_list ap;
	char *c;
	
	fputs(command, stdout);
	va_start(ap, command);
	while ((c = va_arg(ap, char *)) != NULL) {
		fputs(" ", stdout);
		fputs(c, stdout);
	}
	va_end(ap);
	fputs("\n", stdout);
	fflush(stdout); /* make sure debconf sees it to prevent deadlock */

	fgets(buf, DEBCONF_BUFSIZE, stdin);
	buf[strlen(buf)-1] = 0;
	if (strlen(buf)) {
		strtok(buf, " \t\n");
		text=strtok(NULL, "\n");
		return atoi(buf);
	}
	else {
		/* 
		 * Nothing was entered; never really happens except during
		 * debugging.
		 */
		text=buf;
		return 0;
	}
}
