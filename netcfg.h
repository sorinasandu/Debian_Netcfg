#ifndef _NETCFG_H_
#define _NETCFG_H_

#define INTERFACES_FILE "/etc/network/interfaces"
#define HOSTS_FILE      "/etc/hosts"
#define HOSTNAME_FILE   "/etc/hostname"
#define NETWORKS_FILE   "/etc/networks"
#define RESOLV_FILE     "/etc/resolv.conf"
#define DHCLIENT_CONF	"/etc/dhclient.conf"
#define DHCLIENT3_CONF	"/etc/dhcp3/dhclient.conf"

#define _GNU_SOURCE

#include <sys/types.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <cdebconf/debconfclient.h>

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#define empty_str(s) (s && *s == '\0')

typedef enum { NOT_ASKED = 30, GO_BACK } response_t;
typedef enum { DHCP, STATIC, DUNNO } method_t;
typedef enum { ADHOC = 1, MANAGED = 2 } wifimode_t;
typedef enum { DHCLIENT, DHCLIENT3, PUMP, UDHCPC } dhclient_t;

extern int netcfg_progress_displayed;
extern int wfd;
extern int input_result;
extern int have_domain;
extern pid_t dhcp_pid;

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

/* wireless */
extern char *essid, *wepkey;
extern wifimode_t mode;

/* common functions */
extern int is_interface_up (char *inter);

extern void get_name (char *name, char *p);

extern void getif_start ();

extern void getif_end ();

extern char *get_ifdsc (struct debconfclient *client, const char *ifp);

extern FILE *file_open (char *path, const char *opentype);

extern void netcfg_die (struct debconfclient *client);

extern int netcfg_get_interface(struct debconfclient *client, char **interface, int *num_interfaces);

extern int netcfg_get_hostname(struct debconfclient *client, char *template, char **hostname, short hdset);

extern int netcfg_get_nameservers (struct debconfclient *client, char **nameservers);

extern int netcfg_get_domain(struct debconfclient *client,  char **domain);

extern int netcfg_get_dhcp(struct debconfclient *client);

extern int netcfg_get_static(struct debconfclient *client);
extern int do_hostname_jig (struct debconfclient *client);

extern int netcfg_activate_dhcp(struct debconfclient *client);

extern int kill_dhcp_client(void);
extern int ask_dhcp_retry (struct debconfclient *client);
extern int netcfg_activate_static(struct debconfclient *client);

extern void netcfg_write_loopback (const char* prebaseconfig);
extern void netcfg_write_common (const char *prebaseconfig,
				 struct in_addr ipaddress, char *hostname,
				 char *domain);

void netcfg_nameservers_to_array(char *nameservers, struct in_addr array[]);

extern int is_wireless_iface (const char* iface);

extern int netcfg_wireless_set_essid (struct debconfclient *client, char* iface, char* priority);
extern int netcfg_wireless_set_wep (struct debconfclient *client, char* iface);

extern int iface_is_hotpluggable(const char *iface);
extern void deconfigure_network(void);

extern method_t mii_diag_status_lite (char *ifname);

extern void interface_up (char*);
extern void interface_down (char*);

extern void loop_setup(void);
extern void seed_hostname_from_dns(struct debconfclient *client, struct in_addr * ipaddress);

extern int inet_ptom (const char *src, int *dst, struct in_addr * addrp);
extern const char *inet_mtop (int src, char *dst, socklen_t cnt);
extern void parse_args (int argc, char** argv);

#endif /* _NETCFG_H_ */
