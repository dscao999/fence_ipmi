
.PHONY:	all clean install

all: fence_ipmi bmclist.conf

CFLAGS ?= -Wall -g
LDFLAGS = -g

fence_ipmi: fence_ipmi.o
	$(LINK.o) $^ -lpthread -o $@

clean:
	rm -f bmclist.conf fence_ipmi core
	rm -f *.o
	rm -f core.*

install: all
	if [ ! -d $(DESTDIR)/usr/sbin ]; then mkdir -p $(DESTDIR)/usr/sbin; fi
	install -m 0755 fence_ipmi $(DESTDIR)/usr/sbin/fence_ipmi
	if [ ! -d $(DESTDIR)/etc/pacemaker ]; then mkdir -p $(DESTDIR)/etc/pacemaker; fi
	install -m 0644 bmclist.conf $(DESTDIR)/etc/pacemaker/bmclist.conf

bmclist.conf:
	@( echo "#";				\
	echo "## BMC Node list with IP";	\
	echo "#";				\
	echo "##  [ip address] [node name]";	\
	echo "#";				\
	echo "## 192.168.99.108 venus";		\
	echo "#"; ) > $@
