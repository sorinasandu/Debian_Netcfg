/* 
   netcfg-common.c - Shared functions used to configure the network for 
   the debian-installer.

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

#include "netcfg.h"
#include <iwlib.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
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
#include <time.h>
#include <netdb.h>

/* Set if there is currently a progress bar displayed. */
int netcfg_progress_displayed = 0;

/* network config */
char *interface = NULL;
char *hostname = NULL;
char *domain = NULL;
struct in_addr ipaddress = { 0 };
int have_domain = 0;

pid_t dhcp_pid = -1;

/* File descriptors for ioctls and such */
int skfd = 0;
int wfd = 0;

/* convert a netmask (255.255.255.0) into the length (24) */
int inet_ptom (const char *src, int *dst, struct in_addr *addrp)
{
        struct in_addr newaddr, *addr;
        in_addr_t mask, num;

	if (src && !addrp)
	{
          if (inet_pton (AF_INET, src, &newaddr) < 0)
                return 0;
	  addr = &newaddr;
	}
	else
	  addr = addrp;

        mask = ntohl(addr->s_addr);

        for (num = mask; num & 1; num >>= 1);

        if (num != 0 && mask != 0)
        {
                for (num = ~mask; num & 1; num >>= 1);
                if (num)
                        return 0;
        }

        for (num = 0; mask; mask <<= 1)
                num++;

        *dst = num;

        return 1;
}

/* convert a length (24) into the netmask (255.255.255.0) */
const char *inet_mtop (int src, char *dst, socklen_t cnt)
{
        struct in_addr addr;
        in_addr_t mask = 0;

        for(; src; src--)
                mask |= 1 << (32 - src);

        addr.s_addr = htonl(mask);

        return inet_ntop (AF_INET, &addr, dst, cnt);
}

void open_sockets (void)
{
  wfd = iw_sockets_open();
  skfd = socket (AF_INET, SOCK_DGRAM, 0);
}

int is_interface_up(char *inter)
{
    struct ifreq ifr;
    
    strncpy(ifr.ifr_name, inter, sizeof(ifr.ifr_name));
    
    if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0)
        return -1;

    return ((ifr.ifr_flags & IFF_UP) ? 1 : 0);
}

void get_name(char *name, char *p)
{
    while (isspace(*p))
        p++;
    while (*p) {
        if (isspace(*p))
            break;
        if (*p == ':') {        /* could be an alias */
            char *dot = p, *dotname = name;
            *name++ = *p++;
            while (isdigit(*p))
                *name++ = *p++;
            if (*p != ':') {        /* it wasn't, backup */
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

int get_all_ifs (int all, char*** ptr)
{
  FILE *ifs = NULL;
  char ibuf[512], rbuf[512];
  char** list = NULL;
  size_t len = 0;

  if ((ifs = fopen("/proc/net/dev", "r")) != NULL) {
    fgets(ibuf, sizeof(ibuf), ifs); /* eat header */
    fgets(ibuf, sizeof(ibuf), ifs); /* ditto */
  }
  else
    return 0;

  while (fgets(rbuf, sizeof(rbuf), ifs) != NULL)
  {
    get_name(ibuf, rbuf);
    if (!strcmp(ibuf, "lo"))        /* ignore the loopback */
      continue;
    if (all || is_interface_up(ibuf) == 1)
    {
      list = realloc(list, sizeof(char*) * (len + 1));
      list[len] = strdup(ibuf);
      len++;
    }
  }

  /* OK, now terminate it if necessary */
  if (list != NULL) 
  {
    list = realloc(list, sizeof(char*) * (len + 1));
    list[len] = NULL;
  }
  fclose (ifs);

  *ptr = list;
  
  return len;
}

char *find_in_devnames(const char* iface)
{
#define DEVNAMES "/etc/network/devnames"
  FILE* dn = NULL;
  char buf[512], *result = NULL;
  size_t len = strlen(iface);

  if (!(dn = fopen(DEVNAMES, "r")))
    return NULL;

  while (fgets(buf, 512, dn) != NULL)
  {
    char *ptr = strchr(buf, ':'), *desc = ptr + 1;

    if (!ptr)
    {
      result = NULL; /* corrupt */
      break;
    }
    else if (!strncmp(buf, iface, len))
    {
      result = strdup(desc);
      break;
    }
  }

  fclose(dn);

  len = strlen(result);
  
  if (result[len - 1] == '\n')
    result[len - 1] = '\0';

  return result;
}

char *get_ifdsc(struct debconfclient *client, const char *ifp)
{
    char template[256], *ptr = NULL;

    if ((ptr = find_in_devnames(ifp)) != NULL)
      return ptr; /* already strdup'd */
    
    if (strlen(ifp) < 100) {
      if (!is_wireless_iface(ifp))
      {
        /* strip away the number from the interface (eth0 -> eth) */
        char *new_ifp = strdup(ifp), *ptr = new_ifp;
        while ((*ptr < '0' || *ptr > '9') && *ptr != '\0')
            ptr++;
        *ptr = '\0';

        sprintf(template, "netcfg/internal-%s", new_ifp);
        free(new_ifp);

        debconf_metaget(client, template, "description");
        if (client->value != NULL)
            return strdup(client->value);
      }
      else
      {
	strcpy(template, "netcfg/internal-wifi");
	debconf_metaget(client, template, "description");
	return strdup(client->value);
      }
    }
    debconf_metaget(client, "netcfg/internal-unknown-iface", "description");
    if (client->value != NULL)
        return strdup(client->value);
    else
        return strdup("Unknown interface");
}

int iface_is_hotpluggable(const char *iface)
{
#define DEVHOTPLUG "/etc/network/devhotplug"
    FILE* f = NULL;
    char buf[256];
    size_t len = strlen(iface);
    int result = 0;
    
    if (!(f = fopen(DEVHOTPLUG, "r")))
        return 0;
    
    while (fgets(buf, 256, f) != NULL)
    {
        if (!strncmp(buf, iface, len))
        {
            result = 1;
            break;
        }
    }
    
    fclose(f);
    
    return result;
}

FILE *file_open(char *path, const char *opentype)
{
    FILE *fp;
    if ((fp = fopen(path, opentype)))
        return fp;
    else {
        fprintf(stderr, "%s\n", path);
        perror("fopen");
        return NULL;
    }
}

void netcfg_die(struct debconfclient *client)
{
    if (netcfg_progress_displayed)
        debconf_progress_stop(client);
    debconf_capb(client);
    debconf_input(client, "high", "netcfg/error");
    debconf_go(client);
    exit(1);
}

/**
 * @brief Ask which interface to configure
 * @param client - client 
 * @param interface      - set to the answer
 * @param numif - number of interfaces found.
 */

int netcfg_get_interface(struct debconfclient *client, char **interface,
                         int *numif)
{
    char *inter = NULL, **ifs;
    size_t len;
    int ret, i;
    int num_interfaces = 0;
    char *ptr = NULL;
    char *ifdsc = NULL;

    if (*interface) {
        free(*interface);
        *interface = NULL;
    }

    if (!(ptr = malloc(128)))
	goto error;

    len = 128;
    *ptr = '\0';

    num_interfaces = get_all_ifs(1, &ifs);

    for (i = 0; i < num_interfaces; i++)
    {
	size_t newchars;

	inter = ifs[i];

	interface_down(inter);
	ifdsc = get_ifdsc(client, inter);
        newchars = strlen(inter) + strlen(ifdsc) + 5; /* ": , " + NUL */
        if (len < (strlen(ptr) + newchars)) {
            if (!(ptr = realloc(ptr, len + newchars + 128)))
                goto error;
            len += newchars + 128;
        }
        di_snprintfcat(ptr, len, "%s: %s, ", inter, ifdsc);
        free(ifdsc);
    }

    if (num_interfaces == 0)
    {
        debconf_input(client, "high", "netcfg/no_interfaces");
        debconf_go(client);
        free(ptr);
        exit(1);
    }
    else if (num_interfaces > 1)
    {
        *numif = num_interfaces;
        /* remove the trailing ", ", which confuses cdebconf */
        ptr[strlen(ptr) - 2] = '\0';

        debconf_subst(client, "netcfg/choose_interface", "ifchoices", ptr);
        free(ptr);

        debconf_input(client, "high", "netcfg/choose_interface");
        ret = debconf_go(client);

        if (ret)
          return ret;

        debconf_get(client, "netcfg/choose_interface");
        inter = client->value;

        if (ret)
            return ret;
        if (!inter)
            netcfg_die(client);
    }
    else if (num_interfaces == 1)
    {
        inter = ptr;
        *numif = 1;
    }

    /* grab just the interface name, not the description too */
    *interface = inter;
    ptr = strchr(inter, ':');
    if (ptr == NULL)
        goto error;
    *ptr = '\0';

    *interface = strdup(*interface);

    /* Free allocated memory */
    while (ifs && *ifs)
      free(*ifs++);
    
    return 0;

 error:
    if (ptr)
        free(ptr);

    netcfg_die(client);
    return 10; /* unreachable */
}

/*
 * Set the hostname. 
 * @return 0 on success, 30 on BACKUP being selected.
 */
int netcfg_get_hostname(struct debconfclient *client, char *template, char **hostname, short hdset)
{
    static const char *valid_chars =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";
    size_t len;
    int ret;
    char *s;

    do {
        if (hdset)
          have_domain = 0;
        debconf_input(client, "high", template);
        ret = debconf_go(client);

        if (ret == 30) /* backup */
          return ret;
        
        debconf_get(client, template);
       
        free(*hostname);
        *hostname = strdup(client->value);
        len = strlen(*hostname);

        /* Check the hostname for RFC 1123 compliance.  */
        if ((len < 2) ||
            (len > 63) ||
            (strspn(*hostname, valid_chars) != len) ||
            ((*hostname)[len - 1] == '-') ||
            ((*hostname)[0] == '-')) {
            debconf_subst(client, "netcfg/invalid_hostname",
                          "hostname", *hostname);
            debconf_input(client, "high", "netcfg/invalid_hostname");
            debconf_go(client);
            free(*hostname);
            *hostname = NULL;
            debconf_set(client, template, "debian");
        }
        
    } while (!*hostname);

    /* don't strip DHCP hostnames */
    if (hdset && (s = strchr(*hostname, '.')))
    {
      if (s[1] == '\0') /* "somehostname." <- . should be ignored */
	*s = '\0';
      else /* assume we have a valid domain name here */
      {
	if (domain)
	  free(domain);
	domain = strdup(s + 1);
	debconf_set(client, "netcfg/get_domain", domain);
        have_domain = 1;
      }
    }
    return 0;
}

/* @brief Get the domainname.
 * @return 0 for success, with *domain = domain, 30 for 'goback',
 */
int netcfg_get_domain(struct debconfclient *client,  char **domain)
{
    int ret;
       
    if (have_domain == 1)
    {
      debconf_get(client, "netcfg/get_domain");
      assert (!empty_str(client->value));
      if (*domain)
	free(*domain);
      *domain = strdup(client->value);
      return 0;
    }

    debconf_input (client, "high", "netcfg/get_domain");
    ret = debconf_go(client);

    if (ret)
      return ret;
   
    debconf_get (client, "netcfg/get_domain");
    
    if (*domain)
        free(*domain);
    *domain = NULL;
    if (!empty_str(client->value))
        *domain = strdup(client->value);
    return 0;
}

#define HELPFUL_COMMENT \
"# This file describes the network interfaces available on your system\n" \
"# and how to activate them. For more information, see interfaces(5).\n" \
"\n" \
"# This entry denotes the loopback (127.0.0.1) interface.\n"

void netcfg_write_loopback(const char* prebaseconfig)
{
  FILE *fp;

  if ((fp = file_open(INTERFACES_FILE, "w"))) {
    fprintf(fp, HELPFUL_COMMENT);
    fprintf(fp, "auto lo\n");
    fprintf(fp, "iface lo inet loopback\n");
    fclose(fp);
  }
  di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
      INTERFACES_FILE,
      "/target" INTERFACES_FILE);
}

void netcfg_write_common(const char *prebaseconfig, struct in_addr ipaddress,
			 char *hostname, char *domain)
{
    FILE *fp;

    if ((fp = file_open(INTERFACES_FILE, "w"))) {
        fprintf(fp, HELPFUL_COMMENT);
        fprintf(fp, "auto lo\n");
        fprintf(fp, "iface lo inet loopback\n");
        if (iface_is_hotpluggable(interface)) {
            fprintf(fp, "\n");
            fprintf(fp, "# This is a list of hotpluggable network interfaces.\n");
            fprintf(fp, "# They will be activated automatically by the "
                    "hotplug subsystem.\n");
            fprintf(fp, "mapping hotplug\n");
            fprintf(fp, "\tscript grep\n");
            fprintf(fp, "\tmap %s\n", interface);
        }
        fclose(fp);

        di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
                                       INTERFACES_FILE,
                                       "/target" INTERFACES_FILE);
    }

    /* Currently busybox, hostname is not available. */
    sethostname (hostname, strlen(hostname) + 1);

    if ((fp = file_open(HOSTNAME_FILE, "w"))) {
       fprintf(fp, "%s\n", hostname);
       fclose(fp);
       di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
                                       HOSTNAME_FILE, "/target" HOSTNAME_FILE);
    }

    if ((fp = file_open(HOSTS_FILE, "w"))) {
        char ptr1[INET_ADDRSTRLEN];
        if (ipaddress.s_addr) {
            fprintf(fp, "127.0.0.1\tlocalhost\t%s\n", hostname);
            if (domain && !empty_str(domain))
                fprintf(fp, "%s\t%s.%s\t%s\n",
                        inet_ntop (AF_INET, &ipaddress, ptr1, sizeof (ptr1)), hostname,
                        domain, hostname);
            else
                fprintf(fp, "%s\t%s\n", inet_ntop (AF_INET, &ipaddress, ptr1, sizeof (ptr1)),
                        hostname);
        } else {
            fprintf(fp, "127.0.0.1\tlocalhost\t%s\n", hostname);
        }

        fclose(fp);

        di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n",
                                       HOSTS_FILE, "/target" HOSTS_FILE);
    }
}


void deconfigure_network(void)
{
    /* deconfiguring network interfaces */
    interface_down("lo");
    interface_down(interface);
}

void loop_setup(void)
{
  static int afpacket_notloaded = 1;

  deconfigure_network();
  
  if (afpacket_notloaded)
    afpacket_notloaded = di_exec_shell("modprobe af_packet"); /* should become 0 */
      
  di_exec_shell_log("ip link set lo up");
  di_exec_shell_log("ip addr flush dev lo");
  di_exec_shell_log("ip addr add 127.0.0.1 dev lo");
}

void seed_hostname_from_dns (struct debconfclient * client, struct in_addr *ipaddr)
{
  struct addrinfo hints = {
    .ai_family = PF_UNSPEC,
    .ai_socktype = 0,
    .ai_protocol = 0,
    .ai_flags = AI_CANONNAME
  };
  struct addrinfo *res;
  struct sockaddr tmp;
  char ip[INET_ADDRSTRLEN] = { 0 };
  int err;

  /* convert ipaddress into a char* */
  inet_ntop(AF_INET, (void*)ipaddr, ip, sizeof(ip));

  /* attempt resolution */
  err = getaddrinfo(ip, NULL, &hints, &res);

  /* got it? */
  if (err == 0 && res->ai_canonname && !empty_str(res->ai_canonname) &&
      inet_pton(AF_INET, res->ai_canonname, &tmp) == 0)
    debconf_set(client, "netcfg/get_hostname", res->ai_canonname);
  else
  {
    debconf_get(client, "netcfg/get_hostname");
    /* reset the hostname */
    if (strcmp(client->value, "debian"))
      debconf_set(client, "netcfg/get_hostname", "debian");
  }

  freeaddrinfo(res);
}

void interface_up (char* iface)
{
  struct ifreq ifr;

  strncpy(ifr.ifr_name, iface, IFNAMSIZ);

  if (skfd && ioctl(skfd, SIOCGIFFLAGS, &ifr) >= 0)
  {
    strncpy(ifr.ifr_name, iface, IFNAMSIZ);
    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);
    ioctl(skfd, SIOCSIFFLAGS, &ifr);
  }
}

void interface_down (char* iface)
{
  struct ifreq ifr;

  strncpy(ifr.ifr_name, iface, IFNAMSIZ);

  if (skfd && ioctl(skfd, SIOCGIFFLAGS, &ifr) >= 0)
  {
    strncpy(ifr.ifr_name, iface, IFNAMSIZ);
    ifr.ifr_flags &= ~IFF_UP;
    ioctl(skfd, SIOCSIFFLAGS, &ifr);
  }
}

void parse_args (int argc, char ** argv)
{
  if (argc == 2)
  {
    if (!strcmp(argv[0], "ptom"))
    {
      int ret;
      if (inet_ptom(argv[1], &ret, NULL) > 0)
      {
	printf("%d\n", ret);
	exit(EXIT_SUCCESS);
      }
    }
    
    exit(EXIT_FAILURE);
  }
}
