/* 
   netcfg.c : Configures the network 
*/


#define IP4_ADDR_SZ (4)
struct ip4_addr
{
  int i[IP4_ADDR_SZ];
}
ip4_addr_t;
char *host = NULL;
char *domain = NULL;

ip4_addr_t ipaddr = { {192, 168, 1, 1} };
ip4_addr_t netmask = { {255, 255, 255, 0} };
ip4_addr_t network = { {192, 168, 1, 0} };
ip4_addr_t broadcast = { {192, 168, 1, 255} };
ip4_addr_t gateway = { {0, 0, 0, 0} };


/*
 * Checks a string with an IPv4-address for validity and converts it into an
 * ip4_addr_t type;
 */
int
atoIP4 (char *addr, ip4_addr_t * iaddr)
{
  char *end;
  char *tmp_a;
  char *current = strdup (addr);
  char *next = NULL;
  int ix = 0;
  int tmp;

  tmp_a = current;
  end = current + strlen (current) + 1;
  while (next != end)
    {
      next = strchr (current, '.');
      if (next == NULL)
	next = end;
      else
	{
	  *next = '\0';
	  next++;
	}

      if (ix == IP4_ADDR_SZ)
	{
	  free (tmp_a);
	  return 255;
	}
      else
	{
	  tmp = atoi (current);
	  if ((tmp < 0) || (tmp > 255))
	    {
	      free (tmp_a);
	      return 255;
	    }
	  iaddr->i[ix++] = tmp;
	  current = next;
	}
    }
  free (tmp_a);
  if (ix != IP4_ADDR_SZ)
    return 255;
  return 0;
}



static_network_cfg ()
{
  int ix;

  debconf_command ("INPUT", "high", "netcfg/get_hostname", NULL);

  do {
      debconf_command ("INPUT", "critical", "netcfg/get_ipaddress", NULL);
      debconf_command ("GO", NULL);
  } while ( atoIP4(debconf_command ("GET", "netcfg/get_ipaddress", NULL), &ipaddress)) ;

  do {
      debconf_command ("INPUT", "critical", "netcfg/get_netmask", NULL);
      debconf_command ("GO", NULL);
  } while ( atoIP4(debconf_command ("GET", "netcfg/get_netmask", NULL), &netmask)) ;


  /*
   * Generate the network, broadcast and default gateway address
   * Default gateway address is network with last number set to "1", 
   * or "2" if "1" is the local address.
   */
  for (ix = 0; ix < IP4_ADDR_SZ; ix++)
    {
      gateway.i[ix] = network.i[ix] = ipaddr.i[ix] & netmask.i[ix];
      broadcast.i[ix] = (~netmask.i[ix] & 255) | ipaddr.i[ix];
    }
  gateway.i[IP4_ADDR_SZ - 1] |=
    (ipaddr.i[IP4_ADDR_SZ - 1] == (network.i[IP4_ADDR_SZ - 1] | 1)) ? 2 : 1;

}





int main(int argc, char *argv[]){


  debconf_command ("TITLE", "Network Configuration", NULL);

  static_netork_cfg();




}
