#ifndef _NETCFG_H_
#define _NETCFG_H_
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <cdebconf/debconfclient.h>

#define INTERFACES_FILE "/etc/network/interfaces"
#define HOSTS_FILE      "/etc/hosts"
#define HOSTNAME_FILE   "/etc/hostname"
#define NETWORKS_FILE   "/etc/networks"
#define RESOLV_FILE     "/etc/resolv.conf"

#define _GNU_SOURCE

enum { NOT_ASKED = 30, GO_BACK };
typedef enum { DHCP, STATIC, DUNNO } method_t;

extern int netcfg_progress_displayed;
extern int wfd;

/* network config */
extern char *interface;
extern char *hostname;
extern char *dhcp_hostname;
extern char *domain;
extern struct in_addr ipaddress;
extern struct in_addr nameserver_array[4];
extern struct in_addr network;
extern struct in_addr broadcast;
extern struct in_addr netmask;
extern struct in_addr gateway;
extern struct in_addr pointopoint;

/* common functions */
extern int is_interface_up (char *inter);

extern void get_name (char *name, char *p);

extern void getif_start ();

extern void getif_end ();

extern char *get_ifdsc (struct debconfclient *client, const char *ifp);

extern FILE *file_open (char *path, const char *opentype);

extern void netcfg_die (struct debconfclient *client);

extern int netcfg_get_interface(struct debconfclient *client, char **interface, int *num_interfaces);

extern int netcfg_get_hostname(struct debconfclient *client, char **hostname);

extern int netcfg_get_nameservers (struct debconfclient *client, char **nameservers);

extern int netcfg_get_domain(struct debconfclient *client,  char **domain);

extern int netcfg_get_dhcp(struct debconfclient *client);

extern int netcfg_get_static(struct debconfclient *client);

extern int netcfg_activate_dhcp(struct debconfclient *client);

extern int netcfg_activate_static(struct debconfclient *client);

extern int my_debconf_input(struct debconfclient *client, char *priority, char *template, char **result);

extern void netcfg_write_common (const char *prebaseconfig,
				 struct in_addr ipaddress, char *hostname,
				 char *domain);

void netcfg_nameservers_to_array(char *nameservers, struct in_addr array[]);

extern int is_wireless_iface (const char* iface);

extern int netcfg_wireless_set_essid (struct debconfclient *client, char* iface);
extern int netcfg_wireless_set_wep (struct debconfclient *client, char* iface);

#endif /* _NETCFG_H_ */
