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

#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <sys/socket.h>
#include <net/if.h>
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

int is_interface_up(char *inter)
{
    struct ifreq ifr;
    int sfd = -1, ret = -1;
    
    if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
        goto int_up_done;
    
    strncpy(ifr.ifr_name, inter, sizeof(ifr.ifr_name));
    
    if (ioctl(sfd, SIOCGIFFLAGS, &ifr) < 0)
        goto int_up_done;

    ret = (ifr.ifr_flags & IFF_UP) ? 1 : 0;
    
 int_up_done:
    if (sfd != -1)
        close(sfd);
    return ret;
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


static FILE *ifs = NULL;
static char ibuf[512];

void getif_start(void)
{
    if (ifs != NULL) {
        fclose(ifs);
        ifs = NULL;
    }
    if ((ifs = fopen("/proc/net/dev", "r")) != NULL) {
        fgets(ibuf, sizeof(ibuf), ifs); /* eat header */
        fgets(ibuf, sizeof(ibuf), ifs); /* ditto */
    }
    return;
}


char *getif(int all)
{
    char rbuf[512];
    if (ifs == NULL)
        return NULL;

    if (fgets(rbuf, sizeof(rbuf), ifs) != NULL) {
        get_name(ibuf, rbuf);
        if (!strcmp(ibuf, "lo"))        /* ignore the loopback */
            return getif(all);      /* seriously doubt there is an infinite number of lo devices */
        if (all || is_interface_up(ibuf) == 1)
            return ibuf;
    }
    return NULL;
}


void getif_end(void)
{
    if (ifs != NULL) {
        fclose(ifs);
        ifs = NULL;
    }
    return;
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
    char *inter;
    size_t len;
    int ret;
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

    getif_start();

    while ((inter = getif(1)) != NULL) {
	size_t newchars;

	ifconfig_down(inter);
	ifdsc = get_ifdsc(client, inter);
        newchars = strlen(inter) + strlen(ifdsc) + 5;
        if (len < (strlen(ptr) + newchars)) {
            if (!(ptr = realloc(ptr, len + newchars + 128)))
                goto error;
            len += newchars + 128;
        }
        di_snprintfcat(ptr, len, "%s: %s, ", inter, ifdsc);
        num_interfaces++;
        free(ifdsc);
    }
    getif_end();

    if (num_interfaces == 0) {
        debconf_input(client, "high", "netcfg/no_interfaces");
        debconf_go(client);
        free(ptr);
        exit(1);
    } else if (num_interfaces > 1) {
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
    } else if (num_interfaces == 1) {
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
	debconf_set(client, "netcfg/get_domain", strdup(s + 1));
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


int deconfigure_network(void) {

    char buf[256];

    /* deconfiguring network interfaces */
    di_exec_shell_log("ifconfig lo down");
    snprintf(buf, sizeof(buf), "ifconfig %s down", interface);
    di_exec_shell_log(buf);

    return 0;
}

/* Utility functions. */
int ifconfig_up (char* iface)
{
  char * cmd;
  size_t len = sizeof("ifconfig  up") + strlen(iface) + 1;
  int ret;

  if (!(cmd = malloc(len)))
    return 1;

  snprintf(cmd, len, "ifconfig %s up", iface);

  ret = di_exec_shell(cmd);

  free(cmd);

  return ret;
}

int ifconfig_down (char* iface)
{
  char * cmd;
  size_t len = sizeof("ifconfig  down") + strlen(iface) + 1;
  int ret;

  if (!(cmd = malloc(len)))
    return 1;

  snprintf(cmd, len, "ifconfig %s down", iface);

  ret = di_exec_shell(cmd);

  free(cmd);

  return ret;
}

void loop_setup(void)
{
  static int afpacket_notloaded = 1;

  deconfigure_network();
  
  if (afpacket_notloaded)
    afpacket_notloaded = di_exec_shell("modprobe af_packet"); /* should become 0 */
      
  di_exec_shell_log("ifconfig lo 127.0.0.1 up");
}

void seed_hostname_from_dns (struct debconfclient * client)
{
  struct addrinfo hints = {
    .ai_family = PF_UNSPEC,
    .ai_socktype = 0,
    .ai_protocol = 0,
    .ai_flags = AI_CANONNAME
  };
  struct addrinfo *res;
  char ip[16]; /* 255.255.255.255 + 1 */
  int err;

  /* convert ipaddress into a char* */
  inet_ntop(AF_INET, (void*)&ipaddress, ip, 16);

  /* attempt resolution */
  err = getaddrinfo(ip, NULL, &hints, &res);

  /* got it? */
  if (!err && res->ai_canonname && !empty_str(res->ai_canonname))
    debconf_set(client, "netcfg/get_hostname", res->ai_canonname);
}
