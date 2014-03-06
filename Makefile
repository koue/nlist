CC=cc -Wall -Wstrict-prototypes -g -O0 -static
CHROOT=/data/www_SSL
DATA=/opt/nlist
WEB=/koue.chaosophia.net
CGI=index.cgi

all: nlist

nlist: nlist.o cgi.o conf.o 
	$(CC) -o nlist nlist.o cgi.o conf.o -lz

nlist.o: nlist.c cgi.h conf.h 
	$(CC) -c nlist.c

conf.o: conf.c conf.h
	$(CC) -c conf.c

cgi.o: cgi.c cgi.h
	$(CC) -c cgi.c

clean:
	rm -f nlist *.o *.core

install:
	rm -rf $(CHROOT)$(WEB)/$(CGI)
	rm -rf $(CHROOT)$(DATA)${WEB}/html
	rm -rf $(CHROOT)$(DATA)${WEB}/cgi.conf
	cp nlist $(CHROOT)$(WEB)/$(CGI)
	cp -r cgi.conf html $(CHROOT)$(DATA)${WEB}
	

test:
	chroot -u www -g www $(CHROOT) $(WEB)/$(CGI)

#testquery: 
#	QUERY_STRING='/action/submit' chroot -u www -g www $(CHROOT) $(WEB)/$(CGI)
