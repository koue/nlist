/*
 * Copyright (c) 2011-2018 Nikola Kolev <koue@chaosophia.net>
 * Copyright (c) 2004-2006 Daniel Hartmeier. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fts.h>
#include <sha.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

#include <cez_config.h>
#include <cez_misc.h>

#define	VERSION	1001
#define cnf_lookup config_queue_value_get

struct entry {
	char	fn[MAXNAMLEN + 1];	/* absolute path of the file */
	time_t	pubdate;		/* date of publication */
	char	name[64];		/* name of the article */
	char	parent[64];		/* parent directory ot the article */
	char	title[128];		/* first line of the article */
};

static const char	*conffile = "/opt/koue.chaosophia.net/nlist/nlist.conf";
static const char	*corefile = "/opt/koue.chaosophia.net/nlist/nlist.core";
static const char *params[] = { "datadir", "htmldir", "logfile", "excludefile",
    "baseurl", "ct_html", NULL };

static struct 	entry newest[64];
static gzFile	gz = NULL;

typedef	void 	(*render_cb)(const char *, const struct entry *);

static void	render_error(const char *fmt, ...);
static int	render_html(const char *html_fn, render_cb r,
		    const struct entry *e);
static void	render_rss(const char *m, const struct entry *e);
static void	render_rss_item(const char *m, const struct entry *e);
static void	render_front(const char *m, const struct entry *e);
static void	render_front_story(const char *m, const struct entry *e);
static void	find_articles(const char *path, struct entry *a, int size);
static int	read_file(struct entry *e);
static char	*html_esc(const char *s, char *d, size_t len, int allownl);
static int	compare_name_des_fts(const FTSENT * const *a,
		    const FTSENT * const *b);
static void	msg(const char *fmt, ...);
static int	excluded(const char *name);

static void
d_printf(const char *fmt, ...)
{
	static char s[65536];
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);
	if (r < 0 || r >= sizeof(s)) {
		msg("error d_printf: vsnprintf: r %d (%d)", r, (int)sizeof(s));
	}
	if (gz != NULL) {
		r = gzputs(gz, s);
		if (r != strlen(s)) {
			msg("error d_printf: gzputs: r %d (%d)",
			    r, (int)strlen(s));
		}
	} else {
		fprintf(stdout, "%s", s);
	}
}

static void
render_error(const char *fmt, ...)
{
	va_list ap;
	char s[8192];

	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);
	printf("%s\r\n\r\n", cnf_lookup("ct_html"));
	fflush(stdout);
	d_printf("<html><head><title>Error</title></head><body>\n");
	d_printf("<h2>Error</h2><p><b>%s</b><p>\n", s);
	d_printf("Time: <b>%s</b><br>\n", rfc822_time(time(0)));
	d_printf("</body></html>\n");
}

static int
render_html(const char *html_fn, render_cb r, const struct entry *e)
{
	FILE *f;
	char s[8192];

	if ((f = fopen(html_fn, "re")) == NULL) {
		d_printf("ERROR: fopen: %s: %s<br>\n", html_fn,
		    strerror(errno));
		return (1);
	}
	while (fgets(s, sizeof(s), f)) {
		char *a, *b;

		for (a = s; (b = strstr(a, "%%")) != NULL;) {
			*b = 0;
			d_printf("%s", a);
			a = b + 2;
			if ((b = strstr(a, "%%")) != NULL) {
				*b = 0;
				if (strcmp(a, "BASEURL") == 0) {
					d_printf("%s", cnf_lookup("baseurl"));
				} else if (strcmp(a, "CTYPE") == 0) {
					d_printf("%s", cnf_lookup("ct_html"));
				} else if (r != NULL) {
					(*r)(a, e);
				}
				a = b + 2;
			}
		}
		d_printf("%s", a);
	}
	fclose(f);
	return (0);
}

static void
render_rss(const char *m, const struct entry *e)
{
	if (strcmp(m, "ITEMS") == 0) {
		char fn[1024];
		int i;
		snprintf(fn, sizeof(fn), "%s/summary_item.rss",
		    cnf_lookup("htmldir"));
		for (i = 0; newest[i].name[0]; ++i) {
			render_html(fn, &render_rss_item, &newest[i]);
		}
	} else {
		d_printf("render_rss: unknown macro '%s'\n", m);
	}
}

static void
render_rss_item(const char *m, const struct entry *e)
{
	char d[256];

	if (strcmp(m, "TITLE") == 0) {
		d_printf("%s", html_esc(e->title, d, sizeof(d), 0));
	} else if (strcmp(m, "LINK") == 0) {
		d_printf("%s/%s.html", cnf_lookup("baseurl"), e->name);
	} else if (strcmp(m, "DATE") == 0) {
		d_printf("%s", ctime(&e->pubdate));
	} else if (strcmp(m, "BODY") == 0) {
		FILE *f;
		char s[8192];

		if ((f = fopen(e->fn, "re")) == NULL) {
			d_printf("render_rss_item: fopen %s: %s\n",
				e->fn, strerror(errno));
			return;
		}

		fgets(s, sizeof(s), f);		/* skip first line */
		while (fgets(s, sizeof(s), f)) {
			d_printf("%s", s);
		}
		fclose(f);
	} else {
		d_printf("render_rss_item: unknown macro '%s'\n", m);
	}
}

static void
render_front(const char *m, const struct entry *e)
{
	char fn[1024];
	int i;

	if (strcmp(m, "STORY") == 0) {
		snprintf(fn, sizeof(fn), "%s/story.html",
		    cnf_lookup("htmldir"));
		for (i = 0; newest[i].name[0]; ++i) {
			render_html(fn, &render_front_story, &newest[i]);
		}
	} else if (strcmp(m, "HEADER") == 0) {
		snprintf(fn, sizeof(fn), "%s/header.html",
		    cnf_lookup("htmldir"));
		render_html(fn, NULL, NULL);
	} else if (strcmp(m, "FOOTER") == 0) {
		snprintf(fn, sizeof(fn), "%s/footer.html",
		    cnf_lookup("htmldir"));
		render_html(fn, NULL, NULL);
	} else {
		d_printf("render_front: unknown macro '%s'<br>\n", m);
	}
}

static void
render_front_story(const char *m, const struct entry *e)
{
	if (strcmp(m, "NAME") == 0) {
		if (e->title[0]) {
			d_printf("%s", e->title);
		} else {
			d_printf("%s", "NONAMEZ");
		}
	} else if (strcmp(m, "DATE") == 0) {
		d_printf("%s", ctime(&e->pubdate));
	} else if (strcmp(m, "BASEURL") == 0) {
		d_printf("%s", cnf_lookup("baseurl"));
	} else if (strcmp(m, "ARTICLE") == 0) {
		if (e->parent[0]) {
			d_printf("%s/", e->parent);
		}
		d_printf("%s", e->name);
	} else if (strcmp(m, "BODY") == 0) {
		FILE *f;
		char s[8192];

		if ((f = fopen(e->fn, "re")) == NULL) {
			d_printf("render_front_story: fopen: %s: %s<br>\n",
			    e->fn, strerror(errno));
			return;
		}
		fgets(s, sizeof(s), f);		/* skip first line */
		while (fgets(s, sizeof(s), f)) {
			d_printf("%s", s);
		}
		fclose(f);
	} else {
		d_printf("render_front_story: unknown macro '%s'<br>\n", m);
	}
}

static void
find_articles(const char *path, struct entry *a, int size)
{
	FTS *fts;
	FTSENT *e;
	char * const path_argv[] = { (char*)path, NULL };
	int i = 0;
	char *tmp, *pos, article[128], parent[128], query[256];

	parent[0] = article[0] = 0;

	memset(a, 0, size * sizeof(*a));
	if ((fts = fts_open(path_argv, FTS_LOGICAL, compare_name_des_fts))
	    == NULL) {
		d_printf("fts_open: %s: %s<br>\n", path, strerror(errno));
		return;
	} else if ((e = fts_read(fts)) == NULL || (e->fts_info != FTS_D)) {
		d_printf("fts_read: fts_info %s: %s<br>\n", path,
							strerror(errno));
		return;
	} else if ((e = fts_children(fts, FTS_NAMEONLY)) == NULL) {
		if (errno != 0) {
			d_printf("fts_children: %s: %s<br>\n", path,
							strerror(errno));
		}
		return;
	}

/* parsing query string */
//	if(q->query_string != NULL) {
	if((tmp = getenv("QUERY_STRING")) != NULL) {
		snprintf(query, sizeof(query), "%s", tmp);
		pos = query;
		while ((tmp = strsep(&pos, "/")) != NULL) {
			if(strlen(tmp)) {
				if(strstr(tmp, ".html") != NULL) {
					strlcpy(article, tmp, sizeof(article));
					if ((tmp = strstr(article, ".")) != NULL) {
						*tmp = 0;
					}
					strlcat(article,".txt",sizeof(article));
				}
				else {
					strlcpy(parent, tmp, sizeof(parent));
				}
			}
		}
	}

	/*
	If there is parent but not article selected then show only index.txt
	content. If the main directory should be shown then show all *.txt
	files in the current directory.
	*/
	/* if rss generate rss from main directory */
	if((strncmp(parent, "rss", 3)) == 0) {
		parent[0] = 0;
	}
	if(parent[0] && !article[0]) {
		strlcpy(article, "index.txt", sizeof(article));
	}
	while ((( e = fts_read(fts)) != NULL) && (i < size)) {
		if ((e->fts_info == FTS_F)
			&& ((!(strlen(parent)) && (e->fts_level == 1)) || !(strcmp(parent, e->fts_parent->fts_name)))
			&& ((!(strlen(article)) && !excluded(e->fts_path)) || !(strcmp(article, e->fts_name)))
			&& (e->fts_name[strlen(e->fts_name)-1] == 't')
			&& (e->fts_name[strlen(e->fts_name)-2] == 'x')
			&& (e->fts_name[strlen(e->fts_name)-3] == 't')
			&& (e->fts_name[strlen(e->fts_name)-4] == '.')) {

			snprintf(a[i].fn, sizeof(a[i].fn), "%s", e->fts_path);
			if (read_file(&a[i])) {
				memset(&a[i], 0, sizeof(a[i]));
				continue;
			}

			if(strcmp(e->fts_parent->fts_name, "data") != 0) {
				snprintf(a[i].parent, sizeof(a[i].parent), "%s", e->fts_parent->fts_name);
			}

			pos = e->fts_name;
			while (*pos && *pos != '.') {
				pos++;
			}
			*pos = 0;
			strlcpy(a[i].name, e->fts_name, sizeof(a[i].name));
			a[i].pubdate = e->fts_statp->st_mtime;

			i++;
		}
	}
	fts_close(fts);
}

static int
read_file(struct entry *e)
{
	FILE *f;
	char s[8192];

	e->name[0] = e->parent[0] = e->title[0] = 0;
	if ((f = fopen(e->fn, "re")) == NULL) {
		return (1);
	}

	fgets(s, sizeof(s), f);
	if (s[0]) {
		s[strlen(s) - 1] = 0;
		strlcpy(e->title, s, sizeof(e->title));
	}

	fclose(f);
	return (0);
}

static char *
html_esc(const char *s, char *d, size_t len, int allownl)
{
	size_t p;

	for (p = 0; *s && p < len - 1; ++s) {
		switch (*s) {
		case '&':
			if (p < len - 5) {
				strlcpy(d + p, "&amp;", 6);
				p += 5;
			}
			break;
		case '\"':
			if (p < len - 6) {
				strlcpy(d + p, "&quot;", 7);
				p += 6;
			}
			break;
		case '<':
			if (p < len - 4) {
				strlcpy(d + p, "&lt;", 5);
				p += 4;
			}
			break;
		case '>':
			if (p < len - 4) {
				strlcpy(d + p, "&gt;", 5);
				p += 4;
			}
			break;
		case '\r':
		case '\n':
			if (!allownl) {
				/* skip */
				break;
			} else if (allownl > 1 && *s == '\r') {
				if (p < len - 4) {
					strlcpy(d + p, "<br>", 5);
					p += 4;
				}
				break;
			}
			/* else fall through */
		default:
			d[p++] = *s;
		}
	}
	d[p] = 0;
	return (d);
}

static int
compare_name_des_fts(const FTSENT * const *a, const FTSENT * const *b)
{
	return (strcmp((*b)->fts_name, (*a)->fts_name));
}

int
main(void)
{
	const char *s;
	static struct timeval tx;
	time_t if_modified_since = 0;
	int i, query = 0;

	gettimeofday(&tx, NULL);
	umask(007);
	if (configfile_parse(conffile, config_queue_cb) == -1) {
		msg("error load_conf: file '%s'", conffile);
		render_error("load_conf: file '%s'", conffile);
		goto done;
	}
	if ((s = config_queue_check(params)) != NULL) {
		render_error("%s is missing", s);
		goto done;
	}
	if (chdir("/tmp")) {
		msg("error main: chdir: /tmp: %s", strerror(errno));
		render_error("chdir: /tmp: %s", strerror(errno));
		goto done;
	}
	if (!access(corefile, R_OK)) {
		struct stat sb;

		if (stat(corefile, &sb)) {
			msg("error main: stat: %s: %s", corefile,
			    strerror(errno));
		} else {
			char fn[1024];
			struct tm *tm = gmtime(&sb.st_mtime);

			snprintf(fn, sizeof(fn),
			    "%s.%4.4d%2.2d%2.2d%2.2d%2.2d%2.2d", corefile,
			    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			    tm->tm_hour, tm->tm_min, tm->tm_sec);
			if (rename(corefile, fn)) {
				msg("error main: rename: %s: %s: %s", corefile,
				    fn, strerror(errno));
			} else {
				msg("warning main: core file %s renamed to %s",
				    corefile, fn);
			}
		}
	}
	if ((s = getenv("QUERY_STRING")) != NULL) {
		query = 1;
		if (strlen(s) > 64) {
			printf("Status: 400\r\n\r\n You are trying to send very long query!\n");
			fflush(stdout);
			return (0);

		} else if (strstr(s, "&amp;") != NULL) {
			msg("warning main: escaped query '%s'", s);
			printf("Status: 400\r\n\r\nHTML escaped ampersand in cgi "
			    "query string \"%s\"\n", s);
			fflush(stdout);
			return (0);
		} else {
			for (i = 0; i < strlen(s); i++) {
				/*
				sanity check of the query string, accepts
				only alpha, '/' and '_' and '.' if its on 5
				position before the end of the string

					Correct: /follow/this/path/
						 /or/this/
						 /and/this/if/its/single/article.html
				*/
                        	if ((!isalpha(s[i])) && (!isdigit(s[i])) && (s[i] != '/') && (s[i] != '_')) {
					if ((i == (strlen(s)-5)) && (s[i] == '.')) {
						continue;
					}
                                	printf("Status: 400\r\n\r\nYou are trying to send wrong query!\n");
	                                fflush(stdout);
        	                        return (0);
                	        }
                	}
		}
	}

	if ((s = getenv("IF_MODIFIED_SINCE")) != NULL) {
		if_modified_since = convert_rfc822_time(s);
		if (if_modified_since <= 0) {
			if_modified_since =
			    (time_t)strtoul(s, NULL, 10);
		}
		if (!if_modified_since) {
			msg("warning main: invalid IF_MODIFIED_SINCE '%s'", s);
		}
	}
	if ((s = getenv("HTTP_ACCEPT_ENCODING")) != NULL) {
		char *p = strstr(s, "gzip");

		if (p != NULL && ((strncmp(p, "gzip;q=0", 8) != 0) ||
		    strtol(p + 7, (char **)NULL, 10) > 0.0)) {
			gz = gzdopen(fileno(stdout), "wb9");
			if (gz == NULL) {
				msg("error main: gzdopen");
			} else {
				printf("Content-Encoding: gzip\r\n");
			}
		}
	}

	char fn[1024];
	struct entry e;
	strlcpy(fn, cnf_lookup("datadir"), sizeof(fn));
	find_articles(fn, newest, 10);
	printf("%s\r\n\r\n", cnf_lookup("ct_html"));
	fflush(stdout);
	if (query && !strncmp(getenv("QUERY_STRING"), "/rss", 4)) {
		snprintf(fn, sizeof(fn), "%s/summary.rss",
		    cnf_lookup("htmldir"));
		memset(&e, 0, sizeof(e));
		render_html(fn, &render_rss, &e);
	} else {
		snprintf(fn, sizeof(fn), "%s/main.html", cnf_lookup("htmldir"));
		memset(&e, 0, sizeof(e));
		render_html(fn, &render_front, &e);
	}

done:
	if (gz != NULL) {
		if (gzclose(gz) != Z_OK) {
			msg("error main: gzclose");
		}
		gz = NULL;
	} else {
		fflush(stdout);
	}
	msg("total %.1f ms query [%s]", timelapse(&tx), getenv("QUERY_STRING"));
	config_queue_purge();
	return (0);
}

static void
msg(const char *fmt, ...)
{
	extern char *__progname;
	FILE *f;
	va_list ap;
	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);

	if ((f = fopen(cnf_lookup("logfile"), "ae")) == NULL) {
		fprintf(stderr, "%s: cannot open logfile: %s\n", __func__,
							cnf_lookup("logfile"));
		return;
	}
	fprintf(f, "%4.4d.%2.2d.%2.2d %2.2d:%2.2d:%2.2d %s %s %s v%d [%u] ",
	    tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec, getenv("REMOTE_ADDR"), "-" ,
	    __progname, VERSION, (unsigned)getpid());
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fprintf(f, "\n");
	fflush(f);
	fclose(f);
}

static int
excluded(const char *name)
{
	FILE *f;
	char s[8192], *p;

	if (( f = fopen(cnf_lookup("excludefile"), "re")) == NULL) {
		msg("Cannot open exclude file.\n");
		return 0;
	}
	while (fgets(s, sizeof(s), f)) {
		s[strlen(s) - 1] = 0;
		if ((p = strstr(name, s)) != NULL) {
			fclose(f);
			return (1);
		}
	}
	fclose(f);
	return (0);
}
