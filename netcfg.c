/* 
   netcfg.c - Configures the network 
   Author - David Whedon, Karl Hammar, Aspö Data
 
   Copyright terms: GPL

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
static u_int32_t ipaddress = 0;
static u_int32_t network_address = 0;
char network_address_str[16];
static u_int32_t netmask = 0;
char netmask_str[16];
static u_int32_t gateway = 0;
char gateway_str[16];
static u_int32_t nameservers[4]={0};
char nameservers_str[16];
static char address_buf[16];

static struct debconfclient *client;

#define INTERFACES_FILE "/etc/network/interfaces"
#define HOSTS_FILE      "/etc/hosts"
#define NETWORKS_FILE   "/etc/networks"
#define RESOLV_FILE     "/etc/resolv.conf"


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
  char *ptr ,*ns;

  do
    {

      client->command (client, "input", "high", "netcfg/get_hostname",
		       NULL);
      client->command (client, "go", NULL);
      client->command (client, "get", "netcfg/get_hostname", NULL);
      
      hostname = strdup(client->value);
      client->command (client, "subst", "netcfg/confirm_static_cfg",
		       "hostname", hostname, NULL);


      client->command (client, "input", "critical", "netcfg/get_domain",
		       NULL);
      client->command (client, "go", NULL);
      client->command (client, "get", "netcfg/get_domain", NULL);
      domain = strdup(client->value);
      client->command (client, "subst", "netcfg/confirm_static_cfg", "domain",
		       domain, NULL);


      client->command (client, "input", "critical", "netcfg/get_ipaddress",
		       NULL);
      client->command (client, "go", NULL);
      client->command (client, "get", "netcfg/get_ipaddress", NULL);
      ptr = client->value;
      dot2num (&ipaddress, ptr);
      client->command (client, "subst", "netcfg/confirm_static_cfg",
		       "ipaddress", ptr, NULL);


      client->command (client, "input", "critical", "netcfg/get_netmask",
		       NULL);
      client->command (client, "go", NULL);
      client->command (client, "get", "netcfg/get_netmask", NULL);
      ptr = client->value;
      dot2num (&netmask, ptr);
      client->command (client, "subst", "netcfg/confirm_static_cfg",
		       "netmask", ptr, NULL);

      network_address = ipaddress & netmask;
      num2dot(network_address, address_buf);
      client->command (client, "subst", "netcfg/confirm_static_cfg",
		       "network", address_buf, NULL);
      client->command (client, "input", "critical", "netcfg/get_gateway",
		       NULL);
      client->command (client, "go", NULL);
      client->command (client, "get", "netcfg/get_gateway", NULL);
      ptr = client->value;
      dot2num (&ipaddress, ptr);
      client->command (client, "subst", "netcfg/confirm_static_cfg",
		       "gateway", ptr, NULL);


      client->command (client, "input", "critical", "netcfg/get_nameservers",
		       NULL);
      client->command (client, "go", NULL);
      client->command (client, "get", "netcfg/get_nameservers", NULL);
      ptr = strdup(client->value);

      ns = strtok(ptr, " ");
      client->command (client, "subst", "netcfg/confirm_static_cfg",
	      "primary_DNS", ns, NULL);

      ns = strtok(NULL, " ");
      client->command (client, "subst", "netcfg/confirm_static_cfg",
	      "secondary_DNS", ns, NULL);
      ns = strtok(NULL, " ");
      client->command (client, "subst", "netcfg/confirm_static_cfg",
	      "tertiary_DNS", ns, NULL);

      free(ptr);
     
      
      client->command (client, "input", "high", "netcfg/confirm_static_cfg",
		       NULL);
      client->command (client, "go", NULL);
      client->command (client, "get", "netcfg/confirm_static_cfg", NULL);
      ptr = client->value;

      if (strstr (ptr, "true"))
	finished = 1;
      else
	{
	  client->command (client, "fset", "netcfg/get_hostname", "seen",
			   "false", NULL);
	  client->command (client, "fset", "netcfg/get_domain", "seen",
			   "false", NULL);
	  client->command (client, "fset", "netcfg/get_ipaddress", "seen",
			   "false", NULL);
	  client->command (client, "fset", "netcfg/get_netmask", "seen",
			   "false", NULL);
	  client->command (client, "fset", "netcfg/get_gateway", "seen",
			   "false", NULL);
	  client->command (client, "fset", "netcfg/get_nameservers", "seen",
			   "false", NULL);
	  client->command (client, "fset", "netcfg/confirm_static_cfg",
			   "seen", "false", NULL);
	  free(hostname);
	  free(domain);
	}

    }
  while (!finished);

}




void
write_static_cfg (void)
{
  FILE *fp;

  /* /etc/hosts */
  
  if ((fp = fopen (HOSTS_FILE, "w")))
  {
      fprintf(fp, "127.0.0.1\tlocalhost\n");
      num2dot (ipaddress, address_buf);
      if (domain) {
	  fprintf(fp, "%s\t%s.%s\t%s\n", address_buf,
		  hostname, domain, hostname);
      } else {
	  num2dot (ipaddress, address_buf);
      	  fprintf(fp, "%s\t%s\n", address_buf, hostname);
      }

      fclose(fp);
  }
  else
    {
      perror ("fopen");
    }

  /* /etc/networks */
  if ((fp = fopen(NETWORKS_FILE, "w"))) {
      num2dot (network_address, address_buf);
      fprintf(fp, "localnet %s\n", address_buf);
      fclose(fp);
  } else {
      perror("fopen");
  }

  /* /etc/resolv.conf */
  if ((fp = fopen(RESOLV_FILE, "w"))) {
      int i=0;
      if (domain) fprintf(fp, "search %s\n", domain);
      while(nameservers[i]){
	  num2dot (nameservers[i++], address_buf);
	  fprintf(fp, "nameserver %s\n", address_buf);
      }
      fclose(fp);
  } else {
      perror("fopen");
  }

  /* /etc/network/interfaces */
  if ((fp = fopen (INTERFACES_FILE, "w")))
    {
      fprintf (fp,
	       "\n# The first network card - this entry was created during the Debian installation\n");
      fprintf (fp, "# (network, broadcast and gateway are optional)\n");
      fprintf (fp, "iface eth0 inet static\n");

      num2dot (ipaddress, address_buf);
      fprintf (fp, "\taddress %s\n", address_buf);

      num2dot (netmask, address_buf);
      fprintf (fp, "\tnetmask %s\n", address_buf);

      num2dot (ipaddress, address_buf);
      fprintf (fp, "\tnetwork %s\n", address_buf);

      num2dot (ipaddress, address_buf);
      fprintf (fp, "\tbroadcast %s\n", address_buf);

      num2dot (ipaddress, address_buf);
      fprintf (fp, "\tgateway %s\n", address_buf);

      fclose (fp);
    }
  else
    {
      perror ("fopen");
    }

}



int
main (int argc, char *argv[])
{
    int i;

    dot2num(&i,"192.168.0.1");
    fprintf(stderr, "%x\n", i); 

  client = debconfclient_new ();

  client->command (client, "title", "Network Configuration", NULL);

  get_static_cfg ();

  write_static_cfg ();

  return 0;
}
