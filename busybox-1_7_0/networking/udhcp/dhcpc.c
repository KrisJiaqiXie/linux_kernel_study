/* vi: set sw=4 ts=4: */
/* dhcpc.c
 *
 * udhcp DHCP client
 *
 * Russ Dill <Russ.Dill@asu.edu> July 2001
 *
 * Licensed under the GPL v2 or later, see the file LICENSE in this tarball.
 */

#include <getopt.h>
#include <syslog.h>

/* Override ENABLE_FEATURE_PIDFILE - ifupdown needs our pidfile to always exist */
#define WANT_PIDFILE 1
#include "common.h"
#include "dhcpd.h"
#include "dhcpc.h"
#include "options.h"


/* Something is definitely wrong here. IPv4 addresses
 * in variables of type long?? BTW, we use inet_ntoa()
 * in the code. Manpage says that struct in_addr has a member of type long (!)
 * which holds IPv4 address, and the struct is passed by value (!!)
 */
static unsigned timeout;
static uint32_t requested_ip; /* = 0 */
static uint32_t server_addr;
static int packet_num; /* = 0 */
static int sockfd = -1;

#define LISTEN_NONE 0
#define LISTEN_KERNEL 1
#define LISTEN_RAW 2
static smallint listen_mode;

static smallint state;

struct client_config_t client_config;


/* just a little helper */
static void change_mode(int new_mode)
{
	DEBUG("entering %s listen mode",
		new_mode ? (new_mode == 1 ? "kernel" : "raw") : "none");
	if (sockfd >= 0) {
		close(sockfd);
		sockfd = -1;
	}
	listen_mode = new_mode;
}


/* perform a renew */
static void perform_renew(void)
{
	bb_info_msg("Performing a DHCP renew");
	switch (state) {
	case BOUND:
		change_mode(LISTEN_KERNEL);
	case RENEWING:
	case REBINDING:
		state = RENEW_REQUESTED;
		break;
	case RENEW_REQUESTED: /* impatient are we? fine, square 1 */
		udhcp_run_script(NULL, "deconfig");
	case REQUESTING:
	case RELEASED:
		change_mode(LISTEN_RAW);
		state = INIT_SELECTING;
		break;
	case INIT_SELECTING:
		break;
	}

	/* start things over */
	packet_num = 0;

	/* Kill any timeouts because the user wants this to hurry along */
	timeout = 0;
}


/* perform a release */
static void perform_release(void)
{
	char buffer[sizeof("255.255.255.255")];
	struct in_addr temp_addr;

	/* send release packet */
	if (state == BOUND || state == RENEWING || state == REBINDING) {
		temp_addr.s_addr = server_addr;
		strcpy(buffer, inet_ntoa(temp_addr));
		temp_addr.s_addr = requested_ip;
		bb_info_msg("Unicasting a release of %s to %s",
				inet_ntoa(temp_addr), buffer);
		send_release(server_addr, requested_ip); /* unicast */
		udhcp_run_script(NULL, "deconfig");
	}
	bb_info_msg("Entering released state");

	change_mode(LISTEN_NONE);
	state = RELEASED;
	timeout = INT_MAX;
}


static void client_background(void)
{
#if !BB_MMU
	bb_error_msg("cannot background in uclinux (yet)");
/* ... mainly because udhcpc calls client_background()
 * in _the _middle _of _udhcpc _run_, not at the start!
 * If that will be properly disabled for NOMMU, client_background()
 * will work on NOMMU too */
#else
	bb_daemonize(0);
	logmode &= ~LOGMODE_STDIO;
	/* rewrite pidfile, as our pid is different now */
	write_pidfile(client_config.pidfile);
#endif
	/* Do not fork again. */
	client_config.foreground = 1;
	client_config.background_if_no_lease = 0;
}


static uint8_t* alloc_dhcp_option(int code, const char *str, int extra)
{
	uint8_t *storage;
	int len = strlen(str);
	if (len > 255) len = 255;
	storage = xzalloc(len + extra + OPT_DATA);
	storage[OPT_CODE] = code;
	storage[OPT_LEN] = len + extra;
	memcpy(storage + extra + OPT_DATA, str, len);
	return storage;
}


int udhcpc_main(int argc, char **argv);
int udhcpc_main(int argc, char **argv)
{
	uint8_t *temp, *message;
	char *str_c, *str_V, *str_h, *str_F, *str_r, *str_T, *str_t;
	uint32_t xid = 0;
	uint32_t lease = 0; /* can be given as 32-bit quantity */
	unsigned t1 = 0, t2 = 0; /* what a wonderful names */
	unsigned start = 0;
	unsigned now;
	unsigned opt;
	int max_fd;
	int retval;
	int len;
	struct timeval tv;
	struct in_addr temp_addr;
	struct dhcpMessage packet;
	fd_set rfds;

	enum {
		OPT_c = 1 << 0,
		OPT_C = 1 << 1,
		OPT_V = 1 << 2,
		OPT_f = 1 << 3,
		OPT_b = 1 << 4,
		OPT_H = 1 << 5,
		OPT_h = 1 << 6,
		OPT_F = 1 << 7,
		OPT_i = 1 << 8,
		OPT_n = 1 << 9,
		OPT_p = 1 << 10,
		OPT_q = 1 << 11,
		OPT_R = 1 << 12,
		OPT_r = 1 << 13,
		OPT_s = 1 << 14,
		OPT_T = 1 << 15,
		OPT_t = 1 << 16,
		OPT_v = 1 << 17,
		OPT_S = 1 << 18,
	};
#if ENABLE_GETOPT_LONG
	static const char udhcpc_longopts[] ALIGN1 =
		"clientid\0"      Required_argument "c"
		"clientid-none\0" No_argument       "C"
		"vendorclass\0"   Required_argument "V"
		"foreground\0"    No_argument       "f"
		"background\0"    No_argument       "b"
		"hostname\0"      Required_argument "H"
		"hostname\0"      Required_argument "h"
		"fqdn\0"          Required_argument "F"
		"interface\0"     Required_argument "i"
		"now\0"           No_argument       "n"
		"pidfile\0"       Required_argument "p"
		"quit\0"          No_argument       "q"
		"release\0"       No_argument       "R"
		"request\0"       Required_argument "r"
		"script\0"        Required_argument "s"
		"timeout\0"       Required_argument "T"
		"version\0"       No_argument       "v"
		"retries\0"       Required_argument "t"
		"syslog\0"        No_argument       "S"
		;
#endif
	/* Default options. */
	client_config.interface = "eth0";
	client_config.script = DEFAULT_SCRIPT;
	client_config.retries = 3;
	client_config.timeout = 3;

	/* Parse command line */
	opt_complementary = "c--C:C--c" // mutually exclusive
	                    ":hH:Hh"; // -h and -H are the same
#if ENABLE_GETOPT_LONG
	applet_long_options = udhcpc_longopts;
#endif
	opt = getopt32(argv, "c:CV:fbH:h:F:i:np:qRr:s:T:t:vS",
		&str_c, &str_V, &str_h, &str_h, &str_F,
		&client_config.interface, &client_config.pidfile, &str_r,
		&client_config.script, &str_T, &str_t
		);

	if (opt & OPT_c)
		client_config.clientid = alloc_dhcp_option(DHCP_CLIENT_ID, str_c, 0);
	//if (opt & OPT_C)
	if (opt & OPT_V)
		client_config.vendorclass = alloc_dhcp_option(DHCP_VENDOR, str_V, 0);
	if (opt & OPT_f)
		client_config.foreground = 1;
	if (opt & OPT_b)
		client_config.background_if_no_lease = 1;
	if (opt & OPT_h)
		client_config.hostname = alloc_dhcp_option(DHCP_HOST_NAME, str_h, 0);
	if (opt & OPT_F) {
		client_config.fqdn = alloc_dhcp_option(DHCP_FQDN, str_F, 3);
		/* Flags: 0000NEOS
		S: 1 => Client requests Server to update A RR in DNS as well as PTR
		O: 1 => Server indicates to client that DNS has been updated regardless
		E: 1 => Name data is DNS format, i.e. <4>host<6>domain<4>com<0> not "host.domain.com"
		N: 1 => Client requests Server to not update DNS
		*/
		client_config.fqdn[OPT_DATA + 0] = 0x1;
		/* client_config.fqdn[OPT_DATA + 1] = 0; - redundant */
		/* client_config.fqdn[OPT_DATA + 2] = 0; - redundant */
	}
	// if (opt & OPT_i) client_config.interface = ...
	if (opt & OPT_n)
		client_config.abort_if_no_lease = 1;
	// if (opt & OPT_p) client_config.pidfile = ...
	if (opt & OPT_q)
		client_config.quit_after_lease = 1;
	if (opt & OPT_R)
		client_config.release_on_quit = 1;
	if (opt & OPT_r)
		requested_ip = inet_addr(str_r);
	// if (opt & OPT_s) client_config.script = ...
	if (opt & OPT_T)
		client_config.timeout = xatoi_u(str_T);
	if (opt & OPT_t)
		client_config.retries = xatoi_u(str_t);
	if (opt & OPT_v) {
		printf("version %s\n", BB_VER);
		return 0;
	}

	if (opt & OPT_S) {
		openlog(applet_name, LOG_PID, LOG_LOCAL0);
		logmode |= LOGMODE_SYSLOG;
	}

	if (read_interface(client_config.interface, &client_config.ifindex,
			   NULL, client_config.arp))
		return 1;

	/* Make sure fd 0,1,2 are open */
	bb_sanitize_stdio();
	/* Equivalent of doing a fflush after every \n */
	setlinebuf(stdout);

	/* Create pidfile */
	write_pidfile(client_config.pidfile);
	/* if (!..) bb_perror_msg("cannot create pidfile %s", pidfile); */

	/* Goes to stdout and possibly syslog */
	bb_info_msg("%s (v%s) started", applet_name, BB_VER);

	/* if not set, and not suppressed, setup the default client ID */
	if (!client_config.clientid && !(opt & OPT_C)) {
		client_config.clientid = alloc_dhcp_option(DHCP_CLIENT_ID, "", 7);
		client_config.clientid[OPT_DATA] = 1;
		memcpy(client_config.clientid + OPT_DATA+1, client_config.arp, 6);
	}

	if (!client_config.vendorclass)
		client_config.vendorclass = alloc_dhcp_option(DHCP_VENDOR, "udhcp "BB_VER, 0);

	/* setup the signal pipe */
	udhcp_sp_setup();

	state = INIT_SELECTING;
	udhcp_run_script(NULL, "deconfig");
	change_mode(LISTEN_RAW);
	tv.tv_sec = 0;
	goto jump_in;

	for (;;) {
		tv.tv_sec = timeout - monotonic_sec();
 jump_in:
		tv.tv_usec = 0;

		if (listen_mode != LISTEN_NONE && sockfd < 0) {
			if (listen_mode == LISTEN_KERNEL)
				sockfd = listen_socket(/*INADDR_ANY,*/ CLIENT_PORT, client_config.interface);
			else
				sockfd = raw_socket(client_config.ifindex);
		}
		max_fd = udhcp_sp_fd_set(&rfds, sockfd);

		retval = 0; /* If we already timed out, fall through, else... */
		if (tv.tv_sec > 0) {
			DEBUG("Waiting on select...");
			retval = select(max_fd + 1, &rfds, NULL, NULL, &tv);
		}

		now = monotonic_sec();
		if (retval < 0) {
			/* EINTR? signal was caught, don't panic */
			if (errno != EINTR) {
				/* Else: an error occured, panic! */
				bb_perror_msg_and_die("select");
			}
		} else if (retval == 0) {
			/* timeout dropped to zero */
			switch (state) {
			case INIT_SELECTING:
				if (packet_num < client_config.retries) {
					if (packet_num == 0)
						xid = random_xid();

					/* send discover packet */
					send_discover(xid, requested_ip); /* broadcast */

					timeout = now + client_config.timeout;
					packet_num++;
				} else {
					udhcp_run_script(NULL, "leasefail");
					if (client_config.background_if_no_lease) {
						bb_info_msg("No lease, forking to background");
						client_background();
					} else if (client_config.abort_if_no_lease) {
						bb_info_msg("No lease, failing");
						retval = 1;
						goto ret;
					}
					/* wait to try again */
					packet_num = 0;
					timeout = now + 60;
				}
				break;
			case RENEW_REQUESTED:
			case REQUESTING:
				if (packet_num < client_config.retries) {
					/* send request packet */
					if (state == RENEW_REQUESTED)
						send_renew(xid, server_addr, requested_ip); /* unicast */
					else send_selecting(xid, server_addr, requested_ip); /* broadcast */

					timeout = now + ((packet_num == 2) ? 10 : 2);
					packet_num++;
				} else {
					/* timed out, go back to init state */
					if (state == RENEW_REQUESTED)
						udhcp_run_script(NULL, "deconfig");
					state = INIT_SELECTING;
					timeout = now;
					packet_num = 0;
					change_mode(LISTEN_RAW);
				}
				break;
			case BOUND:
				/* Lease is starting to run out, time to enter renewing state */
				state = RENEWING;
				change_mode(LISTEN_KERNEL);
				DEBUG("Entering renew state");
				/* fall right through */
			case RENEWING:
				/* Either set a new T1, or enter REBINDING state */
				if ((t2 - t1) <= (lease / 14400 + 1)) {
					/* timed out, enter rebinding state */
					state = REBINDING;
					timeout = now + (t2 - t1);
					DEBUG("Entering rebinding state");
				} else {
					/* send a request packet */
					send_renew(xid, server_addr, requested_ip); /* unicast */
					t1 = (t2 - t1) / 2 + t1;
					timeout = start + t1;
				}
				break;
			case REBINDING:
				/* Either set a new T2, or enter INIT state */
				if ((lease - t2) <= (lease / 14400 + 1)) {
					/* timed out, enter init state */
					state = INIT_SELECTING;
					bb_info_msg("Lease lost, entering init state");
					udhcp_run_script(NULL, "deconfig");
					timeout = now;
					packet_num = 0;
					change_mode(LISTEN_RAW);
				} else {
					/* send a request packet */
					send_renew(xid, 0, requested_ip); /* broadcast */
					t2 = (lease - t2) / 2 + t2;
					timeout = start + t2;
				}
				break;
			case RELEASED:
				/* yah, I know, *you* say it would never happen */
				timeout = INT_MAX;
				break;
			}
		} else if (listen_mode != LISTEN_NONE && FD_ISSET(sockfd, &rfds)) {
			/* a packet is ready, read it */

			if (listen_mode == LISTEN_KERNEL)
				len = udhcp_get_packet(&packet, sockfd);
			else len = get_raw_packet(&packet, sockfd);

			if (len == -1 && errno != EINTR) {
				DEBUG("error on read, %s, reopening socket", strerror(errno));
				change_mode(listen_mode); /* just close and reopen */
			}
			if (len < 0) continue;

			if (packet.xid != xid) {
				DEBUG("Ignoring XID %x (our xid is %x)",
					(unsigned)packet.xid, (unsigned)xid);
				continue;
			}

			/* Ignore packets that aren't for us */
			if (memcmp(packet.chaddr, client_config.arp, 6)) {
				DEBUG("Packet does not have our chaddr - ignoring");
				continue;
			}

			message = get_option(&packet, DHCP_MESSAGE_TYPE);
			if (message == NULL) {
				bb_error_msg("cannot get option from packet - ignoring");
				continue;
			}

			switch (state) {
			case INIT_SELECTING:
				/* Must be a DHCPOFFER to one of our xid's */
				if (*message == DHCPOFFER) {
					temp = get_option(&packet, DHCP_SERVER_ID);
					if (temp) {
						/* can be misaligned, thus memcpy */
						memcpy(&server_addr, temp, 4);
						xid = packet.xid;
						requested_ip = packet.yiaddr;

						/* enter requesting state */
						state = REQUESTING;
						timeout = now;
						packet_num = 0;
					} else {
						bb_error_msg("no server ID in message");
					}
				}
				break;
			case RENEW_REQUESTED:
			case REQUESTING:
			case RENEWING:
			case REBINDING:
				if (*message == DHCPACK) {
					temp = get_option(&packet, DHCP_LEASE_TIME);
					if (!temp) {
						bb_error_msg("no lease time with ACK, using 1 hour lease");
						lease = 60 * 60;
					} else {
						/* can be misaligned, thus memcpy */
						memcpy(&lease, temp, 4);
						lease = ntohl(lease);
					}

					/* enter bound state */
					t1 = lease / 2;

					/* little fixed point for n * .875 */
					t2 = (lease * 7) >> 3;
					temp_addr.s_addr = packet.yiaddr;
					bb_info_msg("Lease of %s obtained, lease time %u",
						inet_ntoa(temp_addr), (unsigned)lease);
					start = now;
					timeout = start + t1;
					requested_ip = packet.yiaddr;
					udhcp_run_script(&packet,
						   ((state == RENEWING || state == REBINDING) ? "renew" : "bound"));

					state = BOUND;
					change_mode(LISTEN_NONE);
					if (client_config.quit_after_lease) {
						if (client_config.release_on_quit)
							perform_release();
						goto ret0;
					}
					if (!client_config.foreground)
						client_background();

				} else if (*message == DHCPNAK) {
					/* return to init state */
					bb_info_msg("Received DHCP NAK");
					udhcp_run_script(&packet, "nak");
					if (state != REQUESTING)
						udhcp_run_script(NULL, "deconfig");
					state = INIT_SELECTING;
					timeout = now;
					requested_ip = 0;
					packet_num = 0;
					change_mode(LISTEN_RAW);
					sleep(3); /* avoid excessive network traffic */
				}
				break;
			/* case BOUND, RELEASED: - ignore all packets */
			}
		} else {
			int signo = udhcp_sp_read(&rfds);
			switch (signo) {
			case SIGUSR1:
				perform_renew();
				break;
			case SIGUSR2:
				perform_release();
				break;
			case SIGTERM:
				bb_info_msg("Received SIGTERM");
				if (client_config.release_on_quit)
					perform_release();
				goto ret0;
			}
		}
	} /* for (;;) */
 ret0:
	retval = 0;
 ret:
	/*if (client_config.pidfile) - remove_pidfile has it's own check */
		remove_pidfile(client_config.pidfile);
	return retval;
}
