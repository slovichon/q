/* $Id$ */

#include <sys/queue.h>
#include <sys/param.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#define QFIL ".q"

void  parsein(FILE *);
FILE *gethandle(int);
void  puthandle(FILE *);
void  sync(void);
void  clear(void);
void  usage(void) __attribute__((__noreturn__));

struct act {
	char *cmd;
	void (*func)(void);
} acts[] = {
	{ "sync", sync },
	{ "clear", clear },
	{ NULL, NULL }
};

struct fil_rec {
	char			*fn;
	SLIST_ENTRY(fil_rec)	 next;
};

struct tm_rec {
	time_t			 tm;
	SLIST_HEAD(, fil_rec)	 fh;
	SLIST_ENTRY(tm_rec)	 next;
};

char *cvsroot = NULL;
char qfn[MAXPATHLEN];

int
main(int argc, char *argv[])
{
	struct act *t;
	char *home;
	int c;

	while ((c = getopt(argc, argv, "d:")) != -1)
		switch (c) {
		case 'd':
			cvsroot = optarg;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	argc -= optind;
	argv += optind;

	if ((home = getenv("HOME")) == NULL)
		home = "";
	(void)strlcpy(qfn, home, sizeof(qfn));
	(void)strlcat(qfn, "/", sizeof(qfn));
	(void)strlcpy(qfn, QFIL, sizeof(qfn));

	if (argc) {
		for (t = acts; t->cmd != NULL; t++) {
			if (strcasecmp(argv[0], t->cmd) == 0) {
				t->func();
				break;
			}
		}
		if (t->cmd == NULL)
			errx(EX_USAGE, "%s: unknown command", argv[0]);
	} else
		parsein(stdin);
	exit(EX_OK);
}

void
parsein(FILE *fp)
{
	char dir[MAXPATHLEN], *p, *ent, buf[BUFSIZ];
	FILE *outfp;
	time_t tm;

	outfp = gethandle(LOCK_EX);

	strlcpy(dir, "", sizeof(dir));	/* shouldn't be needed */

	tm = time(NULL);
	while ((ent = fgets(buf, sizeof(buf), fp)) != NULL) {
		if ((p = strchr(ent, ':')) != NULL) {
			*p = '\0';
			while (isspace(*ent))
				ent++;
			(void)strlcpy(dir, ent, sizeof(dir));
			ent = ++p;
		}
		for (; ent != NULL && *ent != '\0'; ) {
			while (isspace(*ent))
				ent++;
			p = strsep(&ent, " \n");
			(void)fprintf(outfp, "%d %s/%s\n", tm, dir, p);
		}
	}
	puthandle(outfp);
}

void
clear(void)
{
	FILE *fp;

	fp = gethandle(LOCK_EX);
	if (ftruncate(fileno(fp), 0) == -1)
		err(EX_OSERR, "%s", qfn);
	(void)fclose(fp);
}

void
sync(void)
{
	char cmd[BUFSIZ], *fn, buf[BUFSIZ], tmbuf[BUFSIZ];
	SLIST_HEAD(, tm_rec) tmh;
	struct tm_rec *tmr;
	struct fil_rec *fr;
	time_t tm;
	FILE *fp;

	SLIST_INIT(&tmh);

	fp = gethandle(LOCK_SH);
	while (fgets(buf, sizeof(fp), fp) != NULL) {
		if ((fn = strchr(buf, ' ')) == NULL)
			errx(1, "qfile: bad file format");
		*fn++ = '\0';
		tm = strtoul(buf, NULL, 0);
		SLIST_FOREACH(tmr, &tmh, next)
			if (tmr->tm == tm)
				break;
		if (tmr == NULL) {
			if ((tmr = malloc(sizeof(*tmr))) == NULL)
				err(1, "malloc");
			tmr->tm = tm;
			SLIST_INSERT_HEAD(&tmh, tmr, next);
			SLIST_INIT(&tmr->fh);
		}
		if ((fn = strdup(fn)) == NULL)
			err(1, "strdup");
		if ((fr = malloc(sizeof(*fr))) == NULL)
			err(1, "malloc");
		SLIST_INSERT_HEAD(&tmr->fh, fr, next);
	}
	puthandle(fp);

	SLIST_FOREACH(tmr, &tmh, next) {
		tmr->tm--;
		(void)strlcpy(tmbuf, ctime(&tmr->tm), sizeof(tmbuf));
		tmr->tm += 2;
		(void)snprintf(cmd, sizeof(cmd),
		    "cvs %s %s -D %s -D %s",
		    cvsroot == NULL ? "" : "-d",
		    cvsroot == NULL ? "" : cvsroot,
		    tmbuf, ctime(&tmr->tm));
		SLIST_FOREACH(fr, &tmr->fh, next) {
			(void)strlcat(cmd, " ", sizeof(cmd));
			(void)strlcat(cmd, fr->fn, sizeof(cmd));
		}
		(void)system(cmd);
		(void)sleep(1);
	}
}

#if 0
void
qfil(const char *fn)
{
	char *line;
	FILE *fp;
	int save;

	save = 1;
	fp = gethandle(LOCK_EX);
	while (save && (line = fgetline(fp)) != NULL) {
		/* XXX:  use glob. */
		if (strcmp(line, fn) == 0)
			save = 0;
		free(line);
	}
	if (save)
		(void)fprintf(fp, "%s\n", fn);
	puthandle(fp);
}

void
listq(void)
{
	FILE *fp;
	char buf[BUFSIZ];

	fp = gethandle(LOCK_SH);
	while (fgets(buf, sizeof(buf), fp) != NULL)
		(void)puts(buf);
	puthandle(fp);
}
#endif

FILE *
gethandle(int op)
{
	char fn[MAXPATHLEN];
	FILE *fp;

	if ((fp = fopen(qfn, "r+")) == NULL) {
		if (errno == ENOENT) {
			if ((fp = fopen(fn, "w+")) == NULL)
				err(EX_OSERR, "%s", fn);
		} else
			err(EX_OSERR, "%s", fn);
	}
	if (flock(fileno(fp), op | LOCK_NB) == -1)
		err(EX_OSERR, "flock %s", fn);
	return (fp);
}

void
puthandle(FILE *fp)
{
	(void)flock(fileno(fp), LOCK_UN);
	(void)fclose(fp);
}

#define ADJ 30

char *
fgetline(FILE *fp)
{
	char *dup, *ln;
	size_t cur, max;
	int c;

	cur = max = -1;
	ln = NULL;
	for (;;) {
		if (cur++ >= max) {
			max += ADJ;
			dup = realloc(ln, max * sizeof(*ln));
			if (dup == NULL)
				err(EX_OSERR, "realloc");
		}
		c = fgetc(fp);
		if (c == EOF)
			break;
		ln[cur] = c;
	}
	if (cur++ >= max) {
		max++;
		dup = realloc(ln, max * sizeof(*ln));
		if (dup == NULL)
			err(EX_OSERR, "realloc");
	}
	ln[cur] = '\0';
	return (ln);
}

void
usage(void)
{
	extern char *__progname;

	(void)fprintf(stderr, "usage: %s [-d cvsroot] "
	    "[command argument ...]\n", __progname);
	exit(EX_USAGE);
}
