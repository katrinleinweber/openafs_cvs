/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afs/param.h>
#include <afsconfig.h>

RCSID("$Header: /cvs/openafs/src/venus/gcpags.c,v 1.3 2001/07/05 15:21:11 shadow Exp $");

#include <rx/xdr.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <afs/stds.h>
#include <afs/vice.h>
#include <afs/venus.h>
#ifdef	AFS_AIX32_ENV
#include <signal.h>
#endif
#undef VIRTUE
#undef VICE
#include "afs/prs_fs.h"
#include <afs/afsint.h>
#include <afs/auth.h>
#include <afs/cellconfig.h>
#include <afs/cmd.h>
#include <strings.h>




#include "AFS_component_version_number.c"

main(argc, argv)
int argc;
char **argv; {
    afs_int32 code=0;
    struct ViceIoctl blob;
    
    blob.in = 0;
    blob.out = 0;
    blob.in_size = 0;
    blob.out_size = 0;
    code = pioctl(0, VIOC_GCPAGS, &blob, 1);
    if(code)
	perror("disable gcpags failed");

    exit(code);
}
