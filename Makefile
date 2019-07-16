
.PHONY:	all clean install

all: fence_ipmi

CFLAGS += -Wall

fence_ipmi: fence_ipmi.o
	$(LINK.o) $(LDFLAGS) $^ -o $@

clean:
	rm -f fence_ipmi core
	rm -f *.o
	rm -f core.*

install:
	if [ ! -d $(DESTDIR)/sbin ]; then mkdir -p $(DESTDIR)/sbin; fi
	install -m 0755 fence_ipmi $(DESTDIR)/sbin/fence_ipmi
	if [ ! -d $(DESTDIR)/etc/pacemaker ]; then mkdir -p $(DESTDIR)/etc/pacemaker; fi
	install -m 0644 bmclist.conf $(DESTDIR)/etc/pacemaker/bmclist.conf
