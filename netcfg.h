#ifndef _NETCFG_H_
#define _NETCFG_H_
#include <sys/types.h>

#define ETC_DIR 	"/etc"
#define NETWORK_DIR 	"/etc/network"
#define DHCPCD_DIR 	"/etc/dhcpc"
#define INTERFACES_FILE "/etc/network/interfaces"
#define HOSTS_FILE      "/etc/hosts"
#define NETWORKS_FILE   "/etc/networks"
#define RESOLV_FILE     "/etc/resolv.conf"
#define DHCPCD_FILE     "/etc/dhcpc/config"
#define DHCLIENT_FILE	"/etc/dhclient.conf"
#define DHCLIENT_DIR	"/var/dhcp"


extern int netcfg_mkdir (char *path);

extern int is_interface_up (char *inter);

extern void get_name (char *name, char *p);

extern void getif_start ();

extern void getif_end ();

extern char *get_ifdsc (const char *ifp);

extern FILE *file_open (char *path);

extern void dot2num (u_int32_t * num, char *dot);

extern char *num2dot (u_int32_t num);

extern void netcfg_die (struct debconfclient *client);

extern void netcfg_get_common (struct debconfclient *client, char **interface,
			       char **hostname, char **domain,
			       char **nameservers);

extern void netcfg_write_common (u_int32_t ipaddress, char *domain,
				 char *hostname, u_int32_t nameservers[]);

void netcfg_nameservers_to_array(char *nameservers, u_int32_t array[]);

#endif /* _NETCFG_H_ */
