/* 
   netcfg-dhcp.c - Configure a network via dhcp for the debian-installer
   Author - David Kimdon

   Copyright (C) 2000-2002  David Kimdon <dwhedon@debian.org>
   
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

#include <ctype.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cdebconf/debconfclient.h>
#include <debian-installer.h>
#include "netcfg.h"

static char *interface = NULL;
static char *hostname = NULL;
static char *domain = NULL;
static u_int32_t ipaddress = 0;
static u_int32_t nameserver_array[4] = { 0 };
static struct debconfclient *client;
enum
{
  PUMP,
  DHCLIENT,
  UDHCPC
}
dhcp_client_choices;

int dhcp_client = PUMP;

static char *dhcp_hostname = NULL;

char *
debconf_input (char *priority, char *template)
{
  client->command (client, "fset", template, "seen", "false", NULL);
  client->command (client, "input", priority, template, NULL);
  client->command (client, "go", NULL);
  client->command (client, "get", template, NULL);
  return client->value;
}

static void
netcfg_get_dhcp ()
{
  if (dhcp_hostname)
  {
    free (dhcp_hostname);
    dhcp_hostname = NULL;
  }

  client->command (client, "input", "high", "netcfg/dhcp_hostname", NULL);
  client->command (client, "go", NULL);
  client->command (client, "get", "netcfg/dhcp_hostname", NULL);

  if (client->value)
    dhcp_hostname = strdup (client->value);

  client->command (client, "subst", "netcfg/confirm_dhcp",
		   "dhcp_hostname",
		   (dhcp_hostname ? dhcp_hostname : "<none>"), NULL);
}


static void
netcfg_write_dhcp ()
{

  FILE *fp;

  if ((fp = file_open (INTERFACES_FILE)))
    {
      fprintf (fp,
	       "\n# This entry was created during the Debian installation\n");
      fprintf (fp, "iface %s inet dhcp\n", interface);
      if (dhcp_hostname)
	fprintf (fp, "\thostname %s\n", dhcp_hostname);
      fclose (fp);
    }
}


static void
netcfg_activate_dhcp ()
{
  char buf[128];
  di_execlog ("/sbin/ifconfig lo 127.0.0.1");

  switch (dhcp_client)
    {
      case PUMP:
	snprintf (buf, sizeof (buf), "/sbin/pump -i %s", interface);
	if (dhcp_hostname)
	  di_snprintfcat (buf, sizeof (buf), " -h %s", dhcp_hostname);
	break;

      case DHCLIENT:
	snprintf (buf, sizeof (buf), "/sbin/dhclient %s", interface);
	break;

      case UDHCPC:
	snprintf (buf, sizeof (buf), "/sbin/udhcpc -i %s -n", interface);
	if (dhcp_hostname)
	  di_snprintfcat (buf, sizeof (buf), " -H %s", dhcp_hostname);
	break;
    }

  if (di_execlog (buf))
    netcfg_die (client);
}

int
main (int argc, char *argv[])
{
  char *ptr;
  char *nameservers = NULL;
  int finished = 0;
  struct stat buf;
  client = debconfclient_new ();
  client->command (client, "title", "DHCP Network Configuration", NULL);


  if (stat ("/sbin/dhclient", &buf) == 0)
    dhcp_client = DHCLIENT;
  else if (stat ("/sbin/pump", &buf) == 0)
    dhcp_client = PUMP;
  else if (stat ("/sbin/udhcpc", &buf) == 0)
    dhcp_client = UDHCPC;
  else
    {
      client->command (client, "input", "critical", "netcfg/no_dhcp_client",
		       NULL);
      client->command (client, "go", NULL);
      exit (1);
    }


  do
    {
      netcfg_get_common (client, &interface, &hostname, &domain,
			 &nameservers);

      client->command (client, "subst", "netcfg/confirm_dhcp", "interface",
		       interface, NULL);

      client->command (client, "subst", "netcfg/confirm_dhcp", "hostname",
		       hostname, NULL);

      client->command (client, "subst", "netcfg/confirm_dhcp", "domain",
		       (domain ? domain : "<none>"), NULL);

      netcfg_nameservers_to_array (nameservers, nameserver_array);

      client->command (client, "subst", "netcfg/confirm_dhcp",
		       "nameservers", (nameservers ? nameservers : "<none>"),
		       NULL);
      netcfg_get_dhcp ();


      ptr = debconf_input ("medium", "netcfg/confirm_dhcp");

      if (strstr (ptr, "true"))
	finished = 1;
    }
  while (!finished);

  netcfg_write_dhcp ();
  netcfg_write_common (ipaddress, domain, hostname, nameserver_array);

  debconf_input ("medium", "netcfg/do_dhcp");
  netcfg_activate_dhcp ();

  return 0;
}
