/* $Id$ */

#include <sys/queue.h>
#include <sys/param.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#define QBASENAME ".q"

void		 parsein(FILE *);
FILE		*gethandle(int);
void		 puthandle(FILE *);
void		 sync(void);
void		 clear(void);
__dead void	 usage(void);

struct act {
	char *cmd;
	void (*func)(void);
} acts[] = {
	{ "sync", sync },
	{ "clear", clear },
	{ NULL, NULL }
};

struct fil_rec {
	char			*fr_fn;
	SLIST_ENTRY(fil_rec)	 fr_next;
};

struct tm_rec {
	time_t			 tr_tm;
	SLIST_HEAD(, fil_rec)	 tr_fh;
	SLIST_ENTRY(tm_rec)	 tr_next;
};

char *cvsroot = NULL;
char qfn[MAXPATHLEN];

int
main(int argc, char *argv[])
{
	struct passwd *pwd;
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

	if ((home = getenv("HOME")) == NULL) {
		if ((pwd = getpwuid(getuid())) == NULL)
			home = ".";
		else
			home = pwd->pw_dir;
	}
	(void)strlcpy(qfn, home, sizeof(qfn));
	(void)strlcat(qfn, "/", sizeof(qfn));
	(void)strlcat(qfn, QBASENAME, sizeof(qfn));
	if (argc) {
		for (t = acts; t->cmd != NULL; t++) {
			if (strcmp(argv[0], t->cmd) == 0) {
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

	strlcpy(dir, "", sizeof(dir));	/* shouldn't be needed */

	tm = time(NULL);
	outfp = gethandle(LOCK_EX);
	fseek(outfp, 0, SEEK_END);
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
	char *s, cmd[BUFSIZ], *fn, buf[BUFSIZ];
	char tmbuf[BUFSIZ], tmbuf2[BUFSIZ];
	SLIST_HEAD(, tm_rec) th;
	struct fil_rec *fr;
	struct tm_rec *tr;
	struct tm *tm;
	time_t time;
	FILE *fp;

	SLIST_INIT(&th);
	fp = gethandle(LOCK_SH);
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if ((s = strchr(buf, '\n')) != NULL)
			*s = '\0';
		if ((fn = strchr(buf, ' ')) == NULL)
			errx(1, "%s: bad file format: %s", qfn, buf);
		*fn++ = '\0';
		time = (time_t)strtoul(buf, NULL, 10);
		SLIST_FOREACH(tr, &th, tr_next)
			if (tr->tr_tm == time)
				break;
		if (tr == NULL) {
			if ((tr = malloc(sizeof(*tr))) == NULL)
				err(1, "malloc");
			tr->tr_tm = time;
			SLIST_INSERT_HEAD(&th, tr, tr_next);
			SLIST_INIT(&tr->tr_fh);
		}
		if ((fr = malloc(sizeof(*fr))) == NULL)
			err(1, "malloc");
		if ((fr->fr_fn = strdup(fn)) == NULL)
			err(1, "strdup");
		SLIST_INSERT_HEAD(&tr->tr_fh, fr, fr_next);
	}
	puthandle(fp);

	SLIST_FOREACH(tr, &th, tr_next) {
		tr->tr_tm -= 12 * 60 * 60;
		tm = localtime(&tr->tr_tm);
		(void)strftime(tmbuf, sizeof(tmbuf),
		    "%b %e, %Y %H:%M", tm);

		tr->tr_tm += 13 * 60 * 60;
		tm = localtime(&tr->tr_tm);
		(void)strftime(tmbuf2, sizeof(tmbuf2),
		    "%b %e, %Y %H:%M", tm);

		(void)snprintf(cmd, sizeof(cmd),
		    "cvs %s %s -D '%s' -D '%s'",
		    cvsroot == NULL ? "" : "-d",
		    cvsroot == NULL ? "" : cvsroot,
		    tmbuf, tmbuf2);

		SLIST_FOREACH(fr, &tr->tr_fh, fr_next) {
printf("processing %s\n", fr->fr_fn);
			(void)strlcat(cmd, " ", sizeof(cmd));
			(void)strlcat(cmd, fr->fr_fn, sizeof(cmd));
		}
		(void)printf("%s\n", cmd);
		if (SLIST_NEXT(tr, tr_next))
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
	FILE *fp;

	if ((fp = fopen(qfn, "r+")) == NULL) {
		if (errno == ENOENT && op == LOCK_EX) {
			if ((fp = fopen(qfn, "w+")) == NULL)
				err(EX_OSERR, "%s", qfn);
		} else
			err(EX_OSERR, "%s", qfn);
	}
	if (flock(fileno(fp), op | LOCK_NB) == -1)
		err(EX_OSERR, "flock");
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
