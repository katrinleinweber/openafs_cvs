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

RCSID("$Header: /cvs/openafs/src/venus/cmdebug.c,v 1.7 2002/03/16 22:17:29 kolya Exp $");


#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#ifdef	AFS_AIX32_ENV
#include <signal.h>
#endif
#include <afs/afscbint.h>
#include <afs/cmd.h>
#include <rx/rx.h>
#include <lock.h>

/* XXX is this kosher? */
#include <afs_args.h>

extern struct rx_securityClass *rxnull_NewServerSecurityObject();
extern struct hostent *hostutil_GetHostByName();

static PrintCacheConfig(aconn)
    struct rx_connection *aconn;
{
    struct cacheConfig c;
    afs_uint32 srv_ver, conflen;
    int code;

    c.cacheConfig_len = 0;
    code = RXAFSCB_GetCacheConfig(aconn, 1, &srv_ver, &conflen, &c);
    if (code) {
	printf("cmdebug: error checking cache config: %s\n",
	       error_message(code));
	return 0;
    }

    if (srv_ver == AFS_CLIENT_RETRIEVAL_FIRST_EDITION) {
	struct cm_initparams_v1 *c1;

	if (c.cacheConfig_len != sizeof(*c1) / sizeof(afs_uint32)) {
	    printf("cmdebug: configuration data size mismatch (%d != %d)\n",
		   c.cacheConfig_len, sizeof(*c1) / sizeof(afs_uint32));
	    return 0;
	}

	c1 = (struct cm_initparams_v1 *) c.cacheConfig_val;
	printf("Chunk files:   %d\n", c1->nChunkFiles);
	printf("Stat caches:   %d\n", c1->nStatCaches);
	printf("Data caches:   %d\n", c1->nDataCaches);
	printf("Volume caches: %d\n", c1->nVolumeCaches);
	printf("Chunk size:    %d", c1->otherChunkSize);
	if (c1->firstChunkSize != c1->otherChunkSize)
	    printf(" (first: %d)", c1->firstChunkSize);
	printf("\n");
	printf("Cache size:    %d kB\n", c1->cacheSize);
	printf("Set time:      %s\n", c1->setTime ? "yes" : "no");
	printf("Cache type:    %s\n", c1->memCache ? "memory" : "disk");
    } else {
	printf("cmdebug: unsupported server version %d\n", srv_ver);
    }
}

static PrintInterfaces(aconn)
    struct rx_connection *aconn;
{
    struct interfaceAddr addr;
    int i, code;

    code = RXAFSCB_WhoAreYou(aconn, &addr);
    if (code) {
	printf("cmdebug: error checking interfaces: %s\n", error_message(code));
	return 0;
    }

    printf("Host interfaces:\n");
    for (i=0; i<addr.numberOfInterfaces; i++) {
	printf("%s", inet_ntoa(&addr.addr_in[i]));
	if (addr.subnetmask[i])
	    printf(", netmask %s", inet_ntoa(&addr.subnetmask[i]));
	if (addr.mtu[i])
	    printf(", MTU %d", addr.mtu[i]);
	printf("\n");
    }

    return 0;
}

static IsLocked(alock)
register struct AFSDBLockDesc *alock; {
    if (alock->waitStates || alock->exclLocked
	|| alock->numWaiting || alock->readersReading)
	return 1;
    return 0;
}

static PrintLock(alock)
register struct AFSDBLockDesc *alock; {
    printf("(");
    if (alock->waitStates) {
	if (alock->waitStates & READ_LOCK)
	    printf("reader_waiting");
	if (alock->waitStates & WRITE_LOCK)
	    printf("writer_waiting");
	if (alock->waitStates & SHARED_LOCK)
	    printf("upgrade_waiting");
    }
    else
	printf("none_waiting");
    if (alock->exclLocked) {
	if (alock->exclLocked & WRITE_LOCK)
	    printf(", write_locked");
	if (alock->exclLocked & SHARED_LOCK)
	    printf(", upgrade_locked");
        printf("(pid:%d at:%d)", alock->pid_writer, alock->src_indicator);
    }
    if (alock->readersReading)
	printf(", %d read_locks(pid:%d)", alock->readersReading,alock->pid_last_reader);
    if (alock->numWaiting) printf(", %d waiters", alock->numWaiting);
    printf(")");
    return 0;
}

static PrintLocks(aconn, aint32)
int aint32;
register struct rx_connection *aconn; {
    register int i;
    struct AFSDBLock lock;
    afs_int32 code;

    for(i=0;i<1000;i++) {
	code = RXAFSCB_GetLock(aconn, i, &lock);
	if (code) {
	    if (code == 1) break;
	    /* otherwise we have an unrecognized error */
	    printf("cmdebug: error checking locks: %s\n", error_message(code));
	    return code;
	}
	/* here we have the lock information, so display it, perhaps */
	if (aint32 || IsLocked(&lock.lock)) {
	    printf("Lock %s status: ", lock.name);
	    PrintLock(&lock.lock);
	    printf("\n");
	}
    }
    return 0;
}

static PrintCacheEntries(aconn, aint32)
int aint32;
register struct rx_connection *aconn; {
    register int i;
    register afs_int32 code;
    struct AFSDBCacheEntry centry;

    for(i=0;i<10000;i++) {
	code = RXAFSCB_GetCE(aconn, i, &centry);
	if (code) {
	    if (code == 1) break;
	    printf("cmdebug: failed to get cache entry %d (%s)\n", i,
		   error_message(code));
	    return code;
	}

	if (centry.addr == 0) {
	    /* PS output */
	    printf("Proc %4d sleeping at %08x, pri %3d\n",
		   centry.netFid.Vnode, centry.netFid.Volume, centry.netFid.Unique-25);
	    continue;
	}

	if (!aint32 && !IsLocked(&centry.lock)) continue;

	/* otherwise print this entry */
	printf("** Cache entry @ 0x%08x for %d.%d.%d.%d\n", centry.addr, centry.cell,
	       centry.netFid.Volume, centry.netFid.Vnode, centry.netFid.Unique);
	if (IsLocked(&centry.lock)) {
	    printf("    locks: ");
	    PrintLock(&centry.lock);
	    printf("\n");
	}
	printf("    %d bytes\tDV %d refcnt %d\n", centry.Length, centry.DataVersion, centry.refCount);
	printf("    callback %08x\texpires %u\n", centry.callback, centry.cbExpires);
	printf("    %d opens\t%d writers\n", centry.opens, centry.writers);

	/* now display states */
	printf("    ");
	if (centry.mvstat == 0) printf("normal file");
	else if (centry.mvstat == 1) printf("mount point");
	else if (centry.mvstat == 2) printf("volume root");
	else printf("bogus mvstat %d", centry.mvstat);
	printf("\n    states (0x%x)", centry.states);
	if (centry.states & 1) printf(", stat'd");
	if (centry.states & 2) printf(", backup");
	if (centry.states & 4) printf(", read-only");
	if (centry.states & 8) printf(", mt pt valid");
	if (centry.states & 0x10) printf(", pending core");
	if (centry.states & 0x40) printf(", wait-for-store");
	if (centry.states & 0x80) printf(", mapped");
	printf("\n");
    }
    return 0;
}

static CommandProc(as)
struct cmd_syndesc *as; {
    struct rx_connection *conn;
    register char *hostName;
    register struct hostent *thp;
    afs_int32 port;
    struct rx_securityClass *secobj;
    int int32p;
    afs_int32 addr;

    hostName = as->parms[0].items->data;
    if (as->parms[1].items)
	port = atoi(as->parms[1].items->data);
    else
	port = 7001;
    thp = hostutil_GetHostByName(hostName);
    if (!thp) {
	printf("cmdebug: can't resolve address for host %s.\n", hostName);
	exit(1);
    }
    memcpy(&addr, thp->h_addr, sizeof(afs_int32));
    secobj = rxnull_NewServerSecurityObject();
    conn = rx_NewConnection(addr, htons(port), 1, secobj, 0);
    if (!conn) {
	printf("cmdebug: failed to create connection for host %s\n", hostName);
	exit(1);
    }
    if (as->parms[3].items) {
	/* -addrs */
	PrintInterfaces(conn);
	return 0;
    }
    if (as->parms[4].items) {
	/* -cache */
	PrintCacheConfig(conn);
	return 0;
    }
    if (as->parms[2].items) int32p = 1;
    else int32p = 0;
    PrintLocks(conn, int32p);
    PrintCacheEntries(conn, int32p);
    return 0;
}

#include "AFS_component_version_number.c"

main(argc, argv)
int argc;
char **argv; {
    register struct cmd_syndesc *ts;

#ifdef	AFS_AIX32_ENV
    /*
     * The following signal action for AIX is necessary so that in case of a 
     * crash (i.e. core is generated) we can include the user's data section 
     * in the core dump. Unfortunately, by default, only a partial core is
     * generated which, in many cases, isn't too useful.
     */
    struct sigaction nsa;
    
    sigemptyset(&nsa.sa_mask);
    nsa.sa_handler = SIG_DFL;
    nsa.sa_flags = SA_FULLDUMP;
    sigaction(SIGSEGV, &nsa, NULL);
#endif
    rx_Init(0);

    ts = cmd_CreateSyntax((char *) 0, CommandProc, 0, "probe unik server");
    cmd_AddParm(ts, "-servers", CMD_SINGLE, CMD_REQUIRED, "server machine");
    cmd_AddParm(ts, "-port", CMD_SINGLE, CMD_OPTIONAL, "IP port");
    cmd_AddParm(ts, "-long", CMD_FLAG, CMD_OPTIONAL, "print all info");
    cmd_AddParm(ts, "-addrs", CMD_FLAG, CMD_OPTIONAL, "print only host interfaces");
    cmd_AddParm(ts, "-cache", CMD_FLAG, CMD_OPTIONAL, "print only cache configuration");

    cmd_Dispatch(argc, argv);
    exit(0);
}
