/*
 * file: util.c
 * These functions were taken mostly untouched from dbootstrap
 *
 * */

#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdio.h>

#include "util.h"

int
is_interface_up (char *inter)
{
  struct ifreq ifr;
  int sfd = -1, ret = -1;

  if ((sfd = socket (AF_INET, SOCK_DGRAM, 0)) == -1)
    goto int_up_done;

  strncpy (ifr.ifr_name, inter, sizeof (ifr.ifr_name));

  if (ioctl (sfd, SIOCGIFFLAGS, &ifr) < 0)
    goto int_up_done;

  ret = (ifr.ifr_flags & IFF_UP) ? 1 : 0;

int_up_done:
  if (sfd != -1)
    close (sfd);
  return ret;
}

static FILE *ifs = NULL;
static char ibuf[512];

void
getif_start (void)
{
  if (ifs != NULL)
    {
      fclose (ifs);
      ifs = NULL;
    }
  if ((ifs = fopen ("/proc/net/dev", "r")) != NULL)
    {
      fgets (ibuf, sizeof (ibuf), ifs);	/* eat header */
      fgets (ibuf, sizeof (ibuf), ifs);	/* ditto */
    }
  return;
}

void
getif_end (void)
{
  if (ifs != NULL)
    {
      fclose (ifs);
      ifs = NULL;
    }
  return;
}

char *
getif (int all)
{
  char rbuf[512];
  if (ifs == NULL)
    return NULL;

  if (fgets (rbuf, sizeof (rbuf), ifs) != NULL)
    {
      get_name (ibuf, rbuf);
      if (!strcmp (ibuf, "lo"))	/* ignore the loopback */
	return getif (all);	/* seriously doubt there is an infinite number of lo devices */
      if (all || is_interface_up (ibuf) == 1)
	return ibuf;
    }
  return NULL;
}

void
get_name (char *name, char *p)
{
  while (isspace (*p))
    p++;
  while (*p)
    {
      if (isspace (*p))
	break;
      if (*p == ':')
	{			/* could be an alias */
	  char *dot = p, *dotname = name;
	  *name++ = *p++;
	  while (isdigit (*p))
	    *name++ = *p++;
	  if (*p != ':')
	    {			/* it wasn't, backup */
	      p = dot;
	      name = dotname;
	    }
	  if (*p == '\0')
	    return;
	  p++;
	  break;
	}
      *name++ = *p++;
    }
  *name++ = '\0';
  return;
}



/*
 * Get a description for an interface (i.e. "Ethernet" for ethX).
 */
char *
get_ifdsc (const char *ifp)
{
  int i;
  struct if_alist_struct
  {
    char *interface;
    char *description;
  }
  interface_alist[] =
  {
    /*
     * A _("string") is an element of a char *[], and the linker will
     * know where to find the elements.  This works with `pointerize';
     * perhaps that's different with `gettext'... though if it expands
     * to a function call, this initializer should still work fine.
     * Also see the 1.66 version of choose_cdrom(), which uses the
     * similar technique.  If it works there, it will work here.
     */
    {
    "eth", "Ethernet or Fast Ethernet"}
    ,
    {
    "pcmcia", "PC-Card (PCMCIA) Ethernet or Token Ring"}
    ,
    {
    "tr", "Token Ring"}
    ,
    {
    "arc", "Arcnet"}
    ,
    {
    "slip", "Serial-line IP"}
    ,
    {
    "plip", "Parallel-line IP"}
    ,
    {
    NULL, NULL}
  };

  for (i = 0; interface_alist[i].interface != NULL; i++)
    if (!strncmp (ifp, interface_alist[i].interface,
		  strlen (interface_alist[i].interface)))
      return interface_alist[i].description;
  return NULL;
}
