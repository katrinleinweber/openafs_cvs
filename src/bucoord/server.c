/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <sys/types.h>
#include <afsconfig.h>
#include <afs/param.h>

RCSID
    ("$Header: /cvs/openafs/src/bucoord/server.c,v 1.6 2006/05/04 21:23:17 kenh Exp $");

#ifdef AFS_NT40_ENV
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif
#include <rx/rx.h>

/* services available on incoming message port */
BC_Print(acall, acode, aflags, amessage)
     struct rx_call *acall;
     afs_int32 acode, aflags;
     char *amessage;
{
    struct rx_connection *tconn;
    struct rx_peer *tpeer;

    tconn = rx_ConnectionOf(acall);
    tpeer = rx_PeerOf(tconn);
    printf("From %s: %s <%d>\n", rx_AddrStringOf(tpeer), amessage, acode);
    return 0;
}
