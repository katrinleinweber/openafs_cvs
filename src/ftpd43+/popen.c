/*
 * Copyright (c) 1988 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software written by Ken Arnold and
 * published in UNIX Review, Vol. 6, No. 8.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#include <afs/param.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <stdio.h>

#ifndef	AFS_AIX32_ENV
#ifndef	FD_SETSIZE	/* XXX */
typedef	u_short uid_t;
#endif
#endif

/*
 * Special version of popen which avoids call to shell.  This insures noone
 * may create a pipe to a hidden program as a side effect of a list or dir
 * command.
 */
static uid_t *pids;
static int fds;

FILE *
ftpd_popen(program, type)
	char *program, *type;
{
	register char *cp;
	FILE *iop;
	int argc, gargc, pdes[2], pid;
	char **pop, *argv[100], *gargv[1000], *vv[2];
	extern char **glob(), **copyblk(), *strtok();

	if (*type != 'r' && *type != 'w' || type[1])
		return(NULL);

	if (!pids) {
		if ((fds = getdtablesize()) <= 0)
			return(NULL);
		if (!(pids =
		    (uid_t *)malloc((u_int)(fds * sizeof(uid_t)))))
			return(NULL);
		memset(pids, 0, fds * sizeof(uid_t));
	}
	if (pipe(pdes) < 0)
		return(NULL);

	/* break up string into pieces */
	for (argc = 0, cp = program;; cp = NULL)
		if (!(argv[argc++] = strtok(cp, " \t\n")))
			break;

	/* glob each piece */
	gargv[0] = argv[0];
	for (gargc = argc = 1; argv[argc]; argc++) {
		if (!(pop = glob(argv[argc]))) {	/* globbing failed */
			vv[0] = argv[argc];
			vv[1] = NULL;
			pop = copyblk(vv);
		}
		argv[argc] = (char *)pop;		/* save to free later */
		while (*pop && gargc < 1000)
			gargv[gargc++] = *pop++;
	}
	gargv[gargc] = NULL;

	iop = NULL;
	switch(pid = vfork()) {
	case -1:			/* error */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		goto free;
		/* NOTREACHED */
	case 0:				/* child */
		if (*type == 'r') {
			if (pdes[1] != 1) {
				dup2(pdes[1], 1);
				(void)close(pdes[1]);
			}
			(void)close(pdes[0]);
		} else {
			if (pdes[0] != 0) {
				dup2(pdes[0], 0);
				(void)close(pdes[0]);
			}
			(void)close(pdes[1]);
		}
		execv(gargv[0], gargv);
		_exit(1);
	}
	/* parent; assume fdopen can't fail...  */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		(void)close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		(void)close(pdes[0]);
	}
	pids[fileno(iop)] = pid;

free:	for (argc = 1; argv[argc] != NULL; argc++)
		blkfree((char **)argv[argc]);
	return(iop);
}

ftpd_pclose(iop)
	FILE *iop;
{
	register int fdes;
	sigset_t oset;
	sigset_t sigBlock;
	int someSignals[100];
	int pid, stat_loc;
	u_int waitpid();

	/*
	 * pclose returns -1 if stream is not associated with a
	 * `popened' command, or, if already `pclosed'.
	 */
	if (pids[fdes = fileno(iop)] == 0)
		return(-1);
	(void)fclose(iop);
	memset((char *)someSignals, 0, sizeof(someSignals));
	someSignals[0] = (1<<(SIGINT-1)) + (1<<(SIGQUIT-1)) + (1<<(SIGHUP-1));
	sigBlock = *((sigset_t *) someSignals);
	sigprocmask(SIG_BLOCK, &sigBlock, &oset);
	while ((pid = wait(&stat_loc)) != pids[fdes] && pid != -1);
	sigprocmask(SIG_SETMASK, &oset, (sigset_t *)0);
	pids[fdes] = 0;
	return(stat_loc);
}
