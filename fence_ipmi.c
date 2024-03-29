/*
 * Dashi Cao, dscao999@hotmail.com, caods1@lenovo.com, July 2019.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "loglog.h"

#define unlikely(x) __builtin_expect((x), 0)

struct fork_arg;
struct nodeip {
	const char *node;
	const char *ip;
	struct fork_arg *forked;
};
struct ipmiarg {
	const char *action;
	const char *user;
	const char *pass;
	const char *bmc;
	const char *port;
	const char *nodelist;
	struct nodeip *nips;
	int echo;
};

static void echo_nips(const struct nodeip *nips)
{
	const struct nodeip *ips;

	if (!nips)
		return;
	ips = nips;
	while (ips->node) {
		printf("BMC: %16s, ip: %s\n", ips->node, ips->ip);
		ips++;
	}
}

static int parse_cmd(int argc, char *argv[], struct ipmiarg *opts)
{
	static const struct option options[] = {
		{
			.name = "user",
			.has_arg = 1,
			.flag = NULL,
			.val = 'U'
		},
		{
			.name = "pass",
			.has_arg = 1,
			.flag = NULL,
			.val = 'P'
		},
		{
			.name = "bmc",
			.has_arg = 1,
			.flag = NULL,
			.val = 'H'
		},
		{
			.name = "port",
			.has_arg = 1,
			.flag = NULL,
			.val = 'p'
		},
		{
			.name = "nodelist",
			.has_arg = 1,
			.flag = NULL,
			.val = 'n'
		},
		{
			.name = "echo",
			.has_arg = 0,
			.flag = NULL,
			.val = 'e'
		},
		{}
	};
	static const char *sopts = ":U:P:H:p:n:e";
	extern char *optarg;
	extern int optind, opterr, optopt;
	int fin = 0, c, retv = 0;

	opterr = 0;
	do {
		c = getopt_long(argc, argv, sopts, options, NULL);
		switch(c) {
		case '?':
			logmsg(LOG_ERR, "Unknown option %c\n", optopt);
			break;
		case ':':
			logmsg(LOG_ERR,  "Missing option argument of %c\n",
					optopt);
			break;
		case 'e':
			opts->echo = 1;
			break;
		case -1:
			fin = 1;
			break;
		case 'U':
			opts->user= optarg;
			retv++;
			break;
		case 'P':
			opts->pass= optarg;
			retv++;
			break;
		case 'H':
			opts->bmc = optarg;
			retv++;
			break;
		case 'p':
			opts->port = optarg;
			retv++;
			break;
		case 'n':
			opts->nodelist = optarg;
			retv++;
			break;
		default:
			assert(0);
		}
	} while (fin == 0);
	if (optind < argc) {
		opts->action = argv[optind];
		retv++;
	}
	return retv;
}

static void parse_stdin(struct ipmiarg *opts, char *page)
{
	int llen;
	char *lbuf, *curp;
	size_t buflen = 64;

	lbuf = malloc(buflen);
	if (!lbuf) {
		logmsg(LOG_CRIT, "Out of Memory!\n");
		exit(10000);
	}
	curp = page;
	do {
		llen = getline(&lbuf, &buflen, stdin);
		if (unlikely(llen == -1)) {
			if (feof(stdin))
				break;
			logmsg(LOG_ERR, "getline from stdin failed: %d\n",
					errno);
			return;
		}
		if (llen == 0)
			continue;
		*curp = 0;
		if (llen > 0 && lbuf[llen-1] == '\n')
			lbuf[llen-1] = 0;
		if (strstr(lbuf, "action=") == lbuf) {
			strcpy(curp, lbuf+7);
			opts->action = curp;
		} else if (strstr(lbuf, "nodename=") == lbuf) {
			strcpy(curp, lbuf+9);
			opts->bmc = curp;
		} else if (strstr(lbuf, "user=") == lbuf) {
			strcpy(curp, lbuf+5);
			opts->user = curp;
		} else if (strstr(lbuf, "pass=") == lbuf) {
			strcpy(curp, lbuf+5);
			opts->pass = curp;
		} else if (strstr(lbuf, "nodelist=") == lbuf) {
			strcpy(curp, lbuf+9);
			opts->nodelist = curp;
		}
		if (*curp != 0)
			curp += strlen(curp) + 1;
	} while (!feof(stdin));
}

static void echo_args(const struct ipmiarg *opts)
{
	printf("User Name: ");
	if (opts->user)
		printf("%s", opts->user);
	printf("\nPassword: ");
	if (opts->pass)
		printf("%s", opts->pass);
	printf("\nNode Name: ");
	if (opts->bmc)
		printf("%s", opts->bmc);
	printf("\nPort: ");
	if (opts->port)
		printf("%s", opts->port);
	printf("\nNode List: ");
	if (opts->nodelist)
		printf("%s", opts->nodelist);
	printf("\nAction: ");
	if (opts->action)
		printf("%s", opts->action);
	printf("\n");
}

static int comment_line(const char *lbuf, int len)
{
	const char *chr, *endchr;

	if (len == 0)
		return 1;

	chr = lbuf;
	endchr = chr + len;
	while (*chr == ' ' || *chr == '\t') {
		chr++;
		if (chr == endchr)
			return 1;
	}
	if (*chr == '#' || *chr == '\n')
		return 1;
	else
		return 0;
}

struct nodeip *parse_nodelist(const char *nodelist)
{
	FILE *fin;
	char *lbuf, *buf, *chr;
	size_t llen = 128;
	int lcount, len;
	struct nodeip *nips, *cip;

	lbuf = malloc(llen);
	check_pointer(lbuf);
	fin = fopen(nodelist, "rb");
	if (!fin) {
		logmsg(LOG_ERR, "Cannot open file \"%s\" for read: %s\n",
				nodelist, strerror(errno));
		return NULL;
	}
	lcount = 0;
	do {
		len = getline(&lbuf, &llen, fin);
		if (len > 0 && !comment_line(lbuf, len))
			lcount++;
	} while (!feof(fin));
	rewind(fin);

	buf = malloc(lcount*(128+sizeof(struct nodeip))+sizeof(struct nodeip));
	check_pointer(buf);
	nips = (struct nodeip *)buf;
	buf += sizeof(struct nodeip)*(lcount+1);

	cip = nips;
	chr = buf;
	do {
		len = getline(&lbuf, &llen, fin);
		if (len <= 0 || comment_line(lbuf, len))
			continue;
		strcpy(chr, strtok(lbuf, " \t\n"));
		cip->ip = chr;
		chr += strlen(chr) + 1;
		strcpy(chr, strtok(NULL, " \t\n"));
		cip->node = chr;
		chr += strlen(chr) + 1;
		cip->forked = NULL;
		cip++;
	} while (!feof(fin));
	cip->node = NULL;
	cip->ip = NULL;
	return nips;
}


static int ipmi_action(struct ipmiarg *opts);
static const char *nodelist = "/etc/pacemaker/bmclist.conf";

int main(int argc, char *argv[])
{
	struct ipmiarg cmdarg;
	int retv = 0;
	char *page;

	page = malloc(1024);
	check_pointer(page);

	cmdarg.user= NULL;
	cmdarg.pass= NULL;
	cmdarg.bmc = NULL;
	cmdarg.port = NULL;
	cmdarg.nodelist = NULL;
	cmdarg.action = NULL;
	cmdarg.echo = 0;
	cmdarg.nips = NULL;
	if (!parse_cmd(argc, argv, &cmdarg))
		parse_stdin(&cmdarg, page);
	if (cmdarg.nodelist == NULL)
		cmdarg.nodelist = nodelist;
	if (!cmdarg.action)
		cmdarg.action = "metadata";
	if (!cmdarg.port)
		cmdarg.port = "623";
	if (!cmdarg.user)
		cmdarg.user = "USERID";
	if (!cmdarg.pass)
		cmdarg.pass = "PASSW0RD";
	if (cmdarg.echo)
		echo_args(&cmdarg);

	if (strcmp(cmdarg.action, "metadata") != 0) {
		cmdarg.nips = parse_nodelist(cmdarg.nodelist);
		if (!cmdarg.nips) {
			logmsg(LOG_ERR, "Cannot parse node ip file: %s\n",
					cmdarg.nodelist);
			retv = 4;
			goto exit_10;
		}
	}

	if (cmdarg.echo)
		echo_nips(cmdarg.nips);

	retv = ipmi_action(&cmdarg);

	free(cmdarg.nips);

exit_10:
	free(page);
	return retv;
}

struct fork_arg {
	const struct nodeip *node;
	const char *user, *pass, *port, *action;
	pthread_t thid;
	int err;
	char buf[256];
};

static struct fork_arg *ipmi_spawn(const struct nodeip *node, const char *user,
		const char *pass, const char *port, const char *action);
static void echo_metadata(void);
static int ipmi_action(struct ipmiarg *opt)
{
	const char *action;
	int retv = 0, monitor = 0;
	struct nodeip *cip;

	if (strcmp(opt->action, "status") == 0 ||
			strcmp(opt->action, "monitor") == 0) {
		action = "status";
		monitor = 1;
	} else if (strcmp(opt->action, "off") == 0)
		action = "off";
	else if (strcmp(opt->action, "reboot") == 0)
		action = "cycle";
	else if (strcmp(opt->action, "on") == 0)
		action = "on";
	else if (strcmp(opt->action, "soft") == 0)
		action = "soft";
	else if (strcmp(opt->action, "metadata") == 0) {
		echo_metadata();
		return 0;
	} else if (strcmp(opt->action, "start") == 0 ||
			strcmp(opt->action, "stop") == 0) {
		return 0;
	} else {
		logmsg(LOG_WARNING, "Unknown action ignored.\n");
		return 0;
	}

	cip = opt->nips;
	if (!monitor) {
		if (!opt->bmc) {
			logmsg(LOG_WARNING, "No BMC specified!\n");
			return 0;
		}
		while (cip->node) {
			if (strcmp(cip->node, opt->bmc) == 0)
				break;
			cip++;
		}
		if (!cip->node) {
			logmsg(LOG_WARNING, "Unknown BMC: %s\n", opt->bmc);
			return 0;
		}
		cip->forked = ipmi_spawn(cip, opt->user, opt->pass, opt->port,
				action);
		if (!cip->forked)
			retv = 1;
	} else {
		while (cip->node) {
			cip->forked = ipmi_spawn(cip, opt->user, opt->pass,
					opt->port, action);
			if (!cip->forked)
				retv = 1;
			cip++;
		}
	}

	for (cip = opt->nips; cip->node; cip++) {
		if (!cip->forked)
			continue;
		pthread_join(cip->forked->thid, NULL);
		if (cip->forked->err)
			retv = 1;
		free(cip->forked);
	}

	return retv;
}

static void *fork_thread(void *arg)
{
	int pfd[2], sysret, ipmiret, len;
	struct fork_arg *farg = arg;
	pid_t child;

	farg->err = 0;
	sysret = pipe(pfd);
	if (sysret == -1) {
		logmsg(LOG_ERR, "pipe failed: %d\n", errno);
		farg->err = errno;
		return arg;
	}
	child = fork();
	if (child == -1) {
		logmsg(LOG_ERR, "fork failed: %s\n", strerror(errno));
		farg->err = errno;
		return arg;
	} else if (child == 0) {
		close(pfd[0]);
		close(1);
		close(2);
		if (unlikely((dup(pfd[1]) == -1) || (dup(pfd[1]) == -1))) {
			logmsg(LOG_WARNING, "dup failed: %d\n", errno);
			exit(errno);
		}
		sysret = execlp("ipmitool", "ipmitool", "-I", "lanplus",
				"-H", farg->node->ip, "-U", farg->user,
				"-P", farg->pass, "-p", farg->port, "chassis",
				"power", farg->action, NULL);
		if (sysret == -1) {
			logmsg(LOG_ERR, "exec failed: %s\n", strerror(errno));
			exit(errno);
		}
	}
	close(pfd[1]);
	len = sprintf(farg->buf, "Node: %s ", farg->node->node);
	do {
		sysret = read(pfd[0], farg->buf+len, 256 - len - 1);
		if (sysret > 0)
			len += sysret;
	} while (sysret > 0);
	farg->buf[len] = 0;
	do
		sysret = waitpid(child, &ipmiret, 0);
	while (sysret == -1 && errno == EINTR);
	close(pfd[0]);

	if (!WIFEXITED(ipmiret) || WEXITSTATUS(ipmiret) != 0)
		logmsg(LOG_INFO, farg->buf);
	return farg;
}

static struct fork_arg *ipmi_spawn(const struct nodeip *node, const char *user,
		const char *pass, const char *port, const char *action)
{
	int sysret;
	struct fork_arg *farg;

	farg = malloc(sizeof(struct fork_arg));
	check_pointer(farg);
	farg->node = node;
	farg->user = user;
	farg->pass = pass;
	farg->port = port;
	farg->action = action;

	sysret = pthread_create(&farg->thid, NULL, fork_thread, farg);
	if (sysret) {
		free(farg);
		farg = NULL;
		logmsg(LOG_ERR, "pthread_create failed: %d\n", sysret);
	}
	return farg;
}

static const char *metadata =
"<resource-agent name=\"fence_ipmilan\" shortdesc=\"Fence agent for IPMI\">\n"
"  <longdesc>\n"
"    fence_ipmilan is an I/O Fencing agentwhich can be used with machines controlled by IPMI.This agent calls support software ipmitool (http://ipmitool.sf.net/). WARNING! This fence agent might report success before the node is powered off. You should use -m/method onoff if your fence device works correctly with that option.\n"
"  </longdesc>\n"
"  <parameters>\n"
"    <parameter name=\"action\" required=\"1\">\n"
"      <getopt mixed=\"-o, --action=[action]\"/>\n"
"      <content type=\"string\" default=\"reboot\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        Fencing action\n"
"      </shortdesc>\n"
"    </parameter>\n"
"    <parameter name=\"pass\" required=\"0\">\n"
"      <getopt mixed=\"-p, --pass=[password]\"/>\n"
"      <content type=\"string\" default=\"PASSW0RD\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        Login password or passphrase\n"
"      </shortdesc>\n"
"    </parameter>\n"
"    <parameter name=\"user\" required=\"0\">\n"
"      <getopt mixed=\"-u, --user=[username]\"/>\n"
"      <content type=\"string\" default=\"USERID\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        Login name\n"
"      </shortdesc>\n"
"    </parameter>\n"
"    <parameter name=\"port\" required=\"0\">\n"
"      <getopt mixed=\"-P, --port=[port]\"/>\n"
"      <content type=\"string\" default=\"623\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        Port Number for BMC connection.\n"
"      </shortdesc>\n"
"    </parameter>\n"
"    <parameter name=\"nodelist\" required=\"0\">\n"
"      <getopt mixed=\"-n, --nodelist=[nodefile]\"/>\n"
"      <content type=\"string\" default=\"/etc/pacemaker/bmclist.conf\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        BMC Node List File.\n"
"      </shortdesc>\n"
"    </parameter>\n"
"  </parameters>\n"
"  <actions>\n"
"    <action name=\"on\"/>\n"
"    <action name=\"off\"/>\n"
"    <action name=\"reboot\"/>\n"
"    <action name=\"soft\"/>\n"
"    <action name=\"status\" timeout=\"30s\"/>\n"
"    <action name=\"monitor\" timeout=\"30s\"/>\n"
"    <action name=\"metadata\"/>\n"
"    <action name=\"stop\"/>\n"
"    <action name=\"start\"/>\n"
"  </actions>\n"
"</resource-agent>\n";

static void echo_metadata(void)
{
	printf("%s", metadata);
}
