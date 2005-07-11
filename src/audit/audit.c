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

RCSID
    ("$Header: /cvs/openafs/src/audit/audit.c,v 1.8.2.4 2005/07/11 22:13:39 shadow Exp $");

#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#ifdef AFS_AIX32_ENV
#include <sys/audit.h>
#else
#define AUDIT_OK 0
#define AUDIT_FAIL 1
#define AUDIT_FAIL_AUTH 2
#define AUDIT_FAIL_ACCESS 3
#define AUDIT_FAIL_PRIV 4
#endif /* AFS_AIX32_ENV */
#include <errno.h>

#include "afs/afsint.h"
#include <rx/rx.h>
#include <rx/rxkad.h>
#include "audit.h"
#include "lock.h"
#ifdef AFS_AIX32_ENV
#include <sys/audit.h>
#endif
#include <afs/afsutil.h>

char *bufferPtr;
int bufferLen;
int osi_audit_all = (-1);	/* Not determined yet */
int osi_echo_trail = (-1);

FILE *auditout = NULL;

int osi_audit_check();

static void
audmakebuf(char *audEvent, va_list vaList)
{
#ifdef AFS_AIX32_ENV
    int code;
#endif
    int vaEntry;
    int vaInt;
    afs_int32 vaLong;
    char *vaStr;
    char *vaLst;
    struct AFSFid *vaFid;

    vaEntry = va_arg(vaList, int);
    while (vaEntry != AUD_END) {
	switch (vaEntry) {
	case AUD_STR:		/* String */
        case AUD_NAME:          /* Name */
        case AUD_ACL:           /* ACL */
	    vaStr = (char *)va_arg(vaList, int);
	    if (vaStr) {
		strcpy(bufferPtr, vaStr);
		bufferPtr += strlen(vaStr) + 1;
	    } else {
		strcpy(bufferPtr, "");
		bufferPtr++;
	    }
	    break;
	case AUD_INT:		/* Integer */
        case AUD_ID:            /* ViceId */
	    vaInt = va_arg(vaList, int);
	    *(int *)bufferPtr = vaInt;
	    bufferPtr += sizeof(vaInt);
	    break;
	case AUD_DATE:		/* Date    */
	case AUD_HOST:		/* Host ID */
	case AUD_LONG:		/* long    */
	    vaLong = va_arg(vaList, afs_int32);
	    *(afs_int32 *) bufferPtr = vaLong;
	    bufferPtr += sizeof(vaLong);
	    break;
	case AUD_LST:		/* Ptr to another list */
	    vaLst = (char *)va_arg(vaList, int);
	    audmakebuf(audEvent, vaLst);
	    break;
	case AUD_FID:		/* AFSFid - contains 3 entries */
	    vaFid = (struct AFSFid *)va_arg(vaList, int);
	    if (vaFid) {
		memcpy(bufferPtr, vaFid, sizeof(struct AFSFid));
	    } else {
		memset(bufferPtr, 0, sizeof(struct AFSFid));
	    }
	    bufferPtr += sizeof(struct AFSFid);
	    break;

	    /* Whole array of fids-- don't know how to handle variable length audit
	     * data with AIX audit package, so for now we just store the first fid.
	     * Better one than none. */
	case AUD_FIDS:
	    {
		struct AFSCBFids *Fids;

		Fids = (struct AFSCBFids *)va_arg(vaList, int);
		if (Fids && Fids->AFSCBFids_len) {
		    *((u_int *) bufferPtr) = Fids->AFSCBFids_len;
		    bufferPtr += sizeof(u_int);
		    memcpy(bufferPtr, Fids->AFSCBFids_val,
			   sizeof(struct AFSFid));
		} else {
		    *((u_int *) bufferPtr) = 0;
		    bufferPtr += sizeof(u_int);
		    memset(bufferPtr, 0, sizeof(struct AFSFid));
		}
		bufferPtr += sizeof(struct AFSFid);
		break;
	    }
	default:
#ifdef AFS_AIX32_ENV
	    code =
		auditlog("AFS_Aud_EINVAL", (-1), audEvent,
			 (strlen(audEvent) + 1));
#endif
	    return;
	    break;
	}			/* end switch */

	vaEntry = va_arg(vaList, int);
    }				/* end while */
}

static void
printbuf(FILE *out, int rec, char *audEvent, afs_int32 errCode, va_list vaList)
{
    int vaEntry;
    int vaInt;
    afs_int32 vaLong;
    char *vaStr;
    char *vaLst;
    struct AFSFid *vaFid;
    struct AFSCBFids *vaFids;
    int num = LogThreadNum();
    struct in_addr hostAddr;
    time_t currenttime;
    char *timeStamp;
    char tbuffer[26];
    
    /* Don't print the timestamp or thread id if we recursed */
    if (rec == 0) {
	currenttime = time(0);
	timeStamp = afs_ctime(&currenttime, tbuffer,
			      sizeof(tbuffer));
	timeStamp[24] = ' ';   /* ts[24] is the newline, 25 is the null */
	fprintf(out, timeStamp);

	if (num > -1)
	    fprintf(out, "[%d] ", num);
    }
    
    if (strcmp(audEvent, "VALST") != 0)
	fprintf(out,  "EVENT %s CODE %d ", audEvent, errCode);

    vaEntry = va_arg(vaList, int);
    while (vaEntry != AUD_END) {
	switch (vaEntry) {
	case AUD_STR:		/* String */
	    vaStr = (char *)va_arg(vaList, int);
	    if (vaStr)
		fprintf(out,  "STR %s ", vaStr);
	    else
		fprintf(out,  "STR <null>");
	    break;
	case AUD_NAME:		/* Name */
	    vaStr = (char *)va_arg(vaList, int);
	    if (vaStr)
		fprintf(out,  "NAME %s ", vaStr);
	    else
		fprintf(out,  "NAME <null>");
	    break;
	case AUD_ACL:		/* ACL */
	    vaStr = (char *)va_arg(vaList, int);
	    if (vaStr)
		fprintf(out,  "ACL %s ", vaStr);
	    else
		fprintf(out,  "ACL <null>");
	    break;
	case AUD_INT:		/* Integer */
	    vaInt = va_arg(vaList, int);
	    fprintf(out,  "INT %d ", vaInt);
	    break;
	case AUD_ID:		/* ViceId */
	    vaInt = va_arg(vaList, int);
	    fprintf(out,  "ID %d ", vaInt);
	    break;
	case AUD_DATE:		/* Date    */
	    vaLong = va_arg(vaList, afs_int32);
	    fprintf(out, "DATE %u ", vaLong);
	    break;
	case AUD_HOST:		/* Host ID */
	    vaLong = va_arg(vaList, afs_int32);
            hostAddr.s_addr = vaLong;
	    fprintf(out, "HOST %s ", inet_ntoa(hostAddr));
	    break;
	case AUD_LONG:		/* afs_int32    */
	    vaLong = va_arg(vaList, afs_int32);
	    fprintf(out, "LONG %d ", vaLong);
	    break;
	case AUD_LST:		/* Ptr to another list */
	    vaLst = (char *)va_arg(vaList, int);
	    printbuf(out, 1, "VALST", 0, vaLst);
	    break;
	case AUD_FID:		/* AFSFid - contains 3 entries */
	    vaFid = (struct AFSFid *)va_arg(vaList, int);
	    if (vaFid)
		fprintf(out, "FID %u:%u:%u ", vaFid->Volume, vaFid->Vnode,
		       vaFid->Unique);
	    else
		fprintf(out, "FID %u:%u:%u ", 0, 0, 0);
	    break;
	case AUD_FIDS:		/* array of Fids */
	    vaFids = (struct AFSCBFids *)va_arg(vaList, int);
	    vaFid = NULL;

	    if (vaFids) {
                int i;
                if (vaFid)
                    fprintf(out, "FIDS %u FID %u:%u:%u ", vaFids->AFSCBFids_len, vaFid->Volume,
                             vaFid->Vnode, vaFid->Unique);
                else
                    fprintf(out, "FIDS 0 FID 0:0:0 ");

                for ( i = 1; i < vaFids->AFSCBFids_len; i++ ) {
                    vaFid = vaFids->AFSCBFids_val;
                    if (vaFid)
                        fprintf(out, "FID %u:%u:%u ", vaFid->Volume,
                                 vaFid->Vnode, vaFid->Unique);
                    else
                        fprintf(out, "FID 0:0:0 ");
                }
            }
	    break;
	default:
	    fprintf(out, "--badval-- ");
	    break;
	}			/* end switch */
	vaEntry = va_arg(vaList, int);
    }				/* end while */

    if (strcmp(audEvent, "VALST") != 0)
	fprintf(out, "\n");
}

/* ************************************************************************** */
/* The routine that acually does the audit call.
 * ************************************************************************** */
int
osi_audit(char *audEvent,	/* Event name (15 chars or less) */
	  afs_int32 errCode,	/* The error code */
	  ...)
{
#ifdef AFS_AIX32_ENV
    afs_int32 code;
    afs_int32 err;
#endif
    int result;

    va_list vaList;
#ifdef AFS_AIX32_ENV
    static struct Lock audbuflock = { 0, 0, 0, 0,
#ifdef AFS_PTHREAD_ENV
	PTHREAD_MUTEX_INITIALIZER,
	PTHREAD_COND_INITIALIZER,
	PTHREAD_COND_INITIALIZER
#endif /* AFS_PTHREAD_ENV */
    };
    static char BUFFER[32768];
#endif

    if ((osi_audit_all < 0) || (osi_echo_trail < 0))
	osi_audit_check();
    if (!osi_audit_all && !auditout)
	return 0;

    switch (errCode) {
    case 0:
	result = AUDIT_OK;
	break;
    case KANOAUTH:		/* kautils.h   */
    case RXKADNOAUTH:		/* rxkad.h     */
	result = AUDIT_FAIL_AUTH;
	break;
    case EPERM:		/* errno.h     */
    case EACCES:		/* errno.h     */
    case PRPERM:		/* pterror.h   */
	result = AUDIT_FAIL_ACCESS;
	break;
    case VL_PERM:		/* vlserver.h  */
    case BUDB_NOTPERMITTED:	/* budb_errs.h */
    case BZACCESS:		/* bnode.h     */
    case VOLSERBAD_ACCESS:	/* volser.h    */
	result = AUDIT_FAIL_PRIV;
	break;
    default:
	result = AUDIT_FAIL;
	break;
    }

#ifdef AFS_AIX32_ENV
    ObtainWriteLock(&audbuflock);
    bufferPtr = BUFFER;

    /* Put the error code into the buffer list */
    *(int *)bufferPtr = errCode;
    bufferPtr += sizeof(errCode);

    va_start(vaList, errCode);
    audmakebuf(audEvent, vaList);
#endif

    if (osi_echo_trail) {
	va_start(vaList, errCode);
	printbuf(stdout, 0, audEvent, errCode, vaList);
    }

#ifdef AFS_AIX32_ENV
    bufferLen = (int)((afs_int32) bufferPtr - (afs_int32) & BUFFER[0]);
    code = auditlog(audEvent, result, BUFFER, bufferLen);
#ifdef notdef
    if (code) {
	err = errno;
	code = auditlog("AFS_Aud_Fail", result, &err, sizeof(err));
	if (code)
	    printf("Error while writing audit entry: %d.\n", errno);
    }
#endif /* notdef */
    ReleaseWriteLock(&audbuflock);
#else
    if (auditout) {
	va_start(vaList, errCode);
	printbuf(auditout, 0, audEvent, errCode, vaList);
	fflush(auditout);
    }
#endif

    return 0;
}

/* ************************************************************************** */
/* Given a RPC call structure, this routine extracts the name and host id from the 
 * call and includes it within the audit information.
 * ************************************************************************** */
int
osi_auditU(struct rx_call *call, char *audEvent, int errCode, ...)
{
    struct rx_connection *conn;
    struct rx_peer *peer;
    afs_int32 secClass;
    afs_int32 code;
    char afsName[MAXKTCNAMELEN];
    afs_int32 hostId;
    va_list vaList;

    if (osi_audit_all < 0)
	osi_audit_check();
    if (!osi_audit_all && !auditout)
	return 0;

    strcpy(afsName, "--Unknown--");
    hostId = 0;

    if (call) {
	conn = rx_ConnectionOf(call);	/* call -> conn) */
	if (conn) {
            secClass = rx_SecurityClassOf(conn);	/* conn -> securityIndex */
	    if (secClass == 0) {	/* unauthenticated */
		osi_audit("AFS_Aud_Unauth", (-1), AUD_STR, audEvent, AUD_END);
		strcpy(afsName, "--UnAuth--");
	    } else if (secClass == 2) {	/* authenticated */
                char tcell[MAXKTCREALMLEN];
                char name[MAXKTCNAMELEN];
                char inst[MAXKTCNAMELEN];
                char vname[256];
                int  ilen, clen;

                code =
		    rxkad_GetServerInfo(conn, NULL, NULL, name, inst, tcell,
					NULL);
		if (code) {
		    osi_audit("AFS_Aud_NoAFSId", (-1), AUD_STR, audEvent, AUD_END);
		    strcpy(afsName, "--NoName--");
		} else {
                    strncpy(vname, name, sizeof(vname));
                    if ((ilen = strlen(inst))) {
                        if (strlen(vname) + 1 + ilen >= sizeof(vname))
                            goto done;
                        strcat(vname, ".");
                        strcat(vname, inst);
                    }
                    if ((clen = strlen(tcell))) {
#if defined(AFS_ATHENA_STDENV) || defined(AFS_KERBREALM_ENV)
                        static char local_realm[AFS_REALM_SZ] = "";
                        if (!local_realm[0]) {
                            if (afs_krb_get_lrealm(local_realm, 0) != 0 /*KSUCCESS*/)
                                strncpy(local_realm, "UNKNOWN.LOCAL.REALM", AFS_REALM_SZ);
                        }
                        if (strcasecmp(local_realm, tcell)) {
                            if (strlen(vname) + 1 + clen >= sizeof(vname))
                                goto done;
                            strcat(vname, "@");
                            strcat(vname, tcell);
                        }
#endif
                    }
                    strcpy(afsName, vname);
                }
	    } else {		/* Unauthenticated & unknown */
		osi_audit("AFS_Aud_UnknSec", (-1), AUD_STR, audEvent, AUD_END);
                strcpy(afsName, "--Unknown--");
	    }
	done:
	    peer = rx_PeerOf(conn);	/* conn -> peer */
	    if (peer)
		hostId = rx_HostOf(peer);	/* peer -> host */
	    else
		osi_audit("AFS_Aud_NoHost", (-1), AUD_STR, audEvent, AUD_END);
	} else {		/* null conn */
	    osi_audit("AFS_Aud_NoConn", (-1), AUD_STR, audEvent, AUD_END);
	}
    } else {			/* null call */
	osi_audit("AFS_Aud_NoCall", (-1), AUD_STR, audEvent, AUD_END);
    }
    va_start(vaList, errCode);
    osi_audit(audEvent, errCode, AUD_NAME, afsName, AUD_HOST, hostId, 
              AUD_LST, vaList, AUD_END);

    return 0;
}

/* ************************************************************************** */
/* Determines whether auditing is on or off by looking at the Audit file.
 * If the string AFS_AUDIT_AllEvents is defined in the file, then auditing will be 
 * enabled.
 * ************************************************************************** */

int
osi_audit_check()
{
    FILE *fds;
    int onoff;
    char event[257];

    osi_audit_all = 1;		/* say we made check (>= 0) */
    /* and assume audit all events (for now) */
    onoff = 0;			/* assume we will turn auditing off */
    osi_echo_trail = 0;		/* assume no echoing   */

    fds = fopen(AFSDIR_SERVER_AUDIT_FILEPATH, "r");
    if (fds) {
	while (fscanf(fds, "%256s", event) > 0) {
	    if (strcmp(event, "AFS_AUDIT_AllEvents") == 0)
		onoff = 1;

	    if (strcmp(event, "Echo_Trail") == 0)
		osi_echo_trail = 1;
	}
	fclose(fds);
    }

    /* Audit this event all of the time */
    if (onoff)
	osi_audit("AFS_Aud_On", 0, AUD_END);
    else
	osi_audit("AFS_Aud_Off", 0, AUD_END);

    /* Now set whether we audit all events from here on out */
    osi_audit_all = onoff;

    return 0;
}

int
osi_audit_file(FILE *out)
{
    auditout = out;
    return 0;
}
