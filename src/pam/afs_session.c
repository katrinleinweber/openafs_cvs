/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afsconfig.h>
#include <afs/param.h>

RCSID("$Header: /cvs/openafs/src/pam/afs_session.c,v 1.4 2001/07/12 19:58:53 shadow Exp $");

#include <security/pam_appl.h>
#include <security/pam_modules.h>

extern int
pam_sm_open_session(
	pam_handle_t	*pamh,
	int		flags,
	int		argc,
	const char	**argv)
{
    return PAM_SUCCESS;
}


extern int
pam_sm_close_session(
	pam_handle_t	*pamh,
	int		flags,
	int		argc,
	const char	**argv)
{
    return PAM_SUCCESS;
}
