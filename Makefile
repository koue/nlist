#
SUBDIR=		src

MYHEADER=	src/nlist.h
DOMAINCMD=	grep DOMAIN $(MYHEADER) | cut -d '"' -f2
DOMAIN=		${DOMAINCMD:sh}
CHROOTTESTCMD=	grep CHROOTTEST $(MYHEADER) | cut -d '"' -f2
CHROOTTEST=	${CHROOTTESTCMD:sh}
DATADIR=	/data/${DOMAIN}/nlist
WEBDIR=		/htdocs/${DOMAIN}
ETCDIR=		/etc
TMPDIR=		/tmp
LIBEXECDIR=	/libexec
LIBDIR=		/usr/lib
CGI=		index.cgi
TESTTITLE1=	My first title
TESTLINE1=	My first line
TESTTITLE2=	My second title
TESTLINE2=	My second line
TESTTITLE3=	My third title
TESTLINE3=	My third line
VALGRINDCMD=	valgrind -q --tool=memcheck --leak-check=yes --show-leak-kinds=all --num-callers=20
MYUSERCMD=	grep MYUSER $(MYHEADER) | cut -d '"' -f 2
MYUSER=		${MYUSERCMD:sh}
MYGROUPCMD=	grep MYGROUP $(MYHEADER) | cut -d '"' -f 2
MYGROUP=	${MYGROUPCMD:sh}
# TESTS
TESTCMD=	chroot -u $(MYUSER) -g $(MYGROUP) $(CHROOTTEST) $(WEBDIR)/$(CGI)
LONG=		iamveryverylongqueryxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

.if exists(src/nlist)
CGILDD=		ldd src/nlist | grep "=>" | cut -d ' ' -f 3

CGIlibs=	${CGILDD:sh}
.endif

chroot:
	mkdir -p $(CHROOTTEST)/$(LIBDIR)
	mkdir -p $(CHROOTTEST)/$(DATADIR)/data
	mkdir -p $(CHROOTTEST)/$(DATADIR)/html
	mkdir -p $(CHROOTTEST)/$(WEBDIR)/css
	mkdir -p $(CHROOTTEST)/$(ETCDIR)
	mkdir -p $(CHROOTTEST)/$(LIBEXECDIR)
	mkdir -p $(CHROOTTEST)/$(TMPDIR)
	cp /libexec/ld-elf.so.1 $(CHROOTTEST)/$(LIBEXECDIR)/
.	for l in ${CGIlibs}
	cp -f ${l} $(CHROOTTEST)/$(LIBDIR)/
.	endfor

install:
	cp src/nlist $(CHROOTTEST)/$(WEBDIR)/$(CGI)
	cp etc/nlist.conf $(CHROOTTEST)/$(ETCDIR)/
	sed -i.bak "s/%%DOMAIN%%/$(DOMAIN)/g" $(CHROOTTEST)/$(ETCDIR)/nlist.conf
	cp html/* $(CHROOTTEST)/$(DATADIR)/html/
	cp css/* $(CHROOTTEST)/$(WEBDIR)/css/
	chown -R $(MYUSER):$(MYGROUP) $(CHROOTTEST)/$(DATADIR)/

update:
	cp src/nlist $(CHROOTTEST)/$(WEBDIR)/$(CGI)

first:
	printf "$(TESTTITLE1)\n$(TESTLINE1)" > $(CHROOTTEST)/$(DATADIR)/data/TESTFIRST.txt
	printf "$(TESTTITLE2)\n$(TESTLINE2)" > $(CHROOTTEST)/$(DATADIR)/data/TESTSECOND.txt
	printf "$(TESTTITLE3)\n$(TESTLINE3)" > $(CHROOTTEST)/$(DATADIR)/data/TESTTHIRD.txt
	printf "TESTSECOND.txt" >> $(CHROOTTEST)/$(DATADIR)/data/exclude_files

test:
	$(MAKE) chroot
	$(MAKE) install
	$(MAKE) first
	# default html
	QUERY_STRING='' $(TESTCMD) > tests/test.file
	diff -q -I 'Posted on <time datetime' tests/default.html tests/test.file
	# first item
	QUERY_STRING='/TESTFIRST.html' $(TESTCMD) > tests/test.file
	diff -q -I 'Posted on <time datetime' tests/first.html tests/test.file
	# second item
	QUERY_STRING='/TESTSECOND.html' $(TESTCMD) > tests/test.file
	diff -q -I 'Posted on <time datetime' tests/second.html tests/test.file
	# default rss
	QUERY_STRING='/rss' $(TESTCMD) > tests/test.file
	diff -q -I '<pubDate>' tests/default.xml tests/test.file
	# not exist
	QUERY_STRING='iammissing' $(TESTCMD) > tests/test.file
	diff -q tests/notexist.html tests/test.file
	QUERY_STRING='/iammissing' $(TESTCMD) > tests/test.file
	diff -q tests/notexist.html tests/test.file
	QUERY_STRING='/iammissing.html' $(TESTCMD) > tests/test.file
	diff -q tests/notexist.html tests/test.file
	# long query
	QUERY_STRING='$(LONG)' $(TESTCMD) > tests/test.file
	diff -q tests/longquery.html tests/test.file
	# wrong query
	QUERY_STRING='/i_am_wrong-1' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong#my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong.my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong"my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong`my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong*my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong my.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING="/i_am_wrong'my.html" $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong/../etc/hosts' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong/../etc/hosts.html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	QUERY_STRING='/i_am_wrong/..html' $(TESTCMD) > tests/test.file
	diff -q tests/wrongquery.html tests/test.file
	# remove test file
	rm -f tests/test.file

valgrind:
	$(MAKE) chroot
	$(MAKE) install
	$(MAKE) first
	QUERY_STRING='' $(VALGRINDCMD) ./src/nlist --valgrind | grep "$(TESTTITLE1)"
	QUERY_STRING='$(LONG)' $(VALGRINDCMD) ./src/nlist --valgrind | grep "Status: 400"

.include <bsd.subdir.mk>
