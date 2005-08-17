/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afsconfig.h>
#include "afs/param.h"

RCSID
    ("$Header: /cvs/openafs/src/afs/afs_chunk.c,v 1.7 2005/08/17 16:16:50 rees Exp $");

#include "afs/stds.h"
#include "afs/sysincludes.h"	/* Standard vendor system headers */
#include "afsincludes.h"	/* Afs-based standard headers */
#include "afs/afs_stats.h"

/*
 * Chunk module.
 */

/* Place the defaults in afsd instead of all around the code, so
 * AFS_SETCHUNKSIZE() needs to be called before doing anything */
afs_int32 afs_FirstCSize = 0;
afs_int32 afs_OtherCSize = 0;
afs_int32 afs_LogChunk = 0;
