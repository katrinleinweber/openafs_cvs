/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * rx_kmutex.c - mutex and condition variable macros for kernel environment.
 *
 * MACOS implementation.
 */

#include <afsconfig.h>
#include "afs/param.h"

RCSID
    ("$Header: /cvs/openafs/src/rx/DARWIN/rx_kmutex.c,v 1.4 2005/05/08 06:49:52 shadow Exp $");

#ifndef AFS_DARWIN80_ENV
/*
 * Currently everything is implemented in rx_kmutex.h
 */
#else
lck_grp_t * openafs_lck_grp;
static lck_grp_attr_t * openafs_lck_grp_attr;
void rx_kmutex_setup(void) {
    openafs_lck_grp_attr= lck_grp_attr_alloc_init();
    lck_grp_attr_setstat(openafs_lck_grp_attr);

    openafs_lck_grp = lck_grp_alloc_init("openafs",  openafs_lck_grp_attr);
    lck_grp_attr_free(openafs_lck_grp_attr);
    
}

void rx_kmutex_finish(void) {
    lck_grp_free(openafs_lck_grp);
}

#endif


