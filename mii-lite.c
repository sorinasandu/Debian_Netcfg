/* This contains large amounts of code from Donald Becker's mii-diag.c.
 * The copyright for that file is as follows:
 *
 *      Written/copyright 1997-2002 by Donald Becker <becker@scyld.com>
 *
 *      This program is free software; you can redistribute it
 *      and/or modify it under the terms of the GNU General Public
 *      License as published by the Free Software Foundation.
 *
 *      The author may be reached as becker@scyld.com, or C/O
 *       Scyld Computing Corporation
 *       410 Severn Ave., Suite 210
 *       Annapolis MD 21403
 *
 * The excerpt and the corresponding glue made to make it work with netcfg
 * is (C) 2004 Joshua Kwan <joshk@triplehelix.org>, and by consequence is
 * also under the GPL.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "netcfg.h"

typedef unsigned short u16;

int skfd = -1;			/* AF_INET socket for ioctl() calls.	*/
struct ifreq ifr;
int new_ioctl_nums;

inline int mdio_read(int skfd, int phy_id, int location);

method_t
mii_diag_status_lite (char *ifname)
{
  u16 *data = NULL;

  /* Open a basic socket. */
  if ((skfd = socket(AF_INET, SOCK_DGRAM,0)) < 0)
    return STATIC;

  /* Verify that the interface supports the ioctl(), and if
     it is using the new or old SIOCGMIIPHY value (grrr...).
     */

  data = (u16 *)(&ifr.ifr_data);

  strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
  data[0] = 0;

  if (ioctl(skfd, 0x8947, &ifr) >= 0) {
    new_ioctl_nums = 1;
  } else if (ioctl(skfd, SIOCDEVPRIVATE, &ifr) >= 0) {
    new_ioctl_nums = 0;
  } else {
    close(skfd);
    return STATIC;
  }

  if ((mdio_read(skfd, (unsigned)data[0], 1) & 0x0004) == 0)
    return STATIC;
  else
  {
    close(skfd);
    return DHCP;
  }
}

inline int mdio_read(int skfd, int phy_id, int location)
{
	u16 *data = (u16 *)(&ifr.ifr_data);

	data[0] = phy_id;
	data[1] = location;

	if (ioctl(skfd, new_ioctl_nums ? 0x8948 : SIOCDEVPRIVATE+1, &ifr) < 0) {
		fprintf(stderr, "SIOCGMIIREG on %s failed: %s\n", ifr.ifr_name,
				strerror(errno));
		return -1;
	}
	return data[3];
}
