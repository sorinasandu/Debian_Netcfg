/* 
   netcfg.c - Configure the network for the debian-installer
   Author - David Whedon
 

*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <debconfclient.h>

static char *hostname = NULL;
static char *domain = NULL;
static char *iface = "eth0";
static u_int32_t ipaddress = 0;
static u_int32_t network = 0;
static u_int32_t broadcast = 0;
static u_int32_t netmask = 0;
static u_int32_t gateway = 0;

static u_int32_t nameservers[4] = { 0 };

static struct debconfclient *client;


#define MAXLINE 128 
static char cmd_buf[MAXLINE];

#define INTERFACES_FILE "etc/network/interfaces"
#define HOSTS_FILE      "etc/hosts"
#define NETWORKS_FILE   "etc/networks"
#define RESOLV_FILE     "etc/resolv.conf"


/** 
 * dot2num and num2dot
 * Copyright: Karl Hammar, Aspö Data
*/
char *
dot2num (u_int32_t * num, char *dot)
{
  char *p = dot - 1;
  char *e;
  int ix;
  unsigned long val;

  if (!dot)
    return NULL;
  *num = 0;
  for (ix = 0; ix < 4; ix++)
    {
      *num <<= 8;
      p++;
      val = strtoul (p, &e, 10);
      if (e == p)
	val = 0;
      else if (val > 255)
	return NULL;
      *num += val;
      /*printf("%#8x, %#2x\n", *num, val); */
      if (ix < 3 && *e != '.')
	return NULL;
      p = e;
    }

  return p;
}


static char num2dot_buf[16];

char *
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

char *
debconf_input (char *priority, char *template)
{
  client->command (client, "input", priority, template, NULL);
  client->command (client, "go", NULL);
  client->command (client, "get", template, NULL);
  return client->value;
}

void
debconf_subst (char  *template, char *key, char *string){
    client->command (client, "subst", "netcfg/confirm_static_cfg",
	    key, string, NULL);
}


void
debconf_unseen(char *template){
	  client->command (client, "fset", template, "seen",
			   "false", NULL);
}

void
get_static_cfg (void)
{
  int finished = 0;
  char *ptr, *ns1, *ns2, *ns3;

  do
    {
      hostname = strdup (debconf_input ("high", "netcfg/get_hostname"));
	  
      debconf_subst("netcfg/confirm_static_cfg", "hostname", hostname);

      if ((ptr = debconf_input ("high", "netcfg/get_domain")))
	{
	  domain = strdup (ptr);
	  debconf_subst("netcfg/confirm_static_cfg", "domain", domain);
	}

      if ((ptr = debconf_input ("high", "netcfg/get_ipaddress")))
	{
	  dot2num (&ipaddress, ptr);
	  debconf_subst("netcfg/confirm_static_cfg", "ipaddress", ptr);
	}

      if ((ptr = debconf_input ("high", "netcfg/get_netmask")))
	{
	  dot2num (&netmask, ptr);
	  debconf_subst("netcfg/confirm_static_cfg", "netmask", ptr);
	}

      network = ipaddress & netmask;

      debconf_subst ("netcfg/confirm_static_cfg", "network", num2dot (network));


      if (( ptr = debconf_input ("high", "netcfg/get_gateway")))
	{
	  dot2num (&gateway, ptr);
	  debconf_subst("netcfg/confirm_static_cfg", "gateway", ptr);
	}


      if ((gateway & netmask) != (ipaddress & netmask))
      {
	  client->command (client, "input", "high", "netcfg/gateway_unreachable", NULL);
	  client->command (client, "go", NULL);
      }
     
      broadcast = (network | ~netmask);
      debconf_subst("netcfg/confirm_static_cfg", "broadcast", num2dot(broadcast));

      ptr = debconf_input ("high", "netcfg/get_nameservers");

      if (ptr)
	{

	  ptr = strdup (ptr);
	  ns1 = strtok (ptr, " ");
	  ns2 = strtok (NULL, " ");
	  ns3 = strtok (NULL, " ");

	  if (ns1 != NULL)
	      debconf_subst("netcfg/confirm_static_cfg", "primary_DNS", ns1);
	  else
	      debconf_subst("netcfg/confirm_static_cfg", "primary_DNS", "none");
	  
	  if (ns2 != NULL)
	      debconf_subst("netcfg/confirm_static_cfg", "secondary_DNS", ns1);
	  else
	      debconf_subst("netcfg/confirm_static_cfg", "secondary_DNS", "none");
	  if (ns3 != NULL)
	      debconf_subst("netcfg/confirm_static_cfg", "tertiary_DNS", ns1);
	  else
	      debconf_subst("netcfg/confirm_static_cfg", "tertiary_DNS", "none");
	  free (ptr);
	}

      ptr = debconf_input ("high", "netcfg/confirm_static_cfg");

      if (strstr (ptr, "true"))
	finished = 1;
      else
	{
	  debconf_unseen ("netcfg/get_hostname");
	  debconf_unseen ("netcfg/get_ipaddress");
	  debconf_unseen ("netcfg/get_netmask");
	  debconf_unseen ("netcfg/get_gateway");
	  debconf_unseen ("netcfg/get_nameservers");
	  debconf_unseen ("netcfg/get_confirm_static_cfg");
	  free (hostname);
	  free (domain);
	  hostname = domain = NULL;
	  ipaddress = network = broadcast = netmask = gateway = nameservers[0] = 0 ;
	}

    }
  while (!finished);

}

FILE *
file_open(char *path){
    FILE *fp;
    if (( fp = fopen(path, "w")))
	return fp;
    else 
    {
	perror("fopen");
	return NULL;
    }

}



void
write_static_cfg (void)
{
  FILE *fp;

  if ((fp = file_open (HOSTS_FILE)))
    {
      fprintf (fp, "127.0.0.1\tlocalhost\n");
      if (domain)
	{
	  fprintf (fp, "%s\t%s.%s\t%s\n", num2dot (ipaddress),
		   hostname, domain, hostname);
	}
      else
	{
	  fprintf (fp, "%s\t%s\n", num2dot (ipaddress), hostname);
	}

      fclose (fp);
    }

  if ((fp = file_open (NETWORKS_FILE)))
    {
      fprintf (fp, "localnet %s\n", num2dot (network));
      fclose (fp);
    }

  if ((fp = file_open (RESOLV_FILE)))
    {
      int i = 0;
      if (domain)
	fprintf (fp, "search %s\n", domain);
      while (nameservers[i])
	{
	  fprintf (fp, "nameserver %s\n", num2dot (nameservers[i++]));
	}
      fclose (fp);
    }

  if ((fp = file_open (INTERFACES_FILE)))
    {
      fprintf (fp,
	       "\n# The first network card - this entry was created during the Debian installation\n");
      fprintf (fp, "# (network, broadcast and gateway are optional)\n");
      fprintf (fp, "iface eth0 inet static\n");
      fprintf (fp, "\taddress %s\n", num2dot (ipaddress));
      fprintf (fp, "\tnetmask %s\n", num2dot (netmask));
      fprintf (fp, "\tnetwork %s\n", num2dot (network));
      fprintf (fp, "\tbroadcast %s\n", num2dot (broadcast));
      fprintf (fp, "\tgateway %s\n", num2dot (gateway));
      fclose (fp);
    }

}


int
activate_static_net ()
{


  system ("/sbin/ifconfig lo 127.0.0.1");

  snprintf (cmd_buf, sizeof (cmd_buf),
	    "/sbin/ifconfig %s %s netmask %s broadcast %s", iface,
	    num2dot (ipaddress), num2dot (netmask), num2dot (broadcast));
  system ("cmd_buf");
  return 0;

}



int
main (int argc, char *argv[])
{

  client = debconfclient_new ();

  client->command (client, "title", "Network Configuration", NULL);


  get_static_cfg ();

  write_static_cfg ();

  return 0;
}
