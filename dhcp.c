/*
 * DHCP module for netcfg/netcfg-dhcp.
 *
 * Licensed under the terms of the GNU General Public License
 */

#include "netcfg.h"
#include <stdlib.h>
#include <unistd.h>
#include <debian-installer.h>
#include <stdio.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <time.h>
#include <netdb.h>

int dhcp_running = 0, dhcp_exit_status = 1;

/* Signal handler for DHCP client child */
static void dhcp_client_sigchld(int sig __attribute__ ((unused))) 
{
    if (dhcp_running == 1) {
	dhcp_running = 0;
	wait(&dhcp_exit_status);
        dhcp_pid = -1;
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

/* 
 * This function will start whichever DHCP client is available (if
 * necessary). That's all. The meat of the DHCP code is really in
 * poll_dhcp_client.
 *
 * Its PID will be left in dhcp_pid.
 * If the client is not running, dhcp_pid will always get set to -1.
 */

int start_dhcp_client (struct debconfclient *client, char* dhostname)
{
  char buf[128];
  FILE *dc = NULL;
  dhclient_t dhcp_client;

  if (access("/var/lib/dhcp3", F_OK) == 0)
    dhcp_client = DHCLIENT3;
  else if (access("/sbin/dhclient", F_OK) == 0)
    dhcp_client = DHCLIENT;
  else if (access("/sbin/pump", F_OK) == 0)
    dhcp_client = PUMP;
  else if (access("/sbin/udhcpc", F_OK) == 0)
    dhcp_client = UDHCPC;
  else {
    debconf_input(client, "critical", "netcfg/no_dhcp_client");
    debconf_go(client);
    exit(1);
  }

  /* get dhcp lease */
  switch (dhcp_client) {
    case PUMP:
      if (dhostname)
        snprintf(buf, sizeof(buf), "pump -i %s -h %s", interface, dhostname);
      else
        snprintf(buf, sizeof(buf), "pump -i %s", interface);

      break;

    case DHCLIENT:
      /* First, set up dhclient.conf if necessary */

      if (dhostname)
      {
        if ((dc = file_open(DHCLIENT_CONF, "w")))
        {
          fprintf(dc, "send host-name %s\n", hostname);
          fclose(dc);
        }
      }
      
      snprintf(buf, sizeof(buf), "dhclient -e %s", interface);
      break;

    case DHCLIENT3:
      /* Different place.. */

      if (dhostname)
      {
        if ((dc = file_open(DHCLIENT3_CONF, "w")))
        {
          fprintf(dc, "send host-name %s\n", hostname);
          fclose(dc);
        }
      }
      
      snprintf(buf, sizeof(buf), "dhclient %s", interface);
      break;

    case UDHCPC:
      if (dhostname)
        snprintf(buf, sizeof(buf), "udhcpc -i %s -n -H %s", interface, hostname);
      else
        snprintf(buf, sizeof(buf), "udhcpc -i %s -n", interface);
      
      break;
  }

  if ((dhcp_pid = fork()) == 0) /* child */
  {
    int ret;

    /* these guys log to syslog already */
    if (dhcp_client == DHCLIENT || dhcp_client == DHCLIENT3)
      ret = di_exec_shell(buf);
    else
      ret = di_exec_shell_log(buf);

    _exit(!!ret);
  }
  else if (dhcp_pid == -1)
    return 1;
  else
  {
    dhcp_running = 1;
    signal(SIGCHLD, &dhcp_client_sigchld);
    return 0;
  }
}

/* Poll the started DHCP cilent for ten seconds, and return 0 if a lease was
 * acquired, 1 otherwise. The client should die once a lease is acquired.
 *
 * It will NOT reap the DHCP client after an unsuccessful poll. 
 */

int poll_dhcp_client (struct debconfclient *client)
{
  time_t start_time, now;

  /* show progress bar */
  debconf_progress_start(client, 0, DHCP_SECONDS, "netcfg/dhcp_progress");
  debconf_progress_info(client, "netcfg/dhcp_progress_note");
  netcfg_progress_displayed = 1;

  now = start_time = time(NULL);

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
  if (!dhcp_running && (dhcp_exit_status == 0))
  {
    assert(hostname != NULL);
    assert(dhcp_pid == -1);

    /* write configuration */
    netcfg_write_common("40netcfg", ipaddress, hostname, domain);
    netcfg_write_dhcp("40netcfg", interface);

    return 0;
  }
  
  return 1;
}

int ask_dhcp_retry (struct debconfclient *client)
{
  int ret;
  
  /* critical, we don't want to enter a loop */
  debconf_input(client, "critical", "netcfg/dhcp_retry");
  ret = debconf_go(client);

  if (ret == 30)
    return GO_BACK;
  
  debconf_get(client, "netcfg/dhcp_retry");

    /* strcmp sucks */
  if (client->value[0] == 'R')
  {
    /* with DHCP hostnam_e_ */
    if (client->value[strlen(client->value) - 1] == 'e')
      return 1;
    else
      return 0;
  }
  else if (client->value[0] == 'C')
    return 2; /* manual */
  else
    return 3; /* no config */
}

/* Here comes another Satan machine. */
int netcfg_activate_dhcp (struct debconfclient *client)
{
  enum { START, POLL, DHCP_HOSTNAME, HOSTNAME, STATIC, END } state = START;
  
  loop_setup();

  for (;;)
  {
    switch (state)
    {
      case START:
        if (start_dhcp_client(client, NULL))
          netcfg_die(client); /* change later */
        else
          state = POLL;
        break;

      case DHCP_HOSTNAME:
        break;

      case POLL:
        if (poll_dhcp_client(client)) /* could not get a lease */
        {
          /* ooh, now it's a select */
          switch (ask_dhcp_retry (client))
          {
            case GO_BACK: exit(10); /* XXX */
            case 0: state = POLL; break;
            case 1: state = DHCP_HOSTNAME; break;
            case 2: state = STATIC; break;
            case 3: /* no net config at this time :( */
              kill_dhcp_client();
              exit(0);
          }
        }
        else
        {
          char buf[MAXHOSTNAMELEN + 1];

          /* dhcp hostname, ask for one with the dhcp hostname
           * as a seed */
          if (gethostname(buf, sizeof(buf)) == 0)
            debconf_set(client, "netcfg/get_hostname", buf);
          else
            seed_hostname_from_dns(client);

          state = HOSTNAME;
        }
        break;

      case HOSTNAME:
        if (netcfg_get_hostname (client, "netcfg/get_hostname", &hostname))
          exit(10); /* go back */
        else
          state = END;
        break;

      case STATIC:
        break;
        
      case END:
        return 0;
    }
  }
} 
