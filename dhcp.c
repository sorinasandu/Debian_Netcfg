/*
 * DHCP module for netcfg/netcfg-dhcp.
 *
 * Licensed under the terms of the GNU General Public License
 */

#include "netcfg.h"
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <debian-installer.h>
#include <stdio.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <time.h>
#include <netdb.h>

int dhcp_running = 0, dhcp_exit_status = 1;
pid_t dhcp_pid = -1;

/* Signal handler for DHCP client child */
static void dhcp_client_sigchld(int sig __attribute__ ((unused))) 
{
    if (dhcp_running == 1) {
	wait(&dhcp_exit_status);
	dhcp_running = 0;
    }
}

static void netcfg_write_dhcp (char *iface)
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
	  fprintf(fp, "\t# wireless-* options are implemented by the wireless-tools package\n");
	  fprintf(fp, "\twireless-mode %s\n",
	      (mode == MANAGED) ? "managed" : "adhoc");
	  fprintf(fp, "\twireless-essid %s\n", essid ? essid : "any");
	  if (wepkey != NULL)
	    fprintf(fp, "\twireless-key %s\n", wepkey);
	}
        fclose(fp);
    }

    if ((fp = file_open(RESOLV_FILE, "a"))) {
      fclose(fp);
    }
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
  FILE *dc = NULL;
  enum { DHCLIENT, DHCLIENT3, PUMP } dhcp_client;

  if (access("/var/lib/dhcp3", F_OK) == 0)
    dhcp_client = DHCLIENT3;
  else if (access("/sbin/dhclient", F_OK) == 0)
    dhcp_client = DHCLIENT;
  else if (access("/sbin/pump", F_OK) == 0)
    dhcp_client = PUMP;
  else {
    debconf_input(client, "critical", "netcfg/no_dhcp_client");
    debconf_go(client);
    exit(1);
  }

  if ((dhcp_pid = fork()) == 0) /* child */
  {
    /* get dhcp lease */
    switch (dhcp_client)
    {
      case PUMP:
	if (dhostname)
	  execlp("pump", "pump", "-i", interface, "-h", dhostname, NULL);
	else
	  execlp("pump", "pump", "-i", interface, NULL);

	break;

      case DHCLIENT:
	/* First, set up dhclient.conf if necessary */

	if (dhostname)
	{
	  if ((dc = file_open(DHCLIENT_CONF, "w")))
	  {
	    fprintf(dc, "send host-name \"%s\";\n", dhostname);
	    fclose(dc);
	  }
	}

	execlp("dhclient", "dhclient", "-e", interface, NULL);
	break;

      case DHCLIENT3:
	/* Different place.. */

	if (dhostname)
	{
	  if ((dc = file_open(DHCLIENT3_CONF, "w")))
	  {
	    fprintf(dc, "send host-name \"%s\";\n", dhostname);
	    fclose(dc);
	  }
	}

	execlp("dhclient", "dhclient", "-1", interface, NULL);
	break;
    }
    if (errno != 0)
      di_error("Could not exec dhcp client: %s", strerror(errno));

    return 1; /* should NEVER EVER get here */
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

/*
 * Poll the started DHCP client for DHCP_SECONDS seconds
 * and return 0 if a lease is known to have been acquired,
 * 1 otherwise.
 *
 * The client should be run such that it dies once a lease is acquired.
 *
 * This function will NOT reap the DHCP client after an unsuccessful poll. 
 */

int poll_dhcp_client (struct debconfclient *client)
{
  time_t start_time, now;
  int ret = 1;

  /* show progress bar */
  debconf_progress_start(client, 0, DHCP_SECONDS, "netcfg/dhcp_progress");
  debconf_progress_info(client, "netcfg/dhcp_progress_note");
  netcfg_progress_displayed = 1;

  now = start_time = time(NULL);

  /* wait DHCP_SECONDS seconds for a DHCP lease */
  while (dhcp_running && ((now - start_time) < DHCP_SECONDS)) {
    sleep(1);
    debconf_progress_step(client, 1);
    now = time(NULL);
  }

  /* got a lease? display a success message */
  if (!dhcp_running && (dhcp_exit_status == 0))
  {
    dhcp_pid = -1;
    ret = 0;

    debconf_progress_set(client, DHCP_SECONDS);
    debconf_progress_info(client, "netcfg/dhcp_success_note");
    sleep(2);
  }
  
  /* stop progress bar */
  debconf_progress_stop(client);
  netcfg_progress_displayed = 0;
  
  return ret;
}

#define REPLY_RETRY_AUTOCONFIG       0
#define REPLY_RETRY_WITH_HOSTNAME    1
#define REPLY_CONFIGURE_MANUALLY     2
#define REPLY_DONT_CONFIGURE         3
#define REPLY_RECONFIGURE_WIFI       4
#define REPLY_LOOP_BACK              5

int ask_dhcp_retry (struct debconfclient *client)
{
  int ret;
  
  if (is_wireless_iface(interface))
  {
    debconf_metaget(client, "netcfg/internal-wifireconf", "description");
    debconf_subst(client, "netcfg/dhcp_retry", "wifireconf", client->value);
  }
  else /* blank from last time */
    debconf_subst(client, "netcfg/dhcp_retry", "wifireconf", "");

  /* critical, we don't want to enter a loop */
  debconf_input(client, "critical", "netcfg/dhcp_retry");
  ret = debconf_go(client);

  if (ret == 30)
    return GO_BACK;
  
  debconf_get(client, "netcfg/dhcp_retry");

    /* strcmp sucks */
  if (client->value[0] == 'R') /* _R_etry ... or _R_econfigure ... */
  {
    size_t len = strlen(client->value);
    if (client->value[len - 1] == 'e') /* ... with DHCP hostnam_e_ */
      return REPLY_RETRY_WITH_HOSTNAME;
    else if (client->value[len - 1] == 'k') /* ... wireless networ_k_ */
      return REPLY_RECONFIGURE_WIFI;
    else
      return REPLY_RETRY_AUTOCONFIG;
  }
  else if (client->value[0] == 'C') /* _C_onfigure ... */
    return REPLY_CONFIGURE_MANUALLY;
  else if (empty_str(client->value))
    return REPLY_LOOP_BACK;
  else
    return REPLY_DONT_CONFIGURE;
}

/* Here comes another Satan machine. */
int netcfg_activate_dhcp (struct debconfclient *client)
{
  char* dhostname = NULL;
  enum { START, ASK_RETRY, POLL, DHCP_HOSTNAME, HOSTNAME, DOMAIN, STATIC, END } state = START;
  short quit_after_hostname = 0;

  kill_dhcp_client();
  loop_setup();

  for (;;)
  {
    switch (state)
    {
      case START:
        if (start_dhcp_client(client, dhostname))
          netcfg_die(client); /* change later */
        else
          state = POLL;
        break;

      case DHCP_HOSTNAME:
        if (netcfg_get_hostname(client, "netcfg/dhcp_hostname", &dhostname, 0))
          state = ASK_RETRY;
        else
        {
          if (empty_str(dhostname))
          {
            free(dhostname);
            dhostname = NULL;
          }
          kill_dhcp_client();
          state = START;
        }
        break;

      case ASK_RETRY:
	quit_after_hostname = 0;
        /* ooh, now it's a select */
        switch (ask_dhcp_retry (client))
        {
          case GO_BACK: kill_dhcp_client(); exit(10); /* XXX */
          case REPLY_RETRY_AUTOCONFIG: state = POLL; break;
          case REPLY_RETRY_WITH_HOSTNAME: state = DHCP_HOSTNAME; break;
          case REPLY_CONFIGURE_MANUALLY: state = STATIC; break;
          case REPLY_DONT_CONFIGURE:
                  kill_dhcp_client();
		  netcfg_write_loopback();
                  quit_after_hostname = 1;
		  state = HOSTNAME;
		  break;
	  case REPLY_RECONFIGURE_WIFI:
          {
	    /* oh god - a NESTED satan machine */
 	    enum { ABORT, DONE, ESSID, WEP } wifistate = ESSID;

	    for (;;)
	    {
	      switch (wifistate)
	      {
		case ESSID:
		  wifistate = ( netcfg_wireless_set_essid(client, interface, "high") == GO_BACK ) ?
		    ABORT : WEP;
		  break;

		case WEP:
		  wifistate = ( netcfg_wireless_set_wep (client, interface) == GO_BACK ) ?
		    ESSID : DONE;
		  break;

		case ABORT:
		  state = ASK_RETRY;
		  break;

		case DONE:
		  state = POLL;
		  break;
	      }

	      if (wifistate == DONE || wifistate == ABORT)
		break;
	    }
	  }
        }
        break;

      case POLL:
        if (poll_dhcp_client(client)) /* could not get a lease */
          state = ASK_RETRY;
        else
        {
          char buf[MAXHOSTNAMELEN + 1] = { 0 };
          char *ptr = NULL;
          FILE *d = NULL;

          have_domain = 0;
          
          if ((d = fopen(DOMAIN_FILE, "r")) != NULL)
          {
            char domain[_UTSNAME_LENGTH + 1] = { 0 };
            fgets(domain, _UTSNAME_LENGTH, d);
            fclose(d);
            unlink(DOMAIN_FILE);

            /* Seed the domain. We will prefer the domain name passed
             * by the DHCP server if there is one. */
            if (!empty_str(domain))
            {
              debconf_set(client, "netcfg/get_domain", domain);
              have_domain = 1;
            }
          }
          
          /* dhcp hostname, ask for one with the dhcp hostname
           * as a seed */
          if (gethostname(buf, sizeof(buf)) == 0 && !empty_str(buf)
                && strcmp(buf, "(none)") != 0)
          {
            di_info("DHCP hostname: \"%s\"", buf);
            debconf_set(client, "netcfg/get_hostname", buf);
          }
          else
          {
            struct ifreq ifr;
            struct in_addr d_ipaddr = { 0 };

            ifr.ifr_addr.sa_family = AF_INET;
            strncpy(ifr.ifr_name, interface, IFNAMSIZ);
            if (ioctl(skfd, SIOCGIFADDR, &ifr) == 0)
            {
              d_ipaddr = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
              seed_hostname_from_dns(client, &d_ipaddr);
            }
            else
              di_error("ioctl failed (%s)", strerror(errno));
          }

          if (have_domain == 0 && (ptr = strchr(buf, '.')) != NULL)
          {
            debconf_set(client, "netcfg/get_domain", ptr + 1);
            have_domain = 1;
          }
          
          state = HOSTNAME;
        }
        break;

      case HOSTNAME:
        if (netcfg_get_hostname (client, "netcfg/get_hostname", &hostname, 1))
	{
	  kill_dhcp_client();
	  if (quit_after_hostname) /* go back to retry */
	    state = ASK_RETRY;
	  else
            exit(10); /* go back, going back to poll isn't intuitive */
	}
        else
        {
          /* If we didn't send a DHCP hostname before, use the one specified */
          if (!dhostname)
          {
            FILE* dc = NULL;
            if ((dc = file_open(DHCLIENT_CONF, "w")))
            {
              fprintf(dc, "send host-name \"%s\";\n", hostname);
              fclose(dc);
            }
            /* And the prebaseconfig will take care of copying it in. */
          }
      
          state = DOMAIN;
        }
        break;

      case DOMAIN:
	if (!have_domain && netcfg_get_domain (client, &domain))
	  state = HOSTNAME;
	else if (quit_after_hostname)
	{
	  netcfg_write_common(ipaddress, hostname, domain);
	  exit(0);
	}
	else
	  state = END;
	break;

      case STATIC:
	kill_dhcp_client();
        return 15;
        break;
        
      case END:
        /* write configuration */
        netcfg_write_common(ipaddress, hostname, domain);
        netcfg_write_dhcp(interface);
        
        return 0;
    }
  }
} 

int kill_dhcp_client(void)
{
  system("killall.sh"); 
  return 0;
}
