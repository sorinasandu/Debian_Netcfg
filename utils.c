#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#define EXECLOG_FILE "/var/log/debian-installer.log"
#define MAXLINE 512
#define DEBUG

int
execlog (const char *incmd)
{
  FILE *logfile, *output;
  char cmd[strlen (incmd) + 6];
  char line[MAXLINE];
  strcpy (cmd, incmd);

  if ((logfile = fopen (EXECLOG_FILE, "a")) == NULL)
    {
      perror ("execlog: fopen");
      return system (cmd);
    }

  /* FIXME: this can cause the shell command if there's redirection
     already in the passed string */
  strcat (cmd, " 2>&1");
  fprintf (logfile, "---- executing '%s' ----\n", cmd);
  output = popen (cmd, "r");
  while (fgets (line, MAXLINE, output) != NULL)
    {
      fprintf (logfile, "%s", line);
    }
  fprintf (logfile, "---- finished '%s' ----\n", cmd);
  fclose (logfile);
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
