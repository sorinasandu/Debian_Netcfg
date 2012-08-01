/*
   netcfg.c - Configure a network via DHCP or manual configuration
   for debian-installer

   Copyright (C) 2000-2002  David Kimdon <dwhedon@debian.org>
                            and others (see debian/copyright)

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "netcfg.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <cdebconf/debconfclient.h>
#include <debian-installer.h>
#ifdef WIRELESS
#include <iwlib.h>
#endif

enum wpa_t wpa_supplicant_status;
static method_t netcfg_method = DHCP;

response_t netcfg_get_method(struct debconfclient *client)
{
    int iret, ret;

    iret = debconf_input(client, "medium", "netcfg/use_dhcp");
    ret = debconf_go(client);

    if (ret == CMD_GOBACK)
        return GO_BACK;

    debconf_get(client, "netcfg/use_dhcp");

    if (strcmp(client->value, "true") == 0)
        netcfg_method = DHCP;
    else
        netcfg_method = STATIC;

    if (iret == CMD_INPUTINVISIBLE)
        return NOT_ASKED;

    return 0;
}

int main(int argc, char *argv[])
{
    int num_interfaces = 0;
    enum { BACKUP,
           GET_INTERFACE,
           GET_HOSTNAME_ONLY,
           GET_METHOD,
           GET_DHCP,
           GET_STATIC,
           WCONFIG,
           WCONFIG_ESSID,
           WCONFIG_SECURITY_TYPE,
           WCONFIG_WEP,
           WCONFIG_WPA,
           START_WPA,
           QUIT } state = GET_INTERFACE;

    static struct debconfclient *client;
    static int requested_wireless_tools = 0;
    extern enum wpa_t wpa_supplicant_status;
    char **ifaces;
    char *defiface = NULL, *defwireless = NULL;
    response_t res;

    /* initialize libd-i */
    di_system_init("netcfg");

    if (strcmp(basename(argv[0]), "ptom") != 0)
        di_info("Starting netcfg v.%s (built %s)", NETCFG_VERSION, NETCFG_BUILD_DATE);

    parse_args (argc, argv);
    reap_old_files ();
    open_sockets();

    /* initialize debconf */
    client = debconfclient_new();
    debconf_capb(client, "backup");

    /* Check to see if netcfg should be run at all */
    debconf_get(client, "netcfg/enable");
    if (!strcmp(client->value, "false")) {
        struct in_addr null_ipaddress;
        char *hostname = NULL;

        null_ipaddress.s_addr = 0;
        netcfg_get_hostname(client, "netcfg/get_hostname", &hostname, 0);

        netcfg_write_common(null_ipaddress, hostname, NULL);
        return 0;
    }

    /* always always always default back to DHCP, unless you've specified
     * disable_dhcp on the command line. */
    debconf_get(client, "netcfg/disable_dhcp");

    if (!strcmp(client->value, "true"))
        debconf_set(client, "netcfg/use_dhcp", "false");
    else
        debconf_set(client, "netcfg/use_dhcp", "true");

    for (;;) {
        switch(state) {
        case BACKUP:
            di_info("State BACKUP");
            return 10;
        case GET_INTERFACE:
            /* If we have returned from outside of netcfg and want to
             * reconfigure networking, check to see if wpasupplicant is
             * running, and kill it if it is. If left running when
             * the interfaces are taken up and down, it appears to
             * leave it in an inconsistant state */
            di_info("State GET_INTERFACE");

            kill_wpa_supplicant();

            int ret = 0;

            /* Choose a default by looking for link */
            if (get_all_ifs(1, &ifaces) > 1) {
                while (*ifaces) {
                    if (check_kill_switch(*ifaces)) {
                        debconf_subst(client, "netcfg/kill_switch_enabled", "iface", *ifaces);
                        debconf_input(client, "high", "netcfg/kill_switch_enabled");
                        if (debconf_go(client) == CMD_GOBACK) {
                            state = BACKUP;
                            break;
                        }
                        /* Is it still enabled? */
                        if (check_kill_switch(*ifaces)) {
                            ifaces++;
                            continue;
                        }
                    }

                    interface_up(*ifaces);

                    ret = netcfg_detect_link (client, *ifaces);

                    if (ret == GO_BACK) {
                        state = GET_INTERFACE;
                        break;
                    }

                    if (ret == 1) /* CONNECTED */ {
                        di_info("found link on interface %s, making it the default.", *ifaces);
                        defiface = strdup(*ifaces);
                        break;
                    } else {
#ifdef WIRELESS
                        struct wireless_config wc;
#endif /* WIRELESS */
                        di_info("found no link on interface %s.", *ifaces);
#ifdef WIRELESS
                        if (iw_get_basic_config(wfd, *ifaces, &wc) == 0) {
                            wc.essid[0] = '\0';
                            wc.essid_on = 0;

                            iw_set_basic_config(wfd, *ifaces, &wc);

                            sleep(1);

                            iw_get_basic_config(wfd, *ifaces, &wc);

                            if (!empty_str(wc.essid)) {
                                di_info("%s is associated with %s. Selecting as default", *ifaces, wc.essid);
                                defiface = strdup(*ifaces);
                                interface_down(*ifaces);
                                break;
                            } else {
                                di_info("%s is not associated. Relegating to defwireless", *ifaces);
                                if (defwireless != NULL)
                                    free (defwireless);
                                defwireless = strdup(*ifaces);
                            }
                        }
                        else
                            di_info("%s is not a wireless interface. Continuing.", *ifaces);

                        interface_down(*ifaces);
#endif
                    }

                    interface_down(*ifaces);

                    ifaces++;
                }
            }

            if (state == BACKUP || ret == GO_BACK)
                break;

            if (!defiface && defwireless)
                defiface = defwireless;

            if(netcfg_get_interface(client, &interface, &num_interfaces, defiface))
                state = BACKUP;
            else if (! interface || ! num_interfaces)
                state = GET_HOSTNAME_ONLY;
            else {
                if (is_wireless_iface (interface))
                    state = WCONFIG;
                else
                    state = GET_METHOD;
            }
            break;
        case GET_HOSTNAME_ONLY:
            di_info("State GET_HOSTNAME_ONLY");

            if(netcfg_get_hostname(client, "netcfg/get_hostname", &hostname, 0))
                state = BACKUP;
            else {
                struct in_addr null_ipaddress;
                null_ipaddress.s_addr = 0;
                netcfg_write_common(null_ipaddress, hostname, NULL);
                state = QUIT;
            }
            break;
        case GET_METHOD:
            di_info("State GET_METHOD");

            if ((res = netcfg_get_method(client)) == GO_BACK)
                state = (num_interfaces == 1) ? BACKUP : GET_INTERFACE;
            else {
                if (netcfg_method == DHCP)
                    state = GET_DHCP;
                else
                    state = GET_STATIC;
            }
            break;

        case GET_DHCP:
            di_info("State GET_DHCP");

            switch (netcfg_activate_dhcp(client)) {
            case 0:
                state = QUIT;
                break;
            case 10:
                /*
                 * It doesn't make sense to go back to GET_METHOD because
                 * the user has already been asked whether he wants to
                 * try an alternate method.
                 */
                state = (num_interfaces == 1) ? BACKUP : GET_INTERFACE;
                break;
            case 15:
                di_info("Tried DHCP, now go to static. I'm not so sure about"
                        "that :)");
                state = GET_STATIC;
                break;
            default:
                return 1;
            }
            break;

        case GET_STATIC:
            {
                di_info("State GET_STATIC");

                int ret;
                /* Misnomer - this should actually take care of activation */
                di_info("Get static from netcfg, with essid %s\n", essid);
                if ((ret = netcfg_get_static(client)) == 10)
                    state = GET_INTERFACE;
                else if (ret)
                    state = GET_METHOD;
                else
                    state = QUIT;
                break;
            }

        case WCONFIG:
            di_info("State WCONFIG");

            if (requested_wireless_tools == 0) {
                di_exec_shell_log("apt-install wireless-tools");
                requested_wireless_tools = 1;
            }
            state = WCONFIG_ESSID;
            break;

        case WCONFIG_ESSID:
            di_info("State WCONFIG_ESSID");

            if (netcfg_wireless_set_essid(client, interface) == GO_BACK)
                state = BACKUP;
            else {
                init_wpa_supplicant_support();
                if (wpa_supplicant_status == WPA_UNAVAIL)
                    state = WCONFIG_WEP;
                else
                    state = WCONFIG_SECURITY_TYPE;
            }
            break;

        case WCONFIG_SECURITY_TYPE:
            {
                di_info("State WCONFIG_SECURITY_TYPE");

                int ret;
                ret = wireless_security_type(client, interface);
                if (ret == GO_BACK)
                    state = WCONFIG_ESSID;
                else if (ret == REPLY_WPA)
                    state = WCONFIG_WPA;
                else
                    state = WCONFIG_WEP;
                break;
            }

        case WCONFIG_WEP:
            di_info("State WCONFIG_WEP");

            if (netcfg_wireless_set_wep(client, interface) == GO_BACK)
                if (wpa_supplicant_status == WPA_UNAVAIL)
                    state = WCONFIG_ESSID;
                else
                    state = WCONFIG_SECURITY_TYPE;
            else {
                di_info("Configured WEP, now get method\n");
                state = GET_METHOD;
            }
            break;

        case WCONFIG_WPA:
            di_info("State WCONFIG_WPA");

            if (wpa_supplicant_status == WPA_OK) {
                di_exec_shell_log("apt-install wpasupplicant");
                wpa_supplicant_status = WPA_QUEUED;
            }

            if (netcfg_set_passphrase(client, interface) == GO_BACK)
                state = WCONFIG_SECURITY_TYPE;
            else
                state = START_WPA;
            break;

        case START_WPA:
            di_info("State START_WPA");

            if (wpa_supplicant_start(client, interface, essid, passphrase) == GO_BACK)
                state = WCONFIG_ESSID;
            else
                state = GET_METHOD;
            break;

        case QUIT:
            di_info("State QUIT");

            netcfg_update_entropy();
            di_info("Job done here, netcfg quiting");
            return 0;
        }
    }

    di_info("Netcfg is now going to sleep, somehow left the loop");
}
