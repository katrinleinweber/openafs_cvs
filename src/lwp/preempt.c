/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*******************************************************************\
* 								    *
* 	Information Technology Center				    *
* 	Carnegie-Mellon University				    *
* 								    *
\*******************************************************************/
#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/lwp/preempt.c,v 1.13 2002/08/21 18:13:42 shadow Exp $");


#include "lwp.h"
#include "preempt.h"

#if defined(AFS_LINUX20_ENV) || defined(AFS_DARWIN_ENV) || defined(AFS_XBSD_ENV) || defined(AFS_DJGPP_ENV)
int PRE_Block = 0;


int PRE_InitPreempt(slice)
    struct timeval *slice;
{
    return LWP_SUCCESS;
}

int PRE_EndPreempt()
{
    return LWP_SUCCESS;
}

#else
#include <sys/time.h>
#include <signal.h>
#include <ucontext.h>

#if defined(AFS_OSF_ENV) || defined(AFS_S390_LINUX20_ENV)
int PRE_Block = 0;		/* used in lwp.c and process.s */
#else
char PRE_Block = 0;		/* used in lwp.c and process.s */
#endif

static void AlarmHandler(sig, st, scp)
    int sig;
    siginfo_t *st;
    ucontext_t *scp;
    {
    if (PRE_Block == 0 && lwp_cpptr->level == 0)
	{
	PRE_BeginCritical();
	sigprocmask(SIG_SETMASK, &scp->uc_sigmask, NULL);
	LWP_DispatchProcess();
	PRE_EndCritical();
	}
    
    }

int PRE_InitPreempt(slice)
    struct timeval *slice;
    {
    struct itimerval itv;
    struct sigaction action;

    if (lwp_cpptr == 0) return (LWP_EINIT);
    
    if (slice == 0)
	{
	itv.it_interval.tv_sec = itv.it_value.tv_sec = DEFAULTSLICE;
	itv.it_interval.tv_usec = itv.it_value.tv_usec = 0;
	}
    else
	{
	itv.it_interval = itv.it_value = *slice;
	}

    memset((char *)&action, 0, sizeof(action));
    action.sa_sigaction = AlarmHandler;
    action.sa_flags = SA_SIGINFO;

    if ((sigaction(SIGALRM, &action, NULL) == -1) ||
	(setitimer(ITIMER_REAL, &itv, NULL) == -1))
	return(LWP_ESYSTEM);

    return(LWP_SUCCESS);
    }

int PRE_EndPreempt()
    {
    struct itimerval itv;
    struct sigaction action;

    if (lwp_cpptr == 0) return (LWP_EINIT);
    
    itv.it_value.tv_sec = itv.it_value.tv_usec = 0;

    memset((char *)&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;

    if ((setitimer(ITIMER_REAL, &itv, NULL) == -1) ||
	(sigaction(SIGALRM, &action, NULL) == -1))
	return(LWP_ESYSTEM);

    return(LWP_SUCCESS);
    }

#endif /* AFS_LINUX20_ENV */
