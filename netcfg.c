/* 
   netcfg.c - Configure the network for the debian-installer
   Author - David Whedon
 

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
#include <debconfclient.h>
#include "utils.h"

#define ETC_DIR "/etc"
#define NETWORK_DIR "/etc/network"
#define DHCPCD_DIR "/etc/dhcpc"
#define INTERFACES_FILE "/etc/network/interfaces"
#define HOSTS_FILE      "/etc/hosts"
#define NETWORKS_FILE   "/etc/networks"
#define RESOLV_FILE     "/etc/resolv.conf"
#define DHCPCD_FILE     "/etc/dhcpc/config"

static char *interface = NULL;
static char *hostname = NULL;
static char *domain = NULL;
static u_int32_t ipaddress = 0;
static u_int32_t nameservers[4] = { 0 };
static struct debconfclient *client;


char *
debconf_input (char *priority, char *template)
{
  client->command (client, "fset", template, "seen", "false", NULL);
  client->command (client, "input", priority, template, NULL);
  client->command (client, "go", NULL);
  client->command (client, "get", template, NULL);
  return client->value;
}


static int
netcfg_mkdir (char *path)
{
  if (check_dir (path) == -1)
    if (!mkdir (path, 0700))
      {
	perror ("mkdir");
	return -1;
      }
  return 0;
}


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


static void
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



static FILE *ifs = NULL;
static char ibuf[512];

static void
getif_start ()
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


static char *
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


static void
getif_end ()
{
  if (ifs != NULL)
    {
      fclose (ifs);
      ifs = NULL;
    }
  return;
}


static char *
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
  return "unknown interface";
}


static FILE *
file_open (char *path)
{
  FILE *fp;
  if ((fp = fopen (path, "w")))
    return fp;
  else
    {
      fprintf (stderr, "%s\n", path);
      perror ("fopen");
      return NULL;
    }
}


static void
dot2num (u_int32_t * num, char *dot)
{
  char *p = dot - 1;
  char *e;
  int ix;
  unsigned long val;

  if (!dot)
    goto error;

  *num = 0;
  for (ix = 0; ix < 4; ix++)
    {
      *num <<= 8;
      p++;
      val = strtoul (p, &e, 10);
      if (e == p)
	val = 0;
      else if (val > 255)
	goto error;
      *num += val;
      /*printf("%#8x, %#2x\n", *num, val); */
      if (ix < 3 && *e != '.')
	goto error;
      p = e;
    }

  return;

error:
  *num = 0;
}


static char num2dot_buf[16];

static char *
num2dot (u_int32_t num)
{
  int byte[4];
  int ix;
  char *dot = num2dot_buf;

  for (ix = 3; ix >= 0; ix--)
    {
      byte[ix] = num & 0xff;
      num >>= 8;
    }
  sprintf (dot, "%d.%d.%d.%d", byte[0], byte[1], byte[2], byte[3]);

  return dot;
}



static void
netcfg_die ()
{
  client->command (client, "input", "high", "netcfg/error", NULL);
  client->command (client, "go", NULL);
  exit (1);
}


static void
netcfg_get_interface ()
{
  char *inter;
  int len;
  int newchars;
  char *ptr;
  int num_interfaces = 0;

  if (interface)
    {
      free (interface);
      interface = NULL;
    }

  if (!(ptr = malloc (128)))
    netcfg_die ();
  len = 128;
  *ptr = '\0';

  getif_start ();
  while ((inter = getif (1)) != NULL)
    {
      newchars = strlen (inter) + strlen (get_ifdsc (inter)) + 5;
      if (len < (strlen (ptr) + newchars))
	{
	  if (!(ptr = realloc (ptr, len + newchars + 128)))
	    goto error;
	  len += newchars + 128;
	}

      snprintf (ptr + strlen (ptr), len - strlen (ptr), "%s: %s, ", inter,
		get_ifdsc (inter));
      num_interfaces++;
    }
  getif_end ();

  if (num_interfaces == 0)
    {
      client->command (client, "input", "high", "netcfg/no_interfaces", NULL);
      client->command (client, "go", NULL);
      free (ptr);
      exit (1);
    }
  else if (num_interfaces > 1)
    {
      client->command (client, "subst", "netcfg/choose_interface",
		       "ifchoices", ptr, NULL);
      free (ptr);
      inter = debconf_input ("high", "netcfg/choose_interface");

      if (!inter)
	netcfg_die ();
    }

  /* grab just the interface name, not the description too */
  interface = inter;
  ptr = strchr (inter, ':');
  if (ptr == NULL)
    goto error;
  *ptr = '\0';

  interface = strdup (interface);
  client->command (client, "subst", "netcfg/confirm_static", "interface",
		   interface, NULL);

  return;

error:
  if (ptr)
    free (ptr);

  netcfg_die ();
}


static void
netcfg_get_common ()
{
  char *ptr, *ns;


  netcfg_get_interface ();

  if (hostname)
    {
      free (hostname);
      hostname = NULL;
    }

  hostname = strdup (debconf_input ("medium", "netcfg/get_hostname"));

  if (domain)
    {
      free (domain);
      domain = NULL;
    }

  if ((ptr = debconf_input ("medium", "netcfg/get_domain")))
    domain = strdup (ptr);

  ptr = debconf_input ("medium", "netcfg/get_nameservers");

  if (ptr)
    {
      char *save;
      save = ptr = strdup (ptr);
#ifdef DHCP
      client->command (client, "subst", "netcfg/confirm_dhcp",
		       "nameservers", ptr, NULL);
#else
      client->command (client, "subst", "netcfg/confirm_static",
		       "nameservers", ptr, NULL);
#endif
      ns = strtok_r (ptr, " ", &ptr);
      dot2num (&nameservers[0], ns);

      ns = strtok_r (NULL, " ", &ptr);
      dot2num (&nameservers[1], ns);

      ns = strtok_r (NULL, " ", &ptr);
      dot2num (&nameservers[2], ns);

      free (save);
    }
  else
    nameservers[0] = 0;


}

void
netcfg_write_common ()
{
  FILE *fp;


  netcfg_mkdir (ETC_DIR);
  netcfg_mkdir (NETWORK_DIR);


  if ((fp = file_open (HOSTS_FILE)))
    {
      fprintf (fp, "127.0.0.1\tlocalhost\n");
      if (ipaddress)
	{
	  if (domain)
	    fprintf (fp, "%s\t%s.%s\t%s\n", num2dot (ipaddress),
		     hostname, domain, hostname);
	  else
	    fprintf (fp, "%s\t%s\n", num2dot (ipaddress), hostname);
	}

      fclose (fp);
    }

  if ((fp = file_open (RESOLV_FILE)))
    {
      int i = 0;
      if (domain)
	fprintf (fp, "search %s\n", domain);

      while (nameservers[i])
	fprintf (fp, "nameserver %s\n", num2dot (nameservers[i++]));

      fclose (fp);
    }
}


#ifdef DHCP

static char *dhcp_hostname = NULL;


static void
netcfg_get_dhcp ()
{
  if (dhcp_hostname)
    free (dhcp_hostname);

  client->command (client, "input", "high", "netcfg/dhcp_hostname", NULL);
  client->command (client, "go", NULL);
  client->command (client, "get", "netcfg/dhcp_hostname", NULL);

  if (client->value)
    dhcp_hostname = strdup (client->value);
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
    }

  netcfg_mkdir (DHCPCD_DIR);
  if ((fp = file_open (DHCPCD_FILE)))
    {
      fprintf (fp,
	       "\n# dhcpcd configuration: created during the Debian installation\n");
      fprintf (fp, "IFACE=%s\n", interface);
      if (dhcp_hostname)
	fprintf (fp, "OPTIONS='-h %s'\n", dhcp_hostname);
    }
}


static void
netcfg_activate_dhcp ()
{
  char buf[128];
  char *ptr;
  execlog ("/sbin/ifconfig lo 127.0.0.1");

  ptr = buf;
  ptr += snprintf (buf, sizeof (buf), "/sbin/dhcpcd-2.2.x");
  if (dhcp_hostname)
    ptr +=
      snprintf (ptr, sizeof (buf) - (ptr - buf), " -h %s", dhcp_hostname);

  ptr += snprintf (ptr, sizeof (buf) - (ptr - buf), " %s", interface);

  if (execlog (buf))
    netcfg_die ();
}

int
main (int argc, char *argv[])
{
  char *ptr;
  int finished = 1;
  client = debconfclient_new ();
  client->command (client, "title", "DHCP Network Configuration", NULL);

  do
    {
      netcfg_get_common ();
      netcfg_get_dhcp ();
      client->command (client, "subst", "netcfg/confirm_dhcp", "interface",
		       interface, NULL);
      client->command (client, "subst", "netcfg/confirm_dhcp", "hostname",
		       hostname, NULL);
      client->command (client, "subst", "netcfg/confirm_dhcp", "domain",
		       (domain ? domain : "<none>"), NULL);
      client->command (client, "subst", "netcfg/confirm_dhcp",
		       "dhcp_hostname",
		       (dhcp_hostname ? dhcp_hostname : "<none>"), NULL);
      ptr = debconf_input ("medium", "netcfg/confirm_dhcp");

      if (strstr (ptr, "true"))
	finished = 1;
    }
  while (!finished);

  netcfg_write_dhcp ();
  netcfg_write_common ();

  debconf_input ("medium", "netcfg/do_dhcp");
  netcfg_activate_dhcp ();

  return 0;
}



#endif /* DHCP */

#ifdef STATIC

static u_int32_t network = 0;
static u_int32_t broadcast = 0;
static u_int32_t netmask = 0;
static u_int32_t gateway = 0;
static struct debconfclient *client;


static void
netcfg_get_static ()
{
  char *ptr;

  ipaddress = network = broadcast = netmask = gateway = 0;

  ptr = debconf_input ("critical", "netcfg/get_ipaddress");
  dot2num (&ipaddress, ptr);

  client->command (client, "subst", "netcfg/confirm_static",
		   "ipaddress",
		   (ipaddress ? num2dot (ipaddress) : "<none>"), NULL);

  ptr = debconf_input ("critical", "netcfg/get_netmask");
  dot2num (&netmask, ptr);
  client->command (client, "subst", "netcfg/confirm_static",
		   "netmask", (netmask ? num2dot (netmask) : "<none>"), NULL);

  network = ipaddress & netmask;

  ptr = debconf_input ("critical", "netcfg/get_gateway");
  dot2num (&gateway, ptr);

  client->command (client, "subst", "netcfg/confirm_static",
		   "gateway", (gateway ? num2dot (gateway) : "<none>"), NULL);

  if (gateway && ((gateway & netmask) != network))
    {
      client->command (client, "input", "high",
		       "netcfg/gateway_unreachable", NULL);
      client->command (client, "go", NULL);
    }

  broadcast = (network | ~netmask);


}


static int
netcfg_write_static ()
{
  FILE *fp;

  if ((fp = file_open (NETWORKS_FILE)))
    {
      fprintf (fp, "localnet %s\n", num2dot (network));
      fclose (fp);
    }
  else
    goto error;

  if ((fp = file_open (INTERFACES_FILE)))
    {
      fprintf (fp,
	       "\n# This entry was created during the Debian installation\n");
      fprintf (fp, "# (network, broadcast and gateway are optional)\n");
      fprintf (fp, "iface %s inet static\n", interface);
      fprintf (fp, "\taddress %s\n", num2dot (ipaddress));
      fprintf (fp, "\tnetmask %s\n", num2dot (netmask));
      fprintf (fp, "\tnetwork %s\n", num2dot (network));
      fprintf (fp, "\tbroadcast %s\n", num2dot (broadcast));
      fprintf (fp, "\tgateway %s\n", num2dot (gateway));
      fclose (fp);
    }
  else
    goto error;

  return 0;
error:
  return -1;
}

static int
netcfg_activate_static ()
{
  int rv;
  char *ptr;
  char buf[128];
  execlog ("/sbin/ifconfig lo 127.0.0.1");

  ptr = buf;
  ptr +=
    snprintf (buf, sizeof (buf), "/sbin/ifconfig %s %s", interface,
	      num2dot (ipaddress));
  ptr +=
    snprintf (ptr, sizeof (buf) - (ptr - buf), " netmask %s",
	      num2dot (netmask));
  ptr +=
    snprintf (ptr, sizeof (buf) - (ptr - buf), " broadcast %s",
	      num2dot (broadcast));

  rv = execlog (buf);
  if (rv != 0)
    {
      client->command (client, "input", "critical", "netcfg/error_cfg", NULL);
      client->command (client, "go", NULL);
    }
  return 0;
}

int
main (int argc, char *argv[])
{
  int finished = 0;
  char *ptr;
  client = debconfclient_new ();

  client->command (client, "title", "Static Network Configuration", NULL);


  do
    {
      netcfg_get_common ();
      client->command (client, "subst", "netcfg/confirm_static",
		       "hostname", hostname, NULL);
      client->command (client, "subst", "netcfg/confirm_static", "domain",
		       (domain ? domain : "<none>"), NULL);
      netcfg_get_static ();
      ptr = debconf_input ("medium", "netcfg/confirm_static");

      if (strstr (ptr, "true"))
	finished = 1;

    }
  while (!finished);

  netcfg_write_common ();
  netcfg_write_static ();
  netcfg_activate_static ();

  return 0;
}

#endif /* STATIC */
