/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * This file contains sample code for the rxstats interface 
 */

#include <afs/param.h>
#include <afsconfig.h>

RCSID("$Header: /cvs/openafs/src/libadmin/samples/rxdebug_supported_stats.c,v 1.3 2001/07/05 15:20:32 shadow Exp $");

#ifdef AFS_NT40_ENV
#include <winsock2.h>
#include <pthread.h>
#endif
#include <afs/afs_Admin.h>
#include <afs/afs_clientAdmin.h>
#include <afs/afs_utilAdmin.h>

void Usage()
{
    fprintf(stderr,
	    "Usage: rxdebug_supported_stats <host> <port>\n");
    exit(1);
}

void ParseArgs(
    int argc,
    char *argv[],
    char **srvrName,
    long *srvrPort)
{
    char **argp = argv;

    if (!*(++argp))
	Usage();
    *srvrName = *(argp++);
    if (!*(argp))
	Usage();
    *srvrPort = strtol(*(argp++), NULL, 0);
    if (*srvrPort <= 0 || *srvrPort >= 65536)
	Usage();
    if (*(argp))
	Usage();
}

int main(int argc, char *argv[])
{
    int rc;
    afs_status_t st = 0;
    rxdebugHandle_p handle;
    char *srvrName;
    long srvrPort;
    afs_uint32 supported;

    ParseArgs(argc, argv, &srvrName, &srvrPort);

    rc = afsclient_Init(&st);
    if (!rc) {
	fprintf(stderr, "afsclient_Init, status %d\n", st);
	exit(1);
    }

    rc = afsclient_RXDebugOpenPort(srvrName, srvrPort, &handle, &st);
    if (!rc) {
	fprintf(stderr, "afsclient_RXDebugOpenPort, status %d\n", st);
	exit(1);
    }

    rc = util_RXDebugSupportedStats(handle, &supported, &st);
    if (!rc) {
	fprintf(stderr, "util_RXDebugSupportedStats, status %d\n", st);
	exit(1);
    }

    rc = afsclient_RXDebugClose(handle, &st);
    if (!rc) {
	fprintf(stderr, "afsclient_RXDebugClose, status %d\n", st);
	exit(1);
    }

    printf("\n");
    printf("security stats: %s supported\n",
	   (supported & RX_SERVER_DEBUG_SEC_STATS) ? "" : " not");
    printf("all connections:%s supported\n",
	   (supported & RX_SERVER_DEBUG_ALL_CONN) ? "" : " not");
    printf("rx stats:       %s supported\n",
	   (supported & RX_SERVER_DEBUG_RX_STATS) ? "" : " not");
    printf("waiter count:   %s supported\n",
	   (supported & RX_SERVER_DEBUG_WAITER_CNT) ? "" : " not");
    printf("idle threads:   %s supported\n",
	   (supported & RX_SERVER_DEBUG_IDLE_THREADS) ? "" : " not");
    printf("all peers:      %s supported\n",
	   (supported & RX_SERVER_DEBUG_ALL_PEER) ? "" : " not");
    printf("\n");

    exit(0);
}
