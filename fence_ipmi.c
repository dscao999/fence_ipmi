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
#include <sys/types.h>
#include <sys/wait.h>

struct nodeip {
	const char *node;
	const char *ip;
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
			.val = 'u'
		},
		{
			.name = "pass",
			.has_arg = 1,
			.flag = NULL,
			.val = 'p'
		},
		{
			.name = "bmc",
			.has_arg = 1,
			.flag = NULL,
			.val = 'm'
		},
		{
			.name = "port",
			.has_arg = 1,
			.flag = NULL,
			.val = 'P'
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
	static const char *sopts = ":u:p:m:P:n:e";
	extern char *optarg;
	extern int optind, opterr, optopt;
	int fin = 0, c, retv = 0;

	opterr = 0;
	do {
		c = getopt_long(argc, argv, sopts, options, NULL);
		switch(c) {
		case '?':
			fprintf(stderr, "Unknown option %c\n", optopt);
			break;
		case ':':
			fprintf(stderr, "Missing option argument of %c\n", optopt);
			break;
		case 'e':
			opts->echo = 1;
			break;
		case -1:
			fin = 1;
			break;
		case 'u':
			opts->user= optarg;
			retv++;
			break;
		case 'p':
			opts->pass= optarg;
			retv++;
			break;
		case 'm':
			opts->bmc = optarg;
			retv++;
			break;
		case 'P':
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
	if (!opts->action)
		opts->action = "off";
	if (!opts->port)
		opts->port = "623";
	return retv;
}

static void parse_stdin(struct ipmiarg *opts, char *page)
{
	int llen;
	char *lbuf, *curp;
	size_t buflen = 64;

	lbuf = malloc(buflen);
	if (!lbuf) {
		fprintf(stderr, "Out of Memory!\n");
		exit(10000);
	}
	curp = page;
	llen = getline(&lbuf, &buflen, stdin);
	while (!feof(stdin)) {
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
		} else if (strstr(lbuf, "port=") == lbuf) {
			strcpy(curp, lbuf+5);
			opts->port = curp;
		}
		if (*curp != 0)
			curp += strlen(curp) + 1;
		llen = getline(&lbuf, &buflen, stdin);
	}
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
	if (!lbuf) {
		fprintf(stderr, "Out of Memory!\n");
		exit(10000);
	}
	fin = fopen(nodelist, "rb");
	if (!fin) {
		fprintf(stderr, "Cannot open file \"%s\" for read: %s\n",
				nodelist, strerror(errno));
		return NULL;
	}
	lcount = 0;
	len = getline(&lbuf, &llen, fin);
	while (!feof(fin)) {
		if (!comment_line(lbuf, len))
			lcount++;
		len = getline(&lbuf, &llen, fin);
	}
	rewind(fin);

	buf = malloc(lcount*(64+sizeof(struct nodeip))+sizeof(struct nodeip));
	if (!buf) {
		fprintf(stderr, "Out of Memory!\n");
		exit(10000);
	}
	nips = (struct nodeip *)buf;
	buf += sizeof(struct nodeip)*(lcount+1);

	cip = nips;
	chr = buf;
	len = getline(&lbuf, &llen, fin);
	while (!feof(fin)) {
		if (comment_line(lbuf, len)) {
			len = getline(&lbuf, &llen, fin);
			continue;
		}
		strcpy(chr, strtok(lbuf, " \t\n"));
		cip->node = chr;
		chr += strlen(chr) + 1;
		strcpy(chr, strtok(NULL, " \t\n"));
		cip->ip = chr;
		chr += strlen(chr) + 1;
		len = getline(&lbuf, &llen, fin);
		cip++;
	}
	cip->node = NULL;
	cip->ip = NULL;
	return nips;
}

static int ipmi_action(const struct ipmiarg *opts);
static int ipmi_spawn(const char *ip, const char *user, const char *pass,
		const char *port, const char *action);
static void echo_metadata(void);

static const char *nodelist = "/etc/pacemaker/bmclist.conf";

int main(int argc, char *argv[])
{
	struct ipmiarg cmdarg;
	int retv = 0;
	char *page;

	page = malloc(1024);
	if (!page) {
		fprintf(stderr, "Out of Memory!\n");
		exit(10000);
	}
	cmdarg.user= NULL;
	cmdarg.pass= NULL;
	cmdarg.bmc = NULL;
	cmdarg.port = NULL;
	cmdarg.nodelist = NULL;
	cmdarg.action = NULL;
	cmdarg.echo = 0;
	if (!parse_cmd(argc, argv, &cmdarg))
		parse_stdin(&cmdarg, page);
	if (cmdarg.nodelist == NULL)
		cmdarg.nodelist = nodelist;
	if (cmdarg.echo)
		echo_args(&cmdarg);

	cmdarg.nips = parse_nodelist(cmdarg.nodelist);
	if (!cmdarg.nips) {
		fprintf(stderr, "Cannot parse node ip file: %s\n",
				cmdarg.nodelist);
		retv = 4;
		goto exit_10;
	}

	if (cmdarg.echo)
		echo_nips(cmdarg.nips);

	retv = ipmi_action(&cmdarg);

	free(cmdarg.nips);

exit_10:
	free(page);
	return retv;
}

static int ipmi_action(const struct ipmiarg *opt)
{
	const char *action;
	int retv = 0, monitor = 0, nochild, ipmiret;
	const struct nodeip *cip;

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
	else if (strcmp(opt->action, "metadata") == 0) {
		echo_metadata();
		return retv;
	} else {
		fprintf(stderr, "Unknown action ignored.\n");
		return 1;
	}

	nochild = 0;
	cip = opt->nips;
	if (!monitor) {
		if (!opt->bmc) {
			fprintf(stderr, "No BMC specified!\n");
			return 1;
		}
		while (cip->node) {
			if (strcmp(cip->node, opt->bmc) == 0)
				break;
			cip++;
		}
		if (!cip->node) {
			fprintf(stderr, "Unknown BMC: %s\n", opt->bmc);
			return 1;
		}
		retv = ipmi_spawn(cip->ip, opt->user, opt->pass, opt->port,
				action);
		if (!retv)
			nochild = 1;
	} else {
		while (cip->node) {
			ipmiret = ipmi_spawn(cip->ip, opt->user, opt->pass,
					opt->port, action);
			if (ipmiret == 0)
				nochild++;
			else
				retv = 1;
			cip++;
		}
	}

	while(nochild) {
		if (wait(&ipmiret) == -1) {
			if (errno == EINTR)
				continue;
		}
		if (WIFEXITED(ipmiret))
			retv |= WEXITSTATUS(ipmiret);
		else
			retv = 1;
		nochild--;
	}

	return retv;
}

static int ipmi_spawn(const char *ip, const char *user, const char *pass,
		const char *port, const char *action)
{
	int retv = 0, sysret;

	sysret = fork();
	if (sysret == -1) {
		fprintf(stderr, "fork failed: %s\n", strerror(errno));
		retv = 9;
	} else if (sysret == 0) {
		sysret = execl("/usr/bin/ipmitool", "ipmitool", "-I", "lanplus",
				"-H", ip, "-U", user, "-P", pass, "-p", port,
				 "chassis", "power", action, NULL);
		if (sysret == -1) {
			fprintf(stderr, "exec failed: %s\n", strerror(errno));
			exit(errno);
		}
	}
	return retv;
}

static const char *metadata =
"<resource-agent name=\"fence_ipmilan\" shortdesc=\"Fence agent for IPMI\">\n"
"  <longdesc>\n"
"    fence_ipmilan is an I/O Fencing agentwhich can be used with machines controlled by IPMI.This agent calls support software ipmitool (http://ipmitool.sf.net/). WARNING! This fence agent might report success before the node is powered off. You should use -m/method onoff if your fence device works correctly with that option.\n"
"  </longdesc>\n"
"  <parameters>\n"
"    <parameter name=\"action\" unique=\"0\" required=\"1\">\n"
"      <getopt mixed=\"-o, --action=[action]\"/>\n"
"      <content type=\"string\" default=\"reboot\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        Fencing action\n"
"      </shortdesc>\n"
"    </parameter>\n"
"    <parameter name=\"password\" unique=\"0\" required=\"1\">\n"
"      <getopt mixed=\"-p, --password=[password]\"/>\n"
"      <content type=\"string\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        Login password or passphrase\n"
"      </shortdesc>\n"
"    </parameter>\n"
"    <parameter name=\"username\" unique=\"0\" required=\"1\">\n"
"      <getopt mixed=\"-l, --username=[name]\"/>\n"
"      <content type=\"string\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        Login name\n"
"      </shortdesc>\n"
"    </parameter>\n"
"    <parameter name=\"delay\" unique=\"0\" required=\"0\">\n"
"      <getopt mixed=\"--delay=[seconds]\"/>\n"
"      <content type=\"second\" default=\"0\"/>\n"
"      <shortdesc lang=\"en\">\n"
"        Wait X seconds before fencing is started\n"
"      </shortdesc>\n"
"    </parameter>\n"
"  </parameters>\n"
"  <actions>\n"
"    <action name=\"on\" automatic=\"0\"/>\n"
"    <action name=\"off\"/>\n"
"    <action name=\"reboot\"/>\n"
"    <action name=\"status\" timeout=\"30s\"/>\n"
"    <action name=\"monitor\" timeout=\"30s\"/>\n"
"    <action name=\"metadata\"/>\n"
"    <action name=\"stop\" timeout=\"30s\"/>\n"
"    <action name=\"start\" timeout=\"30s\"/>\n"
"  </actions>\n"
"</resource-agent>\n";

static void echo_metadata(void)
{
	printf("%s", metadata);
}
