#include "netcfg.h"
#include <stdlib.h>
#include <unistd.h>
#include <debian-installer.h>
#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>
#include <time.h>

/* Set if DHCP client exits */
volatile int dhcp_running = 0; /* not running */
int dhcp_exit_status = -1; /* failed */

/* Signal handler for DHCP client child */
static void dhcp_client_sigchld(int sig __attribute__ ((unused))) 
{
    if (dhcp_running == 1) {
	dhcp_running = 0;
	wait(&dhcp_exit_status);
    } 
}

static void netcfg_write_dhcp (char* prebaseconfig, char *iface)
{
    FILE *fp;

    if ((fp = file_open(INTERFACES_FILE, "a"))) {
        fprintf(fp,
                "\n# This entry was created during the Debian installation\n");
        if (!iface_is_hotpluggable(iface))
            fprintf(fp, "auto %s\n", iface);
        fprintf(fp, "iface %s inet dhcp\n", iface);
	if (is_wireless_iface(iface))
	{
	  fprintf(fp, "\twireless_mode %s\n",
	      (mode == MANAGED) ? "managed" : "adhoc");
	  fprintf(fp, "\twireless_essid %s\n", essid ? essid : "any");
	  if (wepkey != NULL)
	    fprintf(fp, "\twireless_key %s\n", wepkey);
	}
        fclose(fp);
    }

    if ((fp = file_open(RESOLV_FILE, "a"))) {
      fclose(fp);
    }
    
    di_system_prebaseconfig_append(prebaseconfig, "cp %s %s\n", RESOLV_FILE,
	"/target" RESOLV_FILE);
}

#define DHCP_SECONDS 15

int netcfg_activate_dhcp(struct debconfclient *client)
{
    char buf[128];
    struct stat stat_buf;
    time_t start_time, now;
    pid_t pid = 0;
    int retry = 1;
    char *ptr;
    FILE *dc = NULL;

    enum { PUMP, DHCLIENT, DHCLIENT3, UDHCPC } dhcp_client;

    if (stat("/var/lib/dhcp3", &stat_buf) == 0)
        dhcp_client = DHCLIENT3;
    if (stat("/sbin/dhclient", &stat_buf) == 0)
        dhcp_client = DHCLIENT;
    else if (stat("/sbin/udhcpc", &stat_buf) == 0)
        dhcp_client = UDHCPC;
    else if (stat("/sbin/pump", &stat_buf) == 0)
        dhcp_client = PUMP;
    else {
        debconf_input(client, "critical", "netcfg/no_dhcp_client");
        debconf_go(client);
        exit(1);
    }

    deconfigure_network();

    /* setup loopback */
    di_exec_shell_log("ifconfig lo 127.0.0.1");

    /* load kernel module for network sockets silently */
    di_exec_shell("modprobe af_packet");

    /* get dhcp lease */
    switch (dhcp_client) {
    case PUMP:
        snprintf(buf, sizeof(buf), "pump -i %s -h %s", interface, hostname);
        break;

    case DHCLIENT:
	/* First, set up dhclient.conf */
	if ((dc = file_open(DHCLIENT_CONF, "w")))
	{
	  fprintf(dc, "send host-name %s\n", hostname);
	  fclose(dc);
	}
        snprintf(buf, sizeof(buf), "dhclient -e %s", interface);
        break;

    case DHCLIENT3:
	/* Different place.. */
	if ((dc = file_open(DHCLIENT3_CONF, "w")))
	{
	  fprintf(dc, "send host-name %s\n", hostname);
	  fclose(dc);
	}
	snprintf(buf, sizeof(buf), "dhclient %s", interface);
	break;

    case UDHCPC:
        snprintf(buf, sizeof(buf), "udhcpc -i %s -n -H %s", hostname, interface);
        break;
    }

    while (retry == 1) {
        /* show progress bar */
        debconf_progress_start(client, 0, DHCP_SECONDS, "netcfg/dhcp_progress");
        debconf_progress_info(client, "netcfg/dhcp_progress_note");
        netcfg_progress_displayed = 1;

        now = start_time = time(NULL);
        if (! (dhcp_running || (dhcp_exit_status == 0))) {
            if ((pid = fork()) == 0) {
                int ret = di_exec_shell_log(buf);
                ((WIFEXITED(ret) && (WEXITSTATUS(ret) != 0)) || WIFSIGNALED(ret)) ?
                    _exit(EXIT_FAILURE) : _exit(EXIT_SUCCESS);
            }
            if (pid)
                dhcp_running = 1;
            else
                return 1;
            signal(SIGCHLD, &dhcp_client_sigchld);
        }

        /* wait 10s for a DHCP lease */
        while (dhcp_running && ((now - start_time) < DHCP_SECONDS)) {
            sleep(1);
            debconf_progress_step(client, 1);
            now = time(NULL);
        }

        /* stop progress bar */
        debconf_progress_stop(client);
        netcfg_progress_displayed = 0;

        /* got a lease? */
        if (!dhcp_running && (dhcp_exit_status == 0)) {
	    assert(hostname != NULL);

            /* write configuration */
            netcfg_write_common("40netcfg", ipaddress, hostname, domain);
            netcfg_write_dhcp("40netcfg", interface);

            return 0;
        }

        /* ask if user wants to retry */
        if (my_debconf_input(client, "high", "netcfg/dhcp_retry", &ptr) == 30) {
            if (dhcp_running) {
                kill(pid, SIGTERM);
            }
	    return 30; /* backup */
	}
        retry = strstr(ptr, "true") ? 1 : 0;
    }

    if (dhcp_running) {
        kill(pid, SIGTERM);
    }
    return 1;
}
