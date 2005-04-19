/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <afs/param.h>
#include <afs/stds.h>

#ifndef DJGPP
#include <windows.h>
#include <winsock2.h>
#else
#include <netdb.h>
#endif /* !DJGPP */
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#include <rx/rx.h>
#include <rx/rx_prototypes.h>

#include "afsd.h"

long cm_daemonCheckInterval = 30;
long cm_daemonTokenCheckInterval = 180;

osi_rwlock_t cm_daemonLock;

long cm_bkgQueueCount;		/* # of queued requests */

int cm_bkgWaitingForCount;	/* true if someone's waiting for cm_bkgQueueCount to drop */

cm_bkgRequest_t *cm_bkgListp;		/* first elt in the list of requests */
cm_bkgRequest_t *cm_bkgListEndp;	/* last elt in the list of requests */

static int daemon_ShutdownFlag = 0;

void cm_BkgDaemon(long parm)
{
    cm_bkgRequest_t *rp;

    rx_StartClientThread();

    lock_ObtainWrite(&cm_daemonLock);
    while (daemon_ShutdownFlag == 0) {
        if (!cm_bkgListEndp) {
            osi_SleepW((long) &cm_bkgListp, &cm_daemonLock);
            lock_ObtainWrite(&cm_daemonLock);
            continue;
        }
                
        /* we found a request */
        rp = cm_bkgListEndp;
        cm_bkgListEndp = (cm_bkgRequest_t *) osi_QPrev(&rp->q);
        osi_QRemove((osi_queue_t **) &cm_bkgListp, &rp->q);
        osi_assert(cm_bkgQueueCount-- > 0);
        lock_ReleaseWrite(&cm_daemonLock);

        (*rp->procp)(rp->scp, rp->p1, rp->p2, rp->p3, rp->p4, rp->userp);
                
        cm_ReleaseUser(rp->userp);
        cm_ReleaseSCache(rp->scp);
        free(rp);

        lock_ObtainWrite(&cm_daemonLock);
    }
    lock_ReleaseWrite(&cm_daemonLock);
}

void cm_QueueBKGRequest(cm_scache_t *scp, cm_bkgProc_t *procp, long p1, long p2, long p3, long p4,
	cm_user_t *userp)
{
    cm_bkgRequest_t *rp;
        
    rp = malloc(sizeof(*rp));
    memset(rp, 0, sizeof(*rp));
        
    cm_HoldSCache(scp);
    rp->scp = scp;
    cm_HoldUser(userp);
    rp->userp = userp;
    rp->procp = procp;
    rp->p1 = p1;
    rp->p2 = p2;
    rp->p3 = p3;
    rp->p4 = p4;

    lock_ObtainWrite(&cm_daemonLock);
    cm_bkgQueueCount++;
    osi_QAdd((osi_queue_t **) &cm_bkgListp, &rp->q);
    if (!cm_bkgListEndp) 
        cm_bkgListEndp = rp;
    lock_ReleaseWrite(&cm_daemonLock);

    osi_Wakeup((long) &cm_bkgListp);
}

/* periodic check daemon */
void cm_Daemon(long parm)
{
    unsigned long now;
    unsigned long lastLockCheck;
    unsigned long lastVolCheck;
    unsigned long lastCBExpirationCheck;
    unsigned long lastDownServerCheck;
    unsigned long lastUpServerCheck;
    unsigned long lastTokenCacheCheck;
    char thostName[200];
    unsigned long code;
    struct hostent *thp;
    HMODULE hHookDll;

    /* ping all file servers, up or down, with unauthenticated connection,
     * to find out whether we have all our callbacks from the server still.
     * Also, ping down VLDBs.
     */
    /*
     * Seed the random number generator with our own address, so that
     * clients starting at the same time don't all do vol checks at the
     * same time.
     */
    gethostname(thostName, sizeof(thostName));
    thp = gethostbyname(thostName);
    if (thp == NULL)    /* In djgpp, gethostname returns the netbios
                           name of the machine.  gethostbyname will fail
                           looking this up if it differs from DNS name. */
        code = 0;
    else
        memcpy(&code, thp->h_addr_list[0], 4);
    srand(ntohl(code));

    now = osi_Time();
    lastVolCheck = now - 1800 + (rand() % 3600);
    lastCBExpirationCheck = now - 60 + (rand() % 60);
    lastLockCheck = now - 60 + (rand() % 60);
    lastDownServerCheck = now - cm_daemonCheckInterval/2 + (rand() % cm_daemonCheckInterval);
    lastUpServerCheck = now - 1800 + (rand() % 3600);
    lastTokenCacheCheck = now - cm_daemonTokenCheckInterval/2 + (rand() % cm_daemonTokenCheckInterval);

    while (daemon_ShutdownFlag == 0) {
        thrd_Sleep(30 * 1000);		/* sleep 30 seconds */
        if (daemon_ShutdownFlag == 1)
            return;

        /* find out what time it is */
        now = osi_Time();

        /* check down servers */
        if (now > lastDownServerCheck + cm_daemonCheckInterval) {
            lastDownServerCheck = now;
            cm_CheckServers(CM_FLAG_CHECKDOWNSERVERS, NULL);
        }

        /* check up servers */
        if (now > lastUpServerCheck + 3600) {
            lastUpServerCheck = now;
            cm_CheckServers(CM_FLAG_CHECKUPSERVERS, NULL);
        }

        if (now > lastVolCheck + 3600) {
            lastVolCheck = now;
            cm_CheckVolumes();
        }

        if (now > lastCBExpirationCheck + 60) {
            lastCBExpirationCheck = now;
            cm_CheckCBExpiration();
        }

        if (now > lastLockCheck + 60) {
            lastLockCheck = now;
            cm_CheckLocks();
        }

        if (now > lastTokenCacheCheck + cm_daemonTokenCheckInterval) {
            lastTokenCacheCheck = now;
            cm_CheckTokenCache(now);
        }

        /* allow an exit to be called prior to stopping the service */
        hHookDll = LoadLibrary(AFSD_HOOK_DLL);
        if (hHookDll)
        {
            BOOL hookRc = TRUE;
            AfsdDaemonHook daemonHook = ( AfsdDaemonHook ) GetProcAddress(hHookDll, AFSD_DAEMON_HOOK);
            if (daemonHook)
            {
                hookRc = daemonHook();
            }
            FreeLibrary(hHookDll);
            hHookDll = NULL;

            if (hookRc == FALSE)
            {
                SetEvent(WaitToTerminate);
            }
        }
    }
}       

void cm_DaemonShutdown(void)
{
    daemon_ShutdownFlag = 1;
}

void cm_InitDaemon(int nDaemons)
{
    static osi_once_t once;
    long pid;
    thread_t phandle;
    int i;
        
    if (osi_Once(&once)) {
        lock_InitializeRWLock(&cm_daemonLock, "cm_daemonLock");
        osi_EndOnce(&once);
                
        /* creating pinging daemon */
        phandle = thrd_Create((SecurityAttrib) 0, 0,
                               (ThreadFunc) cm_Daemon, 0, 0, &pid, "cm_Daemon");
        osi_assert(phandle != NULL);

        thrd_CloseHandle(phandle);
        for(i=0; i < nDaemons; i++) {
            phandle = thrd_Create((SecurityAttrib) 0, 0,
                                   (ThreadFunc) cm_BkgDaemon, 0, 0, &pid,
                                   "cm_BkgDaemon");
            osi_assert(phandle != NULL);
            thrd_CloseHandle(phandle);
        }
    }
}
