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

RCSID("$Header: /cvs/openafs/src/util/sys.c,v 1.4 2001/07/05 15:21:06 shadow Exp $");
 
#include <stdio.h>

#include "AFS_component_version_number.c"

int main ()
{
    printf ("%s\n", SYS_NAME);
    return 0;
}
