/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <stdio.h>
#include <assert.h>
#include <security/pam_appl.h>
#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/pam/afs_util.c,v 1.4 2001/07/12 19:58:53 shadow Exp $");

#include "afs_util.h"


char	*pam_afs_ident		= "pam_afs";
char	*pam_afs_lh		= "TRANSARC_PAM_AFS_AUTH_login_handle";


void lc_cleanup(
	pam_handle_t	*pamh,
	void		*data,
	int		pam_end_status)
{
    if ( data )
    {
	memset(data, 0, strlen(data));
	free(data);
    }
}


void nil_cleanup(
	pam_handle_t	*pamh,
	void		*data,
	int		pam_end_status)
{
    return;
}

/* The PAM module needs to be free from libucb dependency. Otherwise, 
dynamic linking is a problem, the AFS PAM library refuses to coexist
with the DCE library. The sigvec() and sigsetmask() are the only two
calls that neccesiate the inclusion of libucb.a.  There are used by
the lwp library to support premeptive threads and signalling between 
threads. Since the lwp support used by the PAM module uses none of 
these facilities, we can safely define these to be null functions */

#if !defined(AFS_HPUX110_ENV)
/* For HP 11.0, this function is in util/hputil.c */
sigvec()
{
	assert(0);
}
#endif  /* AFS_HPUX110_ENV */
sigsetmask()
{
	assert(0);
}

/* converts string to integer */

char *cv2string(ttp, aval)
    register char *ttp;
    register unsigned long aval;
{
    register char *tp = ttp;
    register int  i;
    int any = 0;

    *(--tp) = 0;
    while (aval != 0) {
        i = aval % 10;
        *(--tp) = '0' + i;
        aval /= 10;
        any = 1;
    }
    if (!any)
        *(--tp) = '0';
    return tp;
}

