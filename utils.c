/* 
   utils.c - common utilities for debian-installer
   Author - David Whedon

   Copyright (C) 2000-2001  David Whedon <dwhedon@debian.org>
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
   
*/


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>


#define MAXLINE 512

int
execlog (const char *incmd)
{
  FILE *output;
  char cmd[strlen (incmd) + 6];
  char line[MAXLINE];
  strcpy (cmd, incmd);

  openlog ("installer", LOG_PID | LOG_PERROR, LOG_USER);
  syslog (LOG_DEBUG, "running cmd '%s'", cmd);

  /* FIXME: this can cause the shell command if there's redirection
     already in the passed string */
  strcat (cmd, " 2>&1");
  output = popen (cmd, "r");
  while (fgets (line, MAXLINE, output) != NULL)
    {
      syslog (LOG_DEBUG, line);
    }
  closelog ();
  /* FIXME we aren't getting the return value from the actual command
     executed, not sure how to do that cleanly */
  return (pclose (output));
}


int
check_dir (const char *dirname)
{
  struct stat check;
  if (stat (dirname, &check) == -1)
    return -1;
  else
    return S_ISDIR (check.st_mode);
}
