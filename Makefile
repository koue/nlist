#
SUBDIR=		src

MYHEADER=	src/nlist.h
DOMAINCMD=	grep DOMAIN $(MYHEADER) | cut -d '"' -f2
DOMAIN=		${DOMAINCMD:sh}
CHROOTCMD=	grep CHROOT $(MYHEADER) | cut -d '"' -f2
LOCALBASE=	${CHROOTCMD:sh}
DATADIR=	/opt/${DOMAIN}/nlist
WEBDIR=		/htdocs/${DOMAIN}
ETCDIR=		/etc
TMPDIR=		/tmp
LIBEXECDIR=	/libexec
CGI=		index.cgi
TESTTITLE=	My first title
TESTLINE=	My first line
VALGRINDCMD=	valgrind -q --tool=memcheck --leak-check=yes --num-callers=20
MYUSERCMD=	grep MYUSER $(MYHEADER) | cut -d '"' -f 2
MYUSER=		${MYUSERCMD:sh}
MYGROUPCMD=	grep MYGROUP $(MYHEADER) | cut -d '"' -f 2
MYGROUP=	${MYGROUPCMD:sh}

.if exists(src/nlist)
CGILDD=		ldd src/nlist | grep "=>" | cut -d ' ' -f 3

CGIlibs=	${CGILDD:sh}
.endif

chroot:
	mkdir -p $(LOCALBASE)$(LIBDIR)
	mkdir -p $(LOCALBASE)$(DATADIR)/data
	mkdir -p $(LOCALBASE)$(WEBDIR)
	mkdir -p $(LOCALBASE)$(ETCDIR)
	mkdir -p $(LOCALBASE)$(LIBEXECDIR)
	mkdir -p $(LOCALBASE)$(TMPDIR)
	cp /libexec/ld-elf.so.1 $(LOCALBASE)$(LIBEXECDIR)/
.	for l in ${CGIlibs}
	cp -f ${l} $(LOCALBASE)$(LIBDIR)/
.	endfor

install:
	rm -rf $(LOCALBASE)$(DATADIR)/html
	rm -rf $(LOCALBASE)$(WEBDIR)/css
	rm -rf $(LOCALBASE)$(ETC)/nlist.conf
	cp src/nlist $(LOCALBASE)$(WEBDIR)/$(CGI)
	cp etc/nlist.conf $(LOCALBASE)$(ETCDIR)/
	sed -i.bak "s/%%DOMAIN%%/$(DOMAIN)/g" $(LOCALBASE)$(ETCDIR)/nlist.conf
	cp -r html $(LOCALBASE)$(DATADIR)/
	cp -r css $(LOCALBASE)$(WEBDIR)/
	chown -R $(MYUSER):$(MYGROUP) $(LOCALBASE)$(DATADIR)/

update:
	cp src/nlist $(LOCALBASE)$(WEBDIR)/$(CGI)

first:
	printf "$(TESTTITLE)\n$(TESTLINE)" > $(LOCALBASE)$(DATADIR)/data/first.txt

test:	chroot install first
	chroot -u $(MYUSER) -g $(MYGROUP) $(LOCALBASE) $(WEBDIR)/$(CGI) | grep "$(TESTTITLE)"
	chroot -u $(MYUSER) -g $(MYGROUP) $(LOCALBASE) $(WEBDIR)/$(CGI) | grep "$(TESTLINE)"

valgrind: chroot install first
	$(VALGRINDCMD) ./src/nlist --valgrind | grep "$(TESTTITLE)"

#testquery:
#	QUERY_STRING='/action/submit' chroot -u www -g www $(LOCALBASE) $(WEBDIR)/$(CGI)
.include <bsd.subdir.mk>
