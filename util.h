#ifndef __UTIL_H
#define __UTIL_H
#define MAXLINE 1024



/*
 * Get a description for an interface (i.e. "Ethernet" for ethX).
 */
extern char *get_ifdsc (const char *ifp);


/* Checks to see if any network interfaces are active (excepting the
 * loopback interface "lo" - Returns the number of active interfaces. If
 * all is non-zero, all interfaces are counted, even if not up */
int is_network_up (int all);

/* Checks to see if the named interface, "inter", is up - Returns 1 if it
 * is, 0 if it is down. -1 is returned if the interface does not exist. */
int is_interface_up (char *inter);

/* Takes the buffer "p" and reads the interface name from it. "p" must be
 * a full line read in from /proc/net/dev. The interface name is places
 * into the "name" buffer (which must already be allocated) */
void get_name (char *name, char *p);

/* Get listing of interfaces on the system. getif_start() must be called
 * first in order to initialize the setup. After which, each successive
 * call get getif() will return a pointer to the next interface, ending
 * with NULL, when no more interfaces are available. If you call getif and
 * all is non-zero, it will return all interfaces, even ones that are not
 * active. Finally when done, call getif_end().
 */
void getif_start (void);
char *getif (int all);
void getif_end (void);


#endif /* __UTIL_H */
