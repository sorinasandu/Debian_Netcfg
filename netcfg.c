/* 
   netcfg.c : Configures the network 

*/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "debconf.h"

char *hostname = NULL;
char *domain = NULL;
u_int32_t ipaddress = 0;
u_int32_t network_address = 0;
u_int32_t netmask = 0;
u_int32_t gateway = 0;

//#define INTERFACES_FILE "/etc/network/interfaces"
#define INTERFACES_FILE "etc/network/interfaces"



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


void
num2dot (u_int32_t num, char *dot)
{
  /* dot MUST point to an char arr[16] or longer area */
  int byte[4];
  int ix;

  if (!dot)
    return;
  for (ix = 3; ix >= 0; ix--)
    {
      byte[ix] = num & 0xff;
      num >>= 8;
    }
  sprintf (dot, "%d.%d.%d.%d", byte[0], byte[1], byte[2], byte[3]);
}




void
get_static_cfg (void)
{
  int finished = 0;
  char *ptr;


  do
    {


      debconf_command ("INPUT", "critical", "netcfg/get_hostname", NULL);
      debconf_command ("GO", NULL);
      debconf_command ("GET", "netcfg/get_hostname", NULL);
      hostname = debconf_ret ();
      debconf_command ("subst", "netcfg/confirm_static_cfg", "hostname",
		       hostname, NULL);

      debconf_command ("INPUT", "critical", "netcfg/get_domain", NULL);
      debconf_command ("GO", NULL);
      debconf_command ("GET", "netcfg/get_domain", NULL);
      domain = debconf_ret ();
      debconf_command ("subst", "netcfg/confirm_static_cfg", "domain", domain,
		       NULL);

      debconf_command ("INPUT", "critical", "netcfg/get_ipaddress", NULL);
      debconf_command ("GO", NULL);
      debconf_command ("GET", "netcfg/get_ipaddress", NULL);
      ptr = debconf_ret ();
      debconf_command ("subst", "netcfg/confirm_static_cfg", "ipaddress", ptr,
		       NULL);
      dot2num (&ipaddress, ptr);

      debconf_command ("INPUT", "critical", "netcfg/get_netmask", NULL);
      debconf_command ("GO", NULL);
      debconf_command ("GET", "netcfg/get_netmask", NULL);
      ptr = debconf_ret ();
      debconf_command ("subst", "netcfg/confirm_static_cfg", "netmask", ptr,
		       NULL);
      dot2num (&netmask, debconf_ret ());

      debconf_command ("INPUT", "critical", "netcfg/get_gateway", NULL);
      debconf_command ("GO", NULL);
      debconf_command ("GET", "netcfg/get_gateway", NULL);
      ptr = debconf_ret ();
      debconf_command ("subst", "netcfg/confirm_static_cfg", "gateway", ptr,
		       NULL);
      dot2num (&gateway, ptr);


      debconf_command ("INPUT", "high", "netcfg/confirm_static_cfg", NULL);
      debconf_command ("GO", NULL);
      debconf_command ("GET", "netcfg/confirm_static_cfg", NULL);
      ptr = debconf_ret ();

      if (strstr (ptr, "true"))
	finished = 1;
      else
	{
	  debconf_command ("fset", "netcfg/get_hostname", "seen", "false",
			   NULL);
	  debconf_command ("fset", "netcfg/get_domain", "seen", "false",
			   NULL);
	  debconf_command ("fset", "netcfg/get_ipaddress", "seen", "false",
			   NULL);
	  debconf_command ("fset", "netcfg/get_netmask", "seen", "false",
			   NULL);
	  debconf_command ("fset", "netcfg/get_gateway", "seen", "false",
			   NULL);
	  debconf_command ("fset", "netcfg/confirm_static_cfg", "seen",
			   "false", NULL);
	}

    }
  while (!finished);


}




void
write_static_cfg (void)
{
  FILE *fp;
  char buf[16];


  if ((fp = fopen (INTERFACES_FILE, "w")))
    {
      fprintf (fp,
	       "\n# The first network card - this entry was created during the Debian installation\n");
      fprintf (fp, "# (network, broadcast and gateway are optional)\n");
      fprintf (fp, "iface eth0 inet static\n");

      num2dot (ipaddress, buf);
      fprintf (fp, "\taddress %s\n", buf);

      num2dot (netmask, buf);
      fprintf (fp, "\tnetmask %s\n", buf);

      num2dot (ipaddress, buf);
      fprintf (fp, "\tnetwork %s\n", buf);

      num2dot (ipaddress, buf);
      fprintf (fp, "\tbroadcast %s\n", buf);

      num2dot (ipaddress, buf);
      fprintf (fp, "\tgateway %s\n", buf);

      fclose (fp);
    }
  else
    {
      perror ("fopen");
      fprintf (stderr, "Unable to write to '%s'\n", INTERFACES_FILE);
    }
}



int
main (int argc, char *argv[])
{


  debconf_command ("TITLE", "Network Configuration", NULL);

  get_static_cfg ();

  write_static_cfg ();

  return 0;
}
