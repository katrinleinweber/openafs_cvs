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
#include <ntstatus.h>
#else
#include <sys/timeb.h>
#include <tzfile.h>
#endif /* !DJGPP */
#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include <osi.h>
#include <rx\rx.h>
#include <rx/rx_prototypes.h>

#include "afsd.h"
#include <WINNT\afsreg.h>

#include "smb.h"
#include "lanahelper.h"

/* These characters are illegal in Windows filenames */
static char *illegalChars = "\\/:*?\"<>|";
BOOL isWindows2000 = FALSE;

smb_vc_t *dead_vcp = NULL;
smb_vc_t *active_vcp = NULL;

/* TODO; logout mechanism needs to be thread-safe */
char *loggedOutName = NULL;
smb_user_t *loggedOutUserp = NULL;
time_t loggedOutTime;
int loggedOut = 0;
int smbShutdownFlag = 0;

int smb_LogoffTokenTransfer;
time_t smb_LogoffTransferTimeout;

int smb_StoreAnsiFilenames = 0;

DWORD last_msg_time = 0;

long ongoingOps = 0;

unsigned int sessionGen = 0;

extern void afsi_log(char *pattern, ...);
extern HANDLE afsi_file;

osi_hyper_t hzero = {0, 0};
osi_hyper_t hones = {0xFFFFFFFF, -1};

osi_log_t *  smb_logp;
osi_rwlock_t smb_globalLock;
osi_rwlock_t smb_rctLock;
osi_mutex_t  smb_ListenerLock;
 
char smb_LANadapter;
unsigned char smb_sharename[NCBNAMSZ+1] = {0};

/* for debugging */
long smb_maxObsConcurrentCalls=0;
long smb_concurrentCalls=0;

smb_dispatch_t smb_dispatchTable[SMB_NOPCODES];

smb_packet_t *smb_packetFreeListp;
smb_ncb_t *smb_ncbFreeListp;

int smb_NumServerThreads;

int numNCBs, numSessions, numVCs;

int smb_maxVCPerServer;
int smb_maxMpxRequests;

int smb_authType = SMB_AUTH_EXTENDED; /* type of SMB auth to use. One of SMB_AUTH_* */
HANDLE smb_lsaHandle;
ULONG smb_lsaSecPackage;
LSA_STRING smb_lsaLogonOrigin;

#define NCBmax MAXIMUM_WAIT_OBJECTS
EVENT_HANDLE NCBavails[NCBmax], NCBevents[NCBmax];
EVENT_HANDLE **NCBreturns;
EVENT_HANDLE **NCBShutdown;
EVENT_HANDLE *smb_ServerShutdown;
DWORD NCBsessions[NCBmax];
NCB *NCBs[NCBmax];
struct smb_packet *bufs[NCBmax];

#define Sessionmax MAXIMUM_WAIT_OBJECTS - 4
EVENT_HANDLE SessionEvents[Sessionmax];
unsigned short LSNs[Sessionmax];
int lanas[Sessionmax];
BOOL dead_sessions[Sessionmax];
LANA_ENUM lana_list;

/* for raw I/O */
osi_mutex_t smb_RawBufLock;
#ifdef DJGPP
#define SMB_RAW_BUFS 4
dos_ptr smb_RawBufs;
int smb_RawBufSel[SMB_RAW_BUFS];
#else
char *smb_RawBufs;
#endif /* DJGPP */

#define SMB_MASKFLAG_TILDE 1
#define SMB_MASKFLAG_CASEFOLD 2

#define RAWTIMEOUT INFINITE

/* for raw write */
typedef struct raw_write_cont {
	long code;
	osi_hyper_t offset;
	long count;
#ifndef DJGPP
	char *buf;
#else
	dos_ptr buf;
#endif /* DJGPP */
	int writeMode;
	long alreadyWritten;
} raw_write_cont_t;

/* dir search stuff */
long smb_dirSearchCounter = 1;
smb_dirSearch_t *smb_firstDirSearchp;
smb_dirSearch_t *smb_lastDirSearchp;

/* hide dot files? */
int smb_hideDotFiles;

/* global state about V3 protocols */
int smb_useV3;		/* try to negotiate V3 */

#ifndef DJGPP
static showErrors = 1;
/* MessageBox or something like it */
int (_stdcall *smb_MBfunc)(HWND, LPCTSTR, LPCTSTR, UINT) = NULL;
#endif /* DJGPP */

/* GMT time info:
 * Time in Unix format of midnight, 1/1/1970 local time.
 * When added to dosUTime, gives Unix (AFS) time.
 */
time_t smb_localZero = 0;

#define USE_NUMERIC_TIME_CONV 1

#ifndef USE_NUMERIC_TIME_CONV
/* Time difference for converting to kludge-GMT */
afs_uint32 smb_NowTZ;
#endif /* USE_NUMERIC_TIME_CONV */

char *smb_localNamep = NULL;

smb_vc_t *smb_allVCsp;

smb_username_t *usernamesp = NULL;

smb_waitingLock_t *smb_allWaitingLocks;

/* forward decl */
void smb_DispatchPacket(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp,
						NCB *ncbp, raw_write_cont_t *rwcp);
void smb_NetbiosInit();
#ifdef DJGPP
#ifndef AFS_WIN95_ENV
DWORD smb_ServerExceptionFilter(void);
#endif

extern char cm_HostName[];
extern char cm_confDir[];
#endif

#ifdef DJGPP
#define LPTSTR char *
#define GetComputerName(str, sizep) \
       strcpy((str), cm_HostName); \
       *(sizep) = strlen(cm_HostName)
#endif /* DJGPP */

#ifdef LOG_PACKET
void smb_LogPacket(smb_packet_t *packet);
#endif /* LOG_PACKET */

char smb_ServerDomainName[MAX_COMPUTERNAME_LENGTH + 1] = ""; /* domain name */
int smb_ServerDomainNameLength = 0;
char smb_ServerOS[] = "Windows 5.0"; /* Faux OS String */
int smb_ServerOSLength = sizeof(smb_ServerOS);
char smb_ServerLanManager[] = "Windows 2000 LAN Manager"; /* Faux LAN Manager string */
int smb_ServerLanManagerLength = sizeof(smb_ServerLanManager);

/* Faux server GUID. This is never checked. */
GUID smb_ServerGUID = { 0x40015cb8, 0x058a, 0x44fc, { 0xae, 0x7e, 0xbb, 0x29, 0x52, 0xee, 0x7e, 0xff }};

char * myCrt_Dispatch(int i)
{
    switch (i)
    {
    case 0x00:
        return "(00)ReceiveCoreMakeDir";
    case 0x01:
        return "(01)ReceiveCoreRemoveDir";
    case 0x02:
        return "(02)ReceiveCoreOpen";
    case 0x03:
        return "(03)ReceiveCoreCreate";
    case 0x04:
        return "(04)ReceiveCoreClose";
    case 0x05:
        return "(05)ReceiveCoreFlush";
    case 0x06:
        return "(06)ReceiveCoreUnlink";
    case 0x07:
        return "(07)ReceiveCoreRename";
    case 0x08:
        return "(08)ReceiveCoreGetFileAttributes";
    case 0x09:
        return "(09)ReceiveCoreSetFileAttributes";
    case 0x0a:
        return "(0a)ReceiveCoreRead";
    case 0x0b:
        return "(0b)ReceiveCoreWrite";
    case 0x0c:
        return "(0c)ReceiveCoreLockRecord";
    case 0x0d:
        return "(0d)ReceiveCoreUnlockRecord";
    case 0x0e:
        return "(0e)SendCoreBadOp";
    case 0x0f:
        return "(0f)ReceiveCoreCreate";
    case 0x10:
        return "(10)ReceiveCoreCheckPath";
    case 0x11:
        return "(11)SendCoreBadOp";
    case 0x12:
        return "(12)ReceiveCoreSeek";
    case 0x1a:
        return "(1a)ReceiveCoreReadRaw";
    case 0x1d:
        return "(1d)ReceiveCoreWriteRawDummy";
    case 0x22:
        return "(22)ReceiveV3SetAttributes";
    case 0x23:
        return "(23)ReceiveV3GetAttributes";
    case 0x24:
        return "(24)ReceiveV3LockingX";
    case 0x25:
        return "(25)ReceiveV3Trans";
    case 0x26:
        return "(26)ReceiveV3Trans[aux]";
    case 0x29:
        return "(29)SendCoreBadOp";
    case 0x2b:
        return "(2b)ReceiveCoreEcho";
    case 0x2d:
        return "(2d)ReceiveV3OpenX";
    case 0x2e:
        return "(2e)ReceiveV3ReadX";
    case 0x32:
        return "(32)ReceiveV3Tran2A";
    case 0x33:
        return "(33)ReceiveV3Tran2A[aux]";
    case 0x34:
        return "(34)ReceiveV3FindClose";
    case 0x35:
        return "(35)ReceiveV3FindNotifyClose";
    case 0x70:
        return "(70)ReceiveCoreTreeConnect";
    case 0x71:
        return "(71)ReceiveCoreTreeDisconnect";
    case 0x72:
        return "(72)ReceiveNegotiate";
    case 0x73:
        return "(73)ReceiveV3SessionSetupX";
    case 0x74:
        return "(74)ReceiveV3UserLogoffX";
    case 0x75:
        return "(75)ReceiveV3TreeConnectX";
    case 0x80:
        return "(80)ReceiveCoreGetDiskAttributes";
    case 0x81:
        return "(81)ReceiveCoreSearchDir";
    case 0x82:
        return "(82)Find";
    case 0x83:
        return "(83)FindUnique";
    case 0x84:
        return "(84)FindClose";
    case 0xA0:
        return "(A0)ReceiveNTTransact";
    case 0xA2:
        return "(A2)ReceiveNTCreateX";
    case 0xA4:
        return "(A4)ReceiveNTCancel";
    case 0xA5:
        return "(A5)ReceiveNTRename";
    case 0xc0:
        return "(C0)OpenPrintFile";
    case 0xc1:
        return "(C1)WritePrintFile";
    case 0xc2:
        return "(C2)ClosePrintFile";
    case 0xc3:
        return "(C3)GetPrintQueue";
    case 0xd8:
        return "(D8)ReadBulk";
    case 0xd9:
        return "(D9)WriteBulk";
    case 0xda:
        return "(DA)WriteBulkData";
    default:
        return "unknown SMB op";
    }
}       

char * myCrt_2Dispatch(int i)
{
    switch (i)
    {
    default:
        return "unknown SMB op-2";
    case 0:
        return "S(00)CreateFile";
    case 1:
        return "S(01)FindFirst";
    case 2:
        return "S(02)FindNext";	/* FindNext */
    case 3:
        return "S(03)QueryFileSystem_ReceiveTran2QFSInfo";
    case 4:
        return "S(04)??";
    case 5:
        return "S(05)QueryFileInfo_ReceiveTran2QPathInfo";
    case 6:
        return "S(06)SetFileInfo_ReceiveTran2SetPathInfo";
    case 7:
        return "S(07)SetInfoHandle_ReceiveTran2QFileInfo";
    case 8:
        return "S(08)??_ReceiveTran2SetFileInfo";
    case 9:
        return "S(09)??_ReceiveTran2FSCTL";
    case 10:
        return "S(0a)_ReceiveTran2IOCTL";
    case 11:
        return "S(0b)_ReceiveTran2FindNotifyFirst";
    case 12:
        return "S(0c)_ReceiveTran2FindNotifyNext";
    case 13:
        return "S(0d)_ReceiveTran2CreateDirectory";
    case 14:
        return "S(0e)_ReceiveTran2SessionSetup";
    case 16:
        return "S(10)_ReceiveTran2GetDfsReferral";
    case 17:
        return "S(11)_ReceiveTran2ReportDfsInconsistency";
    }
}       

char * myCrt_RapDispatch(int i)
{
    switch(i)
    {
    default:
        return "unknown RAP OP";
    case 0:
        return "RAP(0)NetShareEnum";
    case 1:
        return "RAP(1)NetShareGetInfo";
    case 13:
        return "RAP(13)NetServerGetInfo";
    case 63:
        return "RAP(63)NetWkStaGetInfo";
    }
}       

/* scache must be locked */
unsigned int smb_Attributes(cm_scache_t *scp)
{
    unsigned int attrs;

    if ( scp->fileType == CM_SCACHETYPE_DIRECTORY ||
         scp->fileType == CM_SCACHETYPE_MOUNTPOINT ||
         scp->fileType == CM_SCACHETYPE_INVALID)
    {
        attrs = SMB_ATTR_DIRECTORY;
#ifdef SPECIAL_FOLDERS
        attrs |= SMB_ATTR_SYSTEM;		/* FILE_ATTRIBUTE_SYSTEM */
#endif /* SPECIAL_FOLDERS */
    } else if (scp->fileType == CM_SCACHETYPE_DFSLINK) {
        attrs = SMB_ATTR_DIRECTORY | SMB_ATTR_SPARSE_FILE;
    } else
        attrs = 0;

    /*
     * We used to mark a file RO if it was in an RO volume, but that
     * turns out to be impolitic in NT.  See defect 10007.
     */
#ifdef notdef
    if ((scp->unixModeBits & 0222) == 0 || (scp->flags & CM_SCACHEFLAG_RO))
        attrs |= SMB_ATTR_READONLY;	/* turn on read-only flag */
#else
    if ((scp->unixModeBits & 0222) == 0)
        attrs |= SMB_ATTR_READONLY;	/* turn on read-only flag */
#endif

    return attrs;
}

/* Check if the named file/dir is a dotfile/dotdir */
/* String pointed to by lastComp can have leading slashes, but otherwise should have
   no other patch components */
unsigned int smb_IsDotFile(char *lastComp) {
    char *s;
    if(lastComp) {
        /* skip over slashes */
        for(s=lastComp;*s && (*s == '\\' || *s == '/'); s++);
    }
    else
        return 0;

    /* nulls, curdir and parent dir doesn't count */
    if (!*s) 
        return 0;
    if (*s == '.') {
        if (!*(s + 1)) 
            return 0;
        if(*(s+1) == '.' && !*(s + 2)) 
            return 0;
        return 1;
    }
    return 0;
}

static int ExtractBits(WORD bits, short start, short len)
{
    int end;
    WORD num;

    end = start + len;
        
    num = bits << (16 - end);
    num = num >> ((16 - end) + start);

    return (int)num;
}

#ifndef DJGPP
void ShowUnixTime(char *FuncName, time_t unixTime)
{
    FILETIME ft;
    WORD wDate, wTime;

    smb_LargeSearchTimeFromUnixTime(&ft, unixTime);

    if (!FileTimeToDosDateTime(&ft, &wDate, &wTime))
        osi_Log1(smb_logp, "Failed to convert filetime to dos datetime: %d", GetLastError());
    else {
        int day, month, year, sec, min, hour;
        char msg[256];

        day = ExtractBits(wDate, 0, 5);
        month = ExtractBits(wDate, 5, 4);
        year = ExtractBits(wDate, 9, 7) + 1980;

        sec = ExtractBits(wTime, 0, 5);
        min = ExtractBits(wTime, 5, 6);
        hour = ExtractBits(wTime, 11, 5);

        sprintf(msg, "%s = %02d-%02d-%04d %02d:%02d:%02d", FuncName, month, day, year, hour, min, sec);
        osi_Log1(smb_logp, "%s", osi_LogSaveString(smb_logp, msg));
    }
}       
#endif /* DJGPP */

#ifndef DJGPP
/* Determine if we are observing daylight savings time */
void GetTimeZoneInfo(BOOL *pDST, LONG *pDstBias, LONG *pBias)
{
    TIME_ZONE_INFORMATION timeZoneInformation;
    SYSTEMTIME utc, local, localDST;

    /* Get the time zone info. NT uses this to calc if we are in DST. */
    GetTimeZoneInformation(&timeZoneInformation);

    /* Return the daylight bias */
    *pDstBias = timeZoneInformation.DaylightBias;

    /* Return the bias */
    *pBias = timeZoneInformation.Bias;

    /* Now determine if DST is being observed */

    /* Get the UTC (GMT) time */
    GetSystemTime(&utc);

    /* Convert UTC time to local time using the time zone info.  If we are
       observing DST, the calculated local time will include this. 
     */
    SystemTimeToTzSpecificLocalTime(&timeZoneInformation, &utc, &localDST);

    /* Set the daylight bias to 0.  The daylight bias is the amount of change
     * in time that we use for daylight savings time.  By setting this to 0
     * we cause there to be no change in time during daylight savings time. 
     */
    timeZoneInformation.DaylightBias = 0;

    /* Convert the utc time to local time again, but this time without any
       adjustment for daylight savings time. 
       */
    SystemTimeToTzSpecificLocalTime(&timeZoneInformation, &utc, &local);

    /* If the two times are different, then it means that the localDST that
       we calculated includes the daylight bias, and therefore we are
       observing daylight savings time.
     */
    *pDST = localDST.wHour != local.wHour;
}       
#else
/* Determine if we are observing daylight savings time */
void GetTimeZoneInfo(BOOL *pDST, LONG *pDstBias, LONG *pBias)
{
    struct timeb t;

    ftime(&t);
    *pDST = t.dstflag;
    *pDstBias = -60;    /* where can this be different? */
    *pBias = t.timezone;
}       
#endif /* DJGPP */
 

void CompensateForSmbClientLastWriteTimeBugs(afs_uint32 *pLastWriteTime)
{
    BOOL dst;       /* Will be TRUE if observing DST */
    LONG dstBias;   /* Offset from local time if observing DST */
    LONG bias;      /* Offset from GMT for local time */

    /*
     * This function will adjust the last write time to compensate
     * for two bugs in the smb client:
     *
     *    1) During Daylight Savings Time, the LastWriteTime is ahead
     *       in time by the DaylightBias (ignoring the sign - the
     *       DaylightBias is always stored as a negative number).  If
     *       the DaylightBias is -60, then the LastWriteTime will be
     *       ahead by 60 minutes.
     *
     *    2) If the local time zone is a positive offset from GMT, then
     *       the LastWriteTime will be the correct local time plus the
     *       Bias (ignoring the sign - a positive offset from GMT is
     *       always stored as a negative Bias).  If the Bias is -120,
     *       then the LastWriteTime will be ahead by 120 minutes.
     *
     *    These bugs can occur at the same time.
     */

    GetTimeZoneInfo(&dst, &dstBias, &bias);

    /* First adjust for DST */
    if (dst)
        *pLastWriteTime -= (-dstBias * 60);     /* Convert dstBias to seconds */

    /* Now adjust for a positive offset from GMT (a negative bias). */
    if (bias < 0)
        *pLastWriteTime -= (-bias * 60);        /* Convert bias to seconds */
}	        	

#ifndef USE_NUMERIC_TIME_CONV
/*
 * Calculate the difference (in seconds) between local time and GMT.
 * This enables us to convert file times to kludge-GMT.
 */
static void
smb_CalculateNowTZ()
{
    time_t t;
    struct tm gmt_tm, local_tm;
    int days, hours, minutes, seconds;

    t = time(NULL);
    gmt_tm = *(gmtime(&t));
    local_tm = *(localtime(&t));

    days = local_tm.tm_yday - gmt_tm.tm_yday;
    hours = 24 * days + local_tm.tm_hour - gmt_tm.tm_hour;
    minutes = 60 * hours + local_tm.tm_min - gmt_tm.tm_min; 
    seconds = 60 * minutes + local_tm.tm_sec - gmt_tm.tm_sec;

    smb_NowTZ = seconds;
}
#endif /* USE_NUMERIC_TIME_CONV */

#ifndef DJGPP
#ifdef USE_NUMERIC_TIME_CONV
void smb_LargeSearchTimeFromUnixTime(FILETIME *largeTimep, time_t unixTime)
{
    // Note that LONGLONG is a 64-bit value
    LONGLONG ll;

    ll = Int32x32To64(unixTime, 10000000) + 116444736000000000;
    largeTimep->dwLowDateTime = (DWORD)(ll & 0xFFFFFFFF);
    largeTimep->dwHighDateTime = (DWORD)(ll >> 32);
}
#else
void smb_LargeSearchTimeFromUnixTime(FILETIME *largeTimep, time_t unixTime)
{
    struct tm *ltp;
    SYSTEMTIME stm;
    struct tm localJunk;
    time_t ersatz_unixTime;

    /*
     * Must use kludge-GMT instead of real GMT.
     * kludge-GMT is computed by adding time zone difference to localtime.
     *
     * real GMT would be:
     * ltp = gmtime(&unixTime);
     */
    ersatz_unixTime = unixTime - smb_NowTZ;
    ltp = localtime(&ersatz_unixTime);

    /* if we fail, make up something */
    if (!ltp) {
        ltp = &localJunk;
        localJunk.tm_year = 89 - 20;
        localJunk.tm_mon = 4;
        localJunk.tm_mday = 12;
        localJunk.tm_hour = 0;
        localJunk.tm_min = 0;
        localJunk.tm_sec = 0;
    }

    stm.wYear = ltp->tm_year + 1900;
    stm.wMonth = ltp->tm_mon + 1;
    stm.wDayOfWeek = ltp->tm_wday;
    stm.wDay = ltp->tm_mday;
    stm.wHour = ltp->tm_hour;
    stm.wMinute = ltp->tm_min;
    stm.wSecond = ltp->tm_sec;
    stm.wMilliseconds = 0;

    SystemTimeToFileTime(&stm, largeTimep);
}
#endif /* USE_NUMERIC_TIME_CONV */
#else /* DJGPP */
void smb_LargeSearchTimeFromUnixTime(FILETIME *largeTimep, time_t unixTime)
{
    /* unixTime: seconds since 1/1/1970 00:00:00 GMT */
    /* FILETIME: 100ns intervals since 1/1/1601 00:00:00 ??? */
    LARGE_INTEGER *ft = (LARGE_INTEGER *) largeTimep;
    LARGE_INTEGER ut;
    int leap_years = 89;   /* leap years betw 1/1/1601 and 1/1/1970 */

    /* set ft to number of 100ns intervals betw 1/1/1601 and 1/1/1970 GMT */
    *ft = ConvertLongToLargeInteger(((EPOCH_YEAR-1601) * 365 + leap_years)
                                     * 24 * 60);
    *ft = LargeIntegerMultiplyByLong(*ft, 60);
    *ft = LargeIntegerMultiplyByLong(*ft, 10000000);

    /* add unix time */
    ut = ConvertLongToLargeInteger(unixTime);
    ut = LargeIntegerMultiplyByLong(ut, 10000000);
    *ft = LargeIntegerAdd(*ft, ut);
}       
#endif /* !DJGPP */

#ifndef DJGPP
#ifdef USE_NUMERIC_TIME_CONV
void smb_UnixTimeFromLargeSearchTime(time_t *unixTimep, FILETIME *largeTimep)
{
    // Note that LONGLONG is a 64-bit value
    LONGLONG ll;

    ll = largeTimep->dwHighDateTime;
    ll <<= 32;
    ll += largeTimep->dwLowDateTime;

    ll -= 116444736000000000;
    ll /= 10000000;

    *unixTimep = (DWORD)ll;
}
#else /* USE_NUMERIC_TIME_CONV */
void smb_UnixTimeFromLargeSearchTime(time_t *unixTimep, FILETIME *largeTimep)
{
    SYSTEMTIME stm;
    struct tm lt;
    long save_timezone;

    FileTimeToSystemTime(largeTimep, &stm);

    lt.tm_year = stm.wYear - 1900;
    lt.tm_mon = stm.wMonth - 1;
    lt.tm_wday = stm.wDayOfWeek;
    lt.tm_mday = stm.wDay;
    lt.tm_hour = stm.wHour;
    lt.tm_min = stm.wMinute;
    lt.tm_sec = stm.wSecond;
    lt.tm_isdst = -1;

    save_timezone = _timezone;
    _timezone += smb_NowTZ;
    *unixTimep = mktime(&lt);
    _timezone = save_timezone;
}       
#endif /* USE_NUMERIC_TIME_CONV */
#else /* DJGPP */
void smb_UnixTimeFromLargeSearchTime(time_t *unixTimep, FILETIME *largeTimep)
{
    /* unixTime: seconds since 1/1/1970 00:00:00 GMT */
    /* FILETIME: 100ns intervals since 1/1/1601 00:00:00 GMT? */
    LARGE_INTEGER *ft = (LARGE_INTEGER *) largeTimep;
    LARGE_INTEGER a;
    int leap_years = 89;

    /* set to number of 100ns intervals betw 1/1/1601 and 1/1/1970 */
    a = ConvertLongToLargeInteger(((EPOCH_YEAR-1601) * 365 + leap_years) * 24 * 60);
    a = LargeIntegerMultiplyByLong(a, 60);
    a = LargeIntegerMultiplyByLong(a, 10000000);

    /* subtract it from ft */
    a = LargeIntegerSubtract(*ft, a);

    /* divide down to seconds */
    *unixTimep = LargeIntegerDivideByLong(a, 10000000);
}       
#endif /* !DJGPP */

void smb_SearchTimeFromUnixTime(afs_uint32 *searchTimep, time_t unixTime)
{
    struct tm *ltp;
    int dosDate;
    int dosTime;
    struct tm localJunk;
    time_t t = unixTime;

    ltp = localtime((time_t*) &t);

    /* if we fail, make up something */
    if (!ltp) {
        ltp = &localJunk;
        localJunk.tm_year = 89 - 20;
        localJunk.tm_mon = 4;
        localJunk.tm_mday = 12;
        localJunk.tm_hour = 0;
        localJunk.tm_min = 0;
        localJunk.tm_sec = 0;
    }	

    dosDate = ((ltp->tm_year-80)<<9) | ((ltp->tm_mon+1) << 5) | (ltp->tm_mday);
    dosTime = (ltp->tm_hour<<11) | (ltp->tm_min << 5) | (ltp->tm_sec / 2);
    *searchTimep = (dosDate<<16) | dosTime;
}	

void smb_UnixTimeFromSearchTime(time_t *unixTimep, afs_uint32 searchTime)
{
    unsigned short dosDate;
    unsigned short dosTime;
    struct tm localTm;
        
    dosDate = (unsigned short) (searchTime & 0xffff);
    dosTime = (unsigned short) ((searchTime >> 16) & 0xffff);

    localTm.tm_year = 80 + ((dosDate>>9) & 0x3f);
    localTm.tm_mon = ((dosDate >> 5) & 0xf) - 1;	/* January is 0 in localTm */
    localTm.tm_mday = (dosDate) & 0x1f;
    localTm.tm_hour = (dosTime>>11) & 0x1f;
    localTm.tm_min = (dosTime >> 5) & 0x3f;
    localTm.tm_sec = (dosTime & 0x1f) * 2;
    localTm.tm_isdst = -1;				/* compute whether DST in effect */

    *unixTimep = mktime(&localTm);
}

void smb_DosUTimeFromUnixTime(afs_uint32 *dosUTimep, time_t unixTime)
{
    *dosUTimep = unixTime - smb_localZero;
}

void smb_UnixTimeFromDosUTime(time_t *unixTimep, afs_uint32 dosTime)
{
#ifndef DJGPP
    *unixTimep = dosTime + smb_localZero;
#else /* DJGPP */
    /* dosTime seems to be already adjusted for GMT */
    *unixTimep = dosTime;
#endif /* !DJGPP */
}

smb_vc_t *smb_FindVC(unsigned short lsn, int flags, int lana)
{
    smb_vc_t *vcp;

    lock_ObtainWrite(&smb_rctLock);
    for (vcp = smb_allVCsp; vcp; vcp=vcp->nextp) {
        if (lsn == vcp->lsn && lana == vcp->lana) {
            smb_HoldVCNoLock(vcp);
            break;
        }
    }
    if (!vcp && (flags & SMB_FLAG_CREATE)) {
        vcp = malloc(sizeof(*vcp));
        memset(vcp, 0, sizeof(*vcp));
        vcp->vcID = numVCs++;
        vcp->refCount = 1;
        vcp->tidCounter = 1;
        vcp->fidCounter = 1;
        vcp->uidCounter = 1;  /* UID 0 is reserved for blank user */
        vcp->nextp = smb_allVCsp;
        smb_allVCsp = vcp;
        lock_InitializeMutex(&vcp->mx, "vc_t mutex");
        vcp->lsn = lsn;
        vcp->lana = lana;
        vcp->secCtx = NULL;

        if (smb_authType == SMB_AUTH_NTLM || smb_authType == SMB_AUTH_EXTENDED) {
            /* We must obtain a challenge for extended auth 
             * in case the client negotiates smb v3 
             */
            NTSTATUS nts = STATUS_UNSUCCESSFUL, ntsEx = STATUS_UNSUCCESSFUL;
            MSV1_0_LM20_CHALLENGE_REQUEST lsaReq;
            PMSV1_0_LM20_CHALLENGE_RESPONSE lsaResp;
            ULONG lsaRespSize = 0;

            lsaReq.MessageType = MsV1_0Lm20ChallengeRequest;

            nts = LsaCallAuthenticationPackage( smb_lsaHandle,
                                                smb_lsaSecPackage,
                                                &lsaReq,
                                                sizeof(lsaReq),
                                                &lsaResp,
                                                &lsaRespSize,
                                                &ntsEx);
            if (nts != STATUS_SUCCESS)
                osi_Log4(smb_logp,"MsV1_0Lm20ChallengeRequest failure: nts 0x%x ntsEx 0x%x respSize is %u needs %u",
                         nts, ntsEx, sizeof(lsaReq), lsaRespSize);
            osi_assert(nts == STATUS_SUCCESS); /* this had better work! */

            memcpy(vcp->encKey, lsaResp->ChallengeToClient, MSV1_0_CHALLENGE_LENGTH);
            LsaFreeReturnBuffer(lsaResp);
        }
        else
            memset(vcp->encKey, 0, MSV1_0_CHALLENGE_LENGTH);
    }
    lock_ReleaseWrite(&smb_rctLock);
    return vcp;
}

int smb_IsStarMask(char *maskp)
{
    int i;
    char tc;
        
    for(i=0; i<11; i++) {
        tc = *maskp++;
        if (tc == '?' || tc == '*' || tc == '>') return 1;        
    }	
    return 0;
}

void smb_ReleaseVCNoLock(smb_vc_t *vcp)
{
    osi_Log2(smb_logp,"smb_ReleaseVCNoLock vcp %x ref %d",vcp, vcp->refCount);
#ifdef DEBUG
    osi_assert(vcp->refCount-- != 0);
#else
    vcp->refCount--;
#endif
}       

void smb_ReleaseVC(smb_vc_t *vcp)
{
    lock_ObtainWrite(&smb_rctLock);
    osi_Log2(smb_logp,"smb_ReleaseVC       vcp %x ref %d",vcp, vcp->refCount);
#ifdef DEBUG
    osi_assert(vcp->refCount-- != 0);
#else
    vcp->refCount--;
#endif
    lock_ReleaseWrite(&smb_rctLock);
}       

void smb_HoldVCNoLock(smb_vc_t *vcp)
{
    vcp->refCount++;
    osi_Log2(smb_logp,"smb_HoldVCNoLock vcp %x ref %d",vcp, vcp->refCount);
}       

void smb_HoldVC(smb_vc_t *vcp)
{
    lock_ObtainWrite(&smb_rctLock);
    vcp->refCount++;
    osi_Log2(smb_logp,"smb_HoldVC       vcp %x ref %d",vcp, vcp->refCount);
    lock_ReleaseWrite(&smb_rctLock);
}       

smb_tid_t *smb_FindTID(smb_vc_t *vcp, unsigned short tid, int flags)
{
    smb_tid_t *tidp;

    lock_ObtainWrite(&smb_rctLock);
    for (tidp = vcp->tidsp; tidp; tidp = tidp->nextp) {
        if (tid == tidp->tid) {
            tidp->refCount++;
            break;
        }	
    }
    if (!tidp && (flags & SMB_FLAG_CREATE)) {
        tidp = malloc(sizeof(*tidp));
        memset(tidp, 0, sizeof(*tidp));
        tidp->nextp = vcp->tidsp;
        tidp->refCount = 1;
        tidp->vcp = vcp;
        smb_HoldVCNoLock(vcp);
        vcp->tidsp = tidp;
        lock_InitializeMutex(&tidp->mx, "tid_t mutex");
        tidp->tid = tid;
    }
    lock_ReleaseWrite(&smb_rctLock);
    return tidp;
}       	

void smb_ReleaseTID(smb_tid_t *tidp)
{
    smb_tid_t *tp;
    smb_tid_t **ltpp;
    cm_user_t *userp;

    userp = NULL;
    lock_ObtainWrite(&smb_rctLock);
    osi_assert(tidp->refCount-- > 0);
    if (tidp->refCount == 0 && (tidp->flags & SMB_TIDFLAG_DELETE)) {
        ltpp = &tidp->vcp->tidsp;
        for(tp = *ltpp; tp; ltpp = &tp->nextp, tp = *ltpp) {
            if (tp == tidp) 
                break;
        }
        osi_assert(tp != NULL);
        *ltpp = tp->nextp;
        lock_FinalizeMutex(&tidp->mx);
        userp = tidp->userp;	/* remember to drop ref later */
        tidp->userp = NULL;
        smb_ReleaseVCNoLock(tidp->vcp);
        tidp->vcp = 0;
    }
    lock_ReleaseWrite(&smb_rctLock);
    if (userp)
        cm_ReleaseUser(userp);
}	        

smb_user_t *smb_FindUID(smb_vc_t *vcp, unsigned short uid, int flags)
{
    smb_user_t *uidp = NULL;

    lock_ObtainWrite(&smb_rctLock);
    for(uidp = vcp->usersp; uidp; uidp = uidp->nextp) {
        if (uid == uidp->userID) {
            uidp->refCount++;
            osi_LogEvent("AFS smb_FindUID (Find by UID)",NULL," VCP[%x] found-uid[%d] name[%s]",
                          (int)vcp, uidp->userID, 
                          osi_LogSaveString(smb_logp, (uidp->unp) ? uidp->unp->name : ""));
            break;
        }
    }
    if (!uidp && (flags & SMB_FLAG_CREATE)) {
        uidp = malloc(sizeof(*uidp));
        memset(uidp, 0, sizeof(*uidp));
        uidp->nextp = vcp->usersp;
        uidp->refCount = 1;
        uidp->vcp = vcp;
        smb_HoldVCNoLock(vcp);
        vcp->usersp = uidp;
        lock_InitializeMutex(&uidp->mx, "user_t mutex");
        uidp->userID = uid;
        osi_LogEvent("AFS smb_FindUID (Find by UID)",NULL,"VCP[%x] new-uid[%d] name[%s]",(int)vcp,uidp->userID,(uidp->unp ? uidp->unp->name : ""));
    }
    lock_ReleaseWrite(&smb_rctLock);
    return uidp;
}       	

smb_username_t *smb_FindUserByName(char *usern, char *machine, int flags)
{
    smb_username_t *unp= NULL;

    lock_ObtainWrite(&smb_rctLock);
    for(unp = usernamesp; unp; unp = unp->nextp) {
        if (stricmp(unp->name, usern) == 0 &&
             stricmp(unp->machine, machine) == 0) {
            unp->refCount++;
            break;
        }
    }
    if (!unp && (flags & SMB_FLAG_CREATE)) {
        unp = malloc(sizeof(*unp));
        memset(unp, 0, sizeof(*unp));
        unp->refCount = 1;
        unp->nextp = usernamesp;
        unp->name = strdup(usern);
        unp->machine = strdup(machine);
        usernamesp = unp;
        lock_InitializeMutex(&unp->mx, "username_t mutex");
    }
    lock_ReleaseWrite(&smb_rctLock);
    return unp;
}	

smb_user_t *smb_FindUserByNameThisSession(smb_vc_t *vcp, char *usern)
{
    smb_user_t *uidp= NULL;

    lock_ObtainWrite(&smb_rctLock);
    for(uidp = vcp->usersp; uidp; uidp = uidp->nextp) {
        if (!uidp->unp) 
            continue;
        if (stricmp(uidp->unp->name, usern) == 0) {
            uidp->refCount++;
            osi_LogEvent("AFS smb_FindUserByNameThisSession",NULL,"VCP[%x] uid[%d] match-name[%s]",(int)vcp,uidp->userID,usern);
            break;
        } else
            continue;
    }       	
    lock_ReleaseWrite(&smb_rctLock);
    return uidp;
}       
void smb_ReleaseUID(smb_user_t *uidp)
{
    smb_user_t *up;
    smb_user_t **lupp;
    cm_user_t *userp;

    userp = NULL;
    lock_ObtainWrite(&smb_rctLock);
    osi_assert(uidp->refCount-- > 0);
    if (uidp->refCount == 0 && (uidp->flags & SMB_USERFLAG_DELETE)) {
        lupp = &uidp->vcp->usersp;
        for(up = *lupp; up; lupp = &up->nextp, up = *lupp) {
            if (up == uidp) 
                break;
        }
        osi_assert(up != NULL);
        *lupp = up->nextp;
        lock_FinalizeMutex(&uidp->mx);
        if (uidp->unp) {
            userp = uidp->unp->userp;   /* avoid deadlock by releasing */
            uidp->unp->userp = NULL;    /* after releasing the lock */
        }       
        smb_ReleaseVCNoLock(uidp->vcp);
        uidp->vcp = NULL;
    }		
    lock_ReleaseWrite(&smb_rctLock);
    if (userp) {
        cm_ReleaseUserVCRef(userp);
        cm_ReleaseUser(userp);
    }	
}	


/* retrieve a held reference to a user structure corresponding to an incoming
 * request.
 * corresponding release function is cm_ReleaseUser.
 */
cm_user_t *smb_GetUser(smb_vc_t *vcp, smb_packet_t *inp)
{
    smb_user_t *uidp;
    cm_user_t *up;
    smb_t *smbp;

    smbp = (smb_t *) inp;
    uidp = smb_FindUID(vcp, smbp->uid, 0);
    if ((!uidp) ||  (!uidp->unp))
        return NULL;

    lock_ObtainMutex(&uidp->mx);
    up = uidp->unp->userp;
    cm_HoldUser(up);
    lock_ReleaseMutex(&uidp->mx);

    smb_ReleaseUID(uidp);

    return up;
}

/*
 * Return a pointer to a pathname extracted from a TID structure.  The
 * TID structure is not held; assume it won't go away.
 */
long smb_LookupTIDPath(smb_vc_t *vcp, unsigned short tid, char ** treepath)
{
    smb_tid_t *tidp;
    long code = 0;

    tidp = smb_FindTID(vcp, tid, 0);
    if (!tidp) {
        *treepath = NULL;
    } else {
        if (tidp->flags & SMB_TIDFLAG_IPC) {
            code = CM_ERROR_TIDIPC;
            /* tidp->pathname would be NULL, but that's fine */
        }
        *treepath = tidp->pathname;
	smb_ReleaseTID(tidp);
    }
    return code;
}

/* check to see if we have a chained fid, that is, a fid that comes from an
 * OpenAndX message that ran earlier in this packet.  In this case, the fid
 * field in a read, for example, request, isn't set, since the value is
 * supposed to be inherited from the openAndX call.
 */
int smb_ChainFID(int fid, smb_packet_t *inp)
{
    if (inp->fid == 0 || inp->inCount == 0) 
        return fid;
    else 
        return inp->fid;
}

/* are we a priv'd user?  What does this mean on NT? */
int smb_SUser(cm_user_t *userp)
{
    return 1;
}

/* find a file ID.  If we pass in 0 we select an used File ID.
 * If the SMB_FLAG_CREATE flag is set, we allocate a new  
 * smb_fid_t data structure if desired File ID cannot be found.
 */
smb_fid_t *smb_FindFID(smb_vc_t *vcp, unsigned short fid, int flags)
{
    smb_fid_t *fidp;
    int newFid = 0;
        
    if (fid == 0 && !(flags & SMB_FLAG_CREATE))
        return NULL;

    lock_ObtainWrite(&smb_rctLock);
    /* figure out if we need to allocate a new file ID */
    if (fid == 0) {
        newFid = 1;
        fid = vcp->fidCounter;
    }

  retry:
    for(fidp = vcp->fidsp; fidp; fidp = (smb_fid_t *) osi_QNext(&fidp->q)) {
        if (fid == fidp->fid) {
            if (newFid) {
                fid++;
                if (fid == 0) 
                    fid = 1;
                goto retry;
            }
            fidp->refCount++;
            break;
        }
    }
    if (!fidp && (flags & SMB_FLAG_CREATE)) {
        char eventName[MAX_PATH];
        EVENT_HANDLE event;
        sprintf(eventName,"fid_t event vcp=%d fid=%d", vcp->vcID, fid);
        event = thrd_CreateEvent(NULL, FALSE, TRUE, eventName);
        if ( GetLastError() == ERROR_ALREADY_EXISTS ) {
            osi_Log1(smb_logp, "Event Object Already Exists: %s", osi_LogSaveString(smb_logp, eventName));
            thrd_CloseHandle(event);
            fid++;
            if (fid == 0)
                fid = 1;
            goto retry;
        }

        fidp = malloc(sizeof(*fidp));
        memset(fidp, 0, sizeof(*fidp));
        osi_QAdd((osi_queue_t **)&vcp->fidsp, &fidp->q);
        fidp->refCount = 1;
        fidp->vcp = vcp;
        smb_HoldVCNoLock(vcp);
        lock_InitializeMutex(&fidp->mx, "fid_t mutex");
        fidp->fid = fid;
        fidp->curr_chunk = fidp->prev_chunk = -2;
        fidp->raw_write_event = event;
        if (newFid) {
            vcp->fidCounter = fid+1;
            if (vcp->fidCounter == 0) 
                vcp->fidCounter = 1;
        }
    }
    lock_ReleaseWrite(&smb_rctLock);
    return fidp;
}

void smb_ReleaseFID(smb_fid_t *fidp)
{
    cm_scache_t *scp;
    smb_vc_t *vcp = NULL;
    smb_ioctl_t *ioctlp;

    if (!fidp)
        return;

    scp = NULL;
    lock_ObtainWrite(&smb_rctLock);
    osi_assert(fidp->refCount-- > 0);
    if (fidp->refCount == 0 && (fidp->flags & SMB_FID_DELETE)) {
        vcp = fidp->vcp;
        fidp->vcp = 0;
        scp = fidp->scp;    /* release after lock is released */
        fidp->scp = 0;

        osi_QRemove((osi_queue_t **) &vcp->fidsp, &fidp->q);
        thrd_CloseHandle(fidp->raw_write_event);

        /* and see if there is ioctl stuff to free */
        ioctlp = fidp->ioctlp;
        if (ioctlp) {
            if (ioctlp->prefix)
                cm_FreeSpace(ioctlp->prefix);
            if (ioctlp->inAllocp)
                free(ioctlp->inAllocp);
            if (ioctlp->outAllocp)
                free(ioctlp->outAllocp);
            free(ioctlp);
        }       

        free(fidp);

        smb_ReleaseVCNoLock(vcp);
    }
    lock_ReleaseWrite(&smb_rctLock);

    /* now release the scache structure */
    if (scp) 
        cm_ReleaseSCache(scp);
}       

/*
 * Case-insensitive search for one string in another;
 * used to find variable names in submount pathnames.
 */
static char *smb_stristr(char *str1, char *str2)
{
    char *cursor;

    for (cursor = str1; *cursor; cursor++)
        if (stricmp(cursor, str2) == 0)
            return cursor;

    return NULL;
}

/*
 * Substitute a variable value for its name in a submount pathname.  Variable
 * name has been identified by smb_stristr() and is in substr.  Variable name
 * length (plus one) is in substr_size.  Variable value is in newstr.
 */
static void smb_subst(char *str1, char *substr, unsigned int substr_size,
                      char *newstr)
{
    char temp[1024];

    strcpy(temp, substr + substr_size - 1);
    strcpy(substr, newstr);
    strcat(str1, temp);
}       

char VNUserName[] = "%USERNAME%";
char VNLCUserName[] = "%LCUSERNAME%";
char VNComputerName[] = "%COMPUTERNAME%";
char VNLCComputerName[] = "%LCCOMPUTERNAME%";

#ifdef DJGPP
/* List available shares */
int smb_ListShares()
{
    char sbmtpath[256];
    char pathName[256];
    char shareBuf[4096];
    int num_shares=0;
    char *this_share;
    int len;
    char *p;
    int print_afs = 0;
    int code;

    /*strcpy(shareNameList[num_shares], "all");
      strcpy(pathNameList[num_shares++], "/afs");*/
    fprintf(stderr, "The following shares are available:\n");
    fprintf(stderr, "Share Name (AFS Path)\n");
    fprintf(stderr, "---------------------\n");
    fprintf(stderr, "\\\\%s\\%-16s (%s)\n", smb_localNamep, "ALL", cm_mountRoot);

#ifndef DJGPP
    code = GetWindowsDirectory(sbmtpath, sizeof(sbmtpath));
    if (code == 0 || code > sizeof(sbmtpath)) return -1;
#else
    strcpy(sbmtpath, cm_confDir);
#endif /* !DJGPP */
    strcat(sbmtpath, "/afsdsbmt.ini");
    len = GetPrivateProfileString("AFS Submounts", NULL, NULL,
                                   shareBuf, sizeof(shareBuf),
                                   sbmtpath);
    if (len == 0) {
        return num_shares;
    }

    this_share = shareBuf;
    do
    {
        print_afs = 0;
        /*strcpy(shareNameList[num_shares], this_share);*/
        len = GetPrivateProfileString("AFS Submounts", this_share,
                                       NULL,
                                       pathName, 256,
                                       sbmtpath);
        if (!len) 
            return num_shares;
        p = pathName;
        if (strncmp(p, cm_mountRoot, strlen(cm_mountRoot)) != 0)
            print_afs = 1;
        while (*p) {
            if (*p == '\\') *p = '/';    /* change to / */
            p++;
        }

        fprintf(stderr, "\\\\%s\\%-16s (%s%s)\n",
                 smb_localNamep, this_share, (print_afs ? cm_mountRoot : "\0"),
                 pathName);
        num_shares++;
        while (*this_share != 0) this_share++;  /* find next NUL */
        this_share++;   /* skip past the NUL */
    } while (*this_share != 0);  /* stop at final NUL */

    return num_shares;
}
#endif /* DJGPP */

typedef struct smb_findShare_rock {
    char * shareName;
    char * match;
    int matchType;
} smb_findShare_rock_t;

#define SMB_FINDSHARE_EXACT_MATCH 1
#define SMB_FINDSHARE_PARTIAL_MATCH 2

long smb_FindShareProc(cm_scache_t *scp, cm_dirEntry_t *dep, void *rockp,
                       osi_hyper_t *offp)
{
    int matchType = 0;
    smb_findShare_rock_t * vrock = (smb_findShare_rock_t *) rockp;
    if (!strnicmp(dep->name, vrock->shareName, 12)) {
        if(!stricmp(dep->name, vrock->shareName))
            matchType = SMB_FINDSHARE_EXACT_MATCH;
        else
            matchType = SMB_FINDSHARE_PARTIAL_MATCH;
        if(vrock->match) free(vrock->match);
        vrock->match = strdup(dep->name);
        vrock->matchType = matchType;

        if(matchType == SMB_FINDSHARE_EXACT_MATCH)
            return CM_ERROR_STOPNOW;
    }
    return 0;
}


/* find a shareName in the table of submounts */
int smb_FindShare(smb_vc_t *vcp, smb_user_t *uidp, char *shareName,
	char **pathNamep)
{
    DWORD len;
    char pathName[1024];
    char *var;
    char temp[1024];
    DWORD sizeTemp;
#ifdef DJGPP
    char sbmtpath[MAX_PATH];
#endif
    char *p, *q;
    HKEY parmKey;
    DWORD code;
    DWORD allSubmount = 1;

    /* if allSubmounts == 0, only return the //mountRoot/all share 
     * if in fact it has been been created in the subMounts table.  
     * This is to allow sites that want to restrict access to the 
     * world to do so.
     */
    code = RegOpenKeyEx(HKEY_LOCAL_MACHINE, AFSREG_CLT_SVC_PARAM_SUBKEY,
                         0, KEY_QUERY_VALUE, &parmKey);
    if (code == ERROR_SUCCESS) {
        len = sizeof(allSubmount);
        code = RegQueryValueEx(parmKey, "AllSubmount", NULL, NULL,
                                (BYTE *) &allSubmount, &len);
        if (code != ERROR_SUCCESS) {
            allSubmount = 1;
        }
        RegCloseKey (parmKey);
    }

    if (allSubmount && _stricmp(shareName, "all") == 0) {
        *pathNamep = NULL;
        return 1;
    }

    /* In case, the all share is disabled we need to still be able
     * to handle ioctl requests 
     */
    if (_stricmp(shareName, "ioctl$") == 0) {
        *pathNamep = strdup("/.__ioctl__");
        return 1;
    }

    if (_stricmp(shareName, "IPC$") == 0 ||
        _stricmp(shareName, SMB_IOCTL_FILENAME_NOSLASH) == 0 ||
        _stricmp(shareName, "DESKTOP.INI") == 0
         ) {
        *pathNamep = NULL;
        return 0;
    }

#ifndef DJGPP
    code = RegOpenKeyEx(HKEY_LOCAL_MACHINE, AFSREG_CLT_OPENAFS_SUBKEY "\\Submounts",
                         0, KEY_QUERY_VALUE, &parmKey);
    if (code == ERROR_SUCCESS) {
        len = sizeof(pathName);
        code = RegQueryValueEx(parmKey, shareName, NULL, NULL,
                                (BYTE *) pathName, &len);
        if (code != ERROR_SUCCESS)
            len = 0;
        RegCloseKey (parmKey);
    } else {
        len = 0;
    }   
#else /* DJGPP */
    strcpy(sbmtpath, cm_confDir);
    strcat(sbmtpath, "/afsdsbmt.ini");
    len = GetPrivateProfileString("AFS Submounts", shareName, "",
                                   pathName, sizeof(pathName), sbmtpath);
#endif /* !DJGPP */
    if (len != 0 && len != sizeof(pathName) - 1) {
        /* We can accept either unix or PC style AFS pathnames.  Convert
         * Unix-style to PC style here for internal use. 
         */
        p = pathName;
        if (strncmp(p, cm_mountRoot, strlen(cm_mountRoot)) == 0)
            p += strlen(cm_mountRoot);  /* skip mount path */
        q = p;
        while (*q) {
            if (*q == '/') *q = '\\';    /* change to \ */
            q++;
        }

        while (1)
        {
            if (var = smb_stristr(p, VNUserName)) {
                if (uidp && uidp->unp)
                    smb_subst(p, var, sizeof(VNUserName),uidp->unp->name);
                else
                    smb_subst(p, var, sizeof(VNUserName)," ");
            }
            else if (var = smb_stristr(p, VNLCUserName)) 
            {
                if (uidp && uidp->unp)
                    strcpy(temp, uidp->unp->name);
                else 
                    strcpy(temp, " ");
                _strlwr(temp);
                smb_subst(p, var, sizeof(VNLCUserName), temp);
            }
            else if (var = smb_stristr(p, VNComputerName)) 
            {
                sizeTemp = sizeof(temp);
                GetComputerName((LPTSTR)temp, &sizeTemp);
                smb_subst(p, var, sizeof(VNComputerName), temp);
            }
            else if (var = smb_stristr(p, VNLCComputerName)) 
            {
                sizeTemp = sizeof(temp);
                GetComputerName((LPTSTR)temp, &sizeTemp);
                _strlwr(temp);
                smb_subst(p, var, sizeof(VNLCComputerName), temp);
            }
            else     
                break;
        }
        *pathNamep = strdup(p);
        return 1;
    } 
    else
    {
        /* First lookup shareName in root.afs */
        cm_req_t req;
        smb_findShare_rock_t vrock;
        osi_hyper_t thyper;
        char * p = shareName; 
        int rw = 0;

        /*  attempt to locate a partial match in root.afs.  This is because
            when using the ANSI RAP calls, the share name is limited to 13 chars
            and hence is truncated. Of course we prefer exact matches. */
        cm_InitReq(&req);
        thyper.HighPart = 0;
        thyper.LowPart = 0;

        vrock.shareName = shareName;
        vrock.match = NULL;
        vrock.matchType = 0;

        cm_HoldSCache(cm_data.rootSCachep);
        code = cm_ApplyDir(cm_data.rootSCachep, smb_FindShareProc, &vrock, &thyper,
            (uidp? (uidp->unp ? uidp->unp->userp : NULL) : NULL), &req, NULL);
        cm_ReleaseSCache(cm_data.rootSCachep);

        if (vrock.matchType) {
            sprintf(pathName,"/%s/",vrock.match);
            *pathNamep = strdup(strlwr(pathName));
            free(vrock.match);
            return 1;
        }

        /* if we get here, there was no match for the share in root.afs */
        /* so try to create  \\<netbiosName>\<cellname>  */
        if ( *p == '.' ) {
            p++;
            rw = 1;
        }
        /* Get the full name for this cell */
        code = cm_SearchCellFile(p, temp, 0, 0);
#ifdef AFS_AFSDB_ENV
        if (code && cm_dnsEnabled) {
            int ttl;
            code = cm_SearchCellByDNS(p, temp, &ttl, 0, 0);
        }
#endif
        /* construct the path */
        if (code == 0) {     
            sprintf(pathName,rw ? "/.%s/" : "/%s/",temp);
            *pathNamep = strdup(strlwr(pathName));
            return 1;
        }
    }
    /* failure */
    *pathNamep = NULL;
    return 0;
}

/* Client-side offline caching policy types */
#define CSC_POLICY_MANUAL 0
#define CSC_POLICY_DOCUMENTS 1
#define CSC_POLICY_PROGRAMS 2
#define CSC_POLICY_DISABLE 3

int smb_FindShareCSCPolicy(char *shareName)
{
    DWORD len;
    char policy[1024];
    DWORD dwType;
    HKEY hkCSCPolicy;
    int  retval = CSC_POLICY_MANUAL;

    RegCreateKeyEx( HKEY_LOCAL_MACHINE, 
                    AFSREG_CLT_OPENAFS_SUBKEY "\\CSCPolicy",
                    0, 
                    "AFS", 
                    REG_OPTION_NON_VOLATILE,
                    KEY_READ,
                    NULL, 
                    &hkCSCPolicy,
                    NULL );

    len = sizeof(policy);
    if ( RegQueryValueEx( hkCSCPolicy, shareName, 0, &dwType, policy, &len ) ||
         len == 0) {
        retval = stricmp("all",shareName) ? CSC_POLICY_MANUAL : CSC_POLICY_DISABLE;
    }
    else if (stricmp(policy, "documents") == 0)
    {
        retval = CSC_POLICY_DOCUMENTS;
    }
    else if (stricmp(policy, "programs") == 0)
    {
        retval = CSC_POLICY_PROGRAMS;
    }
    else if (stricmp(policy, "disable") == 0)
    {
        retval = CSC_POLICY_DISABLE;
    }
	
    RegCloseKey(hkCSCPolicy);
    return retval;
}

/* find a dir search structure by cookie value, and return it held.
 * Must be called with smb_globalLock held.
 */
smb_dirSearch_t *smb_FindDirSearchNoLock(long cookie)
{
    smb_dirSearch_t *dsp;
        
    for (dsp = smb_firstDirSearchp; dsp; dsp = (smb_dirSearch_t *) osi_QNext(&dsp->q)) {
        if (dsp->cookie == cookie) {
            if (dsp != smb_firstDirSearchp) {
                /* move to head of LRU queue, too, if we're not already there */
                if (smb_lastDirSearchp == (smb_dirSearch_t *) &dsp->q)
                    smb_lastDirSearchp = (smb_dirSearch_t *) osi_QPrev(&dsp->q);
                osi_QRemove((osi_queue_t **) &smb_firstDirSearchp, &dsp->q);
                osi_QAdd((osi_queue_t **) &smb_firstDirSearchp, &dsp->q);
                if (!smb_lastDirSearchp)
                    smb_lastDirSearchp = (smb_dirSearch_t *) &dsp->q;
            }
            lock_ObtainMutex(&dsp->mx);
            dsp->refCount++;
            lock_ReleaseMutex(&dsp->mx);
            break;
        }
    }

    if (dsp == NULL) {
        osi_Log1(smb_logp,"smb_FindDirSearch(%d) == NULL",cookie);
        for (dsp = smb_firstDirSearchp; dsp; dsp = (smb_dirSearch_t *) osi_QNext(&dsp->q)) {
            osi_Log1(smb_logp,"... valid id: %d", dsp->cookie);
        }
    }
    return dsp;
}       

void smb_DeleteDirSearch(smb_dirSearch_t *dsp)
{
    lock_ObtainWrite(&smb_globalLock);
    lock_ObtainMutex(&dsp->mx);
    dsp->flags |= SMB_DIRSEARCH_DELETE;
    if (dsp->scp != NULL) {
        lock_ObtainMutex(&dsp->scp->mx);
        if (dsp->flags & SMB_DIRSEARCH_BULKST) {
            dsp->flags &= ~SMB_DIRSEARCH_BULKST;
            dsp->scp->flags &= ~CM_SCACHEFLAG_BULKSTATTING;
            dsp->scp->bulkStatProgress = hones;
        }	
        lock_ReleaseMutex(&dsp->scp->mx);
    }	
    lock_ReleaseMutex(&dsp->mx);
    lock_ReleaseWrite(&smb_globalLock);
}               

/* Must be called with the smb_globalLock held */
void smb_ReleaseDirSearchNoLock(smb_dirSearch_t *dsp)
{
    cm_scache_t *scp = NULL;

    lock_ObtainMutex(&dsp->mx);
    osi_assert(dsp->refCount-- > 0);
    if (dsp->refCount == 0 && (dsp->flags & SMB_DIRSEARCH_DELETE)) {
        if (&dsp->q == (osi_queue_t *) smb_lastDirSearchp)
            smb_lastDirSearchp = (smb_dirSearch_t *) osi_QPrev(&smb_lastDirSearchp->q);
        osi_QRemove((osi_queue_t **) &smb_firstDirSearchp, &dsp->q);
        lock_ReleaseMutex(&dsp->mx);
        lock_FinalizeMutex(&dsp->mx);
        scp = dsp->scp;
        free(dsp);
    } else {
        lock_ReleaseMutex(&dsp->mx);
    }
    /* do this now to avoid spurious locking hierarchy creation */
    if (scp) 
        cm_ReleaseSCache(scp);
}       

void smb_ReleaseDirSearch(smb_dirSearch_t *dsp)
{
    lock_ObtainWrite(&smb_globalLock);
    smb_ReleaseDirSearchNoLock(dsp);
    lock_ReleaseWrite(&smb_globalLock);
}       

/* find a dir search structure by cookie value, and return it held */
smb_dirSearch_t *smb_FindDirSearch(long cookie)
{
    smb_dirSearch_t *dsp;

    lock_ObtainWrite(&smb_globalLock);
    dsp = smb_FindDirSearchNoLock(cookie);
    lock_ReleaseWrite(&smb_globalLock);
    return dsp;
}

/* GC some dir search entries, in the address space expected by the specific protocol.
 * Must be called with smb_globalLock held; release the lock temporarily.
 */
#define SMB_DIRSEARCH_GCMAX	10	/* how many at once */
void smb_GCDirSearches(int isV3)
{
    smb_dirSearch_t *prevp;
    smb_dirSearch_t *tp;
    smb_dirSearch_t *victimsp[SMB_DIRSEARCH_GCMAX];
    int victimCount;
    int i;
        
    victimCount = 0;	/* how many have we got so far */
    for (tp = smb_lastDirSearchp; tp; tp=prevp) {
        /* we'll move tp from queue, so
         * do this early.
         */
        prevp = (smb_dirSearch_t *) osi_QPrev(&tp->q);	
        /* if no one is using this guy, and we're either in the new protocol,
         * or we're in the old one and this is a small enough ID to be useful
         * to the old protocol, GC this guy.
         */
        if (tp->refCount == 0 && (isV3 || tp->cookie <= 255)) {
            /* hold and delete */
            tp->flags |= SMB_DIRSEARCH_DELETE;
            victimsp[victimCount++] = tp;
            tp->refCount++;
        }

        /* don't do more than this */
        if (victimCount >= SMB_DIRSEARCH_GCMAX) 
            break;
    }
	
    /* now release them */
    for (i = 0; i < victimCount; i++) {
        smb_ReleaseDirSearchNoLock(victimsp[i]);
    }
}

/* function for allocating a dir search entry.  We need these to remember enough context
 * since we don't get passed the path from call to call during a directory search.
 *
 * Returns a held dir search structure, and bumps the reference count on the vnode,
 * since it saves a pointer to the vnode.
 */
smb_dirSearch_t *smb_NewDirSearch(int isV3)
{
    smb_dirSearch_t *dsp;
    int counter;
    int maxAllowed;
    int start;
    int wrapped = 0;

    lock_ObtainWrite(&smb_globalLock);
    counter = 0;

    /* what's the biggest ID allowed in this version of the protocol */
    maxAllowed = isV3 ? 65535 : 255;
    if (smb_dirSearchCounter > maxAllowed)
        smb_dirSearchCounter = 1;

    start = smb_dirSearchCounter;

    while (1) {
        /* twice so we have enough tries to find guys we GC after one pass;
         * 10 extra is just in case I mis-counted.
         */
        if (++counter > 2*maxAllowed+10) 
            osi_panic("afsd: dir search cookie leak", __FILE__, __LINE__);

        if (smb_dirSearchCounter > maxAllowed) {	
            smb_dirSearchCounter = 1;
        }
        if (smb_dirSearchCounter == start) {
            if (wrapped)
                smb_GCDirSearches(isV3);
            wrapped++;
        }
        dsp = smb_FindDirSearchNoLock(smb_dirSearchCounter);
        if (dsp) {
            /* don't need to watch for refcount zero and deleted, since
            * we haven't dropped the global lock.
            */
            lock_ObtainMutex(&dsp->mx);
            dsp->refCount--;
            lock_ReleaseMutex(&dsp->mx);
            ++smb_dirSearchCounter;
            continue;
        }	

        dsp = malloc(sizeof(*dsp));
        memset(dsp, 0, sizeof(*dsp));
        dsp->cookie = smb_dirSearchCounter;
        ++smb_dirSearchCounter;
        dsp->refCount = 1;
        lock_InitializeMutex(&dsp->mx, "cm_dirSearch_t");
        dsp->lastTime = osi_Time();
        osi_QAdd((osi_queue_t **) &smb_firstDirSearchp, &dsp->q);
        if (!smb_lastDirSearchp) 
            smb_lastDirSearchp = (smb_dirSearch_t *) &dsp->q;
        break;
    }	
    lock_ReleaseWrite(&smb_globalLock);
    return dsp;
}

static smb_packet_t *GetPacket(void)
{
    smb_packet_t *tbp;
#ifdef DJGPP
    unsigned int npar, seg, tb_sel;
#endif

    lock_ObtainWrite(&smb_globalLock);
    tbp = smb_packetFreeListp;
    if (tbp) 
        smb_packetFreeListp = tbp->nextp;
    lock_ReleaseWrite(&smb_globalLock);
    if (!tbp) {
#ifndef DJGPP
        tbp = calloc(65540,1);
#else /* DJGPP */
        tbp = malloc(sizeof(smb_packet_t));
#endif /* !DJGPP */
        tbp->magic = SMB_PACKETMAGIC;
        tbp->ncbp = NULL;
        tbp->vcp = NULL;
        tbp->resumeCode = 0;
        tbp->inCount = 0;
        tbp->fid = 0;
        tbp->wctp = NULL;
        tbp->inCom = 0;
        tbp->oddByte = 0;
        tbp->ncb_length = 0;
        tbp->flags = 0;
        tbp->spacep = NULL;
        
#ifdef DJGPP
        npar = SMB_PACKETSIZE >> 4;  /* number of paragraphs */
        {
            signed int retval =
                __dpmi_allocate_dos_memory(npar, &tb_sel); /* DOS segment */
            if (retval == -1) {
                osi_Log1(smb_logp, "Cannot allocate %d paragraphs of DOS memory",
                          npar);
                osi_panic("",__FILE__,__LINE__);
            }
            else {
                osi_Log2(smb_logp, "Allocated %d paragraphs of DOS mem at 0x%X",
                          npar, retval);
                seg = retval;
            }
        }
        tbp->dos_pkt = (seg * 16) + 0;  /* DOS physical address */
        tbp->dos_pkt_sel = tb_sel;
#endif /* DJGPP */
    }
    osi_assert(tbp->magic == SMB_PACKETMAGIC);

    return tbp;
}

smb_packet_t *smb_CopyPacket(smb_packet_t *pkt)
{
    smb_packet_t *tbp;
    tbp = GetPacket();
    memcpy(tbp, pkt, sizeof(smb_packet_t));
    tbp->wctp = tbp->data + ((unsigned int)pkt->wctp - (unsigned int)pkt->data);
	if (tbp->vcp)
		smb_HoldVC(tbp->vcp);
    return tbp;
}

static NCB *GetNCB(void)
{
    smb_ncb_t *tbp;
    NCB *ncbp;
#ifdef DJGPP
    unsigned int npar, seg, tb_sel;
#endif /* DJGPP */

    lock_ObtainWrite(&smb_globalLock);
    tbp = smb_ncbFreeListp;
    if (tbp) 
        smb_ncbFreeListp = tbp->nextp;
    lock_ReleaseWrite(&smb_globalLock);
    if (!tbp) {
#ifndef DJGPP
        tbp = calloc(sizeof(*tbp),1);
#else /* DJGPP */
        tbp = malloc(sizeof(*tbp));
        npar = (sizeof(NCB)+15) >> 4;  /* number of paragraphs */
        {
            signed int retval =
                __dpmi_allocate_dos_memory(npar, &tb_sel); /* DOS segment */
            if (retval == -1) {
                osi_Log1(smb_logp, "Cannot allocate %d paragraphs of DOS mem in GetNCB",
                          npar);
                osi_panic("",__FILE__,__LINE__);
            } else {
                osi_Log2(smb_logp, "Allocated %d paragraphs of DOS mem at 0x%X in GetNCB",
                          npar, retval);
                seg = retval;
            }
        }
        tbp->dos_ncb = (seg * 16) + 0;  /* DOS physical address */
        tbp->dos_ncb_sel = tb_sel;
#endif /* !DJGPP */
        tbp->magic = SMB_NCBMAGIC;
    }
        
    osi_assert(tbp->magic == SMB_NCBMAGIC);

    memset(&tbp->ncb, 0, sizeof(NCB));
    ncbp = &tbp->ncb;
#ifdef DJGPP
    dos_memset(tbp->dos_ncb, 0, sizeof(NCB));
#endif /* DJGPP */
    return ncbp;
}

void smb_FreePacket(smb_packet_t *tbp)
{
    smb_vc_t * vcp = NULL;
    osi_assert(tbp->magic == SMB_PACKETMAGIC);
        
    lock_ObtainWrite(&smb_globalLock);
    tbp->nextp = smb_packetFreeListp;
    smb_packetFreeListp = tbp;
    tbp->magic = SMB_PACKETMAGIC;
    tbp->ncbp = NULL;
    vcp = tbp->vcp;
    tbp->vcp = NULL;
    tbp->resumeCode = 0;
    tbp->inCount = 0;
    tbp->fid = 0;
    tbp->wctp = NULL;
    tbp->inCom = 0;
    tbp->oddByte = 0;
    tbp->ncb_length = 0;
    tbp->flags = 0;
    lock_ReleaseWrite(&smb_globalLock);

    if (vcp)
        smb_ReleaseVC(vcp);
}

static void FreeNCB(NCB *bufferp)
{
    smb_ncb_t *tbp;
        
    tbp = (smb_ncb_t *) bufferp;
    osi_assert(tbp->magic == SMB_NCBMAGIC);
        
    lock_ObtainWrite(&smb_globalLock);
    tbp->nextp = smb_ncbFreeListp;
    smb_ncbFreeListp = tbp;
    lock_ReleaseWrite(&smb_globalLock);
}

/* get a ptr to the data part of a packet, and its count */
unsigned char *smb_GetSMBData(smb_packet_t *smbp, int *nbytesp)
{
    int parmBytes;
    int dataBytes;
    unsigned char *afterParmsp;

    parmBytes = *smbp->wctp << 1;
    afterParmsp = smbp->wctp + parmBytes + 1;
        
    dataBytes = afterParmsp[0] + (afterParmsp[1]<<8);
    if (nbytesp) *nbytesp = dataBytes;
        
    /* don't forget to skip the data byte count, since it follows
     * the parameters; that's where the "2" comes from below.
     */
    return (unsigned char *) (afterParmsp + 2);
}

/* must set all the returned parameters before playing around with the
 * data region, since the data region is located past the end of the
 * variable number of parameters.
 */
void smb_SetSMBDataLength(smb_packet_t *smbp, unsigned int dsize)
{
    unsigned char *afterParmsp;

    afterParmsp = smbp->wctp + ((*smbp->wctp)<<1) + 1;
        
    *afterParmsp++ = dsize & 0xff;
    *afterParmsp = (dsize>>8) & 0xff;
}       

/* return the parm'th parameter in the smbp packet */
unsigned int smb_GetSMBParm(smb_packet_t *smbp, int parm)
{
    int parmCount;
    unsigned char *parmDatap;

    parmCount = *smbp->wctp;

    if (parm >= parmCount) {
        char s[100];
#ifndef DJGPP
        HANDLE h;
        char *ptbuf[1];
        h = RegisterEventSource(NULL, AFS_DAEMON_EVENT_NAME);
#endif  
        sprintf(s, "Bad SMB param %d out of %d, ncb len %d",
                 parm, parmCount, smbp->ncb_length);
#ifndef DJGPP   
        ptbuf[0] = s;
        ReportEvent(h, EVENTLOG_ERROR_TYPE, 0, 1006, NULL,
                     1, smbp->ncb_length, ptbuf, smbp);
        DeregisterEventSource(h);
#endif
        osi_Log3(smb_logp,"Bad SMB param %d out of %d, ncb len %d",
                 parm, parmCount, smbp->ncb_length);
        osi_panic(s, __FILE__, __LINE__);
    }
    parmDatap = smbp->wctp + (2*parm) + 1;
        
    return parmDatap[0] + (parmDatap[1] << 8);
}

/* return the parm'th parameter in the smbp packet */
unsigned int smb_GetSMBOffsetParm(smb_packet_t *smbp, int parm, int offset)
{
    int parmCount;
    unsigned char *parmDatap;

    parmCount = *smbp->wctp;

    if (parm * 2 + offset >= parmCount * 2) {
        char s[100];
#ifndef DJGPP
        HANDLE h;
        char *ptbuf[1];
        h = RegisterEventSource(NULL, AFS_DAEMON_EVENT_NAME);
#endif
        sprintf(s, "Bad SMB param %d offset %d out of %d, ncb len %d",
                parm, offset, parmCount, smbp->ncb_length);
#ifndef DJGPP
        ptbuf[0] = s;
        ReportEvent(h, EVENTLOG_ERROR_TYPE, 0, 1006, NULL,
                    1, smbp->ncb_length, ptbuf, smbp);
        DeregisterEventSource(h);
#endif
        osi_Log4(smb_logp, "Bad SMB param %d offset %d out of %d, ncb len %d",
                parm, offset, parmCount, smbp->ncb_length);
        osi_panic(s, __FILE__, __LINE__);
    }
    parmDatap = smbp->wctp + (2*parm) + 1 + offset;
	
    return parmDatap[0] + (parmDatap[1] << 8);
}

void smb_SetSMBParm(smb_packet_t *smbp, int slot, unsigned int parmValue)
{
    char *parmDatap;

    /* make sure we have enough slots */
    if (*smbp->wctp <= slot) 
        *smbp->wctp = slot+1;
        
    parmDatap = smbp->wctp + 2*slot + 1 + smbp->oddByte;
    *parmDatap++ = parmValue & 0xff;
    *parmDatap = (parmValue>>8) & 0xff;
}       

void smb_SetSMBParmLong(smb_packet_t *smbp, int slot, unsigned int parmValue)
{
    char *parmDatap;

    /* make sure we have enough slots */
    if (*smbp->wctp <= slot) 
        *smbp->wctp = slot+2;

    parmDatap = smbp->wctp + 2*slot + 1 + smbp->oddByte;
    *parmDatap++ = parmValue & 0xff;
    *parmDatap++ = (parmValue>>8) & 0xff;
    *parmDatap++ = (parmValue>>16) & 0xff;
    *parmDatap++ = (parmValue>>24) & 0xff;
}

void smb_SetSMBParmDouble(smb_packet_t *smbp, int slot, char *parmValuep)
{
    char *parmDatap;
    int i;

    /* make sure we have enough slots */
    if (*smbp->wctp <= slot) 
        *smbp->wctp = slot+4;

    parmDatap = smbp->wctp + 2*slot + 1 + smbp->oddByte;
    for (i=0; i<8; i++)
        *parmDatap++ = *parmValuep++;
}       

void smb_SetSMBParmByte(smb_packet_t *smbp, int slot, unsigned int parmValue)
{
    char *parmDatap;

    /* make sure we have enough slots */
    if (*smbp->wctp <= slot) {
        if (smbp->oddByte) {
            smbp->oddByte = 0;
            *smbp->wctp = slot+1;
        } else
            smbp->oddByte = 1;
    }

    parmDatap = smbp->wctp + 2*slot + 1 + (1 - smbp->oddByte);
    *parmDatap++ = parmValue & 0xff;
}

void smb_StripLastComponent(char *outPathp, char **lastComponentp, char *inPathp)
{
    char *lastSlashp;
        
    lastSlashp = strrchr(inPathp, '\\');
    if (lastComponentp)
        *lastComponentp = lastSlashp;
    if (lastSlashp) {
        while (1) {
            if (inPathp == lastSlashp) 
                break;
            *outPathp++ = *inPathp++;
        }
        *outPathp++ = 0;
    }
    else {
        *outPathp++ = 0;
    }
}

unsigned char *smb_ParseASCIIBlock(unsigned char *inp, char **chainpp)
{
    if (*inp++ != 0x4) 
        return NULL;
    if (chainpp) {
        *chainpp = inp + strlen(inp) + 1;	/* skip over null-terminated string */
    }
    return inp;
}

unsigned char *smb_ParseVblBlock(unsigned char *inp, char **chainpp, int *lengthp)
{
    int tlen;

    if (*inp++ != 0x5) 
        return NULL;
    tlen = inp[0] + (inp[1]<<8);
    inp += 2;		/* skip length field */

    if (chainpp) {
        *chainpp = inp + tlen;
    }
        
    if (lengthp) 
        *lengthp = tlen;
        
    return inp;
}	

/* format a packet as a response */
void smb_FormatResponsePacket(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *op)
{
    smb_t *outp;
    smb_t *inSmbp;

    outp = (smb_t *) op;
	
    /* zero the basic structure through the smb_wct field, and zero the data
     * size field, assuming that wct stays zero; otherwise, you have to 
     * explicitly set the data size field, too.
     */
    inSmbp = (smb_t *) inp;
    memset(outp, 0, sizeof(smb_t)+2);
    outp->id[0] = 0xff;
    outp->id[1] = 'S';
    outp->id[2] = 'M';
    outp->id[3] = 'B';
    if (inp) {
        outp->com = inSmbp->com;
        outp->tid = inSmbp->tid;
        outp->pid = inSmbp->pid;
        outp->uid = inSmbp->uid;
        outp->mid = inSmbp->mid;
        outp->res[0] = inSmbp->res[0];
        outp->res[1] = inSmbp->res[1];
        op->inCom = inSmbp->com;
    }
    outp->reb = SMB_FLAGS_SERVER_TO_CLIENT | SMB_FLAGS_CANONICAL_PATHNAMES;
    outp->flg2 = SMB_FLAGS2_KNOWS_LONG_NAMES;

    /* copy fields in generic packet area */
    op->wctp = &outp->wct;
}       

/* send a (probably response) packet; vcp tells us to whom to send it.
 * we compute the length by looking at wct and bcc fields.
 */
void smb_SendPacket(smb_vc_t *vcp, smb_packet_t *inp)
{
    NCB *ncbp;
    int extra;
    long code = 0;
    unsigned char *tp;
    int localNCB = 0;
#ifdef DJGPP
    dos_ptr dos_ncb;
#endif /* DJGPP */
        
    ncbp = inp->ncbp;
    if (ncbp == NULL) {
        ncbp = GetNCB();
        localNCB = 1;
    }
#ifdef DJGPP
    dos_ncb = ((smb_ncb_t *)ncbp)->dos_ncb;
#endif /* DJGPP */
 
    memset((char *)ncbp, 0, sizeof(NCB));

    extra = 2 * (*inp->wctp);	/* space used by parms, in bytes */
    tp = inp->wctp + 1+ extra;	/* points to count of data bytes */
    extra += tp[0] + (tp[1]<<8);
    extra += ((unsigned int)inp->wctp - (unsigned int)inp->data);	/* distance to last wct field */
    extra += 3;			/* wct and length fields */
        
    ncbp->ncb_length = extra;	/* bytes to send */
    ncbp->ncb_lsn = (unsigned char) vcp->lsn;	/* vc to use */
    ncbp->ncb_lana_num = vcp->lana;
    ncbp->ncb_command = NCBSEND;	/* op means send data */
#ifndef DJGPP
    ncbp->ncb_buffer = (char *) inp;/* packet */
    code = Netbios(ncbp);
#else /* DJGPP */
    ncbp->ncb_buffer = inp->dos_pkt;/* packet */
    ((smb_ncb_t*)ncbp)->orig_pkt = inp;

    /* copy header information from virtual to DOS address space */
    dosmemput((char*)inp, SMB_PACKETSIZE, inp->dos_pkt);
    code = Netbios(ncbp, dos_ncb);
#endif /* !DJGPP */
        
    if (code != 0) {
        char * s;
        switch ( code ) {
        case 0x01: s = "llegal buffer length                     "; break; 
        case 0x03: s = "illegal command                          "; break; 
        case 0x05: s = "command timed out                        "; break; 
        case 0x06: s = "message incomplete, issue another command"; break; 
        case 0x07: s = "illegal buffer address                   "; break; 
        case 0x08: s = "session number out of range              "; break; 
        case 0x09: s = "no resource available                    "; break; 
        case 0x0a: s = "session closed                           "; break; 
        case 0x0b: s = "command cancelled                        "; break; 
        case 0x0d: s = "duplicate name                           "; break; 
        case 0x0e: s = "name table full                          "; break; 
        case 0x0f: s = "no deletions, name has active sessions   "; break; 
        case 0x11: s = "local session table full                 "; break; 
        case 0x12: s = "remote session table full                "; break; 
        case 0x13: s = "illegal name number                      "; break; 
        case 0x14: s = "no callname                              "; break; 
        case 0x15: s = "cannot put * in NCB_NAME                 "; break; 
        case 0x16: s = "name in use on remote adapter            "; break; 
        case 0x17: s = "name deleted                             "; break; 
        case 0x18: s = "session ended abnormally                 "; break; 
        case 0x19: s = "name conflict detected                   "; break; 
        case 0x21: s = "interface busy, IRET before retrying     "; break; 
        case 0x22: s = "too many commands outstanding, retry later"; break;
        case 0x23: s = "ncb_lana_num field invalid               "; break; 
        case 0x24: s = "command completed while cancel occurring "; break; 
        case 0x26: s = "command not valid to cancel              "; break; 
        case 0x30: s = "name defined by anther local process     "; break; 
        case 0x34: s = "environment undefined. RESET required    "; break; 
        case 0x35: s = "required OS resources exhausted          "; break; 
        case 0x36: s = "max number of applications exceeded      "; break; 
        case 0x37: s = "no saps available for netbios            "; break; 
        case 0x38: s = "requested resources are not available    "; break; 
        case 0x39: s = "invalid ncb address or length > segment  "; break; 
        case 0x3B: s = "invalid NCB DDID                         "; break; 
        case 0x3C: s = "lock of user area failed                 "; break; 
        case 0x3f: s = "NETBIOS not loaded                       "; break; 
        case 0x40: s = "system error                             "; break;                 
        default:
            s = "unknown error";
        }
        osi_Log2(smb_logp, "SendPacket failure code %d \"%s\"", code, s);
    }

    if (localNCB)
        FreeNCB(ncbp);
}

void smb_MapNTError(long code, unsigned long *NTStatusp)
{
    unsigned long NTStatus;

    /* map CM_ERROR_* errors to NT 32-bit status codes */
    /* NT Status codes are listed in ntstatus.h not winerror.h */
    if (code == CM_ERROR_NOSUCHCELL) {
        NTStatus = 0xC000000FL;	/* No such file */
    }
    else if (code == CM_ERROR_NOSUCHVOLUME) {
        NTStatus = 0xC000000FL;	/* No such file */
    }
    else if (code == CM_ERROR_TIMEDOUT) {
#ifdef COMMENT
        NTStatus = 0xC00000CFL;	/* Sharing Paused */
#else
        NTStatus = 0x00000102L; /* Timeout */
#endif
    }
    else if (code == CM_ERROR_RETRY) {
        NTStatus = 0xC000022DL;	/* Retry */
    }
    else if (code == CM_ERROR_NOACCESS) {
        NTStatus = 0xC0000022L;	/* Access denied */
    }
    else if (code == CM_ERROR_READONLY) {
        NTStatus = 0xC00000A2L;	/* Write protected */
    }	
    else if (code == CM_ERROR_NOSUCHFILE) {
        NTStatus = 0xC000000FL;	/* No such file */
    }
    else if (code == CM_ERROR_NOSUCHPATH) {
        NTStatus = 0xC000003AL;	/* Object path not found */
    }		
    else if (code == CM_ERROR_TOOBIG) {
        NTStatus = 0xC000007BL;	/* Invalid image format */
    }
    else if (code == CM_ERROR_INVAL) {
        NTStatus = 0xC000000DL;	/* Invalid parameter */
    }
    else if (code == CM_ERROR_BADFD) {
        NTStatus = 0xC0000008L;	/* Invalid handle */
    }
    else if (code == CM_ERROR_BADFDOP) {
        NTStatus = 0xC0000022L;	/* Access denied */
    }
    else if (code == CM_ERROR_EXISTS) {
        NTStatus = 0xC0000035L;	/* Object name collision */
    }
    else if (code == CM_ERROR_NOTEMPTY) {
        NTStatus = 0xC0000101L;	/* Directory not empty */
    }	
    else if (code == CM_ERROR_CROSSDEVLINK) {
        NTStatus = 0xC00000D4L;	/* Not same device */
    }
    else if (code == CM_ERROR_NOTDIR) {
        NTStatus = 0xC0000103L;	/* Not a directory */
    }
    else if (code == CM_ERROR_ISDIR) {
        NTStatus = 0xC00000BAL;	/* File is a directory */
    }
    else if (code == CM_ERROR_BADOP) {
#ifdef COMMENT
        /* I have no idea where this comes from */
        NTStatus = 0xC09820FFL;	/* SMB no support */
#else
        NTStatus = 0xC00000BBL;     /* Not supported */
#endif /* COMMENT */
    }
    else if (code == CM_ERROR_BADSHARENAME) {
        NTStatus = 0xC00000CCL;	/* Bad network name */
    }
    else if (code == CM_ERROR_NOIPC) {
#ifdef COMMENT
        NTStatus = 0xC0000022L;	/* Access Denied */
#else   
        NTStatus = 0xC000013DL; /* Remote Resources */
#endif
    }
    else if (code == CM_ERROR_CLOCKSKEW) {
        NTStatus = 0xC0000133L;	/* Time difference at DC */
    }
    else if (code == CM_ERROR_BADTID) {
        NTStatus = 0xC0982005L;	/* SMB bad TID */
    }
    else if (code == CM_ERROR_USESTD) {
        NTStatus = 0xC09820FBL;	/* SMB use standard */
    }
    else if (code == CM_ERROR_QUOTA) {
#ifdef COMMENT
        NTStatus = 0xC0000044L;	/* Quota exceeded */
#else
        NTStatus = 0xC000007FL;	/* Disk full */
#endif
    }
    else if (code == CM_ERROR_SPACE) {
        NTStatus = 0xC000007FL;	/* Disk full */
    }
    else if (code == CM_ERROR_ATSYS) {
        NTStatus = 0xC0000033L;	/* Object name invalid */
    }
    else if (code == CM_ERROR_BADNTFILENAME) {
        NTStatus = 0xC0000033L;	/* Object name invalid */
    }
    else if (code == CM_ERROR_WOULDBLOCK) {
        NTStatus = 0xC0000055L;	/* Lock not granted */
    }
    else if (code == CM_ERROR_PARTIALWRITE) {
        NTStatus = 0xC000007FL;	/* Disk full */
    }
    else if (code == CM_ERROR_BUFFERTOOSMALL) {
        NTStatus = 0xC0000023L;	/* Buffer too small */
    }
    else if (code == CM_ERROR_AMBIGUOUS_FILENAME) {
        NTStatus = 0xC0000035L;	/* Object name collision */
    }   
    else if (code == CM_ERROR_BADPASSWORD) {
        NTStatus = 0xC000006DL; /* unknown username or bad password */
    }
    else if (code == CM_ERROR_BADLOGONTYPE) {
        NTStatus = 0xC000015BL; /* logon type not granted */
    }
    else if (code == CM_ERROR_GSSCONTINUE) {
        NTStatus = 0xC0000016L; /* more processing required */
    }
    else if (code == CM_ERROR_TOO_MANY_SYMLINKS) {
#ifdef COMMENT
        NTStatus = 0xC0000280L; /* reparse point not resolved */
#else
        NTStatus = 0xC0000022L; /* Access Denied */
#endif
    }
    else if (code == CM_ERROR_PATH_NOT_COVERED) {
        NTStatus = 0xC0000257L; /* Path Not Covered */
    } 
#ifdef COMMENT
    else if (code == CM_ERROR_ALLBUSY) {
        NTStatus = 0xC00000BFL; /* Network Busy */
    } 
    else if (code == CM_ERROR_ALLOFFLINE) {
        NTStatus = 0xC0000350L; /* Remote Host Down */
    } 
#else
    /* we do not want to be telling the SMB/CIFS client that
     * the AFS Client Service is busy or down.  
     */
    else if (code == CM_ERROR_ALLBUSY || 
             code == CM_ERROR_ALLOFFLINE) {
        NTStatus = 0xC00000BEL; /* Bad Network Path */
    }
#endif
    else {
        NTStatus = 0xC0982001L;	/* SMB non-specific error */
    }

    *NTStatusp = NTStatus;
    osi_Log2(smb_logp, "SMB SEND code %lX as NT %lX", code, NTStatus);
}       

void smb_MapCoreError(long code, smb_vc_t *vcp, unsigned short *scodep,
                      unsigned char *classp)
{
    unsigned char class;
    unsigned short error;

    /* map CM_ERROR_* errors to SMB errors */
    if (code == CM_ERROR_NOSUCHCELL) {
        class = 1;
        error = 3;	/* bad path */
    }
    else if (code == CM_ERROR_NOSUCHVOLUME) {
        class = 1;
        error = 3;	/* bad path */
    }
    else if (code == CM_ERROR_TIMEDOUT) {
        class = 2;
        error = 81;	/* server is paused */
    }
    else if (code == CM_ERROR_RETRY) {
        class = 2;	/* shouldn't happen */
        error = 1;
    }
    else if (code == CM_ERROR_NOACCESS) {
        class = 2;
        error = 4;	/* bad access */
    }
    else if (code == CM_ERROR_READONLY) {
        class = 3;
        error = 19;	/* read only */
    }
    else if (code == CM_ERROR_NOSUCHFILE) {
        class = 1;
        error = 2;	/* ENOENT! */
    }
    else if (code == CM_ERROR_NOSUCHPATH) {
        class = 1;
        error = 3;	/* Bad path */
    }
    else if (code == CM_ERROR_TOOBIG) {
        class = 1;
        error = 11;	/* bad format */
    }
    else if (code == CM_ERROR_INVAL) {
        class = 2;	/* server non-specific error code */
        error = 1;
    }
    else if (code == CM_ERROR_BADFD) {
        class = 1;
        error = 6;	/* invalid file handle */
    }
    else if (code == CM_ERROR_BADFDOP) {
        class = 1;	/* invalid op on FD */
        error = 5;
    }
    else if (code == CM_ERROR_EXISTS) {
        class = 1;
        error = 80;	/* file already exists */
    }
    else if (code == CM_ERROR_NOTEMPTY) {
        class = 1;
        error = 5;	/* delete directory not empty */
    }
    else if (code == CM_ERROR_CROSSDEVLINK) {
        class = 1;
        error = 17;	/* EXDEV */
    }
    else if (code == CM_ERROR_NOTDIR) {
        class = 1;	/* bad path */
        error = 3;
    }
    else if (code == CM_ERROR_ISDIR) {
        class = 1;	/* access denied; DOS doesn't have a good match */
        error = 5;
    }       
    else if (code == CM_ERROR_BADOP) {
        class = 2;
        error = 65535;
    }
    else if (code == CM_ERROR_BADSHARENAME) {
        class = 2;
        error = 6;
    }
    else if (code == CM_ERROR_NOIPC) {
        class = 2;
        error = 4; /* bad access */
    }
    else if (code == CM_ERROR_CLOCKSKEW) {
        class = 1;	/* invalid function */
        error = 1;
    }
    else if (code == CM_ERROR_BADTID) {
        class = 2;
        error = 5;
    }
    else if (code == CM_ERROR_USESTD) {
        class = 2;
        error = 251;
    }
    else if (code == CM_ERROR_REMOTECONN) {
        class = 2;
        error = 82;
    }
    else if (code == CM_ERROR_QUOTA) {
        if (vcp->flags & SMB_VCFLAG_USEV3) {
            class = 3;
            error = 39;	/* disk full */
        }
        else {
            class = 1;
            error = 5;	/* access denied */
        }
    }
    else if (code == CM_ERROR_SPACE) {
        if (vcp->flags & SMB_VCFLAG_USEV3) {
            class = 3;
            error = 39;	/* disk full */
        }
        else {
            class = 1;
            error = 5;	/* access denied */
        }
    }
    else if (code == CM_ERROR_PARTIALWRITE) {
        class = 3;
        error = 39;	/* disk full */
    }
    else if (code == CM_ERROR_ATSYS) {
        class = 1;
        error = 2;	/* ENOENT */
    }
    else if (code == CM_ERROR_WOULDBLOCK) {
        class = 1;
        error = 33;	/* lock conflict */
    }
    else if (code == CM_ERROR_NOFILES) {
        class = 1;
        error = 18;	/* no files in search */
    }
    else if (code == CM_ERROR_RENAME_IDENTICAL) {
        class = 1;
        error = 183;     /* Samba uses this */
    }
    else if (code == CM_ERROR_BADPASSWORD || code == CM_ERROR_BADLOGONTYPE) {
        /* we don't have a good way of reporting CM_ERROR_BADLOGONTYPE */
        class = 2;
        error = 2; /* bad password */
    }
    else if (code == CM_ERROR_PATH_NOT_COVERED) {
        class = 2;
        error = 3;     /* bad path */
    }
    else {
        class = 2;
        error = 1;
    }

    *scodep = error;
    *classp = class;
    osi_Log3(smb_logp, "SMB SEND code %lX as SMB %d: %d", code, class, error);
}       

long smb_SendCoreBadOp(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    osi_Log0(smb_logp,"SendCoreBadOp - NOT_SUPPORTED");
    return CM_ERROR_BADOP;
}

long smb_ReceiveCoreEcho(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    unsigned short EchoCount, i;
    char *data, *outdata;
    int dataSize;

    EchoCount = (unsigned short) smb_GetSMBParm(inp, 0);

    for (i=1; i<=EchoCount; i++) {
        data = smb_GetSMBData(inp, &dataSize);
        smb_SetSMBParm(outp, 0, i);
        smb_SetSMBDataLength(outp, dataSize);
        outdata = smb_GetSMBData(outp, NULL);
        memcpy(outdata, data, dataSize);
        smb_SendPacket(vcp, outp);
    }

    return 0;
}

long smb_ReceiveCoreReadRaw(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    osi_hyper_t offset;
    long count, minCount, finalCount;
    unsigned short fd;
    smb_fid_t *fidp;
    long code = 0;
    cm_user_t *userp = NULL;
    NCB *ncbp;
    int rc;
#ifndef DJGPP
    char *rawBuf = NULL;
#else
    dos_ptr rawBuf = NULL;
    dos_ptr dos_ncb;
#endif /* DJGPP */

    rawBuf = NULL;
    finalCount = 0;

    fd = smb_GetSMBParm(inp, 0);
    count = smb_GetSMBParm(inp, 3);
    minCount = smb_GetSMBParm(inp, 4);
    offset.HighPart = 0;	/* too bad */
    offset.LowPart = smb_GetSMBParm(inp, 1) | (smb_GetSMBParm(inp, 2) << 16);

    osi_Log3(smb_logp, "smb_ReceieveCoreReadRaw fd %d, off 0x%x, size 0x%x",
             fd, offset.LowPart, count);

    fidp = smb_FindFID(vcp, fd, 0);
    if (!fidp)
        goto send1;

    lock_ObtainMutex(&smb_RawBufLock);
    if (smb_RawBufs) {
        /* Get a raw buf, from head of list */
        rawBuf = smb_RawBufs;
#ifndef DJGPP
        smb_RawBufs = *(char **)smb_RawBufs;
#else /* DJGPP */
        smb_RawBufs = _farpeekl(_dos_ds, smb_RawBufs);
#endif /* !DJGPP */
    }
    lock_ReleaseMutex(&smb_RawBufLock);
    if (!rawBuf)
        goto send1a;

    if (fidp->flags & SMB_FID_IOCTL)
    {
#ifndef DJGPP
        rc = smb_IoctlReadRaw(fidp, vcp, inp, outp);
#else
        rc = smb_IoctlReadRaw(fidp, vcp, inp, outp, rawBuf);
#endif
        if (rawBuf) {
            /* Give back raw buffer */
            lock_ObtainMutex(&smb_RawBufLock);
#ifndef DJGPP
            *((char **) rawBuf) = smb_RawBufs;
#else /* DJGPP */
            _farpokel(_dos_ds, rawBuf, smb_RawBufs);
#endif /* !DJGPP */
            
            smb_RawBufs = rawBuf;
            lock_ReleaseMutex(&smb_RawBufLock);
        }

        smb_ReleaseFID(fidp);
        return rc;
    }
        
    userp = smb_GetUser(vcp, inp);

#ifndef DJGPP
    code = smb_ReadData(fidp, &offset, count, rawBuf, userp, &finalCount);
#else /* DJGPP */
    /* have to give ReadData flag so it will treat buffer as DOS mem. */
    code = smb_ReadData(fidp, &offset, count, (unsigned char *)rawBuf,
                        userp, &finalCount, TRUE /* rawFlag */);
#endif /* !DJGPP */

    if (code != 0)
        goto send;

  send:
    cm_ReleaseUser(userp);

  send1a:
    smb_ReleaseFID(fidp);

  send1:
    ncbp = outp->ncbp;
#ifdef DJGPP
    dos_ncb = ((smb_ncb_t *)ncbp)->dos_ncb;
#endif /* DJGPP */
    memset((char *)ncbp, 0, sizeof(NCB));

    ncbp->ncb_length = (unsigned short) finalCount;
    ncbp->ncb_lsn = (unsigned char) vcp->lsn;
    ncbp->ncb_lana_num = vcp->lana;
    ncbp->ncb_command = NCBSEND;
    ncbp->ncb_buffer = rawBuf;

#ifndef DJGPP
    code = Netbios(ncbp);
#else /* DJGPP */
    code = Netbios(ncbp, dos_ncb);
#endif /* !DJGPP */
    if (code != 0)
        osi_Log1(smb_logp, "ReadRaw send failure code %d", code);

    if (rawBuf) {
        /* Give back raw buffer */
        lock_ObtainMutex(&smb_RawBufLock);
#ifndef DJGPP
        *((char **) rawBuf) = smb_RawBufs;
#else /* DJGPP */
        _farpokel(_dos_ds, rawBuf, smb_RawBufs);
#endif /* !DJGPP */

        smb_RawBufs = rawBuf;
        lock_ReleaseMutex(&smb_RawBufLock);
    }

    return 0;
}

long smb_ReceiveCoreLockRecord(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    osi_Log1(smb_logp, "SMB receive core lock record (not implemented); %d + 1 ongoing ops",
			 ongoingOps - 1);
    return 0;
}

long smb_ReceiveCoreUnlockRecord(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    osi_Log1(smb_logp, "SMB receive core unlock record (not implemented); %d + 1 ongoing ops",
			 ongoingOps - 1);
    return 0;
}

long smb_ReceiveNegotiate(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    char *namep;
    char *datap;
    int coreProtoIndex;
    int v3ProtoIndex;
    int NTProtoIndex;
    int protoIndex;			        /* index we're using */
    int namex;
    int dbytes;
    int entryLength;
    int tcounter;
    char protocol_array[10][1024];  /* protocol signature of the client */
    int caps;                       /* capabilities */
    time_t unixTime;
    afs_uint32 dosTime;
    TIME_ZONE_INFORMATION tzi;

    osi_Log1(smb_logp, "SMB receive negotiate; %d + 1 ongoing ops",
			 ongoingOps - 1);
    if (!isGateway) {
        if (active_vcp) {
            DWORD now = GetCurrentTime();
            if (now - last_msg_time >= 30000
                 && now - last_msg_time <= 90000) {
                osi_Log1(smb_logp,
                          "Setting dead_vcp %x", active_vcp);
                if (dead_vcp) {
                    smb_ReleaseVC(dead_vcp);
                    osi_Log1(smb_logp,
                             "Previous dead_vcp %x", dead_vcp);
                }
                smb_HoldVC(active_vcp);
                dead_vcp = active_vcp;
                dead_vcp->flags |= SMB_VCFLAG_ALREADYDEAD;
            }
        }
    }

    inp->flags |= SMB_PACKETFLAG_PROFILE_UPDATE_OK;

    namep = smb_GetSMBData(inp, &dbytes);
    namex = 0;
    tcounter = 0;
    coreProtoIndex = -1;		/* not found */
    v3ProtoIndex = -1;
    NTProtoIndex = -1;
    while(namex < dbytes) {
        osi_Log1(smb_logp, "Protocol %s",
                  osi_LogSaveString(smb_logp, namep+1));
        strcpy(protocol_array[tcounter], namep+1);

        /* namep points at the first protocol, or really, a 0x02
         * byte preceding the null-terminated ASCII name.
         */
        if (strcmp("PC NETWORK PROGRAM 1.0", namep+1) == 0) {
            coreProtoIndex = tcounter;
        }	
        else if (smb_useV3 && strcmp("LM1.2X002", namep+1) == 0) {
            v3ProtoIndex = tcounter;
        }
        else if (smb_useV3 && strcmp("NT LM 0.12", namep+1) == 0) {
            NTProtoIndex = tcounter;
        }

        /* compute size of protocol entry */
        entryLength = strlen(namep+1);
        entryLength += 2;	/* 0x02 bytes and null termination */

        /* advance over this protocol entry */
        namex += entryLength;
        namep += entryLength;
        tcounter++;		/* which proto entry we're looking at */
    }

    if (NTProtoIndex != -1) {
        protoIndex = NTProtoIndex;
        vcp->flags |= (SMB_VCFLAG_USENT | SMB_VCFLAG_USEV3);
    }
    else if (v3ProtoIndex != -1) {
        protoIndex = v3ProtoIndex;
        vcp->flags |= SMB_VCFLAG_USEV3;
    }	
    else if (coreProtoIndex != -1) {
        protoIndex = coreProtoIndex;
        vcp->flags |= SMB_VCFLAG_USECORE;
    }	
    else protoIndex = -1;

    if (protoIndex == -1)
        return CM_ERROR_INVAL;
    else if (NTProtoIndex != -1) {
        smb_SetSMBParm(outp, 0, protoIndex);
        if (smb_authType != SMB_AUTH_NONE) {
            smb_SetSMBParmByte(outp, 1,
                               NEGOTIATE_SECURITY_USER_LEVEL |
                               NEGOTIATE_SECURITY_CHALLENGE_RESPONSE);	/* user level security, challenge response */
        } else {
            smb_SetSMBParmByte(outp, 1, 0); /* share level auth with plaintext password. */
        }
        smb_SetSMBParm(outp, 1, smb_maxMpxRequests);	/* max multiplexed requests */
        smb_SetSMBParm(outp, 2, smb_maxVCPerServer);	/* max VCs per consumer/server connection */
        smb_SetSMBParmLong(outp, 3, SMB_PACKETSIZE);    /* xmit buffer size */
        smb_SetSMBParmLong(outp, 5, SMB_MAXRAWSIZE);	/* raw buffer size */
        /* The session key is not a well documented field however most clients
         * will echo back the session key to the server.  Currently we are using
         * the same value for all sessions.  We should generate a random value
         * and store it into the vcp 
         */
        smb_SetSMBParm(outp, 7, 1);	/* next 2: session key */
        smb_SetSMBParm(outp, 8, 1);
        /* 
         * Tried changing the capabilities to support for W2K - defect 117695
         * Maybe something else needs to be changed here?
         */
        /*
        if (isWindows2000) 
        smb_SetSMBParmLong(outp, 9, 0x43fd);
        else 
        smb_SetSMBParmLong(outp, 9, 0x251);
        */
        /* Capabilities: *
         * 32-bit error codes *
         * and NT Find *
         * and NT SMB's *
         * and raw mode 
         * and DFS */
        caps = NTNEGOTIATE_CAPABILITY_NTSTATUS |
#ifdef DFS_SUPPORT
               NTNEGOTIATE_CAPABILITY_DFS |
#endif
               NTNEGOTIATE_CAPABILITY_NTFIND |
               NTNEGOTIATE_CAPABILITY_RAWMODE |
               NTNEGOTIATE_CAPABILITY_NTSMB;

        if ( smb_authType == SMB_AUTH_EXTENDED )
            caps |= NTNEGOTIATE_CAPABILITY_EXTENDED_SECURITY;

        smb_SetSMBParmLong(outp, 9, caps);
        time(&unixTime);
        smb_SearchTimeFromUnixTime(&dosTime, unixTime);
        smb_SetSMBParmLong(outp, 11, LOWORD(dosTime));/* server time */
        smb_SetSMBParmLong(outp, 13, HIWORD(dosTime));/* server date */

        GetTimeZoneInformation(&tzi);
        smb_SetSMBParm(outp, 15, (unsigned short) tzi.Bias);	/* server tzone */

        if (smb_authType == SMB_AUTH_NTLM) {
            smb_SetSMBParmByte(outp, 16, MSV1_0_CHALLENGE_LENGTH);/* Encryption key length */
            smb_SetSMBDataLength(outp, MSV1_0_CHALLENGE_LENGTH + smb_ServerDomainNameLength);
            /* paste in encryption key */
            datap = smb_GetSMBData(outp, NULL);
            memcpy(datap,vcp->encKey,MSV1_0_CHALLENGE_LENGTH);
            /* and the faux domain name */
            strcpy(datap + MSV1_0_CHALLENGE_LENGTH,smb_ServerDomainName);
        } else if ( smb_authType == SMB_AUTH_EXTENDED ) {
            void * secBlob;
            int secBlobLength;

            smb_SetSMBParmByte(outp, 16, 0); /* Encryption key length */

            smb_NegotiateExtendedSecurity(&secBlob, &secBlobLength);

            smb_SetSMBDataLength(outp, secBlobLength + sizeof(smb_ServerGUID));
			
            datap = smb_GetSMBData(outp, NULL);
            memcpy(datap, &smb_ServerGUID, sizeof(smb_ServerGUID));

            if (secBlob) {
                datap += sizeof(smb_ServerGUID);
                memcpy(datap, secBlob, secBlobLength);
                free(secBlob);
            }
        } else {
            smb_SetSMBParmByte(outp, 16, 0); /* Encryption key length */
            smb_SetSMBDataLength(outp, 0);   /* Perhaps we should specify 8 bytes anyway */
        }
    }
    else if (v3ProtoIndex != -1) {
        smb_SetSMBParm(outp, 0, protoIndex);

        /* NOTE: Extended authentication cannot be negotiated with v3
         * therefore we fail over to NTLM 
         */
        if (smb_authType == SMB_AUTH_NTLM || smb_authType == SMB_AUTH_EXTENDED) {
            smb_SetSMBParm(outp, 1,
                           NEGOTIATE_SECURITY_USER_LEVEL |
                           NEGOTIATE_SECURITY_CHALLENGE_RESPONSE);	/* user level security, challenge response */
        } else {
            smb_SetSMBParm(outp, 1, 0); /* share level auth with clear password */
        }
        smb_SetSMBParm(outp, 2, SMB_PACKETSIZE);
        smb_SetSMBParm(outp, 3, smb_maxMpxRequests);	/* max multiplexed requests */
        smb_SetSMBParm(outp, 4, smb_maxVCPerServer);	/* max VCs per consumer/server connection */
        smb_SetSMBParm(outp, 5, 0);	/* no support of block mode for read or write */
        smb_SetSMBParm(outp, 6, 1);	/* next 2: session key */
        smb_SetSMBParm(outp, 7, 1);
        time(&unixTime);
        smb_SearchTimeFromUnixTime(&dosTime, unixTime);
        smb_SetSMBParm(outp, 8, LOWORD(dosTime));	/* server time */
        smb_SetSMBParm(outp, 9, HIWORD(dosTime));	/* server date */

        GetTimeZoneInformation(&tzi);
        smb_SetSMBParm(outp, 10, (unsigned short) tzi.Bias);	/* server tzone */

        /* NOTE: Extended authentication cannot be negotiated with v3
         * therefore we fail over to NTLM 
         */
        if (smb_authType == SMB_AUTH_NTLM || smb_authType == SMB_AUTH_EXTENDED) {
            smb_SetSMBParm(outp, 11, MSV1_0_CHALLENGE_LENGTH);	/* encryption key length */
            smb_SetSMBParm(outp, 12, 0);	/* resvd */
            smb_SetSMBDataLength(outp, MSV1_0_CHALLENGE_LENGTH + smb_ServerDomainNameLength);	/* perhaps should specify 8 bytes anyway */
            datap = smb_GetSMBData(outp, NULL);
            /* paste in a new encryption key */
            memcpy(datap, vcp->encKey, MSV1_0_CHALLENGE_LENGTH);
            /* and the faux domain name */
            strcpy(datap + MSV1_0_CHALLENGE_LENGTH, smb_ServerDomainName);
        } else {
            smb_SetSMBParm(outp, 11, 0); /* encryption key length */
            smb_SetSMBParm(outp, 12, 0); /* resvd */
            smb_SetSMBDataLength(outp, 0);
        }
    }
    else if (coreProtoIndex != -1) {     /* not really supported anymore */
        smb_SetSMBParm(outp, 0, protoIndex);
        smb_SetSMBDataLength(outp, 0);
    }
    return 0;
}

void smb_Daemon(void *parmp)
{
    afs_uint32 count = 0;

    while(smbShutdownFlag == 0) {
        count++;
        thrd_Sleep(10000);

        if (smbShutdownFlag == 1)
            break;
        
        if ((count % 72) == 0)	{	/* every five minutes */
            struct tm myTime;
            time_t old_localZero = smb_localZero;
		 
            /* Initialize smb_localZero */
            myTime.tm_isdst = -1;		/* compute whether on DST or not */
            myTime.tm_year = 70;
            myTime.tm_mon = 0;
            myTime.tm_mday = 1;
            myTime.tm_hour = 0;
            myTime.tm_min = 0;
            myTime.tm_sec = 0;
            smb_localZero = mktime(&myTime);

#ifndef USE_NUMERIC_TIME_CONV
            smb_CalculateNowTZ();
#endif /* USE_NUMERIC_TIME_CONV */
#ifdef AFS_FREELANCE
            if ( smb_localZero != old_localZero )
                cm_noteLocalMountPointChange();
#endif
        }
        /* XXX GC dir search entries */
    }
}

void smb_WaitingLocksDaemon()
{
    smb_waitingLock_t *wL, *nwL;
    int first;
    smb_vc_t *vcp;
    smb_packet_t *inp, *outp;
    NCB *ncbp;
    long code = 0;

    while (smbShutdownFlag == 0) {
        lock_ObtainWrite(&smb_globalLock);
        nwL = smb_allWaitingLocks;
        if (nwL == NULL) {
            osi_SleepW((long)&smb_allWaitingLocks, &smb_globalLock);
            thrd_Sleep(1000);
            continue;
        } else 
            first = 1;

        do {
            if (first)
                first = 0;
            else
                lock_ObtainWrite(&smb_globalLock);
            wL = nwL;
            nwL = (smb_waitingLock_t *) osi_QNext(&wL->q);
            lock_ReleaseWrite(&smb_globalLock);
            code = cm_RetryLock((cm_file_lock_t *) wL->lockp,
                                 wL->vcp->flags & SMB_VCFLAG_ALREADYDEAD);
            if (code == CM_ERROR_WOULDBLOCK) {
                /* no progress */
                if (wL->timeRemaining != 0xffffffff
                     && (wL->timeRemaining -= 1000) < 0)
                    goto endWait;
                continue;
            }

          endWait:
            vcp = wL->vcp;
            inp = wL->inp;
            outp = wL->outp;
            ncbp = GetNCB();
            ncbp->ncb_length = inp->ncb_length;
            inp->spacep = cm_GetSpace();

            /* Remove waitingLock from list */
            lock_ObtainWrite(&smb_globalLock);
            osi_QRemove((osi_queue_t **)&smb_allWaitingLocks,
                         &wL->q);
            lock_ReleaseWrite(&smb_globalLock);

            /* Resume packet processing */
            if (code == 0)
                smb_SetSMBDataLength(outp, 0);
            outp->flags |= SMB_PACKETFLAG_SUSPENDED;
            outp->resumeCode = code;
            outp->ncbp = ncbp;
            smb_DispatchPacket(vcp, inp, outp, ncbp, NULL);

            /* Clean up */
            cm_FreeSpace(inp->spacep);
            smb_FreePacket(inp);
            smb_FreePacket(outp);
            smb_ReleaseVC(vcp);
            FreeNCB(ncbp);
            free(wL);
        } while (nwL && smbShutdownFlag == 0);
        thrd_Sleep(1000);
    }
}

long smb_ReceiveCoreGetDiskAttributes(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    osi_Log0(smb_logp, "SMB receive get disk attributes");

    smb_SetSMBParm(outp, 0, 32000);
    smb_SetSMBParm(outp, 1, 64);
    smb_SetSMBParm(outp, 2, 1024);
    smb_SetSMBParm(outp, 3, 30000);
    smb_SetSMBParm(outp, 4, 0);
    smb_SetSMBDataLength(outp, 0);
    return 0;
}

long smb_ReceiveCoreTreeConnect(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *rsp)
{
    smb_tid_t *tidp;
    smb_user_t *uidp;
    unsigned short newTid;
    char shareName[256];
    char *sharePath;
    int shareFound;
    char *tp;
    char *pathp;
    char *passwordp;
    cm_user_t *userp;

    osi_Log0(smb_logp, "SMB receive tree connect");

    /* parse input parameters */
    tp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(tp, &tp);
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);
    passwordp = smb_ParseASCIIBlock(tp, &tp);
    tp = strrchr(pathp, '\\');
    if (!tp)
        return CM_ERROR_BADSMB;
    strcpy(shareName, tp+1);

    userp = smb_GetUser(vcp, inp);

    lock_ObtainMutex(&vcp->mx);
    newTid = vcp->tidCounter++;
    lock_ReleaseMutex(&vcp->mx);

    tidp = smb_FindTID(vcp, newTid, SMB_FLAG_CREATE);
    uidp = smb_FindUID(vcp, ((smb_t *)inp)->uid, 0);
    shareFound = smb_FindShare(vcp, uidp, shareName, &sharePath);
    if (uidp)
        smb_ReleaseUID(uidp);
    if (!shareFound) {
        smb_ReleaseTID(tidp);
        return CM_ERROR_BADSHARENAME;
    }
    lock_ObtainMutex(&tidp->mx);
    tidp->userp = userp;
    tidp->pathname = sharePath;
    lock_ReleaseMutex(&tidp->mx);
    smb_ReleaseTID(tidp);

    smb_SetSMBParm(rsp, 0, SMB_PACKETSIZE);
    smb_SetSMBParm(rsp, 1, newTid);
    smb_SetSMBDataLength(rsp, 0);

    osi_Log1(smb_logp, "SMB tree connect created ID %d", newTid);
    return 0;
}

unsigned char *smb_ParseDataBlock(unsigned char *inp, char **chainpp, int *lengthp)
{
    int tlen;

    if (*inp++ != 0x1) return NULL;
    tlen = inp[0] + (inp[1]<<8);
    inp += 2;		/* skip length field */
        
    if (chainpp) {
        *chainpp = inp + tlen;
    }	

    if (lengthp) *lengthp = tlen;
        
    return inp;
}

/* set maskp to the mask part of the incoming path.
 * Mask is 11 bytes long (8.3 with the dot elided).
 * Returns true if succeeds with a valid name, otherwise it does
 * its best, but returns false.
 */
int smb_Get8Dot3MaskFromPath(unsigned char *maskp, unsigned char *pathp)
{
    char *tp;
    char *up;
    int i;
    int tc;
    int valid8Dot3;

    /* starts off valid */
    valid8Dot3 = 1;

    /* mask starts out all blanks */
    memset(maskp, ' ', 11);

    /* find last backslash, or use whole thing if there is none */
    tp = strrchr(pathp, '\\');
    if (!tp) tp = pathp;
    else tp++;	/* skip slash */
        
    up = maskp;

    /* names starting with a dot are illegal */
    if (*tp == '.') valid8Dot3 = 0;

    for(i=0;; i++) {
        tc = *tp++;
        if (tc == 0) return valid8Dot3;
        if (tc == '.' || tc == '"') break;
        if (i < 8) *up++ = tc;
        else valid8Dot3 = 0;
    }
        
    /* if we get here, tp point after the dot */
    up = maskp+8;	/* ext goes here */
    for(i=0;;i++) {
        tc = *tp++;
        if (tc == 0) 
            return valid8Dot3;

        /* too many dots */
        if (tc == '.' || tc == '"') 
            valid8Dot3 = 0;

        /* copy extension if not too long */
        if (i < 3) 
            *up++ = tc;
        else 
            valid8Dot3 = 0;
    }   

    /* unreachable */
}

int smb_Match8Dot3Mask(char *unixNamep, char *maskp)
{
    char umask[11];
    int valid;
    int i;
    char tc1;
    char tc2;
    char *tp1;
    char *tp2;

    /* XXX redo this, calling smb_V3MatchMask with a converted mask */

    valid = smb_Get8Dot3MaskFromPath(umask, unixNamep);
    if (!valid) 
        return 0;
 
    /* otherwise, we have a valid 8.3 name; see if we have a match,
     * treating '?' as a wildcard in maskp (but not in the file name).
     */
    tp1 = umask;	/* real name, in mask format */
    tp2 = maskp;	/* mask, in mask format */
    for(i=0; i<11; i++) {
        tc1 = *tp1++;	/* char from real name */
        tc2 = *tp2++;	/* char from mask */
        tc1 = (char) cm_foldUpper[(unsigned char)tc1];
        tc2 = (char) cm_foldUpper[(unsigned char)tc2];
        if (tc1 == tc2) 
            continue;
        if (tc2 == '?' && tc1 != ' ') 
            continue;
        if (tc2 == '>') 
            continue;
        return 0;
    }

    /* we got a match */
    return 1;
}

char *smb_FindMask(char *pathp)
{
    char *tp;
        
    tp = strrchr(pathp, '\\');	/* find last slash */

    if (tp) 
        return tp+1;	/* skip the slash */
    else 
        return pathp;	/* no slash, return the entire path */
}       

long smb_ReceiveCoreSearchVolume(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    unsigned char *pathp;
    unsigned char *tp;
    unsigned char mask[11];
    unsigned char *statBlockp;
    unsigned char initStatBlock[21];
    int statLen;
        
    osi_Log0(smb_logp, "SMB receive search volume");

    /* pull pathname and stat block out of request */
    tp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(tp, (char **) &tp);
    osi_assert(pathp != NULL);
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);
    statBlockp = smb_ParseVblBlock(tp, (char **) &tp, &statLen);
    osi_assert(statBlockp != NULL);
    if (statLen == 0) {
        statBlockp = initStatBlock;
        statBlockp[0] = 8;
    }
        
    /* for returning to caller */
    smb_Get8Dot3MaskFromPath(mask, pathp);

    smb_SetSMBParm(outp, 0, 1);		/* we're returning one entry */
    tp = smb_GetSMBData(outp, NULL);
    *tp++ = 5;
    *tp++ = 43;	/* bytes in a dir entry */
    *tp++ = 0;	/* high byte in counter */

    /* now marshall the dir entry, starting with the search status */
    *tp++ = statBlockp[0];		/* Reserved */
    memcpy(tp, mask, 11); tp += 11;	/* FileName */

    /* now pass back server use info, with 1st byte non-zero */
    *tp++ = 1;
    memset(tp, 0, 4); tp += 4;	/* reserved for server use */

    memcpy(tp, statBlockp+17, 4); tp += 4;	/* reserved for consumer */

    *tp++ = 0x8;		/* attribute: volume */

    /* copy out time */
    *tp++ = 0;
    *tp++ = 0;

    /* copy out date */
    *tp++ = 18;
    *tp++ = 178;

    /* 4 byte file size */
    *tp++ = 0;
    *tp++ = 0;
    *tp++ = 0;
    *tp++ = 0;

    /* finally, null-terminated 8.3 pathname, which we set to AFS */
    memset(tp, ' ', 13);
    strcpy(tp, "AFS");

    /* set the length of the data part of the packet to 43 + 3, for the dir
     * entry plus the 5 and the length fields.
     */
    smb_SetSMBDataLength(outp, 46);
    return 0;
}       

long smb_ApplyDirListPatches(smb_dirListPatch_t **dirPatchespp,
                             cm_user_t *userp, cm_req_t *reqp)
{
    long code = 0;
    cm_scache_t *scp;
    char *dptr;
    afs_uint32 dosTime;
    u_short shortTemp;
    char attr;
    smb_dirListPatch_t *patchp;
    smb_dirListPatch_t *npatchp;

    for (patchp = *dirPatchespp; patchp; patchp =
         (smb_dirListPatch_t *) osi_QNext(&patchp->q)) {

        dptr = patchp->dptr;

        code = cm_GetSCache(&patchp->fid, &scp, userp, reqp);
        if (code) {
            if( patchp->flags & SMB_DIRLISTPATCH_DOTFILE )
                *dptr++ = SMB_ATTR_HIDDEN;
            continue;
        }
        lock_ObtainMutex(&scp->mx);
        code = cm_SyncOp(scp, NULL, userp, reqp, 0,
                          CM_SCACHESYNC_NEEDCALLBACK | CM_SCACHESYNC_GETSTATUS);
        if (code) {	
            lock_ReleaseMutex(&scp->mx);
            cm_ReleaseSCache(scp);
            if (patchp->flags & SMB_DIRLISTPATCH_DOTFILE)
                *dptr++ = SMB_ATTR_HIDDEN;
            continue;
        }

        attr = smb_Attributes(scp);
        /* check hidden attribute (the flag is only ON when dot file hiding is on ) */
        if (patchp->flags & SMB_DIRLISTPATCH_DOTFILE)
            attr |= SMB_ATTR_HIDDEN;
        *dptr++ = attr;

        /* get dos time */
        smb_SearchTimeFromUnixTime(&dosTime, scp->clientModTime);
                
        /* copy out time */
        shortTemp = (unsigned short) (dosTime & 0xffff);
        *((u_short *)dptr) = shortTemp;
        dptr += 2;

        /* and copy out date */
        shortTemp = (unsigned short) ((dosTime>>16) & 0xffff);
        *((u_short *)dptr) = shortTemp;
        dptr += 2;
                
        /* copy out file length */
        *((u_long *)dptr) = scp->length.LowPart;
        dptr += 4;
        lock_ReleaseMutex(&scp->mx);
        cm_ReleaseSCache(scp);
    }
        
    /* now free the patches */
    for (patchp = *dirPatchespp; patchp; patchp = npatchp) {
        npatchp = (smb_dirListPatch_t *) osi_QNext(&patchp->q);
        free(patchp);
    }	
        
    /* and mark the list as empty */
    *dirPatchespp = NULL;

    return code;
}

long smb_ReceiveCoreSearchDir(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    int attribute;
    long nextCookie;
    char *tp;
    long code = 0;
    char *pathp;
    cm_dirEntry_t *dep;
    int maxCount;
    smb_dirListPatch_t *dirListPatchesp;
    smb_dirListPatch_t *curPatchp;
    int dataLength;
    cm_buf_t *bufferp;
    long temp;
    osi_hyper_t dirLength;
    osi_hyper_t bufferOffset;
    osi_hyper_t curOffset;
    osi_hyper_t thyper;
    unsigned char *inCookiep;
    smb_dirSearch_t *dsp;
    cm_scache_t *scp;
    long entryInDir;
    long entryInBuffer;
    unsigned long clientCookie;
    cm_pageHeader_t *pageHeaderp;
    cm_user_t *userp = NULL;
    int slotInPage;
    char shortName[13];
    char *actualName;
    char *shortNameEnd;
    char mask[11];
    int returnedNames;
    long nextEntryCookie;
    int numDirChunks;		/* # of 32 byte dir chunks in this entry */
    char resByte;		/* reserved byte from the cookie */
    char *op;			/* output data ptr */
    char *origOp;		/* original value of op */
    cm_space_t *spacep;		/* for pathname buffer */
    int starPattern;
    int rootPath = 0;
    int caseFold;
    char *tidPathp;
    cm_req_t req;
    cm_fid_t fid;
    int fileType;

    cm_InitReq(&req);

    maxCount = smb_GetSMBParm(inp, 0);

    dirListPatchesp = NULL;
        
    caseFold = CM_FLAG_CASEFOLD;

    tp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(tp, &tp);
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);
    inCookiep = smb_ParseVblBlock(tp, &tp, &dataLength);

    /* bail out if request looks bad */
    if (!tp || !pathp) {
        return CM_ERROR_BADSMB;
    }

    /* We can handle long names */
    if (vcp->flags & SMB_VCFLAG_USENT)
        ((smb_t *)outp)->flg2 |= SMB_FLAGS2_IS_LONG_NAME;

    /* make sure we got a whole search status */
    if (dataLength < 21) {
        nextCookie = 0;		/* start at the beginning of the dir */
        resByte = 0;
        clientCookie = 0;
        attribute = smb_GetSMBParm(inp, 1);

        /* handle volume info in another function */
        if (attribute & 0x8)
            return smb_ReceiveCoreSearchVolume(vcp, inp, outp);

        osi_Log2(smb_logp, "SMB receive search dir count %d [%s]",
                  maxCount, osi_LogSaveString(smb_logp, pathp));

        if (*pathp == 0) {	/* null pathp, treat as root dir */
            if (!(attribute & SMB_ATTR_DIRECTORY))	/* exclude dirs */
                return CM_ERROR_NOFILES;
            rootPath = 1;
        }

        dsp = smb_NewDirSearch(0);
        dsp->attribute = attribute;
        smb_Get8Dot3MaskFromPath(mask, pathp);
        memcpy(dsp->mask, mask, 11);

        /* track if this is likely to match a lot of entries */
        if (smb_IsStarMask(mask)) 
            starPattern = 1;
        else 
            starPattern = 0;
    } else {
        /* pull the next cookie value out of the search status block */
        nextCookie = inCookiep[13] + (inCookiep[14]<<8) + (inCookiep[15]<<16)
            + (inCookiep[16]<<24);
        dsp = smb_FindDirSearch(inCookiep[12]);
        if (!dsp) {
            /* can't find dir search status; fatal error */
            osi_Log3(smb_logp, "SMB receive search dir bad cookie: cookie %d nextCookie %u [%s]",
                     inCookiep[12], nextCookie, osi_LogSaveString(smb_logp, pathp));
            return CM_ERROR_BADFD;
        }
        attribute = dsp->attribute;
        resByte = inCookiep[0];

        /* copy out client cookie, in host byte order.  Don't bother
         * interpreting it, since we're just passing it through, anyway.
         */
        memcpy(&clientCookie, &inCookiep[17], 4);

        memcpy(mask, dsp->mask, 11);

        /* assume we're doing a star match if it has continued for more
         * than one call.
         */
        starPattern = 1;
    }

    osi_Log3(smb_logp, "SMB search dir cookie 0x%x, connection %d, attr 0x%x",
             nextCookie, dsp->cookie, attribute);

    userp = smb_GetUser(vcp, inp);

    /* try to get the vnode for the path name next */
    lock_ObtainMutex(&dsp->mx);
    if (dsp->scp) {
        scp = dsp->scp;
        cm_HoldSCache(scp);
        code = 0;
    } else {
        spacep = inp->spacep;
        smb_StripLastComponent(spacep->data, NULL, pathp);
        code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
        if (code) {
            lock_ReleaseMutex(&dsp->mx);
            cm_ReleaseUser(userp);
            smb_DeleteDirSearch(dsp);
            smb_ReleaseDirSearch(dsp);
            return CM_ERROR_NOFILES;
        }
        code = cm_NameI(cm_data.rootSCachep, spacep->data,
                        caseFold | CM_FLAG_FOLLOW, userp, tidPathp, &req, &scp);
        if (code == 0) {
#ifdef DFS_SUPPORT
            if (scp->fileType == CM_SCACHETYPE_DFSLINK) {
                cm_ReleaseSCache(scp);
                lock_ReleaseMutex(&dsp->mx);
                cm_ReleaseUser(userp);
                smb_DeleteDirSearch(dsp);
                smb_ReleaseDirSearch(dsp);
                if ( WANTS_DFS_PATHNAMES(inp) )
                    return CM_ERROR_PATH_NOT_COVERED;
                else
                    return CM_ERROR_BADSHARENAME;
            }
#endif /* DFS_SUPPORT */

            dsp->scp = scp;
            /* we need one hold for the entry we just stored into,
             * and one for our own processing.  When we're done with this
             * function, we'll drop the one for our own processing.
             * We held it once from the namei call, and so we do another hold
             * now.
             */
            cm_HoldSCache(scp);
            lock_ObtainMutex(&scp->mx);
            if ((scp->flags & CM_SCACHEFLAG_BULKSTATTING) == 0
                 && LargeIntegerGreaterOrEqualToZero(scp->bulkStatProgress)) {
                scp->flags |= CM_SCACHEFLAG_BULKSTATTING;
                dsp->flags |= SMB_DIRSEARCH_BULKST;
            }
            lock_ReleaseMutex(&scp->mx);
        }
    }
    lock_ReleaseMutex(&dsp->mx);
    if (code) {
        cm_ReleaseUser(userp);
        smb_DeleteDirSearch(dsp);
        smb_ReleaseDirSearch(dsp);
        return code;
    }

    /* reserves space for parameter; we'll adjust it again later to the
     * real count of the # of entries we returned once we've actually
     * assembled the directory listing.
     */
    smb_SetSMBParm(outp, 0, 0);

    /* get the directory size */
    lock_ObtainMutex(&scp->mx);
    code = cm_SyncOp(scp, NULL, userp, &req, 0,
                     CM_SCACHESYNC_NEEDCALLBACK | CM_SCACHESYNC_GETSTATUS);
    if (code) {
        lock_ReleaseMutex(&scp->mx);
        cm_ReleaseSCache(scp);
        cm_ReleaseUser(userp);
        smb_DeleteDirSearch(dsp);
        smb_ReleaseDirSearch(dsp);
        return code;
    }
        
    dirLength = scp->length;
    bufferp = NULL;
    bufferOffset.LowPart = bufferOffset.HighPart = 0;
    curOffset.HighPart = 0;
    curOffset.LowPart = nextCookie;
    origOp = op = smb_GetSMBData(outp, NULL);
    /* and write out the basic header */
    *op++ = 5;		/* variable block */
    op += 2;		/* skip vbl block length; we'll fill it in later */
    code = 0;
    returnedNames = 0;
    while (1) {
        /* make sure that curOffset.LowPart doesn't point to the first
         * 32 bytes in the 2nd through last dir page, and that it doesn't
         * point at the first 13 32-byte chunks in the first dir page,
         * since those are dir and page headers, and don't contain useful
         * information.
         */
        temp = curOffset.LowPart & (2048-1);
        if (curOffset.HighPart == 0 && curOffset.LowPart < 2048) {
            /* we're in the first page */
            if (temp < 13*32) temp = 13*32;
        }
        else {
            /* we're in a later dir page */
            if (temp < 32) temp = 32;
        }

        /* make sure the low order 5 bits are zero */
        temp &= ~(32-1);

        /* now put temp bits back ito curOffset.LowPart */
        curOffset.LowPart &= ~(2048-1);
        curOffset.LowPart |= temp;

        /* check if we've returned all the names that will fit in the
         * response packet.
         */
        if (returnedNames >= maxCount) {
            osi_Log2(smb_logp, "SMB search dir returnedNames %d >= maxCount %d",
                      returnedNames, maxCount);
            break;
        }
                
        /* check if we've passed the dir's EOF */
        if (LargeIntegerGreaterThanOrEqualTo(curOffset, dirLength)) break;

        /* see if we can use the bufferp we have now; compute in which page
         * the current offset would be, and check whether that's the offset
         * of the buffer we have.  If not, get the buffer.
         */
        thyper.HighPart = curOffset.HighPart;
        thyper.LowPart = curOffset.LowPart & ~(cm_data.buf_blockSize-1);
        if (!bufferp || !LargeIntegerEqualTo(thyper, bufferOffset)) {
            /* wrong buffer */
            if (bufferp) {
                buf_Release(bufferp);
                bufferp = NULL;
            }	
            lock_ReleaseMutex(&scp->mx);
            lock_ObtainRead(&scp->bufCreateLock);
            code = buf_Get(scp, &thyper, &bufferp);
            lock_ReleaseRead(&scp->bufCreateLock);
            lock_ObtainMutex(&dsp->mx);

            /* now, if we're doing a star match, do bulk fetching of all of 
             * the status info for files in the dir.
             */
            if (starPattern) {
                smb_ApplyDirListPatches(&dirListPatchesp, userp, &req);
                lock_ObtainMutex(&scp->mx);
                if ((dsp->flags & SMB_DIRSEARCH_BULKST) &&
                     LargeIntegerGreaterThanOrEqualTo(thyper, 
                                                      scp->bulkStatProgress)) {
                    /* Don't bulk stat if risking timeout */
                    int now = GetCurrentTime();
                    if (now - req.startTime > 5000) {
                        scp->bulkStatProgress = thyper;
                        scp->flags &= ~CM_SCACHEFLAG_BULKSTATTING;
                        dsp->flags &= ~SMB_DIRSEARCH_BULKST;
                    } else
                        cm_TryBulkStat(scp, &thyper, userp, &req);
                }
            } else {
                lock_ObtainMutex(&scp->mx);
            }
            lock_ReleaseMutex(&dsp->mx);
            if (code) {
                osi_Log2(smb_logp, "SMB search dir buf_Get scp %x failed %d", scp, code);
                break;
            }

            bufferOffset = thyper;

            /* now get the data in the cache */
            while (1) {
                code = cm_SyncOp(scp, bufferp, userp, &req,
                                 PRSFS_LOOKUP,
                                 CM_SCACHESYNC_NEEDCALLBACK |
                                 CM_SCACHESYNC_READ);
                if (code) {
                    osi_Log2(smb_logp, "SMB search dir cm_SyncOp scp %x failed %d", scp, code);
                    break;
                }
                                
                if (cm_HaveBuffer(scp, bufferp, 0)) {
                    osi_Log2(smb_logp, "SMB search dir !HaveBuffer scp %x bufferp %x", scp, bufferp);
                    break;
                }

                /* otherwise, load the buffer and try again */
                code = cm_GetBuffer(scp, bufferp, NULL, userp, &req);
                if (code) {
                    osi_Log3(smb_logp, "SMB search dir cm_GetBuffer failed scp %x bufferp %x code %d", 
                              scp, bufferp, code);
                    break;
                }
            }
            if (code) {
                buf_Release(bufferp);
                bufferp = NULL;
                break;
            }
        }	/* if (wrong buffer) ... */

        /* now we have the buffer containing the entry we're interested in; copy
         * it out if it represents a non-deleted entry.
         */
        entryInDir = curOffset.LowPart & (2048-1);
        entryInBuffer = curOffset.LowPart & (cm_data.buf_blockSize - 1);

        /* page header will help tell us which entries are free.  Page header
         * can change more often than once per buffer, since AFS 3 dir page size
         * may be less than (but not more than a buffer package buffer.
         */
        temp = curOffset.LowPart & (cm_data.buf_blockSize - 1);  /* only look intra-buffer */
        temp &= ~(2048 - 1);	/* turn off intra-page bits */
        pageHeaderp = (cm_pageHeader_t *) (bufferp->datap + temp);

        /* now determine which entry we're looking at in the page.  If it is
         * free (there's a free bitmap at the start of the dir), we should
         * skip these 32 bytes.
         */
        slotInPage = (entryInDir & 0x7e0) >> 5;
        if (!(pageHeaderp->freeBitmap[slotInPage>>3] & (1 << (slotInPage & 0x7)))) {
            /* this entry is free */
            numDirChunks = 1;		/* only skip this guy */
            goto nextEntry;
        }

        tp = bufferp->datap + entryInBuffer;
        dep = (cm_dirEntry_t *) tp;		/* now points to AFS3 dir entry */

        /* while we're here, compute the next entry's location, too,
         * since we'll need it when writing out the cookie into the dir
         * listing stream.
         *
         * XXXX Probably should do more sanity checking.
         */
        numDirChunks = cm_NameEntries(dep->name, NULL);

        /* compute the offset of the cookie representing the next entry */
        nextEntryCookie = curOffset.LowPart + (CM_DIR_CHUNKSIZE * numDirChunks);

        /* Compute 8.3 name if necessary */
        actualName = dep->name;
        if (dep->fid.vnode != 0 && !cm_Is8Dot3(actualName)) {
            cm_Gen8Dot3Name(dep, shortName, &shortNameEnd);
            actualName = shortName;
        }

        osi_Log3(smb_logp, "SMB search dir vn %d name %s (%s)",
                  dep->fid.vnode, osi_LogSaveString(smb_logp, dep->name),
                  osi_LogSaveString(smb_logp, actualName));

        if (dep->fid.vnode != 0 && smb_Match8Dot3Mask(actualName, mask)) {
            /* this is one of the entries to use: it is not deleted
             * and it matches the star pattern we're looking for.
             */

            /* Eliminate entries that don't match requested
             * attributes */

            /* no hidden files */
            if (smb_hideDotFiles && !(dsp->attribute & SMB_ATTR_HIDDEN) && smb_IsDotFile(actualName)) {
                osi_Log0(smb_logp, "SMB search dir skipping hidden");
                goto nextEntry;
            }

            if (!(dsp->attribute & SMB_ATTR_DIRECTORY))  /* no directories */
            {
                /* We have already done the cm_TryBulkStat above */
                fid.cell = scp->fid.cell;
                fid.volume = scp->fid.volume;
                fid.vnode = ntohl(dep->fid.vnode);
                fid.unique = ntohl(dep->fid.unique);
                fileType = cm_FindFileType(&fid);
                osi_Log2(smb_logp, "smb_ReceiveCoreSearchDir: file %s "
                         "has filetype %d", osi_LogSaveString(smb_logp, dep->name),
                          fileType);
                if (fileType == CM_SCACHETYPE_DIRECTORY ||
                    fileType == CM_SCACHETYPE_DFSLINK ||
                    fileType == CM_SCACHETYPE_INVALID)
                    osi_Log0(smb_logp, "SMB search dir skipping directory or bad link");
                goto nextEntry;
            }

            *op++ = resByte;
            memcpy(op, mask, 11); op += 11;
            *op++ = (char) dsp->cookie;	/* they say it must be non-zero */
            *op++ = nextEntryCookie & 0xff;
            *op++ = (nextEntryCookie>>8) & 0xff;
            *op++ = (nextEntryCookie>>16) & 0xff;
            *op++ = (nextEntryCookie>>24) & 0xff;
            memcpy(op, &clientCookie, 4); op += 4;

            /* now we emit the attribute.  This is sort of tricky,
             * since we need to really stat the file to find out
             * what type of entry we've got.  Right now, we're
             * copying out data from a buffer, while holding the
             * scp locked, so it isn't really convenient to stat
             * something now.  We'll put in a place holder now,
             * and make a second pass before returning this to get
             * the real attributes.  So, we just skip the data for
             * now, and adjust it later.  We allocate a patch
             * record to make it easy to find this point later.
             * The replay will happen at a time when it is safe to
             * unlock the directory.
             */
            curPatchp = malloc(sizeof(*curPatchp));
            osi_QAdd((osi_queue_t **) &dirListPatchesp, &curPatchp->q);
            curPatchp->dptr = op;
            curPatchp->fid.cell = scp->fid.cell;
            curPatchp->fid.volume = scp->fid.volume;
            curPatchp->fid.vnode = ntohl(dep->fid.vnode);
            curPatchp->fid.unique = ntohl(dep->fid.unique);

            /* do hidden attribute here since name won't be around when applying
             * dir list patches
             */

            if ( smb_hideDotFiles && smb_IsDotFile(actualName) )
                curPatchp->flags = SMB_DIRLISTPATCH_DOTFILE;
            else
                curPatchp->flags = 0;

            op += 9;	/* skip attr, time, date and size */

            /* zero out name area.  The spec says to pad with
             * spaces, but Samba doesn't, and neither do we.
             */
            memset(op, 0, 13);

            /* finally, we get to copy out the name; we know that
             * it fits in 8.3 or the pattern wouldn't match, but it
             * never hurts to be sure.
             */
            strncpy(op, actualName, 13);
            if (smb_StoreAnsiFilenames)
                CharToOem(op, op);

            /* Uppercase if requested by client */
            if (!KNOWS_LONG_NAMES(inp))
                _strupr(op);

            op += 13;

            /* now, adjust the # of entries copied */
            returnedNames++;
        }	/* if we're including this name */

      nextEntry:
        /* and adjust curOffset to be where the new cookie is */
        thyper.HighPart = 0;
        thyper.LowPart = CM_DIR_CHUNKSIZE * numDirChunks;
        curOffset = LargeIntegerAdd(thyper, curOffset);
    }		/* while copying data for dir listing */

    /* release the mutex */
    lock_ReleaseMutex(&scp->mx);
    if (bufferp) buf_Release(bufferp);

    /* apply and free last set of patches; if not doing a star match, this
     * will be empty, but better safe (and freeing everything) than sorry.
     */
    smb_ApplyDirListPatches(&dirListPatchesp, userp, &req);

    /* special return code for unsuccessful search */
    if (code == 0 && dataLength < 21 && returnedNames == 0)
        code = CM_ERROR_NOFILES;

    osi_Log2(smb_logp, "SMB search dir done, %d names, code %d",
             returnedNames, code);

    if (code != 0) {
        smb_DeleteDirSearch(dsp);
        smb_ReleaseDirSearch(dsp);
        cm_ReleaseSCache(scp);
        cm_ReleaseUser(userp);
        return code;
    }

    /* finalize the output buffer */
    smb_SetSMBParm(outp, 0, returnedNames);
    temp = (long) (op - origOp);
    smb_SetSMBDataLength(outp, temp);

    /* the data area is a variable block, which has a 5 (already there)
     * followed by the length of the # of data bytes.  We now know this to
     * be "temp," although that includes the 3 bytes of vbl block header.
     * Deduct for them and fill in the length field.
     */
    temp -= 3;		/* deduct vbl block info */
    osi_assert(temp == (43 * returnedNames));
    origOp[1] = temp & 0xff;
    origOp[2] = (temp>>8) & 0xff;
    if (returnedNames == 0) 
        smb_DeleteDirSearch(dsp);
    smb_ReleaseDirSearch(dsp);
    cm_ReleaseSCache(scp);
    cm_ReleaseUser(userp);
    return code;
}	

/* verify that this is a valid path to a directory.  I don't know why they
 * don't use the get file attributes call.
 */
long smb_ReceiveCoreCheckPath(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    char *pathp;
    long code = 0;
    cm_scache_t *rootScp;
    cm_scache_t *newScp;
    cm_user_t *userp;
    unsigned int attrs;
    int caseFold;
    char *tidPathp;
    cm_req_t req;

    cm_InitReq(&req);

    pathp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(pathp, NULL);
    if (!pathp)
        return CM_ERROR_BADFD;
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);
    osi_Log1(smb_logp, "SMB receive check path %s",
             osi_LogSaveString(smb_logp, pathp));
        
    rootScp = cm_data.rootSCachep;
        
    userp = smb_GetUser(vcp, inp);

    caseFold = CM_FLAG_CASEFOLD;

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHPATH;
    }
    code = cm_NameI(rootScp, pathp,
                    caseFold | CM_FLAG_FOLLOW | CM_FLAG_CHECKPATH,
                    userp, tidPathp, &req, &newScp);

    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
        
#ifdef DFS_SUPPORT
    if (newScp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(newScp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    /* now lock the vnode with a callback; returns with newScp locked */
    lock_ObtainMutex(&newScp->mx);
    code = cm_SyncOp(newScp, NULL, userp, &req, PRSFS_LOOKUP,
                     CM_SCACHESYNC_GETSTATUS | CM_SCACHESYNC_NEEDCALLBACK);
    if (code && code != CM_ERROR_NOACCESS) {
        lock_ReleaseMutex(&newScp->mx);
        cm_ReleaseSCache(newScp);
        cm_ReleaseUser(userp);
        return code;
    }

    attrs = smb_Attributes(newScp);

    if (!(attrs & SMB_ATTR_DIRECTORY))
        code = CM_ERROR_NOTDIR;

    lock_ReleaseMutex(&newScp->mx);

    cm_ReleaseSCache(newScp);
    cm_ReleaseUser(userp);
    return code;
}	

long smb_ReceiveCoreSetFileAttributes(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    char *pathp;
    long code = 0;
    cm_scache_t *rootScp;
    unsigned short attribute;
    cm_attr_t attr;
    cm_scache_t *newScp;
    afs_uint32 dosTime;
    cm_user_t *userp;
    int caseFold;
    char *tidPathp;
    cm_req_t req;

    cm_InitReq(&req);

    /* decode basic attributes we're passed */
    attribute = smb_GetSMBParm(inp, 0);
    dosTime = smb_GetSMBParm(inp, 1) | (smb_GetSMBParm(inp, 2) << 16);

    pathp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(pathp, NULL);
    if (!pathp)
        return CM_ERROR_BADSMB;
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);
               
    osi_Log2(smb_logp, "SMB receive setfile attributes time %d, attr 0x%x",
             dosTime, attribute);

    rootScp = cm_data.rootSCachep;
        
    userp = smb_GetUser(vcp, inp);

    caseFold = CM_FLAG_CASEFOLD;

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHFILE;
    }
    code = cm_NameI(rootScp, pathp, caseFold | CM_FLAG_FOLLOW, userp,
                    tidPathp, &req, &newScp);

    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
	
#ifdef DFS_SUPPORT
    if (newScp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(newScp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    /* now lock the vnode with a callback; returns with newScp locked; we
     * need the current status to determine what the new status is, in some
     * cases.
     */
    lock_ObtainMutex(&newScp->mx);
    code = cm_SyncOp(newScp, NULL, userp, &req, 0,
                     CM_SCACHESYNC_GETSTATUS | CM_SCACHESYNC_NEEDCALLBACK);
    if (code) {
        lock_ReleaseMutex(&newScp->mx);
        cm_ReleaseSCache(newScp);
        cm_ReleaseUser(userp);
        return code;
    }

    /* Check for RO volume */
    if (newScp->flags & CM_SCACHEFLAG_RO) {
        lock_ReleaseMutex(&newScp->mx);
        cm_ReleaseSCache(newScp);
        cm_ReleaseUser(userp);
        return CM_ERROR_READONLY;
    }

    /* prepare for setattr call */
    attr.mask = 0;
    if (dosTime != 0) {
        attr.mask |= CM_ATTRMASK_CLIENTMODTIME;
        smb_UnixTimeFromDosUTime(&attr.clientModTime, dosTime);
    }
    if ((newScp->unixModeBits & 0222) && (attribute & 1) != 0) {
        /* we're told to make a writable file read-only */
        attr.unixModeBits = newScp->unixModeBits & ~0222;
        attr.mask |= CM_ATTRMASK_UNIXMODEBITS;
    }
    else if ((newScp->unixModeBits & 0222) == 0 && (attribute & 1) == 0) {
        /* we're told to make a read-only file writable */
        attr.unixModeBits = newScp->unixModeBits | 0222;
        attr.mask |= CM_ATTRMASK_UNIXMODEBITS;
    }
    lock_ReleaseMutex(&newScp->mx);

    /* now call setattr */
    if (attr.mask)
        code = cm_SetAttr(newScp, &attr, userp, &req);
    else
        code = 0;
        
    cm_ReleaseSCache(newScp);
    cm_ReleaseUser(userp);

    return code;
}

long smb_ReceiveCoreGetFileAttributes(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    char *pathp;
    long code = 0;
    cm_scache_t *rootScp;
    cm_scache_t *newScp, *dscp;
    afs_uint32 dosTime;
    int attrs;
    cm_user_t *userp;
    int caseFold;
    char *tidPathp;
    cm_space_t *spacep;
    char *lastComp;
    cm_req_t req;

    cm_InitReq(&req);

    pathp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(pathp, NULL);
    if (!pathp)
        return CM_ERROR_BADSMB;
        
    if (*pathp == 0)		/* null path */
        pathp = "\\";
    else
        if (smb_StoreAnsiFilenames)
            OemToChar(pathp,pathp);

    osi_Log1(smb_logp, "SMB receive getfile attributes path %s",
             osi_LogSaveString(smb_logp, pathp));

    rootScp = cm_data.rootSCachep;
        
    userp = smb_GetUser(vcp, inp);

    /* we shouldn't need this for V3 requests, but we seem to */
    caseFold = CM_FLAG_CASEFOLD;

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHFILE;
    }

    /*
     * XXX Strange hack XXX
     *
     * As of Patch 5 (16 July 97), we are having the following problem:
     * In NT Explorer 4.0, whenever we click on a directory, AFS gets
     * requests to look up "desktop.ini" in all the subdirectories.
     * This can cause zillions of timeouts looking up non-existent cells
     * and volumes, especially in the top-level directory.
     *
     * We have not found any way to avoid this or work around it except
     * to explicitly ignore the requests for mount points that haven't
     * yet been evaluated and for directories that haven't yet been
     * fetched.
     *
     * We should modify this hack to provide a fake desktop.ini file
     * http://msdn.microsoft.com/library/en-us/shellcc/platform/shell/programmersguide/shell_basics/shell_basics_extending/custom.asp
     */
    spacep = inp->spacep;
    smb_StripLastComponent(spacep->data, &lastComp, pathp);
#ifndef SPECIAL_FOLDERS
    if (lastComp && stricmp(lastComp, "\\desktop.ini") == 0) {
        code = cm_NameI(rootScp, spacep->data,
                        caseFold | CM_FLAG_DIRSEARCH | CM_FLAG_FOLLOW,
                        userp, tidPathp, &req, &dscp);
        if (code == 0) {
#ifdef DFS_SUPPORT
            if (dscp->fileType == CM_SCACHETYPE_DFSLINK) {
                if ( WANTS_DFS_PATHNAMES(inp) )
                    return CM_ERROR_PATH_NOT_COVERED;
                else
                    return CM_ERROR_BADSHARENAME;
            } else
#endif /* DFS_SUPPORT */
            if (dscp->fileType == CM_SCACHETYPE_MOUNTPOINT && !dscp->mountRootFid.volume)
                code = CM_ERROR_NOSUCHFILE;
            else if (dscp->fileType == CM_SCACHETYPE_DIRECTORY) {
                cm_buf_t *bp = buf_Find(dscp, &hzero);
                if (bp)
                    buf_Release(bp);
                else
                    code = CM_ERROR_NOSUCHFILE;
            }
            cm_ReleaseSCache(dscp);
            if (code) {
                cm_ReleaseUser(userp);
                return code;
            }
        }
    }
#endif /* SPECIAL_FOLDERS */

    code = cm_NameI(rootScp, pathp, caseFold | CM_FLAG_FOLLOW, userp,
                    tidPathp, &req, &newScp);
    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
        
#ifdef DFS_SUPPORT
    if (newScp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(newScp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    /* now lock the vnode with a callback; returns with newScp locked */
    lock_ObtainMutex(&newScp->mx);
    code = cm_SyncOp(newScp, NULL, userp, &req, 0,
                     CM_SCACHESYNC_GETSTATUS | CM_SCACHESYNC_NEEDCALLBACK);
    if (code) {
        lock_ReleaseMutex(&newScp->mx);
        cm_ReleaseSCache(newScp);
        cm_ReleaseUser(userp);
        return code;
    }

#ifdef undef
    /* use smb_Attributes instead.   Also the fact that a file is 
     * in a readonly volume doesn't mean it shojuld be marked as RO 
     */
    if (newScp->fileType == CM_SCACHETYPE_DIRECTORY ||
        newScp->fileType == CM_SCACHETYPE_MOUNTPOINT)
        attrs = SMB_ATTR_DIRECTORY;
    else
        attrs = 0;
    if ((newScp->unixModeBits & 0222) == 0 || (newScp->flags & CM_SCACHEFLAG_RO))
        attrs |= SMB_ATTR_READONLY;	/* turn on read-only flag */
#else
    attrs = smb_Attributes(newScp);
#endif

    smb_SetSMBParm(outp, 0, attrs);
        
    smb_DosUTimeFromUnixTime(&dosTime, newScp->clientModTime);
    smb_SetSMBParm(outp, 1, (unsigned int)(dosTime & 0xffff));
    smb_SetSMBParm(outp, 2, (unsigned int)((dosTime>>16) & 0xffff));
    smb_SetSMBParm(outp, 3, newScp->length.LowPart & 0xffff);
    smb_SetSMBParm(outp, 4, (newScp->length.LowPart >> 16) & 0xffff);
    smb_SetSMBParm(outp, 5, 0);
    smb_SetSMBParm(outp, 6, 0);
    smb_SetSMBParm(outp, 7, 0);
    smb_SetSMBParm(outp, 8, 0);
    smb_SetSMBParm(outp, 9, 0);
    smb_SetSMBDataLength(outp, 0);
    lock_ReleaseMutex(&newScp->mx);

    cm_ReleaseSCache(newScp);
    cm_ReleaseUser(userp);

    return 0;
}	

long smb_ReceiveCoreTreeDisconnect(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    smb_tid_t *tidp;
        
    osi_Log0(smb_logp, "SMB receive tree disconnect");

    /* find the tree and free it */
    tidp = smb_FindTID(vcp, ((smb_t *)inp)->tid, 0);
    if (tidp) {
        lock_ObtainMutex(&tidp->mx);
        tidp->flags |= SMB_TIDFLAG_DELETE;
        lock_ReleaseMutex(&tidp->mx);
        smb_ReleaseTID(tidp);
    }

    return 0;
}

long smb_ReceiveCoreOpen(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    smb_fid_t *fidp;
    char *pathp;
    char *lastNamep;
    int share;
    int attribute;
    long code = 0;
    cm_user_t *userp;
    cm_scache_t *scp;
    afs_uint32 dosTime;
    int caseFold;
    cm_space_t *spacep;
    char *tidPathp;
    cm_req_t req;

    cm_InitReq(&req);

    pathp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(pathp, NULL);
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);
	
    osi_Log1(smb_logp, "SMB receive open file [%s]", osi_LogSaveString(smb_logp, pathp));

#ifdef DEBUG_VERBOSE
    {
        char *hexpath;

        hexpath = osi_HexifyString( pathp );
        DEBUG_EVENT2("AFS", "CoreOpen H[%s] A[%s]", hexpath, pathp);
        free(hexpath);
    }
#endif

    share = smb_GetSMBParm(inp, 0);
    attribute = smb_GetSMBParm(inp, 1);

    spacep = inp->spacep;
    smb_StripLastComponent(spacep->data, &lastNamep, pathp);
    if (lastNamep && strcmp(lastNamep, SMB_IOCTL_FILENAME) == 0) {
        /* special case magic file name for receiving IOCTL requests
         * (since IOCTL calls themselves aren't getting through).
         */
        fidp = smb_FindFID(vcp, 0, SMB_FLAG_CREATE);
        smb_SetupIoctlFid(fidp, spacep);
        smb_SetSMBParm(outp, 0, fidp->fid);
        smb_SetSMBParm(outp, 1, 0);	/* attrs */
        smb_SetSMBParm(outp, 2, 0);	/* next 2 are DOS time */
        smb_SetSMBParm(outp, 3, 0);
        smb_SetSMBParm(outp, 4, 0);	/* next 2 are length */
        smb_SetSMBParm(outp, 5, 0x7fff);
        /* pass the open mode back */
        smb_SetSMBParm(outp, 6, (share & 0xf));
        smb_SetSMBDataLength(outp, 0);
        smb_ReleaseFID(fidp);
        return 0;
    }

    userp = smb_GetUser(vcp, inp);

    caseFold = CM_FLAG_CASEFOLD;

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHPATH;
    }
    code = cm_NameI(cm_data.rootSCachep, pathp, caseFold | CM_FLAG_FOLLOW, userp,
                    tidPathp, &req, &scp);
        
    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }

#ifdef DFS_SUPPORT
    if (scp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(scp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    code = cm_CheckOpen(scp, share & 0x7, 0, userp, &req);
    if (code) {
        cm_ReleaseSCache(scp);
        cm_ReleaseUser(userp);
        return code;
    }

    /* don't need callback to check file type, since file types never
     * change, and namei and cm_Lookup all stat the object at least once on
     * a successful return.
     */
    if (scp->fileType != CM_SCACHETYPE_FILE) {
        cm_ReleaseSCache(scp);
        cm_ReleaseUser(userp);
        return CM_ERROR_ISDIR;
    }

    fidp = smb_FindFID(vcp, 0, SMB_FLAG_CREATE);
    osi_assert(fidp);

    /* save a pointer to the vnode */
    fidp->scp = scp;

    if ((share & 0xf) == 0)
        fidp->flags |= SMB_FID_OPENREAD;
    else if ((share & 0xf) == 1)
        fidp->flags |= SMB_FID_OPENWRITE;
    else 
        fidp->flags |= (SMB_FID_OPENREAD | SMB_FID_OPENWRITE);

    lock_ObtainMutex(&scp->mx);
    smb_SetSMBParm(outp, 0, fidp->fid);
    smb_SetSMBParm(outp, 1, smb_Attributes(scp));
    smb_DosUTimeFromUnixTime(&dosTime, scp->clientModTime);
    smb_SetSMBParm(outp, 2, (unsigned int)(dosTime & 0xffff));
    smb_SetSMBParm(outp, 3, (unsigned int)((dosTime >> 16) & 0xffff));
    smb_SetSMBParm(outp, 4, scp->length.LowPart & 0xffff);
    smb_SetSMBParm(outp, 5, (scp->length.LowPart >> 16) & 0xffff);
    /* pass the open mode back; XXXX add access checks */
    smb_SetSMBParm(outp, 6, (share & 0xf));
    smb_SetSMBDataLength(outp, 0);
    lock_ReleaseMutex(&scp->mx);
        
    /* notify open */
    cm_Open(scp, 0, userp);

    /* send and free packet */
    smb_ReleaseFID(fidp);
    cm_ReleaseUser(userp);
    /* don't release scp, since we've squirreled away the pointer in the fid struct */
    return 0;
}

typedef struct smb_unlinkRock {
    cm_scache_t *dscp;
    cm_user_t *userp;
    cm_req_t *reqp;
    smb_vc_t *vcp;
    char *maskp;		/* pointer to the star pattern */
    int flags;
    int any;
} smb_unlinkRock_t;

int smb_UnlinkProc(cm_scache_t *dscp, cm_dirEntry_t *dep, void *vrockp, osi_hyper_t *offp)
{
    long code = 0;
    smb_unlinkRock_t *rockp;
    int caseFold;
    int match;
    char shortName[13];
    char *matchName;
        
    rockp = vrockp;

    caseFold = ((rockp->flags & SMB_MASKFLAG_CASEFOLD)? CM_FLAG_CASEFOLD : 0);
    if (!(rockp->vcp->flags & SMB_VCFLAG_USEV3))
        caseFold |= CM_FLAG_8DOT3;

    matchName = dep->name;
    match = smb_V3MatchMask(matchName, rockp->maskp, caseFold);
    if (!match &&
         (rockp->flags & SMB_MASKFLAG_TILDE) &&
         !cm_Is8Dot3(dep->name)) {
        cm_Gen8Dot3Name(dep, shortName, NULL);
        matchName = shortName;
        /* 8.3 matches are always case insensitive */
        match = smb_V3MatchMask(matchName, rockp->maskp, caseFold | CM_FLAG_CASEFOLD);
    }
    if (match) {
        osi_Log1(smb_logp, "Unlinking %s",
                 osi_LogSaveString(smb_logp, matchName));
        code = cm_Unlink(dscp, dep->name, rockp->userp, rockp->reqp);
        if (code == 0 && (dscp->flags & CM_SCACHEFLAG_ANYWATCH))
            smb_NotifyChange(FILE_ACTION_REMOVED,
                             FILE_NOTIFY_CHANGE_FILE_NAME,
                             dscp, dep->name, NULL, TRUE);
        if (code == 0) {
            rockp->any = 1;

            /* If we made a case sensitive exact match, we might as well quit now. */
            if (!(rockp->flags & SMB_MASKFLAG_CASEFOLD) && !strcmp(matchName, rockp->maskp))
                code = CM_ERROR_STOPNOW;
        }
    }
    else code = 0;

    return code;
}

long smb_ReceiveCoreUnlink(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    int attribute;
    long code = 0;
    char *pathp;
    char *tp;
    cm_space_t *spacep;
    cm_scache_t *dscp;
    char *lastNamep;
    smb_unlinkRock_t rock;
    cm_user_t *userp;
    osi_hyper_t thyper;
    int caseFold;
    char *tidPathp;
    cm_req_t req;

    cm_InitReq(&req);

    attribute = smb_GetSMBParm(inp, 0);
        
    tp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(tp, &tp);
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);

    osi_Log1(smb_logp, "SMB receive unlink %s",
             osi_LogSaveString(smb_logp, pathp));

    spacep = inp->spacep;
    smb_StripLastComponent(spacep->data, &lastNamep, pathp);

    userp = smb_GetUser(vcp, inp);

    caseFold = CM_FLAG_FOLLOW | CM_FLAG_CASEFOLD;

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHPATH;
    }
    code = cm_NameI(cm_data.rootSCachep, spacep->data, caseFold, userp, tidPathp,
                    &req, &dscp);
    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
        
#ifdef DFS_SUPPORT
    if (dscp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(dscp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    /* otherwise, scp points to the parent directory. */
    if (!lastNamep) 
        lastNamep = pathp;
    else 
        lastNamep++;

    rock.any = 0;
    rock.maskp = smb_FindMask(pathp);
    rock.flags = ((strchr(rock.maskp, '~') != NULL) ? SMB_MASKFLAG_TILDE : 0);

    thyper.LowPart = 0;
    thyper.HighPart = 0;
    rock.userp = userp;
    rock.reqp = &req;
    rock.dscp = dscp;
    rock.vcp = vcp;

    /* Now, if we aren't dealing with a wildcard match, we first try an exact 
     * match.  If that fails, we do a case insensitve match. 
     */
    if (!(rock.flags & SMB_MASKFLAG_TILDE) &&
        !smb_IsStarMask(rock.maskp)) {
        code = cm_ApplyDir(dscp, smb_UnlinkProc, &rock, &thyper, userp, &req, NULL);
        if (!rock.any) {
            thyper.LowPart = 0;
            thyper.HighPart = 0;
            rock.flags |= SMB_MASKFLAG_CASEFOLD;
        }
    }
 
    if (!rock.any)
        code = cm_ApplyDir(dscp, smb_UnlinkProc, &rock, &thyper, userp, &req, NULL);
    
    if (code == CM_ERROR_STOPNOW) 
        code = 0;

    cm_ReleaseUser(userp);
        
    cm_ReleaseSCache(dscp);

    if (code == 0 && !rock.any)
        code = CM_ERROR_NOSUCHFILE;
    return code;
}       

typedef struct smb_renameRock {
    cm_scache_t *odscp;	/* old dir */
    cm_scache_t *ndscp;	/* new dir */
    cm_user_t *userp;	/* user */
    cm_req_t *reqp;		/* request struct */
    smb_vc_t *vcp;		/* virtual circuit */
    char *maskp;		/* pointer to star pattern of old file name */
    int flags;		    /* tilde, casefold, etc */
    char *newNamep;		/* ptr to the new file's name */
} smb_renameRock_t;

int smb_RenameProc(cm_scache_t *dscp, cm_dirEntry_t *dep, void *vrockp, osi_hyper_t *offp)
{
    long code = 0;
    smb_renameRock_t *rockp;
    int caseFold;
    int match;
    char shortName[13];

    rockp = (smb_renameRock_t *) vrockp;

    caseFold = ((rockp->flags & SMB_MASKFLAG_CASEFOLD)? CM_FLAG_CASEFOLD : 0);
    if (!(rockp->vcp->flags & SMB_VCFLAG_USEV3))
        caseFold |= CM_FLAG_8DOT3;

    match = smb_V3MatchMask(dep->name, rockp->maskp, caseFold);
    if (!match &&
        (rockp->flags & SMB_MASKFLAG_TILDE) &&
         !cm_Is8Dot3(dep->name)) {
        cm_Gen8Dot3Name(dep, shortName, NULL);
        match = smb_V3MatchMask(shortName, rockp->maskp, caseFold);
    }
    if (match) {
        code = cm_Rename(rockp->odscp, dep->name,
                         rockp->ndscp, rockp->newNamep, rockp->userp,
                         rockp->reqp);	
        /* if the call worked, stop doing the search now, since we
         * really only want to rename one file.
         */
        if (code == 0) 
            code = CM_ERROR_STOPNOW;
    }       
    else code = 0;

    return code;
}


long 
smb_Rename(smb_vc_t *vcp, smb_packet_t *inp, char * oldPathp, char * newPathp, int attrs)
{
    long code = 0;
    cm_space_t *spacep = NULL;
    smb_renameRock_t rock;
    cm_scache_t *oldDscp = NULL;
    cm_scache_t *newDscp = NULL;
    cm_scache_t *tmpscp= NULL;
    cm_scache_t *tmpscp2 = NULL;
    char *oldLastNamep;
    char *newLastNamep;
    osi_hyper_t thyper;
    cm_user_t *userp;
    int caseFold;
    char *tidPathp;
    DWORD filter;
    cm_req_t req;

    userp = smb_GetUser(vcp, inp);
    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHPATH;
    }

    cm_InitReq(&req);
    spacep = inp->spacep;
    smb_StripLastComponent(spacep->data, &oldLastNamep, oldPathp);

    /*
     * Changed to use CASEFOLD always.  This enables us to rename Foo/baz when
     * what actually exists is foo/baz.  I don't know why the code used to be
     * the way it was.  1/29/96
     *
     *     	caseFold = ((vcp->flags & SMB_VCFLAG_USEV3) ? 0: CM_FLAG_CASEFOLD);
     *
     * Changed to use CM_FLAG_FOLLOW.  7/24/96
     *
     *	caseFold = CM_FLAG_CASEFOLD;
     */
    caseFold = CM_FLAG_FOLLOW | CM_FLAG_CASEFOLD;
    code = cm_NameI(cm_data.rootSCachep, spacep->data, caseFold,
                    userp, tidPathp, &req, &oldDscp);
    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
        
#ifdef DFS_SUPPORT
    if (oldDscp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    smb_StripLastComponent(spacep->data, &newLastNamep, newPathp);
    code = cm_NameI(cm_data.rootSCachep, spacep->data, caseFold,
                    userp, tidPathp, &req, &newDscp);

    if (code) {
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseUser(userp);
        return code;
    }

#ifdef DFS_SUPPORT
    if (newDscp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseSCache(newDscp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */


    /* otherwise, oldDscp and newDscp point to the corresponding directories.
     * next, get the component names, and lower case them.
     */

    /* handle the old name first */
    if (!oldLastNamep) 
        oldLastNamep = oldPathp;
    else 
        oldLastNamep++;

    /* and handle the new name, too */
    if (!newLastNamep) 
        newLastNamep = newPathp;
    else 
        newLastNamep++;

    /* TODO: The old name could be a wildcard.  The new name must not be */

    /* do the vnode call */
    rock.odscp = oldDscp;
    rock.ndscp = newDscp;
    rock.userp = userp;
    rock.reqp = &req;
    rock.vcp = vcp;
    rock.maskp = oldLastNamep;
    rock.flags = ((strchr(oldLastNamep, '~') != NULL) ? SMB_MASKFLAG_TILDE : 0);
    rock.newNamep = newLastNamep;

    /* Check if the file already exists; if so return error */
    code = cm_Lookup(newDscp,newLastNamep,CM_FLAG_CHECKPATH,userp,&req,&tmpscp);
    if ((code != CM_ERROR_NOSUCHFILE) && (code != CM_ERROR_NOSUCHPATH) && (code != CM_ERROR_NOSUCHVOLUME) ) {
        osi_Log2(smb_logp, "  lookup returns %ld for [%s]", code,
                 osi_LogSaveString(afsd_logp, newLastNamep));

        /* Check if the old and the new names differ only in case. If so return
         * success, else return CM_ERROR_EXISTS 
         */
        if (!code && oldDscp == newDscp && !stricmp(oldLastNamep, newLastNamep)) {

            /* This would be a success only if the old file is *as same as* the new file */
            code = cm_Lookup(oldDscp, oldLastNamep, CM_FLAG_CHECKPATH, userp, &req, &tmpscp2);
            if (!code) {
                if (tmpscp == tmpscp2) 
                    code = 0;
                else 
                    code = CM_ERROR_EXISTS;
                cm_ReleaseSCache(tmpscp2);
                tmpscp2 = NULL;
            } else {
                code = CM_ERROR_NOSUCHFILE;
            }
        } else {
            /* file exist, do not rename, also fixes move */
            osi_Log0(smb_logp, "Can't rename.  Target already exists");
            code = CM_ERROR_EXISTS;
        }

        if (tmpscp != NULL)
            cm_ReleaseSCache(tmpscp);
        cm_ReleaseSCache(newDscp);
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseUser(userp);
        return code; 
    }

    /* Now search the directory for the pattern, and do the appropriate rename when found */
    thyper.LowPart = 0;		/* search dir from here */
    thyper.HighPart = 0;

    code = cm_ApplyDir(oldDscp, smb_RenameProc, &rock, &thyper, userp, &req, NULL);

    if (code == CM_ERROR_STOPNOW)
        code = 0;
    else if (code == 0)
        code = CM_ERROR_NOSUCHFILE;

    /* Handle Change Notification */
    /*
    * Being lazy, not distinguishing between files and dirs in this
    * filter, since we'd have to do a lookup.
    */
    filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME;
    if (oldDscp == newDscp) {
        if (oldDscp->flags & CM_SCACHEFLAG_ANYWATCH)
            smb_NotifyChange(FILE_ACTION_RENAMED_OLD_NAME,
                             filter, oldDscp, oldLastNamep,
                             newLastNamep, TRUE);
    } else {
        if (oldDscp->flags & CM_SCACHEFLAG_ANYWATCH)
            smb_NotifyChange(FILE_ACTION_RENAMED_OLD_NAME,
                             filter, oldDscp, oldLastNamep,
                             NULL, TRUE);
        if (newDscp->flags & CM_SCACHEFLAG_ANYWATCH)
            smb_NotifyChange(FILE_ACTION_RENAMED_NEW_NAME,
                             filter, newDscp, newLastNamep,
                             NULL, TRUE);
    }

    if (tmpscp != NULL) 
        cm_ReleaseSCache(tmpscp);
    cm_ReleaseUser(userp);
    cm_ReleaseSCache(oldDscp);
    cm_ReleaseSCache(newDscp);
    return code;
}       

long 
smb_Link(smb_vc_t *vcp, smb_packet_t *inp, char * oldPathp, char * newPathp) 
{
    long code = 0;
    cm_space_t *spacep = NULL;
    cm_scache_t *oldDscp = NULL;
    cm_scache_t *newDscp = NULL;
    cm_scache_t *tmpscp= NULL;
    cm_scache_t *tmpscp2 = NULL;
    cm_scache_t *sscp = NULL;
    char *oldLastNamep;
    char *newLastNamep;
    cm_user_t *userp;
    int caseFold;
    char *tidPathp;
    DWORD filter;
    cm_req_t req;

    userp = smb_GetUser(vcp, inp);

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHPATH;
    }

    cm_InitReq(&req);

    caseFold = CM_FLAG_FOLLOW | CM_FLAG_CASEFOLD;

    spacep = inp->spacep;
    smb_StripLastComponent(spacep->data, &oldLastNamep, oldPathp);
    
    code = cm_NameI(cm_data.rootSCachep, spacep->data, caseFold,
                    userp, tidPathp, &req, &oldDscp);
    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
        
#ifdef DFS_SUPPORT
    if (oldDscp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    smb_StripLastComponent(spacep->data, &newLastNamep, newPathp);
    code = cm_NameI(cm_data.rootSCachep, spacep->data, caseFold,
                    userp, tidPathp, &req, &newDscp);
    if (code) {
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseUser(userp);
        return code;
    }

#ifdef DFS_SUPPORT
    if (newDscp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(newDscp);
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    /* Now, although we did two lookups for the two directories (because the same
     * directory can be referenced through different paths), we only allow hard links
     * within the same directory. */
    if (oldDscp != newDscp) {
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseSCache(newDscp);
        cm_ReleaseUser(userp);
        return CM_ERROR_CROSSDEVLINK;
    }

    /* handle the old name first */
    if (!oldLastNamep) 
        oldLastNamep = oldPathp;
    else 
        oldLastNamep++;

    /* and handle the new name, too */
    if (!newLastNamep) 
        newLastNamep = newPathp;
    else 
        newLastNamep++;

    /* now lookup the old name */
    osi_Log1(smb_logp,"  looking up [%s]", osi_LogSaveString(smb_logp,oldLastNamep));
    code = cm_Lookup(oldDscp, oldLastNamep, CM_FLAG_CHECKPATH | CM_FLAG_CASEFOLD, userp, &req, &sscp);
    if (code) {
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseSCache(newDscp);
        cm_ReleaseUser(userp);
        return code;
    }

    /* Check if the file already exists; if so return error */
    code = cm_Lookup(newDscp,newLastNamep,CM_FLAG_CHECKPATH,userp,&req,&tmpscp);
    if ((code != CM_ERROR_NOSUCHFILE) && (code != CM_ERROR_NOSUCHPATH) && (code != CM_ERROR_NOSUCHVOLUME) ) {
        osi_Log2(smb_logp, "  lookup returns %ld for [%s]", code,
                 osi_LogSaveString(afsd_logp, newLastNamep));

        /* if the existing link is to the same file, then we return success */
        if (!code) {
            if(sscp == tmpscp) {
                code = 0;
            } else {
                osi_Log0(smb_logp, "Can't create hardlink.  Target already exists");
                code = CM_ERROR_EXISTS;
            }
        }

        if (tmpscp != NULL)
            cm_ReleaseSCache(tmpscp);
        cm_ReleaseSCache(sscp);
        cm_ReleaseSCache(newDscp);
        cm_ReleaseSCache(oldDscp);
        cm_ReleaseUser(userp);
        return code; 
    }

    /* now create the hardlink */
    osi_Log1(smb_logp,"  Attempting to create new link [%s]", osi_LogSaveString(smb_logp, newLastNamep));
    code = cm_Link(newDscp, newLastNamep, sscp, 0, userp, &req);
    osi_Log1(smb_logp,"  Link returns %d", code);

    /* Handle Change Notification */
    if (code == 0) {
        filter = (sscp->fileType == CM_SCACHETYPE_FILE)? FILE_NOTIFY_CHANGE_FILE_NAME : FILE_NOTIFY_CHANGE_DIR_NAME;
        if (newDscp->flags & CM_SCACHEFLAG_ANYWATCH)
            smb_NotifyChange(FILE_ACTION_ADDED,
                             filter, newDscp, newLastNamep,
                             NULL, TRUE);
    }

    if (tmpscp != NULL) 
        cm_ReleaseSCache(tmpscp);
    cm_ReleaseUser(userp);
    cm_ReleaseSCache(sscp);
    cm_ReleaseSCache(oldDscp);
    cm_ReleaseSCache(newDscp);
    return code;
}

long 
smb_ReceiveCoreRename(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    char *oldPathp;
    char *newPathp;
    char *tp;

    tp = smb_GetSMBData(inp, NULL);
    oldPathp = smb_ParseASCIIBlock(tp, &tp);
    if (smb_StoreAnsiFilenames)
        OemToChar(oldPathp,oldPathp);
    newPathp = smb_ParseASCIIBlock(tp, &tp);
    if (smb_StoreAnsiFilenames)
        OemToChar(newPathp,newPathp);

    osi_Log2(smb_logp, "smb rename [%s] to [%s]",
              osi_LogSaveString(smb_logp, oldPathp),
              osi_LogSaveString(smb_logp, newPathp));

    return smb_Rename(vcp,inp,oldPathp,newPathp,0);
}



typedef struct smb_rmdirRock {
    cm_scache_t *dscp;
    cm_user_t *userp;
    cm_req_t *reqp;
    char *maskp;		/* pointer to the star pattern */
    int flags;
    int any;
} smb_rmdirRock_t;

int smb_RmdirProc(cm_scache_t *dscp, cm_dirEntry_t *dep, void *vrockp, osi_hyper_t *offp)
{       
    long code = 0;
    smb_rmdirRock_t *rockp;
    int match;
    char shortName[13];
    char *matchName;
        
    rockp = (smb_rmdirRock_t *) vrockp;

    matchName = dep->name;
    if (rockp->flags & SMB_MASKFLAG_CASEFOLD)
        match = (cm_stricmp(matchName, rockp->maskp) == 0);
    else
        match = (strcmp(matchName, rockp->maskp) == 0);
    if (!match &&
         (rockp->flags & SMB_MASKFLAG_TILDE) &&
         !cm_Is8Dot3(dep->name)) {
        cm_Gen8Dot3Name(dep, shortName, NULL);
        matchName = shortName;
        match = (cm_stricmp(matchName, rockp->maskp) == 0);
    }       
    if (match) {
        osi_Log1(smb_logp, "Removing directory %s",
                 osi_LogSaveString(smb_logp, matchName));
        code = cm_RemoveDir(dscp, dep->name, rockp->userp, rockp->reqp);
        if (code == 0 && (dscp->flags & CM_SCACHEFLAG_ANYWATCH))
            smb_NotifyChange(FILE_ACTION_REMOVED,
                             FILE_NOTIFY_CHANGE_DIR_NAME,
                             dscp, dep->name, NULL, TRUE);
        if (code == 0)
            rockp->any = 1;
    }
    else code = 0;

    return code;
}

long smb_ReceiveCoreRemoveDir(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    long code = 0;
    char *pathp;
    char *tp;
    cm_space_t *spacep;
    cm_scache_t *dscp;
    char *lastNamep;
    smb_rmdirRock_t rock;
    cm_user_t *userp;
    osi_hyper_t thyper;
    int caseFold;
    char *tidPathp;
    cm_req_t req;

    cm_InitReq(&req);

    tp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(tp, &tp);
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);

    spacep = inp->spacep;
    smb_StripLastComponent(spacep->data, &lastNamep, pathp);

    userp = smb_GetUser(vcp, inp);

    caseFold = CM_FLAG_CASEFOLD;

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHPATH;
    }
    code = cm_NameI(cm_data.rootSCachep, spacep->data, caseFold | CM_FLAG_FOLLOW,
                    userp, tidPathp, &req, &dscp);

    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
        
#ifdef DFS_SUPPORT
    if (dscp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(dscp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    /* otherwise, scp points to the parent directory. */
    if (!lastNamep) 
        lastNamep = pathp;
    else 
        lastNamep++;
	
    rock.any = 0;
    rock.maskp = lastNamep;
    rock.flags = ((strchr(rock.maskp, '~') != NULL) ? SMB_MASKFLAG_TILDE : 0);

    thyper.LowPart = 0;
    thyper.HighPart = 0;
    rock.userp = userp;
    rock.reqp = &req;
    rock.dscp = dscp;
    /* First do a case sensitive match, and if that fails, do a case insensitive match */
    code = cm_ApplyDir(dscp, smb_RmdirProc, &rock, &thyper, userp, &req, NULL);
    if (code == 0 && !rock.any) {
        thyper.LowPart = 0;
        thyper.HighPart = 0;
        rock.flags |= SMB_MASKFLAG_CASEFOLD;
        code = cm_ApplyDir(dscp, smb_RmdirProc, &rock, &thyper, userp, &req, NULL);
    }

    cm_ReleaseUser(userp);
        
    cm_ReleaseSCache(dscp);

    if (code == 0 && !rock.any)
        code = CM_ERROR_NOSUCHFILE;        
    return code;
}

long smb_ReceiveCoreFlush(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    unsigned short fid;
    smb_fid_t *fidp;
    cm_user_t *userp;
    long code = 0;
    cm_req_t req;

    cm_InitReq(&req);

    fid = smb_GetSMBParm(inp, 0);

    osi_Log1(smb_logp, "SMB flush fid %d", fid);

    fid = smb_ChainFID(fid, inp);
    fidp = smb_FindFID(vcp, fid, 0);
    if (!fidp || (fidp->flags & SMB_FID_IOCTL)) {
        if (fidp)
            smb_ReleaseFID(fidp);
        return CM_ERROR_BADFD;
    }
        
    userp = smb_GetUser(vcp, inp);

    lock_ObtainMutex(&fidp->mx);
    if (fidp->flags & SMB_FID_OPENWRITE)
        code = cm_FSync(fidp->scp, userp, &req);
    else 
        code = 0;
    lock_ReleaseMutex(&fidp->mx);
        
    smb_ReleaseFID(fidp);
        
    cm_ReleaseUser(userp);
        
    return code;
}

struct smb_FullNameRock {
    char *name;
    cm_scache_t *vnode;
    char *fullName;
};

int smb_FullNameProc(cm_scache_t *scp, cm_dirEntry_t *dep, void *rockp,
                     osi_hyper_t *offp)
{
    char shortName[13];
    struct smb_FullNameRock *vrockp;

    vrockp = (struct smb_FullNameRock *)rockp;

    if (!cm_Is8Dot3(dep->name)) {
        cm_Gen8Dot3Name(dep, shortName, NULL);

        if (cm_stricmp(shortName, vrockp->name) == 0) {
            vrockp->fullName = strdup(dep->name);
            return CM_ERROR_STOPNOW;
        }
    }
    if (cm_stricmp(dep->name, vrockp->name) == 0 &&
        ntohl(dep->fid.vnode) == vrockp->vnode->fid.vnode &&
        ntohl(dep->fid.unique) == vrockp->vnode->fid.unique) {
        vrockp->fullName = strdup(dep->name);
        return CM_ERROR_STOPNOW;
    }
    return 0;
}

void smb_FullName(cm_scache_t *dscp, cm_scache_t *scp, char *pathp,
                  char **newPathp, cm_user_t *userp, cm_req_t *reqp)
{
    struct smb_FullNameRock rock;
    long code = 0;

    rock.name = pathp;
    rock.vnode = scp;

    code = cm_ApplyDir(dscp, smb_FullNameProc, &rock, NULL, userp, reqp, NULL); 
    if (code == CM_ERROR_STOPNOW)
        *newPathp = rock.fullName;
    else
        *newPathp = strdup(pathp);
}

long smb_ReceiveCoreClose(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    unsigned short fid;
    smb_fid_t *fidp;
    cm_user_t *userp;
    afs_uint32 dosTime;
    long code = 0;
    cm_req_t req;

    cm_InitReq(&req);

    fid = smb_GetSMBParm(inp, 0);
    dosTime = smb_GetSMBParm(inp, 1) | (smb_GetSMBParm(inp, 2) << 16);

    osi_Log1(smb_logp, "SMB close fid %d", fid);

    fid = smb_ChainFID(fid, inp);
    fidp = smb_FindFID(vcp, fid, 0);
    if (!fidp) {
        return CM_ERROR_BADFD;
    }
        
    userp = smb_GetUser(vcp, inp);

    lock_ObtainMutex(&fidp->mx);

    /* Don't jump the gun on an async raw write */
    while (fidp->raw_writers) {
        lock_ReleaseMutex(&fidp->mx);
        thrd_WaitForSingleObject_Event(fidp->raw_write_event, RAWTIMEOUT);
        lock_ObtainMutex(&fidp->mx);
    }

    fidp->flags |= SMB_FID_DELETE;
        
    /* watch for ioctl closes, and read-only opens */
    if (fidp->scp != NULL &&
        (fidp->flags & (SMB_FID_OPENWRITE | SMB_FID_DELONCLOSE))
         == SMB_FID_OPENWRITE) {
        if (dosTime != 0 && dosTime != -1) {
            fidp->scp->mask |= CM_SCACHEMASK_CLIENTMODTIME;
            /* This fixes defect 10958 */
            CompensateForSmbClientLastWriteTimeBugs(&dosTime);
            smb_UnixTimeFromDosUTime(&fidp->scp->clientModTime, dosTime);
        }
        code = cm_FSync(fidp->scp, userp, &req);
    }
    else 
        code = 0;

    if (fidp->flags & SMB_FID_DELONCLOSE) {
        cm_scache_t *dscp = fidp->NTopen_dscp;
        char *pathp = fidp->NTopen_pathp;
        char *fullPathp;

        smb_FullName(dscp, fidp->scp, pathp, &fullPathp, userp, &req);
        if (fidp->scp->fileType == CM_SCACHETYPE_DIRECTORY) {
            code = cm_RemoveDir(dscp, fullPathp, userp, &req);
            if (code == 0 && (dscp->flags & CM_SCACHEFLAG_ANYWATCH))
                smb_NotifyChange(FILE_ACTION_REMOVED,
                                 FILE_NOTIFY_CHANGE_DIR_NAME,
                                 dscp, fullPathp, NULL, TRUE);
        }
        else 
        {
            code = cm_Unlink(dscp, fullPathp, userp, &req);
            if (code == 0 && (dscp->flags & CM_SCACHEFLAG_ANYWATCH))
                smb_NotifyChange(FILE_ACTION_REMOVED,
                                 FILE_NOTIFY_CHANGE_FILE_NAME,
                                 dscp, fullPathp, NULL, TRUE);
        }
        free(fullPathp);
    }
    lock_ReleaseMutex(&fidp->mx);

    if (fidp->flags & SMB_FID_NTOPEN) {
        cm_ReleaseSCache(fidp->NTopen_dscp);
        free(fidp->NTopen_pathp);
    }
    if (fidp->NTopen_wholepathp)
        free(fidp->NTopen_wholepathp);
    
    smb_ReleaseFID(fidp);
    cm_ReleaseUser(userp);
    return code;
}

/*
 * smb_ReadData -- common code for Read, Read And X, and Raw Read
 */
#ifndef DJGPP
long smb_ReadData(smb_fid_t *fidp, osi_hyper_t *offsetp, long count, char *op,
	cm_user_t *userp, long *readp)
#else /* DJGPP */
long smb_ReadData(smb_fid_t *fidp, osi_hyper_t *offsetp, long count, char *op,
	cm_user_t *userp, long *readp, int dosflag)
#endif /* !DJGPP */
{
    osi_hyper_t offset;
    long code = 0;
    cm_scache_t *scp;
    cm_buf_t *bufferp;
    osi_hyper_t fileLength;
    osi_hyper_t thyper;
    osi_hyper_t lastByte;
    osi_hyper_t bufferOffset;
    long bufIndex, nbytes;
    int chunk;
    int sequential = 0;
    cm_req_t req;

    cm_InitReq(&req);

    bufferp = NULL;
    offset = *offsetp;

    lock_ObtainMutex(&fidp->mx);
    scp = fidp->scp;
    lock_ObtainMutex(&scp->mx);

    if (offset.HighPart == 0) {
        chunk = offset.LowPart >> cm_logChunkSize;
        if (chunk != fidp->curr_chunk) {
            fidp->prev_chunk = fidp->curr_chunk;
            fidp->curr_chunk = chunk;
        }
        if (fidp->curr_chunk == fidp->prev_chunk + 1)
            sequential = 1;
    }       

    /* start by looking up the file's end */
    code = cm_SyncOp(scp, NULL, userp, &req, 0,
                     CM_SCACHESYNC_NEEDCALLBACK | CM_SCACHESYNC_GETSTATUS);
    if (code) goto done;

    /* now we have the entry locked, look up the length */
    fileLength = scp->length;

    /* adjust count down so that it won't go past EOF */
    thyper.LowPart = count;
    thyper.HighPart = 0;
    thyper = LargeIntegerAdd(offset, thyper);	/* where read should end */
    lastByte = thyper;
    if (LargeIntegerGreaterThan(thyper, fileLength)) {
        /* we'd read past EOF, so just stop at fileLength bytes.
         * Start by computing how many bytes remain in the file.
         */
        thyper = LargeIntegerSubtract(fileLength, offset);

        /* if we are past EOF, read 0 bytes */
        if (LargeIntegerLessThanZero(thyper))
            count = 0;
        else
            count = thyper.LowPart;
    }       

    *readp = count;

    /* now, copy the data one buffer at a time,
     * until we've filled the request packet
     */
    while (1) {
        /* if we've copied all the data requested, we're done */
        if (count <= 0) break;

        /* otherwise, load up a buffer of data */
        thyper.HighPart = offset.HighPart;
        thyper.LowPart = offset.LowPart & ~(cm_data.buf_blockSize-1);
        if (!bufferp || !LargeIntegerEqualTo(thyper, bufferOffset)) {
            /* wrong buffer */
            if (bufferp) {
                buf_Release(bufferp);
                bufferp = NULL;
            }
            lock_ReleaseMutex(&scp->mx);

            lock_ObtainRead(&scp->bufCreateLock);
            code = buf_Get(scp, &thyper, &bufferp);
            lock_ReleaseRead(&scp->bufCreateLock);

            lock_ObtainMutex(&scp->mx);
            if (code) goto done;
            bufferOffset = thyper;

            /* now get the data in the cache */
            while (1) {
                code = cm_SyncOp(scp, bufferp, userp, &req, 0,
                                 CM_SCACHESYNC_NEEDCALLBACK |
                                 CM_SCACHESYNC_READ);
                if (code) goto done;
                                
                if (cm_HaveBuffer(scp, bufferp, 0)) break;

                /* otherwise, load the buffer and try again */
                code = cm_GetBuffer(scp, bufferp, NULL, userp, &req);
                if (code) break;
            }
            if (code) {
                buf_Release(bufferp);
                bufferp = NULL;
                goto done;
            }
        }	/* if (wrong buffer) ... */

        /* now we have the right buffer loaded.  Copy out the
         * data from here to the user's buffer.
         */
        bufIndex = offset.LowPart & (cm_data.buf_blockSize - 1);

        /* and figure out how many bytes we want from this buffer */
        nbytes = cm_data.buf_blockSize - bufIndex;	/* what remains in buffer */
        if (nbytes > count) nbytes = count;	/* don't go past EOF */

        /* now copy the data */
#ifdef DJGPP
        if (dosflag)
            dosmemput(bufferp->datap + bufIndex, nbytes, (dos_ptr)op);
        else
#endif /* DJGPP */
            memcpy(op, bufferp->datap + bufIndex, nbytes);
                
        /* adjust counters, pointers, etc. */
        op += nbytes;
        count -= nbytes;
        thyper.LowPart = nbytes;
        thyper.HighPart = 0;
        offset = LargeIntegerAdd(thyper, offset);
    } /* while 1 */

  done:
    lock_ReleaseMutex(&scp->mx);
    lock_ReleaseMutex(&fidp->mx);
    if (bufferp) 
        buf_Release(bufferp);

    if (code == 0 && sequential)
        cm_ConsiderPrefetch(scp, &lastByte, userp, &req);

    return code;
}

/*
 * smb_WriteData -- common code for Write and Raw Write
 */
#ifndef DJGPP
long smb_WriteData(smb_fid_t *fidp, osi_hyper_t *offsetp, long count, char *op,
	cm_user_t *userp, long *writtenp)
#else /* DJGPP */
long smb_WriteData(smb_fid_t *fidp, osi_hyper_t *offsetp, long count, char *op,
	cm_user_t *userp, long *writtenp, int dosflag)
#endif /* !DJGPP */
{
    osi_hyper_t offset;
    long code = 0;
    long written = 0;
    cm_scache_t *scp;
    osi_hyper_t fileLength;	/* file's length at start of write */
    osi_hyper_t minLength;	/* don't read past this */
    long nbytes;		/* # of bytes to transfer this iteration */
    cm_buf_t *bufferp;
    osi_hyper_t thyper;		/* hyper tmp variable */
    osi_hyper_t bufferOffset;
    long bufIndex;		/* index in buffer where our data is */
    int doWriteBack;
    osi_hyper_t writeBackOffset;/* offset of region to write back when
                                 * I/O is done */
    DWORD filter = 0;
    cm_req_t req;

    osi_Log3(smb_logp, "smb_WriteData fid %d, off 0x%x, size 0x%x",
              fidp->fid, offsetp->LowPart, count);

    *writtenp = 0;

    cm_InitReq(&req);

    bufferp = NULL;
    doWriteBack = 0;
    offset = *offsetp;

    lock_ObtainMutex(&fidp->mx);
    scp = fidp->scp;
    lock_ObtainMutex(&scp->mx);

    /* start by looking up the file's end */
    code = cm_SyncOp(scp, NULL, userp, &req, 0,
                      CM_SCACHESYNC_NEEDCALLBACK
                      | CM_SCACHESYNC_SETSTATUS
                      | CM_SCACHESYNC_GETSTATUS);
    if (code) 
        goto done;
        
    /* make sure we have a writable FD */
    if (!(fidp->flags & SMB_FID_OPENWRITE)) {
        code = CM_ERROR_BADFDOP;
        goto done;
    }

    /* now we have the entry locked, look up the length */
    fileLength = scp->length;
    minLength = fileLength;
    if (LargeIntegerGreaterThan(minLength, scp->serverLength))
        minLength = scp->serverLength;

    /* adjust file length if we extend past EOF */
    thyper.LowPart = count;
    thyper.HighPart = 0;
    thyper = LargeIntegerAdd(offset, thyper);	/* where write should end */
    if (LargeIntegerGreaterThan(thyper, fileLength)) {
        /* we'd write past EOF, so extend the file */
        scp->mask |= CM_SCACHEMASK_LENGTH;
        scp->length = thyper;
        filter |= (FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
    } else
        filter |= FILE_NOTIFY_CHANGE_LAST_WRITE;

    /* now, if the new position (thyper) and the old (offset) are in
     * different storeback windows, remember to store back the previous
     * storeback window when we're done with the write.
     */
    if ((thyper.LowPart & (-cm_chunkSize)) !=
         (offset.LowPart & (-cm_chunkSize))) {
        /* they're different */
        doWriteBack = 1;
        writeBackOffset.HighPart = offset.HighPart;
        writeBackOffset.LowPart = offset.LowPart & (-cm_chunkSize);
    }
        
    *writtenp = count;

    /* now, copy the data one buffer at a time, until we've filled the
     * request packet */
    while (1) {
        /* if we've copied all the data requested, we're done */
        if (count <= 0) 
            break;

        /* handle over quota or out of space */
        if (scp->flags & (CM_SCACHEFLAG_OVERQUOTA | CM_SCACHEFLAG_OUTOFSPACE)) {
            *writtenp = written;
            code = CM_ERROR_QUOTA;
            break;
        }

        /* otherwise, load up a buffer of data */
        thyper.HighPart = offset.HighPart;
        thyper.LowPart = offset.LowPart & ~(cm_data.buf_blockSize-1);
        if (!bufferp || !LargeIntegerEqualTo(thyper, bufferOffset)) {
            /* wrong buffer */
            if (bufferp) {
                lock_ReleaseMutex(&bufferp->mx);
                buf_Release(bufferp);
                bufferp = NULL;
            }	
            lock_ReleaseMutex(&scp->mx);

            lock_ObtainRead(&scp->bufCreateLock);
            code = buf_Get(scp, &thyper, &bufferp);
            lock_ReleaseRead(&scp->bufCreateLock);

            lock_ObtainMutex(&bufferp->mx);
            lock_ObtainMutex(&scp->mx);
            if (code) goto done;

            bufferOffset = thyper;

            /* now get the data in the cache */
            while (1) {
                code = cm_SyncOp(scp, bufferp, userp, &req, 0,
                                  CM_SCACHESYNC_NEEDCALLBACK
                                  | CM_SCACHESYNC_WRITE
                                  | CM_SCACHESYNC_BUFLOCKED);
                if (code) 
                    goto done;

                /* If we're overwriting the entire buffer, or
                 * if we're writing at or past EOF, mark the
                 * buffer as current so we don't call
                 * cm_GetBuffer.  This skips the fetch from the
                 * server in those cases where we're going to 
                 * obliterate all the data in the buffer anyway,
                 * or in those cases where there is no useful
                 * data at the server to start with.
                 *
                 * Use minLength instead of scp->length, since
                 * the latter has already been updated by this
                 * call.
                 */
                if (LargeIntegerGreaterThanOrEqualTo(bufferp->offset, minLength)
                     || LargeIntegerEqualTo(offset, bufferp->offset)
                     && (count >= cm_data.buf_blockSize
                          || LargeIntegerGreaterThanOrEqualTo(LargeIntegerAdd(offset,
                                                                               ConvertLongToLargeInteger(count)),
                                                               minLength))) {
                    if (count < cm_data.buf_blockSize
                         && bufferp->dataVersion == -1)
                        memset(bufferp->datap, 0,
                                cm_data.buf_blockSize);
                    bufferp->dataVersion = scp->dataVersion;
                }

                if (cm_HaveBuffer(scp, bufferp, 1)) break;

                /* otherwise, load the buffer and try again */
                lock_ReleaseMutex(&bufferp->mx);
                code = cm_GetBuffer(scp, bufferp, NULL, userp,
                                     &req);
                lock_ReleaseMutex(&scp->mx);
                lock_ObtainMutex(&bufferp->mx);
                lock_ObtainMutex(&scp->mx);
                if (code) break;
            }
            if (code) {
                lock_ReleaseMutex(&bufferp->mx);
                buf_Release(bufferp);
                bufferp = NULL;
                goto done;
            }
        }	/* if (wrong buffer) ... */

        /* now we have the right buffer loaded.  Copy out the
         * data from here to the user's buffer.
         */
        bufIndex = offset.LowPart & (cm_data.buf_blockSize - 1);

        /* and figure out how many bytes we want from this buffer */
        nbytes = cm_data.buf_blockSize - bufIndex;	/* what remains in buffer */
        if (nbytes > count) 
            nbytes = count;	/* don't go past end of request */

        /* now copy the data */
#ifdef DJGPP
        if (dosflag)
            dosmemget((dos_ptr)op, nbytes, bufferp->datap + bufIndex);
        else
#endif /* DJGPP */
            memcpy(bufferp->datap + bufIndex, op, nbytes);
        buf_SetDirty(bufferp);

        /* and record the last writer */
        if (bufferp->userp != userp) {
            cm_HoldUser(userp);
            if (bufferp->userp) 
                cm_ReleaseUser(bufferp->userp);
            bufferp->userp = userp;
        }

        /* adjust counters, pointers, etc. */
        op += nbytes;
        count -= nbytes;
        written += nbytes;
        thyper.LowPart = nbytes;
        thyper.HighPart = 0;
        offset = LargeIntegerAdd(thyper, offset);
    } /* while 1 */

  done:
    lock_ReleaseMutex(&scp->mx);
    lock_ReleaseMutex(&fidp->mx);
    if (bufferp) {
        lock_ReleaseMutex(&bufferp->mx);
        buf_Release(bufferp);
    }

    if (code == 0 && filter != 0 && (fidp->flags & SMB_FID_NTOPEN)
         && (fidp->NTopen_dscp->flags & CM_SCACHEFLAG_ANYWATCH)) {
        smb_NotifyChange(FILE_ACTION_MODIFIED, filter,
                          fidp->NTopen_dscp, fidp->NTopen_pathp,
                          NULL, TRUE);
    }       

    if (code == 0 && doWriteBack) {
        long code2;
        lock_ObtainMutex(&scp->mx);
        osi_Log1(smb_logp, "smb_WriteData fid %d calling cm_SyncOp ASYNCSTORE",
                  fidp->fid);
        code2 = cm_SyncOp(scp, NULL, userp, &req, 0, CM_SCACHESYNC_ASYNCSTORE);
        osi_Log2(smb_logp, "smb_WriteData fid %d calling cm_SyncOp ASYNCSTORE returns %d",
                  fidp->fid,code2);
        lock_ReleaseMutex(&scp->mx);
        cm_QueueBKGRequest(scp, cm_BkgStore, writeBackOffset.LowPart,
                            writeBackOffset.HighPart, cm_chunkSize, 0, userp);
    }

    osi_Log3(smb_logp, "smb_WriteData fid %d returns %d written %d",
              fidp->fid, code, *writtenp);
    return code;
}

long smb_ReceiveCoreWrite(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    osi_hyper_t offset;
    long count, written = 0, total_written = 0;
    unsigned short fd;
    smb_fid_t *fidp;
    long code = 0;
    cm_user_t *userp;
    cm_attr_t truncAttr;	/* attribute struct used for truncating file */
    char *op;
    int inDataBlockCount;

    fd = smb_GetSMBParm(inp, 0);
    count = smb_GetSMBParm(inp, 1);
    offset.HighPart = 0;	/* too bad */
    offset.LowPart = smb_GetSMBParm(inp, 2) | (smb_GetSMBParm(inp, 3) << 16);

    op = smb_GetSMBData(inp, NULL);
    op = smb_ParseDataBlock(op, NULL, &inDataBlockCount);

    osi_Log3(smb_logp, "smb_ReceiveCoreWrite fid %d, off 0x%x, size 0x%x",
             fd, offset.LowPart, count);
        
    fd = smb_ChainFID(fd, inp);
    fidp = smb_FindFID(vcp, fd, 0);
    if (!fidp) {
        return CM_ERROR_BADFD;
    }
        
    if (fidp->flags & SMB_FID_IOCTL)
        return smb_IoctlWrite(fidp, vcp, inp, outp);
        
    userp = smb_GetUser(vcp, inp);

	/* special case: 0 bytes transferred means truncate to this position */
    if (count == 0) {
        cm_req_t req;

        cm_InitReq(&req);

        truncAttr.mask = CM_ATTRMASK_LENGTH;
        truncAttr.length.LowPart = offset.LowPart;
        truncAttr.length.HighPart = 0;
        lock_ObtainMutex(&fidp->mx);
        code = cm_SetAttr(fidp->scp, &truncAttr, userp, &req);
        lock_ReleaseMutex(&fidp->mx);
        smb_SetSMBParm(outp, 0, /* count */ 0);
        smb_SetSMBDataLength(outp, 0);
        fidp->flags |= SMB_FID_LENGTHSETDONE;
        goto done;
    }

    /*
     * Work around bug in NT client
     *
     * When copying a file, the NT client should first copy the data,
     * then copy the last write time.  But sometimes the NT client does
     * these in the wrong order, so the data copies would inadvertently
     * cause the last write time to be overwritten.  We try to detect this,
     * and don't set client mod time if we think that would go against the
     * intention.
     */
    if ((fidp->flags & SMB_FID_MTIMESETDONE) != SMB_FID_MTIMESETDONE) {
        fidp->scp->mask |= CM_SCACHEMASK_CLIENTMODTIME;
        fidp->scp->clientModTime = time(NULL);
    }

    code = 0;
    while ( code == 0 && count > 0 ) {
#ifndef DJGPP
	code = smb_WriteData(fidp, &offset, count, op, userp, &written);
#else /* DJGPP */
	code = smb_WriteData(fidp, &offset, count, op, userp, &written, FALSE);
#endif /* !DJGPP */
	if (code == 0 && written == 0)
            code = CM_ERROR_PARTIALWRITE;

        offset.LowPart += written;
        count -= written;
        total_written += written;
        written = 0;
    }
    
    /* set the packet data length to 3 bytes for the data block header,
     * plus the size of the data.
     */
    smb_SetSMBParm(outp, 0, total_written);
    smb_SetSMBDataLength(outp, 0);

  done:
    smb_ReleaseFID(fidp);
    cm_ReleaseUser(userp);

    return code;
}

void smb_CompleteWriteRaw(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp,
                          NCB *ncbp, raw_write_cont_t *rwcp)
{
    unsigned short fd;
    smb_fid_t *fidp;
    cm_user_t *userp;
#ifndef DJGPP
    char *rawBuf;
#else /* DJGPP */
    dos_ptr rawBuf;
#endif /* !DJGPP */
    long written = 0;
    long code = 0;

    fd = smb_GetSMBParm(inp, 0);
    fidp = smb_FindFID(vcp, fd, 0);

    osi_Log2(smb_logp, "Completing Raw Write offset %x count %x",
             rwcp->offset.LowPart, rwcp->count);

    userp = smb_GetUser(vcp, inp);

#ifndef DJGPP
    rawBuf = rwcp->buf;
    code = smb_WriteData(fidp, &rwcp->offset, rwcp->count, rawBuf, userp,
						 &written);
#else /* DJGPP */
    rawBuf = (dos_ptr) rwcp->buf;
    code = smb_WriteData(fidp, &rwcp->offset, rwcp->count,
                         (unsigned char *) rawBuf, userp,
                         &written, TRUE);
#endif /* !DJGPP */

    if (rwcp->writeMode & 0x1) {	/* synchronous */
        smb_t *op;

        smb_FormatResponsePacket(vcp, inp, outp);
        op = (smb_t *) outp;
        op->com = 0x20;		/* SMB_COM_WRITE_COMPLETE */
        smb_SetSMBParm(outp, 0, written + rwcp->alreadyWritten);
        smb_SetSMBDataLength(outp,  0);
        smb_SendPacket(vcp, outp);
        smb_FreePacket(outp);
    }
    else {				/* asynchronous */
        lock_ObtainMutex(&fidp->mx);
        fidp->raw_writers--;
        if (fidp->raw_writers == 0)
            thrd_SetEvent(fidp->raw_write_event);
        lock_ReleaseMutex(&fidp->mx);
    }

    /* Give back raw buffer */
    lock_ObtainMutex(&smb_RawBufLock);
#ifndef DJGPP
    *((char **)rawBuf) = smb_RawBufs;
#else /* DJGPP */
    _farpokel(_dos_ds, rawBuf, smb_RawBufs);
#endif /* !DJGPP */
    smb_RawBufs = rawBuf;
    lock_ReleaseMutex(&smb_RawBufLock);

    smb_ReleaseFID(fidp);
    cm_ReleaseUser(userp);
}

long smb_ReceiveCoreWriteRawDummy(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    return 0;
}

long smb_ReceiveCoreWriteRaw(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp, raw_write_cont_t *rwcp)
{
    osi_hyper_t offset;
    long count, written = 0, total_written = 0;
    long totalCount;
    unsigned short fd;
    smb_fid_t *fidp;
    long code = 0;
    cm_user_t *userp;
    char *op;
    unsigned short writeMode;
#ifndef DJGPP
    char *rawBuf;
#else /* DJGPP */
    dos_ptr rawBuf;
#endif /* !DJGPP */

    fd = smb_GetSMBParm(inp, 0);
    totalCount = smb_GetSMBParm(inp, 1);
    count = smb_GetSMBParm(inp, 10);
    offset.HighPart = 0;	/* too bad */
    offset.LowPart = smb_GetSMBParm(inp, 3) | (smb_GetSMBParm(inp, 4) << 16);
    writeMode = smb_GetSMBParm(inp, 7);

    op = (char *) inp->data;
    op += smb_GetSMBParm(inp, 11);

    osi_Log4(smb_logp,
             "smb_ReceiveCoreWriteRaw fd %d, off 0x%x, size 0x%x, WriteMode 0x%x",
             fd, offset.LowPart, count, writeMode);
        
    fd = smb_ChainFID(fd, inp);
    fidp = smb_FindFID(vcp, fd, 0);
    if (!fidp) {
        return CM_ERROR_BADFD;
    }
        
    userp = smb_GetUser(vcp, inp);

    /*
     * Work around bug in NT client
     *
     * When copying a file, the NT client should first copy the data,
     * then copy the last write time.  But sometimes the NT client does
     * these in the wrong order, so the data copies would inadvertently
     * cause the last write time to be overwritten.  We try to detect this,
     * and don't set client mod time if we think that would go against the
     * intention.
     */
    if ((fidp->flags & SMB_FID_LOOKSLIKECOPY) != SMB_FID_LOOKSLIKECOPY) {
        fidp->scp->mask |= CM_SCACHEMASK_CLIENTMODTIME;
        fidp->scp->clientModTime = time(NULL);
    }

    code = 0;
    while ( code == 0 && count > 0 ) {
#ifndef DJGPP
	code = smb_WriteData(fidp, &offset, count, op, userp, &written);
#else /* DJGPP */
	code = smb_WriteData(fidp, &offset, count, op, userp, &written, FALSE);
#endif /* !DJGPP */
	if (code == 0 && written == 0)
            code = CM_ERROR_PARTIALWRITE;

        offset.LowPart += written;
        count -= written;
        total_written += written;
        written = 0;
    }

    /* Get a raw buffer */
    if (code == 0) {
        rawBuf = NULL;
        lock_ObtainMutex(&smb_RawBufLock);
        if (smb_RawBufs) {
            /* Get a raw buf, from head of list */
            rawBuf = smb_RawBufs;
#ifndef DJGPP
            smb_RawBufs = *(char **)smb_RawBufs;
#else /* DJGPP */
            smb_RawBufs = _farpeekl(_dos_ds, smb_RawBufs);
#endif /* !DJGPP */
        }
        else
            code = CM_ERROR_USESTD;
		
        lock_ReleaseMutex(&smb_RawBufLock);
    }

    /* Don't allow a premature Close */
    if (code == 0 && (writeMode & 1) == 0) {
        lock_ObtainMutex(&fidp->mx);
        fidp->raw_writers++;
        thrd_ResetEvent(fidp->raw_write_event);
        lock_ReleaseMutex(&fidp->mx);
    }

    smb_ReleaseFID(fidp);
    cm_ReleaseUser(userp);

    if (code) {
        smb_SetSMBParm(outp, 0, total_written);
        smb_SetSMBDataLength(outp, 0);
        ((smb_t *)outp)->com = 0x20;	/* SMB_COM_WRITE_COMPLETE */
        rwcp->code = code;
        return code;
    }

    rwcp->code = 0;
    rwcp->buf = rawBuf;
    rwcp->offset.HighPart = 0;
    rwcp->offset.LowPart = offset.LowPart + count;
    rwcp->count = totalCount - count;
    rwcp->writeMode = writeMode;
    rwcp->alreadyWritten = total_written;

    /* set the packet data length to 3 bytes for the data block header,
     * plus the size of the data.
     */
    smb_SetSMBParm(outp, 0, 0xffff);
    smb_SetSMBDataLength(outp, 0);

    return 0;
}

long smb_ReceiveCoreRead(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    osi_hyper_t offset;
    long count, finalCount;
    unsigned short fd;
    smb_fid_t *fidp;
    long code = 0;
    cm_user_t *userp;
    char *op;
        
    fd = smb_GetSMBParm(inp, 0);
    count = smb_GetSMBParm(inp, 1);
    offset.HighPart = 0;	/* too bad */
    offset.LowPart = smb_GetSMBParm(inp, 2) | (smb_GetSMBParm(inp, 3) << 16);
        
    osi_Log3(smb_logp, "smb_ReceiveCoreRead fd %d, off 0x%x, size 0x%x",
             fd, offset.LowPart, count);
        
    fd = smb_ChainFID(fd, inp);
    fidp = smb_FindFID(vcp, fd, 0);
    if (!fidp) {
        return CM_ERROR_BADFD;
    }
        
    if (fidp->flags & SMB_FID_IOCTL) {
        return smb_IoctlRead(fidp, vcp, inp, outp);
    }
        
    userp = smb_GetUser(vcp, inp);

    /* remember this for final results */
    smb_SetSMBParm(outp, 0, count);
    smb_SetSMBParm(outp, 1, 0);
    smb_SetSMBParm(outp, 2, 0);
    smb_SetSMBParm(outp, 3, 0);
    smb_SetSMBParm(outp, 4, 0);

    /* set the packet data length to 3 bytes for the data block header,
     * plus the size of the data.
     */
    smb_SetSMBDataLength(outp, count+3);
        
    /* get op ptr after putting in the parms, since otherwise we don't
     * know where the data really is.
     */
    op = smb_GetSMBData(outp, NULL);

    /* now emit the data block header: 1 byte of type and 2 bytes of length */
    *op++ = 1;	/* data block marker */
    *op++ = (unsigned char) (count & 0xff);
    *op++ = (unsigned char) ((count >> 8) & 0xff);
                
#ifndef DJGPP
    code = smb_ReadData(fidp, &offset, count, op, userp, &finalCount);
#else /* DJGPP */
    code = smb_ReadData(fidp, &offset, count, op, userp, &finalCount, FALSE);
#endif /* !DJGPP */

    /* fix some things up */
    smb_SetSMBParm(outp, 0, finalCount);
    smb_SetSMBDataLength(outp, finalCount+3);

    smb_ReleaseFID(fidp);
	
    cm_ReleaseUser(userp);
    return code;
}

long smb_ReceiveCoreMakeDir(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    char *pathp;
    long code = 0;
    cm_space_t *spacep;
    char *tp;
    cm_user_t *userp;
    cm_scache_t *dscp;			/* dir we're dealing with */
    cm_scache_t *scp;			/* file we're creating */
    cm_attr_t setAttr;
    int initialModeBits;
    char *lastNamep;
    int caseFold;
    char *tidPathp;
    cm_req_t req;

    cm_InitReq(&req);

    scp = NULL;
        
    /* compute initial mode bits based on read-only flag in attributes */
    initialModeBits = 0777;
        
    tp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(tp, &tp);
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);

    if (strcmp(pathp, "\\") == 0)
        return CM_ERROR_EXISTS;

    spacep = inp->spacep;
    smb_StripLastComponent(spacep->data, &lastNamep, pathp);

    userp = smb_GetUser(vcp, inp);

    caseFold = CM_FLAG_CASEFOLD;

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHPATH;
    }

    code = cm_NameI(cm_data.rootSCachep, spacep->data,
                    caseFold | CM_FLAG_FOLLOW | CM_FLAG_CHECKPATH,
                    userp, tidPathp, &req, &dscp);

    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
        
#ifdef DFS_SUPPORT
    if (dscp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(dscp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    /* otherwise, scp points to the parent directory.  Do a lookup, and
     * fail if we find it.  Otherwise, we do the create.
     */
    if (!lastNamep) 
        lastNamep = pathp;
    else 
        lastNamep++;
    code = cm_Lookup(dscp, lastNamep, 0, userp, &req, &scp);
    if (scp) cm_ReleaseSCache(scp);
    if (code != CM_ERROR_NOSUCHFILE) {
        if (code == 0) code = CM_ERROR_EXISTS;
        cm_ReleaseSCache(dscp);
        cm_ReleaseUser(userp);
        return code;
    }
        
    setAttr.mask = CM_ATTRMASK_CLIENTMODTIME;
    setAttr.clientModTime = time(NULL);
    code = cm_MakeDir(dscp, lastNamep, 0, &setAttr, userp, &req);
    if (code == 0 && (dscp->flags & CM_SCACHEFLAG_ANYWATCH))
        smb_NotifyChange(FILE_ACTION_ADDED,
                         FILE_NOTIFY_CHANGE_DIR_NAME,
                         dscp, lastNamep, NULL, TRUE);
        
    /* we don't need this any longer */
    cm_ReleaseSCache(dscp);

    if (code) {
        /* something went wrong creating or truncating the file */
        cm_ReleaseUser(userp);
        return code;
    }
        
    /* otherwise we succeeded */
    smb_SetSMBDataLength(outp, 0);
    cm_ReleaseUser(userp);

    return 0;
}

BOOL smb_IsLegalFilename(char *filename)
{
    /* 
     *  Find the longest substring of filename that does not contain
     *  any of the chars in illegalChars.  If that substring is less
     *  than the length of the whole string, then one or more of the
     *  illegal chars is in filename. 
     */
    if (strcspn(filename, illegalChars) < strlen(filename))
        return FALSE;

    return TRUE;
}        

long smb_ReceiveCoreCreate(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    char *pathp;
    long code = 0;
    cm_space_t *spacep;
    char *tp;
    int excl;
    cm_user_t *userp;
    cm_scache_t *dscp;			/* dir we're dealing with */
    cm_scache_t *scp;			/* file we're creating */
    cm_attr_t setAttr;
    int initialModeBits;
    smb_fid_t *fidp;
    int attributes;
    char *lastNamep;
    int caseFold;
    afs_uint32 dosTime;
    char *tidPathp;
    cm_req_t req;

    cm_InitReq(&req);

    scp = NULL;
    excl = (inp->inCom == 0x03)? 0 : 1;
        
    attributes = smb_GetSMBParm(inp, 0);
    dosTime = smb_GetSMBParm(inp, 1) | (smb_GetSMBParm(inp, 2) << 16);
        
    /* compute initial mode bits based on read-only flag in attributes */
    initialModeBits = 0666;
    if (attributes & 1) initialModeBits &= ~0222;
        
    tp = smb_GetSMBData(inp, NULL);
    pathp = smb_ParseASCIIBlock(tp, &tp);
    if (smb_StoreAnsiFilenames)
        OemToChar(pathp,pathp);

    spacep = inp->spacep;
    smb_StripLastComponent(spacep->data, &lastNamep, pathp);

    userp = smb_GetUser(vcp, inp);

    caseFold = CM_FLAG_CASEFOLD;

    code = smb_LookupTIDPath(vcp, ((smb_t *)inp)->tid, &tidPathp);
    if (code) {
        cm_ReleaseUser(userp);
        return CM_ERROR_NOSUCHPATH;
    }
    code = cm_NameI(cm_data.rootSCachep, spacep->data, caseFold | CM_FLAG_FOLLOW,
                    userp, tidPathp, &req, &dscp);

    if (code) {
        cm_ReleaseUser(userp);
        return code;
    }
        
#ifdef DFS_SUPPORT
    if (dscp->fileType == CM_SCACHETYPE_DFSLINK) {
        cm_ReleaseSCache(dscp);
        cm_ReleaseUser(userp);
        if ( WANTS_DFS_PATHNAMES(inp) )
            return CM_ERROR_PATH_NOT_COVERED;
        else
            return CM_ERROR_BADSHARENAME;
    }
#endif /* DFS_SUPPORT */

    /* otherwise, scp points to the parent directory.  Do a lookup, and
     * truncate the file if we find it, otherwise we create the file.
     */
    if (!lastNamep) 
        lastNamep = pathp;
    else 
        lastNamep++;

    if (!smb_IsLegalFilename(lastNamep))
        return CM_ERROR_BADNTFILENAME;

    osi_Log1(smb_logp, "SMB receive create [%s]", osi_LogSaveString( smb_logp, pathp ));
#ifdef DEBUG_VERBOSE
    {
        char *hexp;
        hexp = osi_HexifyString( lastNamep );
        DEBUG_EVENT2("AFS", "CoreCreate H[%s] A[%s]", hexp, lastNamep );
        free(hexp);
    }
#endif    

    code = cm_Lookup(dscp, lastNamep, 0, userp, &req, &scp);
    if (code && code != CM_ERROR_NOSUCHFILE) {
        cm_ReleaseSCache(dscp);
        cm_ReleaseUser(userp);
        return code;
    }
        
    /* if we get here, if code is 0, the file exists and is represented by
     * scp.  Otherwise, we have to create it.
     */
    if (code == 0) {
        if (excl) {
            /* oops, file shouldn't be there */
            cm_ReleaseSCache(dscp);
            cm_ReleaseSCache(scp);
            cm_ReleaseUser(userp);
            return CM_ERROR_EXISTS;
        }

        setAttr.mask = CM_ATTRMASK_LENGTH;
        setAttr.length.LowPart = 0;
        setAttr.length.HighPart = 0;
        code = cm_SetAttr(scp, &setAttr, userp, &req);
    }
    else {
        setAttr.mask = CM_ATTRMASK_CLIENTMODTIME;
        smb_UnixTimeFromDosUTime(&setAttr.clientModTime, dosTime);
        code = cm_Create(dscp, lastNamep, 0, &setAttr, &scp, userp,
                         &req);
        if (code == 0 && (dscp->flags & CM_SCACHEFLAG_ANYWATCH))
            smb_NotifyChange(FILE_ACTION_ADDED,
                             FILE_NOTIFY_CHANGE_FILE_NAME,
                             dscp, lastNamep, NULL, TRUE);
        if (!excl && code == CM_ERROR_EXISTS) {
            /* not an exclusive create, and someone else tried
             * creating it already, then we open it anyway.  We
             * don't bother retrying after this, since if this next
             * fails, that means that the file was deleted after
             * we started this call.
             */
            code = cm_Lookup(dscp, lastNamep, caseFold, userp,
                             &req, &scp);
            if (code == 0) {
                setAttr.mask = CM_ATTRMASK_LENGTH;
                setAttr.length.LowPart = 0;
                setAttr.length.HighPart = 0;
                code = cm_SetAttr(scp, &setAttr, userp, &req);
            }
        }
    }
        
    /* we don't need this any longer */
    cm_ReleaseSCache(dscp);

    if (code) {
        /* something went wrong creating or truncating the file */
        if (scp) cm_ReleaseSCache(scp);
        cm_ReleaseUser(userp);
        return code;
    }

    /* make sure we only open files */
    if (scp->fileType != CM_SCACHETYPE_FILE) {
        cm_ReleaseSCache(scp);
        cm_ReleaseUser(userp);
        return CM_ERROR_ISDIR;
    }

    /* now all we have to do is open the file itself */
    fidp = smb_FindFID(vcp, 0, SMB_FLAG_CREATE);
    osi_assert(fidp);
	
    /* save a pointer to the vnode */
    fidp->scp = scp;
        
    /* always create it open for read/write */
    fidp->flags |= (SMB_FID_OPENREAD | SMB_FID_OPENWRITE);

    smb_ReleaseFID(fidp);
        
    smb_SetSMBParm(outp, 0, fidp->fid);
    smb_SetSMBDataLength(outp, 0);

    cm_Open(scp, 0, userp);

    cm_ReleaseUser(userp);
    /* leave scp held since we put it in fidp->scp */
    return 0;
}

long smb_ReceiveCoreSeek(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp)
{
    long code = 0;
    long offset;
    int whence;
    unsigned short fd;
    smb_fid_t *fidp;
    cm_scache_t *scp;
    cm_user_t *userp;
    cm_req_t req;

    cm_InitReq(&req);
        
    fd = smb_GetSMBParm(inp, 0);
    whence = smb_GetSMBParm(inp, 1);
    offset = smb_GetSMBParm(inp, 2) | (smb_GetSMBParm(inp, 3) << 16);
        
    /* try to find the file descriptor */
    fd = smb_ChainFID(fd, inp);
    fidp = smb_FindFID(vcp, fd, 0);
    if (!fidp || (fidp->flags & SMB_FID_IOCTL)) {
        return CM_ERROR_BADFD;
    }
	
    userp = smb_GetUser(vcp, inp);

    lock_ObtainMutex(&fidp->mx);
    scp = fidp->scp;
    lock_ObtainMutex(&scp->mx);
    code = cm_SyncOp(scp, NULL, userp, &req, 0,
                     CM_SCACHESYNC_NEEDCALLBACK | CM_SCACHESYNC_GETSTATUS);
    if (code == 0) {
        if (whence == 1) {
            /* offset from current offset */
            offset += fidp->offset;
        }
        else if (whence == 2) {
            /* offset from current EOF */
            offset += scp->length.LowPart;
        }
        fidp->offset = offset;
        smb_SetSMBParm(outp, 0, offset & 0xffff);
        smb_SetSMBParm(outp, 1, (offset>>16) & 0xffff);
        smb_SetSMBDataLength(outp, 0);
    }
    lock_ReleaseMutex(&scp->mx);
    lock_ReleaseMutex(&fidp->mx);
    smb_ReleaseFID(fidp);
    cm_ReleaseUser(userp);
    return code;
}

/* dispatch all of the requests received in a packet.  Due to chaining, this may
 * be more than one request.
 */
void smb_DispatchPacket(smb_vc_t *vcp, smb_packet_t *inp, smb_packet_t *outp,
                        NCB *ncbp, raw_write_cont_t *rwcp)
{
    smb_dispatch_t *dp;
    smb_t *smbp;
    unsigned long code = 0;
    unsigned char *outWctp;
    int nparms;			/* # of bytes of parameters */
    char tbuffer[200];
    int nbytes;			/* bytes of data, excluding count */
    int temp;
    unsigned char *tp;
    unsigned short errCode;
    unsigned long NTStatus;
    int noSend;
    unsigned char errClass;
    unsigned int oldGen;
    DWORD oldTime, newTime;

    /* get easy pointer to the data */
    smbp = (smb_t *) inp->data;

    if (!(outp->flags & SMB_PACKETFLAG_SUSPENDED)) {
        /* setup the basic parms for the initial request in the packet */
        inp->inCom = smbp->com;
        inp->wctp = &smbp->wct;
        inp->inCount = 0;
        inp->ncb_length = ncbp->ncb_length;
    }
    noSend = 0;

    /* Sanity check */
    if (ncbp->ncb_length < offsetof(struct smb, vdata)) {
        /* log it and discard it */
#ifndef DJGPP
        HANDLE h;
        char *ptbuf[1];
        char s[100];
        h = RegisterEventSource(NULL, AFS_DAEMON_EVENT_NAME);
        sprintf(s, "SMB message too short, len %d", ncbp->ncb_length);
        ptbuf[0] = s;
        ReportEvent(h, EVENTLOG_WARNING_TYPE, 0, 1007, NULL,
                     1, ncbp->ncb_length, ptbuf, inp);
        DeregisterEventSource(h);
#else /* DJGPP */
        osi_Log1(smb_logp, "SMB message too short, len %d", ncbp->ncb_length);
#endif /* !DJGPP */
        return;
    }

    /* We are an ongoing op */
    thrd_Increment(&ongoingOps);

    /* set up response packet for receiving output */
    if (!(outp->flags & SMB_PACKETFLAG_SUSPENDED))
        smb_FormatResponsePacket(vcp, inp, outp);
    outWctp = outp->wctp;

    /* Remember session generation number and time */
    oldGen = sessionGen;
    oldTime = GetCurrentTime();

    while (inp->inCom != 0xff) {
        dp = &smb_dispatchTable[inp->inCom];

        if (outp->flags & SMB_PACKETFLAG_SUSPENDED) {
            outp->flags &= ~SMB_PACKETFLAG_SUSPENDED;
            code = outp->resumeCode;
            goto resume;
        }

        /* process each request in the packet; inCom, wctp and inCount
         * are already set up.
         */
        osi_Log2(smb_logp, "SMB received op 0x%x lsn %d", inp->inCom,
                  ncbp->ncb_lsn);

        /* now do the dispatch */
        /* start by formatting the response record a little, as a default */
        if (dp->flags & SMB_DISPATCHFLAG_CHAINED) {
            outWctp[0] = 2;
            outWctp[1] = 0xff;	/* no operation */
            outWctp[2] = 0;		/* padding */
            outWctp[3] = 0;
            outWctp[4] = 0;
        }
        else {
            /* not a chained request, this is a more reasonable default */
            outWctp[0] = 0;	/* wct of zero */
            outWctp[1] = 0;	/* and bcc (word) of zero */
            outWctp[2] = 0;
        }   

        /* once set, stays set.  Doesn't matter, since we never chain
         * "no response" calls.
         */
        if (dp->flags & SMB_DISPATCHFLAG_NORESPONSE)
            noSend = 1;

        if (dp->procp) {
            /* we have a recognized operation */

            if (inp->inCom == 0x1d)
                /* Raw Write */
                code = smb_ReceiveCoreWriteRaw (vcp, inp, outp,
                                                 rwcp);
            else {
                osi_LogEvent("AFS Dispatch %s",(myCrt_Dispatch(inp->inCom)),"vcp 0x%x lana %d lsn %d",(int)vcp,vcp->lana,vcp->lsn);
                osi_Log4(smb_logp,"Dispatch %s vcp 0x%x lana %d lsn %d",myCrt_Dispatch(inp->inCom),vcp,vcp->lana,vcp->lsn);
                code = (*(dp->procp)) (vcp, inp, outp);
                osi_LogEvent("AFS Dispatch return",NULL,"Code 0x%x",(code==0)?0:code-CM_ERROR_BASE);
                osi_Log4(smb_logp,"Dispatch return  code 0x%x vcp 0x%x lana %d lsn %d",(code==0)?0:code-CM_ERROR_BASE,vcp,vcp->lana,vcp->lsn);
#ifdef LOG_PACKET
                if ( code == CM_ERROR_BADSMB ||
                     code == CM_ERROR_BADOP )
                smb_LogPacket(inp);
#endif /* LOG_PACKET */
            }   

            if (oldGen != sessionGen) {
#ifndef DJGPP
                HANDLE h;
                char *ptbuf[1];
                char s[100];
                newTime = GetCurrentTime();
                h = RegisterEventSource(NULL, AFS_DAEMON_EVENT_NAME);
                sprintf(s, "Pkt straddled session startup, took %d ms, ncb length %d",
                         newTime - oldTime, ncbp->ncb_length);
                ptbuf[0] = s;
                ReportEvent(h, EVENTLOG_WARNING_TYPE, 0,
                             1005, NULL, 1, ncbp->ncb_length, ptbuf, smbp);
                DeregisterEventSource(h);
#endif /* !DJGPP */
                osi_Log1(smb_logp, "Pkt straddled session startup, "
                          "ncb length %d", ncbp->ncb_length);
            }
        }
        else {
            /* bad opcode, fail the request, after displaying it */
            osi_Log1(smb_logp, "Received bad SMB req 0x%X", inp->inCom);
#ifdef LOG_PACKET
            smb_LogPacket(inp);
#endif  /* LOG_PACKET */

#ifndef DJGPP
            if (showErrors) {
                sprintf(tbuffer, "Received bad SMB req 0x%x", inp->inCom);
                code = (*smb_MBfunc)(NULL, tbuffer, "Cancel: don't show again",
                                      MB_OKCANCEL|MB_SERVICE_NOTIFICATION);
                if (code == IDCANCEL) 
                    showErrors = 0;
            }
#endif /* DJGPP */
            code = CM_ERROR_BADOP;
        }

        /* catastrophic failure:  log as much as possible */
        if (code == CM_ERROR_BADSMB) {
#ifndef DJGPP
            HANDLE h;
            char *ptbuf[1];
            char s[100];

            osi_Log1(smb_logp,
                      "Invalid SMB, ncb_length %d",
                      ncbp->ncb_length);

            h = RegisterEventSource(NULL, AFS_DAEMON_EVENT_NAME);
            sprintf(s, "Invalid SMB message, length %d",
                     ncbp->ncb_length);
            ptbuf[0] = s;
            ReportEvent(h, EVENTLOG_ERROR_TYPE, 0, 1002, NULL,
                         1, ncbp->ncb_length, ptbuf, smbp);
            DeregisterEventSource(h);
#ifdef LOG_PACKET
            smb_LogPacket(inp);
#endif /* LOG_PACKET */
#endif /* !DJGPP */
            osi_Log1(smb_logp, "Invalid SMB message, length %d",
                     ncbp->ncb_length);

            code = CM_ERROR_INVAL;
        }

        if (outp->flags & SMB_PACKETFLAG_NOSEND) {
            thrd_Decrement(&ongoingOps);
            return;
        }

      resume:
        /* now, if we failed, turn the current response into an empty
         * one, and fill in the response packet's error code.
         */
        if (code) {
            if (vcp->flags & SMB_VCFLAG_STATUS32) {
                smb_MapNTError(code, &NTStatus);
                outWctp = outp->wctp;
                smbp = (smb_t *) &outp->data;
                if (code != CM_ERROR_PARTIALWRITE
                     && code != CM_ERROR_BUFFERTOOSMALL 
                     && code != CM_ERROR_GSSCONTINUE) {
                    /* nuke wct and bcc.  For a partial
                     * write or an in-process authentication handshake, 
                     * assume they're OK.
                     */
                    *outWctp++ = 0;
                    *outWctp++ = 0;
                    *outWctp++ = 0;
                }
                smbp->rcls = (unsigned char) (NTStatus & 0xff);
                smbp->reh = (unsigned char) ((NTStatus >> 8) & 0xff);
                smbp->errLow = (unsigned char) ((NTStatus >> 16) & 0xff);
                smbp->errHigh = (unsigned char) ((NTStatus >> 24) & 0xff);
                smbp->flg2 |= SMB_FLAGS2_32BIT_STATUS;
                break;
            }
            else {
                smb_MapCoreError(code, vcp, &errCode, &errClass);
                outWctp = outp->wctp;
                smbp = (smb_t *) &outp->data;
                if (code != CM_ERROR_PARTIALWRITE) {
                    /* nuke wct and bcc.  For a partial
                     * write, assume they're OK.
                     */
                    *outWctp++ = 0;
                    *outWctp++ = 0;
                    *outWctp++ = 0;
                }
                smbp->errLow = (unsigned char) (errCode & 0xff);
                smbp->errHigh = (unsigned char) ((errCode >> 8) & 0xff);
                smbp->rcls = errClass;
                break;
            }
        }	/* error occurred */

        /* if we're here, we've finished one request.  Look to see if
         * this is a chained opcode.  If it is, setup things to process
         * the chained request, and setup the output buffer to hold the
         * chained response.  Start by finding the next input record.
         */
        if (!(dp->flags & SMB_DISPATCHFLAG_CHAINED))
            break;		/* not a chained req */
        tp = inp->wctp;		/* points to start of last request */
        /* in a chained request, the first two
         * parm fields are required, and are
         * AndXCommand/AndXReserved and
         * AndXOffset. */
        if (tp[0] < 2) break;	
        if (tp[1] == 0xff) break;	/* no more chained opcodes */
        inp->inCom = tp[1];
        inp->wctp = inp->data + tp[3] + (tp[4] << 8);
        inp->inCount++;

        /* and now append the next output request to the end of this
         * last request.  Begin by finding out where the last response
         * ends, since that's where we'll put our new response.
         */
        outWctp = outp->wctp;		/* ptr to out parameters */
        osi_assert (outWctp[0] >= 2);	/* need this for all chained requests */
        nparms = outWctp[0] << 1;
        tp = outWctp + nparms + 1;	/* now points to bcc field */
        nbytes = tp[0] + (tp[1] << 8);	/* # of data bytes */
        tp += 2 /* for the count itself */ + nbytes;
        /* tp now points to the new output record; go back and patch the
         * second parameter (off2) to point to the new record.
         */
        temp = (unsigned int)tp - ((unsigned int) outp->data);
        outWctp[3] = (unsigned char) (temp & 0xff);
        outWctp[4] = (unsigned char) ((temp >> 8) & 0xff);
        outWctp[2] = 0;	/* padding */
        outWctp[1] = inp->inCom;	/* next opcode */

        /* finally, setup for the next iteration */
        outp->wctp = tp;
        outWctp = tp;
    }	/* while loop over all requests in the packet */

    /* done logging out, turn off logging-out flag */
    if (!(inp->flags & SMB_PACKETFLAG_PROFILE_UPDATE_OK)) {
        vcp->justLoggedOut = NULL;
        if (loggedOut) {
            loggedOut = 0;
            free(loggedOutName);
            loggedOutName = NULL;
            smb_ReleaseUID(loggedOutUserp);
            loggedOutUserp = NULL;
        }
    }
 
    /* now send the output packet, and return */
    if (!noSend)
        smb_SendPacket(vcp, outp);
    thrd_Decrement(&ongoingOps);

    if (!(vcp->flags & SMB_VCFLAG_ALREADYDEAD)) {
        if (active_vcp != vcp) {
            if (active_vcp) {
                smb_ReleaseVC(active_vcp);
                osi_Log2(smb_logp,
                      "Replacing active_vcp %x with %x", active_vcp, vcp);
            }
            smb_HoldVC(vcp);
            active_vcp = vcp;
        }
        last_msg_time = GetCurrentTime();
    } else if (active_vcp == vcp) {
        smb_ReleaseVC(active_vcp);
        active_vcp = NULL;
    }

    return;
}

#ifndef DJGPP
/* Wait for Netbios() calls to return, and make the results available to server
 * threads.  Note that server threads can't wait on the NCBevents array
 * themselves, because NCB events are manual-reset, and the servers would race
 * each other to reset them.
 */
void smb_ClientWaiter(void *parmp)
{
    DWORD code;
    int   idx;

    while (smbShutdownFlag == 0) {
        code = thrd_WaitForMultipleObjects_Event(numNCBs, NCBevents,
                                                 FALSE, INFINITE);
        if (code == WAIT_OBJECT_0)
            continue;

        /* error checking */
        if (code >= WAIT_ABANDONED_0 && code < (WAIT_ABANDONED_0 + numNCBs))
        {
            int abandonIdx = code - WAIT_ABANDONED_0;
            osi_Log2(smb_logp, "Error: smb_ClientWaiter event %d abandoned, errno %d\n", abandonIdx, GetLastError());
        }

        if (code == WAIT_IO_COMPLETION)
        {
            osi_Log0(smb_logp, "Error: smb_ClientWaiter WAIT_IO_COMPLETION\n");
            continue;
        }
        
        if (code == WAIT_TIMEOUT)
        {
            osi_Log1(smb_logp, "Error: smb_ClientWaiter WAIT_TIMEOUT, errno %d\n", GetLastError());
        }

        if (code == WAIT_FAILED)
        {
            osi_Log1(smb_logp, "Error: smb_ClientWaiter WAIT_FAILED, errno %d\n", GetLastError());
        }

        idx = code - WAIT_OBJECT_0;
 
        /* check idx range! */
        if (idx < 0 || idx > (sizeof(NCBevents) / sizeof(NCBevents[0])))
        {
            /* this is fatal - log as much as possible */
            osi_Log1(smb_logp, "Fatal: NCBevents idx [ %d ] out of range.\n", idx);
            osi_assert(0);
        }
        
        thrd_ResetEvent(NCBevents[idx]);
        thrd_SetEvent(NCBreturns[0][idx]);
    }
}
#endif /* !DJGPP */

/*
 * Try to have one NCBRECV request waiting for every live session.  Not more
 * than one, because if there is more than one, it's hard to handle Write Raw.
 */
void smb_ServerWaiter(void *parmp)
{
    DWORD code;
    int idx_session, idx_NCB;
    NCB *ncbp;
#ifdef DJGPP
    dos_ptr dos_ncb;
#endif /* DJGPP */

    while (smbShutdownFlag == 0) {
        /* Get a session */
        code = thrd_WaitForMultipleObjects_Event(numSessions, SessionEvents,
                                                 FALSE, INFINITE);
        if (code == WAIT_OBJECT_0)
            continue;

        if (code >= WAIT_ABANDONED_0 && code < (WAIT_ABANDONED_0 + numSessions))
        {
            int abandonIdx = code - WAIT_ABANDONED_0;
            osi_Log2(smb_logp, "Error: smb_ServerWaiter (SessionEvents) event %d abandoned, errno %d\n", abandonIdx, GetLastError());
        }
	
        if (code == WAIT_IO_COMPLETION)
        {
            osi_Log0(smb_logp, "Error: smb_ServerWaiter (SessionEvents) WAIT_IO_COMPLETION\n");
            continue;
        }
	
        if (code == WAIT_TIMEOUT)
        {
            osi_Log1(smb_logp, "Error: smb_ServerWaiter (SessionEvents) WAIT_TIMEOUT, errno %d\n", GetLastError());
        }
	
        if (code == WAIT_FAILED)
        {
            osi_Log1(smb_logp, "Error: smb_ServerWaiter (SessionEvents) WAIT_FAILED, errno %d\n", GetLastError());
        }
	
        idx_session = code - WAIT_OBJECT_0;

        /* check idx range! */
        if (idx_session < 0 || idx_session > (sizeof(SessionEvents) / sizeof(SessionEvents[0])))
        {
            /* this is fatal - log as much as possible */
            osi_Log1(smb_logp, "Fatal: session idx [ %d ] out of range.\n", idx_session);
            osi_assert(0);
        }

		/* Get an NCB */
      NCBretry:
        code = thrd_WaitForMultipleObjects_Event(numNCBs, NCBavails,
                                                 FALSE, INFINITE);
        if (code == WAIT_OBJECT_0) {
            if (smbShutdownFlag == 1) 
                break;
            else
                goto NCBretry;
        }

        /* error checking */
        if (code >= WAIT_ABANDONED_0 && code < (WAIT_ABANDONED_0 + numNCBs))
        {
            int abandonIdx = code - WAIT_ABANDONED_0;
            osi_Log2(smb_logp, "Error: smb_ClientWaiter (NCBavails) event %d abandoned, errno %d\n", abandonIdx, GetLastError());
        }
	
        if (code == WAIT_IO_COMPLETION)
        {
            osi_Log0(smb_logp, "Error: smb_ClientWaiter (NCBavails) WAIT_IO_COMPLETION\n");
            continue;
        }
	
        if (code == WAIT_TIMEOUT)
        {
            osi_Log1(smb_logp, "Error: smb_ClientWaiter (NCBavails) WAIT_TIMEOUT, errno %d\n", GetLastError());
        }
	
        if (code == WAIT_FAILED)
        {
            osi_Log1(smb_logp, "Error: smb_ClientWaiter (NCBavails) WAIT_FAILED, errno %d\n", GetLastError());
        }
		
        idx_NCB = code - WAIT_OBJECT_0;

        /* check idx range! */
        if (idx_NCB < 0 || idx_NCB > (sizeof(NCBsessions) / sizeof(NCBsessions[0])))
        {
            /* this is fatal - log as much as possible */
            osi_Log1(smb_logp, "Fatal: idx_NCB [ %d ] out of range.\n", idx_NCB);
            osi_assert(0);
        }

        /* Link them together */
        NCBsessions[idx_NCB] = idx_session;

        /* Fire it up */
        ncbp = NCBs[idx_NCB];
        ncbp->ncb_lsn = (unsigned char) LSNs[idx_session];
        ncbp->ncb_command = NCBRECV | ASYNCH;
        ncbp->ncb_lana_num = lanas[idx_session];
#ifndef DJGPP
        ncbp->ncb_buffer = (unsigned char *) bufs[idx_NCB];
        ncbp->ncb_event = NCBevents[idx_NCB];
        ncbp->ncb_length = SMB_PACKETSIZE;
        Netbios(ncbp);
#else /* DJGPP */
        ncbp->ncb_buffer = bufs[idx_NCB]->dos_pkt;
        ((smb_ncb_t*)ncbp)->orig_pkt = bufs[idx_NCB];
        ncbp->ncb_event = NCBreturns[0][idx_NCB];
        ncbp->ncb_length = SMB_PACKETSIZE;
        dos_ncb = ((smb_ncb_t *)ncbp)->dos_ncb;
        Netbios(ncbp, dos_ncb);
#endif /* !DJGPP */
    }
}

/*
 * The top level loop for handling SMB request messages.  Each server thread
 * has its own NCB and buffer for sending replies (outncbp, outbufp), but the
 * NCB and buffer for the incoming request are loaned to us.
 *
 * Write Raw trickery starts here.  When we get a Write Raw, we are supposed
 * to immediately send a request for the rest of the data.  This must come
 * before any other traffic for that session, so we delay setting the session
 * event until that data has come in.
 */
void smb_Server(VOID *parmp)
{
    int myIdx = (int) parmp;
    NCB *ncbp;
    NCB *outncbp;
    smb_packet_t *bufp;
    smb_packet_t *outbufp;
    DWORD code, rcode;
    int idx_NCB, idx_session;
    UCHAR rc;
    smb_vc_t *vcp = NULL;
    smb_t *smbp;
#ifdef DJGPP
    dos_ptr dos_ncb;
#endif /* DJGPP */

    rx_StartClientThread();

    outncbp = GetNCB();
    outbufp = GetPacket();
    outbufp->ncbp = outncbp;

    while (1) {
        code = thrd_WaitForMultipleObjects_Event(numNCBs, NCBreturns[myIdx],
                                                 FALSE, INFINITE);

        /* terminate silently if shutdown flag is set */
        if (code == WAIT_OBJECT_0) {
            if (smbShutdownFlag == 1) {
                thrd_SetEvent(smb_ServerShutdown[myIdx]);
                break;
            } else
                continue;
        }

        /* error checking */
        if (code >= WAIT_ABANDONED_0 && code < (WAIT_ABANDONED_0 + numNCBs))
        {
            int abandonIdx = code - WAIT_ABANDONED_0;
            osi_Log3(smb_logp, "Error: smb_Server ( NCBreturns[%d] ) event %d abandoned, errno %d\n", myIdx, abandonIdx, GetLastError());
        }
	
        if (code == WAIT_IO_COMPLETION)
        {
            osi_Log1(smb_logp, "Error: smb_Server ( NCBreturns[%d] ) WAIT_IO_COMPLETION\n", myIdx);
            continue;
        }
	
        if (code == WAIT_TIMEOUT)
        {
            osi_Log2(smb_logp, "Error: smb_Server ( NCBreturns[%d] ) WAIT_TIMEOUT, errno %d\n", myIdx, GetLastError());
        }
	
        if (code == WAIT_FAILED)
        {
            osi_Log2(smb_logp, "Error: smb_Server ( NCBreturns[%d] ) WAIT_FAILED, errno %d\n", myIdx, GetLastError());
        }

        idx_NCB = code - WAIT_OBJECT_0;
        
        /* check idx range! */
        if (idx_NCB < 0 || idx_NCB > (sizeof(NCBs) / sizeof(NCBs[0])))
        {
            /* this is fatal - log as much as possible */
            osi_Log1(smb_logp, "Fatal: idx_NCB %d out of range.\n", idx_NCB);
            osi_assert(0);
        }

        ncbp = NCBs[idx_NCB];
#ifdef DJGPP
        dos_ncb = ((smb_ncb_t *)ncbp)->dos_ncb;
#endif /* DJGPP */
        idx_session = NCBsessions[idx_NCB];
        rc = ncbp->ncb_retcode;

        if (rc != NRC_PENDING && rc != NRC_GOODRET) {
            switch (rc) {
            case 0x01:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: illegal buffer length", ncbp->ncb_lsn, idx_session);
                break;
            case 0x03:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: illegal command", ncbp->ncb_lsn, idx_session);
                break;
            case 0x05:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: command timed out", ncbp->ncb_lsn, idx_session);
                break;
            case 0x06:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: message incomplete, issue another command", ncbp->ncb_lsn, idx_session);
                break;
            case 0x07:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: illegal buffer address", ncbp->ncb_lsn, idx_session);
                break;
            case 0x08:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: session number out of range", ncbp->ncb_lsn, idx_session);
                break;
            case 0x09:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: no resource available", ncbp->ncb_lsn, idx_session);
                break;
            case 0x0a:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: session closed", ncbp->ncb_lsn, idx_session);
                break;
            case 0x0b:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: command cancelled", ncbp->ncb_lsn, idx_session);
                break;
            case 0x0d:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: duplicate name", ncbp->ncb_lsn, idx_session);
                break;
            case 0x0e:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: name table full", ncbp->ncb_lsn, idx_session);
                break;
            case 0x0f:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: no deletions, name has active lsn %d sessions", ncbp->ncb_lsn, idx_session);
                break;
            case 0x11:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: local session table full", ncbp->ncb_lsn, idx_session);
                break;
            case 0x12:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: remote session table full", ncbp->ncb_lsn, idx_session);
                break;
            case 0x13:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: illegal name number", ncbp->ncb_lsn, idx_session);
                break;
            case 0x14:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: no callname", ncbp->ncb_lsn, idx_session);
                break;
            case 0x15:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: cannot put * in NCB_NAME", ncbp->ncb_lsn, idx_session);
                break;
            case 0x16:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: name in use on remote adapter", ncbp->ncb_lsn, idx_session);
                break;
            case 0x17:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: name deleted", ncbp->ncb_lsn, idx_session);
                break;
            case 0x18:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: session ended abnormally", ncbp->ncb_lsn, idx_session);
                break;
            case 0x19:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: name conflict detected", ncbp->ncb_lsn, idx_session);
                break;
            case 0x21:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: interface busy, IRET before retrying", ncbp->ncb_lsn, idx_session);
                break;
            case 0x22:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: too many commands outstanding, retry later", ncbp->ncb_lsn, idx_session);
                break;
            case 0x23:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: ncb_lana_num field invalid", ncbp->ncb_lsn, idx_session);
                break;
            case 0x24:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: command completed while cancel occurring", ncbp->ncb_lsn, idx_session);
                break;
            case 0x26:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: command not valid to cancel", ncbp->ncb_lsn, idx_session);
                break;
            case 0x30:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: name defined by anther local process", ncbp->ncb_lsn, idx_session);
                break;
            case 0x34:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: environment undefined. RESET required", ncbp->ncb_lsn, idx_session);
                break;
            case 0x35:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: required OS resources exhausted", ncbp->ncb_lsn, idx_session);
                break;
            case 0x36:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: max number of applications exceeded", ncbp->ncb_lsn, idx_session);
                break;
            case 0x37:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: no saps available for netbios", ncbp->ncb_lsn, idx_session);
                break;
            case 0x38:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: requested resources are not available", ncbp->ncb_lsn, idx_session);
                break;
            case 0x39:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: invalid ncb address or length > segment", ncbp->ncb_lsn, idx_session);
                break;
            case 0x3B:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: invalid NCB DDID", ncbp->ncb_lsn, idx_session);
                break;
            case 0x3C:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: lock of user area failed", ncbp->ncb_lsn, idx_session);
                break;
            case 0x3f:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: NETBIOS not loaded", ncbp->ncb_lsn, idx_session);
                break;
            case 0x40:
                osi_Log2(smb_logp, "NCBRECV failure lsn %d session %d: system error", ncbp->ncb_lsn, idx_session);
                break;
            default:
                osi_Log3(smb_logp, "NCBRECV failure lsn %d session %d code %d", ncbp->ncb_lsn, idx_session, rc);
                break;
            }
        }

        switch (rc) {
        case NRC_GOODRET: 
            break;

        case NRC_PENDING:
            /* Can this happen? Or is it just my UNIX paranoia? */
            osi_Log2(smb_logp, "NCBRECV pending lsn %d session %d", ncbp->ncb_lsn, idx_session);
            continue;

        case NRC_SCLOSED:
        case NRC_SNUMOUT:
            /* Client closed session */
            dead_sessions[idx_session] = TRUE;
            if (vcp)
                smb_ReleaseVC(vcp);
            vcp = smb_FindVC(ncbp->ncb_lsn, 0, lanas[idx_session]);
            /* Should also release vcp.  [done] 2004-05-11 jaltman
             * Also, should do
             * sanity check that all TID's are gone. 
             *
             * TODO: check if we could use LSNs[idx_session] instead, 
             * also cleanup after dead vcp 
             */
            if (vcp) {
                if (dead_vcp == vcp)
                    osi_Log1(smb_logp, "dead_vcp already set, 0x%x", dead_vcp);
                else if (!(vcp->flags & SMB_VCFLAG_ALREADYDEAD)) {
                    osi_Log2(smb_logp, "setting dead_vcp 0x%x, user struct 0x%x",
                             vcp, vcp->usersp);
                    smb_HoldVC(vcp);
                    if (dead_vcp) {
                        smb_ReleaseVC(dead_vcp);
                        osi_Log1(smb_logp,
                                  "Previous dead_vcp %x", dead_vcp);
                    }
                    dead_vcp = vcp;
                    vcp->flags |= SMB_VCFLAG_ALREADYDEAD;
                }
                if (vcp->justLoggedOut) {
                    loggedOut = 1;
                    loggedOutTime = vcp->logoffTime;
                    loggedOutName = strdup(vcp->justLoggedOut->unp->name);
                    loggedOutUserp = vcp->justLoggedOut;
                    lock_ObtainWrite(&smb_rctLock);
                    loggedOutUserp->refCount++;
                    lock_ReleaseWrite(&smb_rctLock);
                }
            }
            goto doneWithNCB;

        case NRC_INCOMP:
            /* Treat as transient error */
            {
#ifndef DJGPP
                EVENT_HANDLE h;
                char *ptbuf[1];
                char s[100];

                h = RegisterEventSource(NULL, AFS_DAEMON_EVENT_NAME);
                sprintf(s, "SMB message incomplete, length %d",
                         ncbp->ncb_length);
                ptbuf[0] = s;
                ReportEvent(h, EVENTLOG_WARNING_TYPE, 0,
                             1001, NULL, 1,
                             ncbp->ncb_length, ptbuf,
                             bufp);
                DeregisterEventSource(h);
#endif /* !DJGPP */
                osi_Log1(smb_logp,
                          "dispatch smb recv failed, message incomplete, ncb_length %d",
                          ncbp->ncb_length);
                osi_Log1(smb_logp,
                          "SMB message incomplete, "
                          "length %d", ncbp->ncb_length);

                /*
                 * We used to discard the packet.
                 * Instead, try handling it normally.
                 *
                 continue;
                 */
                break;
            }

        default:
            /* A weird error code.  Log it, sleep, and
            * continue. */
            if (vcp && vcp->errorCount++ > 3) {
                osi_Log2(smb_logp, "session [ %d ] closed, vcp->errorCount = %d", idx_session, vcp->errorCount);
                dead_sessions[idx_session] = TRUE;
            }
            else {
                thrd_Sleep(1000);
                thrd_SetEvent(SessionEvents[idx_session]);
            }
            continue;
        }

        /* Success, so now dispatch on all the data in the packet */

        smb_concurrentCalls++;
        if (smb_concurrentCalls > smb_maxObsConcurrentCalls)
            smb_maxObsConcurrentCalls = smb_concurrentCalls;

        if (vcp)
            smb_ReleaseVC(vcp);
        vcp = smb_FindVC(ncbp->ncb_lsn, 0, ncbp->ncb_lana_num);
        /*
         * If at this point vcp is NULL (implies that packet was invalid)
         * then we are in big trouble. This means either :
         *   a) we have the wrong NCB.
         *   b) Netbios screwed up the call.
         * Obviously this implies that 
         *   ( LSNs[idx_session] != ncbp->ncb_lsn ||
         *   lanas[idx_session] != ncbp->ncb_lana_num )
         * Either way, we can't do anything with this packet.
         * Log, sleep and resume.
         */
        if (!vcp) {
            HANDLE h;
            char buf[1000];
            char *ptbuf[1];

            sprintf(buf,
                     "Bad vcp!! : "
                     "LSNs[idx_session]=[%d],"
                     "lanas[idx_session]=[%d],"
                     "ncbp->ncb_lsn=[%d],"
                     "ncbp->ncb_lana_num=[%d]",
                     LSNs[idx_session],
                     lanas[idx_session],
                     ncbp->ncb_lsn,
                     ncbp->ncb_lana_num);

            ptbuf[0] = buf;

            h = RegisterEventSource(NULL,AFS_DAEMON_EVENT_NAME);
            if (h) {
                ReportEvent(h, EVENTLOG_ERROR_TYPE, 0, 1001, NULL,1,sizeof(*ncbp),ptbuf,(void*)ncbp);
                DeregisterEventSource(h);
            }

            /* Also log in the trace log. */
            osi_Log4(smb_logp, "Server: BAD VCP!"
                      "LSNs[idx_session]=[%d],"
                      "lanas[idx_session]=[%d],"
                      "ncbp->ncb_lsn=[%d],"
                      "ncbp->ncb_lana_num=[%d]",
                      LSNs[idx_session],
                      lanas[idx_session],
                      ncbp->ncb_lsn,
                      ncbp->ncb_lana_num);

            /* thrd_Sleep(1000); Don't bother sleeping */
            thrd_SetEvent(SessionEvents[idx_session]);
            smb_concurrentCalls--;
            continue;
        }


        vcp->errorCount = 0;
        bufp = (struct smb_packet *) ncbp->ncb_buffer;
#ifdef DJGPP
        bufp = ((smb_ncb_t *) ncbp)->orig_pkt;
        /* copy whole packet to virtual memory */
        /*fprintf(stderr, "smb_Server: copying dos packet at 0x%x, "
        "bufp=0x%x\n",
        bufp->dos_pkt / 16, bufp);*/
        fflush(stderr);
        dosmemget(bufp->dos_pkt, ncbp->ncb_length, bufp->data);
#endif /* DJGPP */
        smbp = (smb_t *)bufp->data;
        outbufp->flags = 0;

#if !defined(DJGPP) && !defined(AFS_WIN32_ENV)
        __try
        {
#endif
            if (smbp->com == 0x1d) {
                /* Special handling for Write Raw */
                raw_write_cont_t rwc;
                EVENT_HANDLE rwevent;
                char eventName[MAX_PATH];
            
                smb_DispatchPacket(vcp, bufp, outbufp, ncbp, &rwc);
                if (rwc.code == 0) {
                    rwevent = thrd_CreateEvent(NULL, FALSE, FALSE, TEXT("smb_Server() rwevent"));
                    if ( GetLastError() == ERROR_ALREADY_EXISTS )
                        osi_Log1(smb_logp, "Event Object Already Exists: %s", osi_LogSaveString(smb_logp, eventName));
                    ncbp->ncb_command = NCBRECV | ASYNCH;
                    ncbp->ncb_lsn = (unsigned char) vcp->lsn;
                    ncbp->ncb_lana_num = vcp->lana;
                    ncbp->ncb_buffer = rwc.buf;
                    ncbp->ncb_length = 65535;
                    ncbp->ncb_event = rwevent;
#ifndef DJGPP
                    Netbios(ncbp);
#else
                    Netbios(ncbp, dos_ncb);
#endif /* !DJGPP */
                    rcode = thrd_WaitForSingleObject_Event(rwevent, RAWTIMEOUT);
                    thrd_CloseHandle(rwevent);
                }
                thrd_SetEvent(SessionEvents[idx_session]);
                if (rwc.code == 0)
                    smb_CompleteWriteRaw(vcp, bufp, outbufp, ncbp, &rwc);
            } 
            else if (smbp->com == 0xa0) {
                /* 
                 * Serialize the handling for NT Transact 
                 * (defect 11626)
                 */
                smb_DispatchPacket(vcp, bufp, outbufp, ncbp, NULL);
                thrd_SetEvent(SessionEvents[idx_session]);
            } else {
                thrd_SetEvent(SessionEvents[idx_session]);
                /* TODO: what else needs to be serialized? */
                smb_DispatchPacket(vcp, bufp, outbufp, ncbp, NULL);
            }
#if !defined(DJGPP) && !defined(AFS_WIN95_ENV)
        }
        __except( smb_ServerExceptionFilter() ) {
        }
#endif

        smb_concurrentCalls--;

      doneWithNCB:
        thrd_SetEvent(NCBavails[idx_NCB]);
    }
    if (vcp)
        smb_ReleaseVC(vcp);
}

/*
 * Exception filter for the server threads.  If an exception occurs in the
 * dispatch routines, which is where exceptions are most common, then do a
 * force trace and give control to upstream exception handlers. Useful for
 * debugging.
 */
#if !defined(DJGPP) && !defined(AFS_WIN95_ENV)
DWORD smb_ServerExceptionFilter(void) {
    /* While this is not the best time to do a trace, if it succeeds, then
     * we have a trace (assuming tracing was enabled). Otherwise, this should
     * throw a second exception.
     */
    HANDLE h;
    char *ptbuf[1];

    ptbuf[0] = "Unhandled exception forcing trace";

    h = RegisterEventSource(NULL,AFS_DAEMON_EVENT_NAME);
    if(h) {
        ReportEvent(h, EVENTLOG_ERROR_TYPE, 0, 1001, NULL,1,0,ptbuf,NULL);
        DeregisterEventSource(h);
    }

    afsd_ForceTrace(TRUE);
    buf_ForceTrace(TRUE);
    return EXCEPTION_CONTINUE_SEARCH;
}       
#endif

/*
 * Create a new NCB and associated events, packet buffer, and "space" buffer.
 * If the number of server threads is M, and the number of live sessions is
 * N, then the number of NCB's in use at any time either waiting for, or
 * holding, received messages is M + N, so that is how many NCB's get created.
 */
void InitNCBslot(int idx)
{
    struct smb_packet *bufp;
    EVENT_HANDLE retHandle;
    int i;
    char eventName[MAX_PATH];

    osi_assert( idx < (sizeof(NCBs) / sizeof(NCBs[0])) );

    NCBs[idx] = GetNCB();
    sprintf(eventName,"NCBavails[%d]", idx);
    NCBavails[idx] = thrd_CreateEvent(NULL, FALSE, TRUE, eventName);
    if ( GetLastError() == ERROR_ALREADY_EXISTS )
        osi_Log1(smb_logp, "Event Object Already Exists: %s", osi_LogSaveString(smb_logp, eventName));
#ifndef DJGPP
    sprintf(eventName,"NCBevents[%d]", idx);
    NCBevents[idx] = thrd_CreateEvent(NULL, TRUE, FALSE, eventName);
    if ( GetLastError() == ERROR_ALREADY_EXISTS )
        osi_Log1(smb_logp, "Event Object Already Exists: %s", osi_LogSaveString(smb_logp, eventName));
#endif /* !DJGPP */
    sprintf(eventName,"NCBReturns[0<=i<smb_NumServerThreads][%d]", idx);
    retHandle = thrd_CreateEvent(NULL, FALSE, FALSE, eventName);
    if ( GetLastError() == ERROR_ALREADY_EXISTS )
        osi_Log1(smb_logp, "Event Object Already Exists: %s", osi_LogSaveString(smb_logp, eventName));
    for (i=0; i<smb_NumServerThreads; i++)
        NCBreturns[i][idx] = retHandle;
    bufp = GetPacket();
    bufp->spacep = cm_GetSpace();
    bufs[idx] = bufp;
}

/* listen for new connections */
void smb_Listener(void *parmp)
{
    NCB *ncbp;
    long code = 0;
    long len;
    long i, j;
    smb_vc_t *vcp = 0;
    int flags = 0;
    char rname[NCBNAMSZ+1];
    char cname[MAX_COMPUTERNAME_LENGTH+1];
    int cnamelen = MAX_COMPUTERNAME_LENGTH+1;
#ifdef DJGPP
    dos_ptr dos_ncb;
    time_t now;
#endif /* DJGPP */
    int lana = (int) parmp;

    ncbp = GetNCB();
#ifdef DJGPP
    dos_ncb = ((smb_ncb_t *)ncbp)->dos_ncb;
#endif /* DJGPP */

    /* retrieve computer name */
    GetComputerName(cname, &cnamelen);
    _strupr(cname);

    while (1) {
        memset(ncbp, 0, sizeof(NCB));
        flags = 0;

        ncbp->ncb_command = NCBLISTEN;
        ncbp->ncb_rto = 0;	/* No receive timeout */
        ncbp->ncb_sto = 0;	/* No send timeout */

        /* pad out with spaces instead of null termination */
        len = strlen(smb_localNamep);
        strncpy(ncbp->ncb_name, smb_localNamep, NCBNAMSZ);
        for (i=len; i<NCBNAMSZ; i++) ncbp->ncb_name[i] = ' ';
        
        strcpy(ncbp->ncb_callname, "*");
        for (i=1; i<NCBNAMSZ; i++) ncbp->ncb_callname[i] = ' ';
        
        ncbp->ncb_lana_num = lana;

#ifndef DJGPP
        code = Netbios(ncbp);
#else /* DJGPP */
        code = Netbios(ncbp, dos_ncb);
#endif

        if (code != 0)
        {
#ifndef DJGPP
            char tbuffer[256];
#endif

            /* terminate silently if shutdown flag is set */
            if (smbShutdownFlag == 1) {
#ifndef DJGPP
                ExitThread(1);
#else
                thrd_Exit(1);
#endif
            }

            osi_Log2(smb_logp, 
                     "NCBLISTEN lana=%d failed with code %d",
                     ncbp->ncb_lana_num, code);
            osi_Log0(smb_logp, 
                     "Client exiting due to network failure. Please restart client.\n");

#ifndef DJGPP
            sprintf(tbuffer, 
                     "Client exiting due to network failure.  Please restart client.\n"
                     "NCBLISTEN lana=%d failed with code %d",
                     ncbp->ncb_lana_num, code);
            if (showErrors)
                code = (*smb_MBfunc)(NULL, tbuffer, "AFS Client Service: Fatal Error",
                                      MB_OK|MB_SERVICE_NOTIFICATION);
            osi_assert(tbuffer);
            ExitThread(1);
#else
            fprintf(stderr, "NCBLISTEN lana=%d failed with code %d\n",
                     ncbp->ncb_lana_num, code);
            fprintf(stderr, "\nClient exiting due to network failure "
                     "(possibly due to power-saving mode)\n");
            fprintf(stderr, "Please restart client.\n");
            afs_exit(AFS_EXITCODE_NETWORK_FAILURE);
#endif /* !DJGPP */
        }

        /* check for remote conns */
        /* first get remote name and insert null terminator */
        memcpy(rname, ncbp->ncb_callname, NCBNAMSZ);
        for (i=NCBNAMSZ; i>0; i--) {
            if (rname[i-1] != ' ' && rname[i-1] != 0) {
                rname[i] = 0;
                break;
            }
        }

        /* compare with local name */
        if (!isGateway)
            if (strncmp(rname, cname, NCBNAMSZ) != 0)
                flags |= SMB_VCFLAG_REMOTECONN;

        osi_Log1(smb_logp, "New session lsn %d", ncbp->ncb_lsn);
        /* lock */
        lock_ObtainMutex(&smb_ListenerLock);

        /* New generation */
        sessionGen++;

        /* Log session startup */
#ifdef NOTSERVICE
        fprintf(stderr, "New session(ncb_lsn,ncb_lana_num) %d,%d starting from host "
                 "%s\n",
                 ncbp->ncb_lsn,ncbp->ncb_lana_num, rname);
#endif /* NOTSERVICE */
        osi_Log4(smb_logp, "New session(ncb_lsn,ncb_lana_num) (%d,%d) starting from host %s, %d ongoing ops",
                  ncbp->ncb_lsn,ncbp->ncb_lana_num, osi_LogSaveString(smb_logp, rname), ongoingOps);

        if (reportSessionStartups) {
#ifndef DJGPP
            HANDLE h;
            char *ptbuf[1];
            char s[100];

            h = RegisterEventSource(NULL, AFS_DAEMON_EVENT_NAME);
            sprintf(s, "SMB session startup, %d ongoing ops", ongoingOps);
            ptbuf[0] = s;
            ReportEvent(h, EVENTLOG_WARNING_TYPE, 0, 1004, NULL,
                         1, 0, ptbuf, NULL);
            DeregisterEventSource(h);
#else /* DJGPP */
            time(&now);
            fprintf(stderr, "%s: New session %d starting from host %s\n",
                    asctime(localtime(&now)), ncbp->ncb_lsn, rname);
            fflush(stderr);
#endif /* !DJGPP */
        }
        osi_Log1(smb_logp, "NCBLISTEN completed, call from %s", osi_LogSaveString(smb_logp, rname));
        osi_Log1(smb_logp, "SMB session startup, %d ongoing ops",
                  ongoingOps);

        /* now ncbp->ncb_lsn is the connection ID */
        vcp = smb_FindVC(ncbp->ncb_lsn, SMB_FLAG_CREATE, ncbp->ncb_lana_num);
        vcp->flags |= flags;
        strcpy(vcp->rname, rname);

        /* Allocate slot in session arrays */
        /* Re-use dead session if possible, otherwise add one more */
        /* But don't look at session[0], it is reserved */
        for (i = 1; i < numSessions; i++) {
            if (dead_sessions[i]) {
                osi_Log1(smb_logp, "connecting to dead session [ %d ]", i);
                dead_sessions[i] = FALSE;
                break;
            }
        }

        if (i >= Sessionmax - 1  || numNCBs >= NCBmax - 1) {
            unsigned long code = CM_ERROR_ALLBUSY;
            smb_packet_t * outp = GetPacket();
            unsigned char *outWctp;
            smb_t *smbp;
            
            outp->ncbp = ncbp;

            if (vcp->flags & SMB_VCFLAG_STATUS32) {
                unsigned long NTStatus;
                smb_MapNTError(code, &NTStatus);
                outWctp = outp->wctp;
                smbp = (smb_t *) &outp->data;
                *outWctp++ = 0;
                *outWctp++ = 0;
                *outWctp++ = 0;
                smbp->rcls = (unsigned char) (NTStatus & 0xff);
                smbp->reh = (unsigned char) ((NTStatus >> 8) & 0xff);
                smbp->errLow = (unsigned char) ((NTStatus >> 16) & 0xff);
                smbp->errHigh = (unsigned char) ((NTStatus >> 24) & 0xff);
                smbp->flg2 |= SMB_FLAGS2_32BIT_STATUS;
            } else {
                unsigned short errCode;
                unsigned char errClass;
                smb_MapCoreError(code, vcp, &errCode, &errClass);
                outWctp = outp->wctp;
                smbp = (smb_t *) &outp->data;
                *outWctp++ = 0;
                *outWctp++ = 0;
                *outWctp++ = 0;
                smbp->errLow = (unsigned char) (errCode & 0xff);
                smbp->errHigh = (unsigned char) ((errCode >> 8) & 0xff);
                smbp->rcls = errClass;
            }
            smb_SendPacket(vcp, outp);
            smb_FreePacket(outp);
        } else {
            /* assert that we do not exceed the maximum number of sessions or NCBs.
            * we should probably want to wait for a session to be freed in case
            * we run out.
            */
            osi_assert(i < Sessionmax - 1);
            osi_assert(numNCBs < NCBmax - 1);   /* if we pass this test we can allocate one more */

            LSNs[i] = ncbp->ncb_lsn;
            lanas[i] = ncbp->ncb_lana_num;
		
            if (i == numSessions) {
                /* Add new NCB for new session */
                char eventName[MAX_PATH];

                osi_Log1(smb_logp, "smb_Listener creating new session %d", i);

                InitNCBslot(numNCBs);
                numNCBs++;
                thrd_SetEvent(NCBavails[0]);
                thrd_SetEvent(NCBevents[0]);
                for (j = 0; j < smb_NumServerThreads; j++)
                    thrd_SetEvent(NCBreturns[j][0]);
                /* Also add new session event */
                sprintf(eventName, "SessionEvents[%d]", i);
                SessionEvents[i] = thrd_CreateEvent(NULL, FALSE, TRUE, eventName);
                if ( GetLastError() == ERROR_ALREADY_EXISTS )
                    osi_Log1(smb_logp, "Event Object Already Exists: %s", osi_LogSaveString(smb_logp, eventName));
                numSessions++;
                osi_Log2(smb_logp, "increasing numNCBs [ %d ] numSessions [ %d ]", numNCBs, numSessions);
                thrd_SetEvent(SessionEvents[0]);
            } else {
                thrd_SetEvent(SessionEvents[i]);
            }
        }
        
        smb_ReleaseVC(vcp);

        /* unlock */
        lock_ReleaseMutex(&smb_ListenerLock);
    }	/* dispatch while loop */
}

/* initialize Netbios */
void smb_NetbiosInit()
{
    NCB *ncbp;
#ifdef DJGPP
    dos_ptr dos_ncb;
#endif /* DJGPP */
    int i, lana, code, l;
    char s[100];
    int delname_tried=0;
    int len;
    int lana_found = 0;
    OSVERSIONINFO Version;

    /* Get the version of Windows */
    memset(&Version, 0x00, sizeof(Version));
    Version.dwOSVersionInfoSize = sizeof(Version);
    GetVersionEx(&Version);

    /* setup the NCB system */
    ncbp = GetNCB();
#ifdef DJGPP
    dos_ncb = ((smb_ncb_t *)ncbp)->dos_ncb;
#endif /* DJGPP */

#ifndef DJGPP
    if (smb_LANadapter == -1) {
        ncbp->ncb_command = NCBENUM;
        ncbp->ncb_buffer = (PUCHAR)&lana_list;
        ncbp->ncb_length = sizeof(lana_list);
        code = Netbios(ncbp);
        if (code != 0) {
            afsi_log("Netbios NCBENUM error code %d", code);
            osi_panic(s, __FILE__, __LINE__);
        }
    }
    else {
        lana_list.length = 1;
        lana_list.lana[0] = smb_LANadapter;
    }
	  
    for (i = 0; i < lana_list.length; i++) {
        /* reset the adaptor: in Win32, this is required for every process, and
         * acts as an init call, not as a real hardware reset.
         */
        ncbp->ncb_command = NCBRESET;
        ncbp->ncb_callname[0] = 100;
        ncbp->ncb_callname[2] = 100;
        ncbp->ncb_lana_num = lana_list.lana[i];
        code = Netbios(ncbp);
        if (code == 0) 
            code = ncbp->ncb_retcode;
        if (code != 0) {
            afsi_log("Netbios NCBRESET lana %d error code %d", lana_list.lana[i], code);
            lana_list.lana[i] = 255;  /* invalid lana */
        } else {
            afsi_log("Netbios NCBRESET lana %d succeeded", lana_list.lana[i]);
        }
    }
#else
    /* for DJGPP, there is no NCBENUM and NCBRESET is a real reset.  so
       we will just fake the LANA list */
    if (smb_LANadapter == -1) {
        for (i = 0; i < 8; i++)
	    lana_list.lana[i] = i;
        lana_list.length = 8;
    }
    else {
        lana_list.length = 1;
        lana_list.lana[0] = smb_LANadapter;
    }
#endif /* !DJGPP */

    /* and declare our name so we can receive connections */
    memset(ncbp, 0, sizeof(*ncbp));
    len=lstrlen(smb_localNamep);
    memset(smb_sharename,' ',NCBNAMSZ);
    memcpy(smb_sharename,smb_localNamep,len);
    afsi_log("lana_list.length %d", lana_list.length);

    /* Keep the name so we can unregister it later */
    for (l = 0; l < lana_list.length; l++) {
        lana = lana_list.lana[l];

        ncbp->ncb_command = NCBADDNAME;
        ncbp->ncb_lana_num = lana;
        memcpy(ncbp->ncb_name,smb_sharename,NCBNAMSZ);
#ifndef DJGPP
        code = Netbios(ncbp);
#else /* DJGPP */
        code = Netbios(ncbp, dos_ncb);
#endif /* !DJGPP */
          
        afsi_log("Netbios NCBADDNAME lana=%d code=%d retcode=%d complete=%d",
                 lana, code, ncbp->ncb_retcode, ncbp->ncb_cmd_cplt);
        {
            char name[NCBNAMSZ+1];
            name[NCBNAMSZ]=0;
            memcpy(name,ncbp->ncb_name,NCBNAMSZ);
            afsi_log("Netbios NCBADDNAME added new name >%s<",name);
        }

        if (code == 0) code = ncbp->ncb_retcode;
        if (code == 0) {
            afsi_log("Netbios NCBADDNAME succeeded on lana %d\n", lana);
#ifdef DJGPP
            /* we only use one LANA with djgpp */
            lana_list.lana[0] = lana;
            lana_list.length = 1;
#endif	  
        }
        else {
            afsi_log("Netbios NCBADDNAME lana %d error code %d", lana, code);
            if (code == NRC_BRIDGE) {    /* invalid LANA num */
                lana_list.lana[l] = 255;
                continue;
            }
            else if (code == NRC_DUPNAME) {
                afsi_log("Name already exists; try to delete it");
                memset(ncbp, 0, sizeof(*ncbp));
                ncbp->ncb_command = NCBDELNAME;
                memcpy(ncbp->ncb_name,smb_sharename,NCBNAMSZ);
                ncbp->ncb_lana_num = lana;
#ifndef DJGPP
                code = Netbios(ncbp);
#else
                code = Netbios(ncbp, dos_ncb);
#endif /* DJGPP */
                if (code == 0) 
                    code = ncbp->ncb_retcode;
                else {
                    afsi_log("Netbios NCBDELNAME lana %d error code %d\n", lana, code);
                }
                if (code != 0 || delname_tried) {
                    lana_list.lana[l] = 255;
                }
                else if (code == 0) {
                    if (!delname_tried) {
                        lana--;
                        delname_tried = 1;
                        continue;
                    }
                }
            }
            else {
                afsi_log("Netbios NCBADDNAME lana %d error code %d", lana, code);
                lana_list.lana[l] = 255;  /* invalid lana */
                osi_panic(s, __FILE__, __LINE__);
            }
        }
        if (code == 0) {
            lana_found = 1;   /* at least one worked */
#ifdef DJGPP
            break;
#endif
        }
    }

    osi_assert(lana_list.length >= 0);
    if (!lana_found) {
        osi_panic("No valid LANA numbers found!", __FILE__, __LINE__);
    }
        
    /* we're done with the NCB now */
    FreeNCB(ncbp);
}

void smb_Init(osi_log_t *logp, char *snamep, int useV3, int LANadapt,
              int nThreads
#ifndef DJGPP
              , void *aMBfunc
#endif
  )

{
    thread_t phandle;
    int lpid;
    int i;
    int len;
    struct tm myTime;
#ifdef DJGPP
    int npar, seg, sel;
    dos_ptr rawBuf;
#endif /* DJGPP */
    EVENT_HANDLE retHandle;
    char eventName[MAX_PATH];

#ifndef DJGPP
    smb_MBfunc = aMBfunc;
#endif /* DJGPP */

    smb_useV3 = useV3;
    smb_LANadapter = LANadapt;

    /* Initialize smb_localZero */
    myTime.tm_isdst = -1;		/* compute whether on DST or not */
    myTime.tm_year = 70;
    myTime.tm_mon = 0;
    myTime.tm_mday = 1;
    myTime.tm_hour = 0;
    myTime.tm_min = 0;
    myTime.tm_sec = 0;
    smb_localZero = mktime(&myTime);

#ifndef USE_NUMERIC_TIME_CONV
    /* Initialize kludge-GMT */
    smb_CalculateNowTZ();
#endif /* USE_NUMERIC_TIME_CONV */
#ifdef AFS_FREELANCE_CLIENT
    /* Make sure the root.afs volume has the correct time */
    cm_noteLocalMountPointChange();
#endif

    /* initialize the remote debugging log */
    smb_logp = logp;
        
    /* remember the name */
    len = strlen(snamep);
    smb_localNamep = malloc(len+1);
    strcpy(smb_localNamep, snamep);
    afsi_log("smb_localNamep is >%s<", smb_localNamep);

    /* and the global lock */
    lock_InitializeRWLock(&smb_globalLock, "smb global lock");
    lock_InitializeRWLock(&smb_rctLock, "smb refct and tree struct lock");

    /* Raw I/O data structures */
    lock_InitializeMutex(&smb_RawBufLock, "smb raw buffer lock");

    lock_InitializeMutex(&smb_ListenerLock, "smb listener lock");
	
    /* 4 Raw I/O buffers */
#ifndef DJGPP
    smb_RawBufs = calloc(65536,1);
    *((char **)smb_RawBufs) = NULL;
    for (i=0; i<3; i++) {
        char *rawBuf = calloc(65536,1);
        *((char **)rawBuf) = smb_RawBufs;
        smb_RawBufs = rawBuf;
    }
#else /* DJGPP */
    npar = 65536 >> 4;  /* number of paragraphs */
    seg = __dpmi_allocate_dos_memory(npar, &smb_RawBufSel[0]);
    if (seg == -1) {
        afsi_log("Cannot allocate %d paragraphs of DOS memory",
                  npar);
        osi_panic("",__FILE__,__LINE__);
    }
    else {
        afsi_log("Allocated %d paragraphs of DOS mem at 0x%X",
                  npar, seg);
    }
    smb_RawBufs = (seg * 16) + 0;  /* DOS physical address */
        
    _farpokel(_dos_ds, smb_RawBufs, NULL);
    for (i=0; i<SMB_RAW_BUFS-1; i++) {
        npar = 65536 >> 4;  /* number of paragraphs */
        seg = __dpmi_allocate_dos_memory(npar, &smb_RawBufSel[i+1]);
        if (seg == -1) {
            afsi_log("Cannot allocate %d paragraphs of DOS memory",
                      npar);
            osi_panic("",__FILE__,__LINE__);
        }
        else {
            afsi_log("Allocated %d paragraphs of DOS mem at 0x%X",
                      npar, seg);
        }
        rawBuf = (seg * 16) + 0;  /* DOS physical address */
        /*_farpokel(_dos_ds, smb_RawBufs, smb_RawBufs);*/
        _farpokel(_dos_ds, rawBuf, smb_RawBufs);
        smb_RawBufs = rawBuf;
    }
#endif /* !DJGPP */

    /* global free lists */
    smb_ncbFreeListp = NULL;
    smb_packetFreeListp = NULL;

    smb_NetbiosInit();

    /* Initialize listener and server structures */
    numVCs = 0;
    memset(dead_sessions, 0, sizeof(dead_sessions));
    sprintf(eventName, "SessionEvents[0]");
    SessionEvents[0] = thrd_CreateEvent(NULL, FALSE, FALSE, eventName);
    if ( GetLastError() == ERROR_ALREADY_EXISTS )
        afsi_log("Event Object Already Exists: %s", eventName);
    numSessions = 1;
    smb_NumServerThreads = nThreads;
    sprintf(eventName, "NCBavails[0]");
    NCBavails[0] = thrd_CreateEvent(NULL, FALSE, FALSE, eventName);
    if ( GetLastError() == ERROR_ALREADY_EXISTS )
        afsi_log("Event Object Already Exists: %s", eventName);
    sprintf(eventName, "NCBevents[0]");
    NCBevents[0] = thrd_CreateEvent(NULL, FALSE, FALSE, eventName);
    if ( GetLastError() == ERROR_ALREADY_EXISTS )
        afsi_log("Event Object Already Exists: %s", eventName);
    NCBreturns = malloc(smb_NumServerThreads * sizeof(EVENT_HANDLE *));
    sprintf(eventName, "NCBreturns[0<=i<smb_NumServerThreads][0]");
    retHandle = thrd_CreateEvent(NULL, FALSE, FALSE, eventName);
    if ( GetLastError() == ERROR_ALREADY_EXISTS )
        afsi_log("Event Object Already Exists: %s", eventName);
    for (i = 0; i < smb_NumServerThreads; i++) {
        NCBreturns[i] = malloc(NCBmax * sizeof(EVENT_HANDLE));
        NCBreturns[i][0] = retHandle;
    }

    smb_ServerShutdown = malloc(smb_NumServerThreads * sizeof(EVENT_HANDLE));
    for (i = 0; i < smb_NumServerThreads; i++) {
        sprintf(eventName, "smb_ServerShutdown[%d]", i);
        smb_ServerShutdown[i] = thrd_CreateEvent(NULL, FALSE, FALSE, eventName);
        if ( GetLastError() == ERROR_ALREADY_EXISTS )
            afsi_log("Event Object Already Exists: %s", eventName);
        InitNCBslot(i+1);
    }
    numNCBs = smb_NumServerThreads + 1;

    /* Initialize dispatch table */
    memset(&smb_dispatchTable, 0, sizeof(smb_dispatchTable));
    /* Prepare the table for unknown operations */
    for(i=0; i<= SMB_NOPCODES; i++) {
        smb_dispatchTable[i].procp = smb_SendCoreBadOp;
    }
    /* Fill in the ones we do know */
    smb_dispatchTable[0x00].procp = smb_ReceiveCoreMakeDir;
    smb_dispatchTable[0x01].procp = smb_ReceiveCoreRemoveDir;
    smb_dispatchTable[0x02].procp = smb_ReceiveCoreOpen;
    smb_dispatchTable[0x03].procp = smb_ReceiveCoreCreate;
    smb_dispatchTable[0x04].procp = smb_ReceiveCoreClose;
    smb_dispatchTable[0x05].procp = smb_ReceiveCoreFlush;
    smb_dispatchTable[0x06].procp = smb_ReceiveCoreUnlink;
    smb_dispatchTable[0x07].procp = smb_ReceiveCoreRename;
    smb_dispatchTable[0x08].procp = smb_ReceiveCoreGetFileAttributes;
    smb_dispatchTable[0x09].procp = smb_ReceiveCoreSetFileAttributes;
    smb_dispatchTable[0x0a].procp = smb_ReceiveCoreRead;
    smb_dispatchTable[0x0b].procp = smb_ReceiveCoreWrite;
    smb_dispatchTable[0x0c].procp = smb_ReceiveCoreLockRecord;
    smb_dispatchTable[0x0d].procp = smb_ReceiveCoreUnlockRecord;
    smb_dispatchTable[0x0e].procp = smb_SendCoreBadOp; /* create temporary */
    smb_dispatchTable[0x0f].procp = smb_ReceiveCoreCreate;
    smb_dispatchTable[0x10].procp = smb_ReceiveCoreCheckPath;
    smb_dispatchTable[0x11].procp = smb_SendCoreBadOp;	/* process exit */
    smb_dispatchTable[0x12].procp = smb_ReceiveCoreSeek;
    smb_dispatchTable[0x1a].procp = smb_ReceiveCoreReadRaw;
    /* Set NORESPONSE because smb_ReceiveCoreReadRaw() does the responses itself */
    smb_dispatchTable[0x1a].flags |= SMB_DISPATCHFLAG_NORESPONSE;
    smb_dispatchTable[0x1d].procp = smb_ReceiveCoreWriteRawDummy;
    smb_dispatchTable[0x22].procp = smb_ReceiveV3SetAttributes;
    smb_dispatchTable[0x23].procp = smb_ReceiveV3GetAttributes;
    smb_dispatchTable[0x24].procp = smb_ReceiveV3LockingX;
    smb_dispatchTable[0x24].flags |= SMB_DISPATCHFLAG_CHAINED;
    smb_dispatchTable[0x25].procp = smb_ReceiveV3Trans;
    smb_dispatchTable[0x25].flags |= SMB_DISPATCHFLAG_NORESPONSE;
    smb_dispatchTable[0x26].procp = smb_ReceiveV3Trans;
    smb_dispatchTable[0x26].flags |= SMB_DISPATCHFLAG_NORESPONSE;
    smb_dispatchTable[0x29].procp = smb_SendCoreBadOp;	/* copy file */
    smb_dispatchTable[0x2b].procp = smb_ReceiveCoreEcho;
    /* Set NORESPONSE because smb_ReceiveCoreEcho() does the responses itself */
    smb_dispatchTable[0x2b].flags |= SMB_DISPATCHFLAG_NORESPONSE;
    smb_dispatchTable[0x2d].procp = smb_ReceiveV3OpenX;
    smb_dispatchTable[0x2d].flags |= SMB_DISPATCHFLAG_CHAINED;
    smb_dispatchTable[0x2e].procp = smb_ReceiveV3ReadX;
    smb_dispatchTable[0x2e].flags |= SMB_DISPATCHFLAG_CHAINED;
    smb_dispatchTable[0x32].procp = smb_ReceiveV3Tran2A;	/* both are same */
    smb_dispatchTable[0x32].flags |= SMB_DISPATCHFLAG_NORESPONSE;
    smb_dispatchTable[0x33].procp = smb_ReceiveV3Tran2A;
    smb_dispatchTable[0x33].flags |= SMB_DISPATCHFLAG_NORESPONSE;
    smb_dispatchTable[0x34].procp = smb_ReceiveV3FindClose;
    smb_dispatchTable[0x35].procp = smb_ReceiveV3FindNotifyClose;
    smb_dispatchTable[0x70].procp = smb_ReceiveCoreTreeConnect;
    smb_dispatchTable[0x71].procp = smb_ReceiveCoreTreeDisconnect;
    smb_dispatchTable[0x72].procp = smb_ReceiveNegotiate;
    smb_dispatchTable[0x73].procp = smb_ReceiveV3SessionSetupX;
    smb_dispatchTable[0x73].flags |= SMB_DISPATCHFLAG_CHAINED;
    smb_dispatchTable[0x74].procp = smb_ReceiveV3UserLogoffX;
    smb_dispatchTable[0x74].flags |= SMB_DISPATCHFLAG_CHAINED;
    smb_dispatchTable[0x75].procp = smb_ReceiveV3TreeConnectX;
    smb_dispatchTable[0x75].flags |= SMB_DISPATCHFLAG_CHAINED;
    smb_dispatchTable[0x80].procp = smb_ReceiveCoreGetDiskAttributes;
    smb_dispatchTable[0x81].procp = smb_ReceiveCoreSearchDir;
    smb_dispatchTable[0x82].procp = smb_SendCoreBadOp; /* Find */
    smb_dispatchTable[0x83].procp = smb_SendCoreBadOp; /* Find Unique */
    smb_dispatchTable[0x84].procp = smb_SendCoreBadOp; /* Find Close */
    smb_dispatchTable[0xA0].procp = smb_ReceiveNTTransact;
    smb_dispatchTable[0xA2].procp = smb_ReceiveNTCreateX;
    smb_dispatchTable[0xA2].flags |= SMB_DISPATCHFLAG_CHAINED;
    smb_dispatchTable[0xA4].procp = smb_ReceiveNTCancel;
    smb_dispatchTable[0xA4].flags |= SMB_DISPATCHFLAG_NORESPONSE;
    smb_dispatchTable[0xA5].procp = smb_ReceiveNTRename;
    smb_dispatchTable[0xc0].procp = smb_SendCoreBadOp;  /* Open Print File */
    smb_dispatchTable[0xc1].procp = smb_SendCoreBadOp;  /* Write Print File */
    smb_dispatchTable[0xc2].procp = smb_SendCoreBadOp;  /* Close Print File */
    smb_dispatchTable[0xc3].procp = smb_SendCoreBadOp;  /* Get Print Queue */
    smb_dispatchTable[0xD8].procp = smb_SendCoreBadOp;  /* Read Bulk */
    smb_dispatchTable[0xD9].procp = smb_SendCoreBadOp;  /* Write Bulk */
    smb_dispatchTable[0xDA].procp = smb_SendCoreBadOp;  /* Write Bulk Data */

    /* setup tran 2 dispatch table */
    smb_tran2DispatchTable[0].procp = smb_ReceiveTran2Open;
    smb_tran2DispatchTable[1].procp = smb_ReceiveTran2SearchDir;	/* FindFirst */
    smb_tran2DispatchTable[2].procp = smb_ReceiveTran2SearchDir;	/* FindNext */
    smb_tran2DispatchTable[3].procp = smb_ReceiveTran2QFSInfo;
    smb_tran2DispatchTable[4].procp = smb_ReceiveTran2SetFSInfo;
    smb_tran2DispatchTable[5].procp = smb_ReceiveTran2QPathInfo;
    smb_tran2DispatchTable[6].procp = smb_ReceiveTran2SetPathInfo;
    smb_tran2DispatchTable[7].procp = smb_ReceiveTran2QFileInfo;
    smb_tran2DispatchTable[8].procp = smb_ReceiveTran2SetFileInfo;
    smb_tran2DispatchTable[9].procp = smb_ReceiveTran2FSCTL;
    smb_tran2DispatchTable[10].procp = smb_ReceiveTran2IOCTL;
    smb_tran2DispatchTable[11].procp = smb_ReceiveTran2FindNotifyFirst;
    smb_tran2DispatchTable[12].procp = smb_ReceiveTran2FindNotifyNext;
    smb_tran2DispatchTable[13].procp = smb_ReceiveTran2CreateDirectory;
    smb_tran2DispatchTable[14].procp = smb_ReceiveTran2SessionSetup;
    smb_tran2DispatchTable[16].procp = smb_ReceiveTran2GetDFSReferral;
    smb_tran2DispatchTable[17].procp = smb_ReceiveTran2ReportDFSInconsistency;

    /* setup the rap dispatch table */
    memset(smb_rapDispatchTable, 0, sizeof(smb_rapDispatchTable));
    smb_rapDispatchTable[0].procp = smb_ReceiveRAPNetShareEnum;
    smb_rapDispatchTable[1].procp = smb_ReceiveRAPNetShareGetInfo;
    smb_rapDispatchTable[63].procp = smb_ReceiveRAPNetWkstaGetInfo;
    smb_rapDispatchTable[13].procp = smb_ReceiveRAPNetServerGetInfo;

    smb3_Init();

    /* if we are doing SMB authentication we have register outselves as a logon process */
    if (smb_authType != SMB_AUTH_NONE) {
        NTSTATUS nts = STATUS_UNSUCCESSFUL, ntsEx = STATUS_UNSUCCESSFUL;
        LSA_STRING afsProcessName;
        LSA_OPERATIONAL_MODE dummy; /*junk*/

        afsProcessName.Buffer = "OpenAFSClientDaemon";
        afsProcessName.Length = strlen(afsProcessName.Buffer);
        afsProcessName.MaximumLength = afsProcessName.Length + 1;

        nts = LsaRegisterLogonProcess(&afsProcessName, &smb_lsaHandle, &dummy);

        if (nts == STATUS_SUCCESS) {
            LSA_STRING packageName;
            /* we are registered. Find out the security package id */
            packageName.Buffer = MSV1_0_PACKAGE_NAME;
            packageName.Length = strlen(packageName.Buffer);
            packageName.MaximumLength = packageName.Length + 1;
            nts = LsaLookupAuthenticationPackage(smb_lsaHandle, &packageName , &smb_lsaSecPackage);
            if (nts == STATUS_SUCCESS) {
                /* BEGIN 
                 * This code forces Windows to authenticate against the Logon Cache 
                 * first instead of attempting to authenticate against the Domain 
                 * Controller.  When the Windows logon cache is enabled this improves
                 * performance by removing the network access and works around a bug
                 * seen at sites which are using a MIT Kerberos principal to login
                 * to machines joined to a non-root domain in a multi-domain forest.
                 */
                PVOID pResponse = NULL;
                ULONG cbResponse = 0;
                MSV1_0_SETPROCESSOPTION_REQUEST OptionsRequest;

                RtlZeroMemory(&OptionsRequest, sizeof(OptionsRequest));
                OptionsRequest.MessageType = (MSV1_0_PROTOCOL_MESSAGE_TYPE) MsV1_0SetProcessOption;
                OptionsRequest.ProcessOptions = MSV1_0_OPTION_TRY_CACHE_FIRST; 
                OptionsRequest.DisableOptions = FALSE;

                nts = LsaCallAuthenticationPackage( smb_lsaHandle,
                                                    smb_lsaSecPackage,
                                                    &OptionsRequest,
                                                    sizeof(OptionsRequest),
                                                    &pResponse,
                                                    &cbResponse,
                                                    &ntsEx
                                                    );

                if (nts != STATUS_SUCCESS && ntsEx != STATUS_SUCCESS) {
                    char message[256];
                    sprintf(message,"MsV1_0SetProcessOption failure: nts 0x%x ntsEx 0x%x",
                                       nts, ntsEx);
                    OutputDebugString(message);
                    afsi_log(message);
                } else {
                    OutputDebugString("MsV1_0SetProcessOption success");
                    afsi_log("MsV1_0SetProcessOption success");
                }
                /* END - code from Larry */

                smb_lsaLogonOrigin.Buffer = "OpenAFS";
                smb_lsaLogonOrigin.Length = strlen(smb_lsaLogonOrigin.Buffer);
                smb_lsaLogonOrigin.MaximumLength = smb_lsaLogonOrigin.Length + 1;
            } else {
                afsi_log("Can't determine security package name for NTLM!! NTSTATUS=[%l]",nts);

                /* something went wrong. We report the error and revert back to no authentication
                because we can't perform any auth requests without a successful lsa handle
                or sec package id. */
                afsi_log("Reverting to NO SMB AUTH");
                smb_authType = SMB_AUTH_NONE;
            }
        } else {
            afsi_log("Can't register logon process!! NTSTATUS=[%l]",nts);

            /* something went wrong. We report the error and revert back to no authentication
            because we can't perform any auth requests without a successful lsa handle
            or sec package id. */
            afsi_log("Reverting to NO SMB AUTH");
            smb_authType = SMB_AUTH_NONE;
        }

#ifdef COMMENT
        /* Don't fallback to SMB_AUTH_NTLM.  Apparently, allowing SPNEGO to be used each
         * time prevents the failure of authentication when logged into Windows with an
         * external Kerberos principal mapped to a local account.
         */
        else if ( smb_authType == SMB_AUTH_EXTENDED) {
            /* Test to see if there is anything to negotiate.  If SPNEGO is not going to be used 
             * then the only option is NTLMSSP anyway; so just fallback. 
             */
            void * secBlob;
            int secBlobLength;

            smb_NegotiateExtendedSecurity(&secBlob, &secBlobLength);
            if (secBlobLength == 0) {
                smb_authType = SMB_AUTH_NTLM;
                afsi_log("Reverting to SMB AUTH NTLM");
            } else
                free(secBlob);
        }
#endif
    }

    {
        DWORD bufsize;
        /* Now get ourselves a domain name. */
        /* For now we are using the local computer name as the domain name.
         * It is actually the domain for local logins, and we are acting as
         * a local SMB server. 
         */
        bufsize = sizeof(smb_ServerDomainName) - 1;
        GetComputerName(smb_ServerDomainName, &bufsize);
        smb_ServerDomainNameLength = bufsize + 1; /* bufsize doesn't include terminator */
        afsi_log("Setting SMB server domain name to [%s]", smb_ServerDomainName);
    }

    /* Start listeners, waiters, servers, and daemons */

    for (i = 0; i < lana_list.length; i++) {
        if (lana_list.lana[i] == 255) 
            continue;
        phandle = thrd_Create(NULL, 65536, (ThreadFunc) smb_Listener,
                               (void*)lana_list.lana[i], 0, &lpid, "smb_Listener");
        osi_assert(phandle != NULL);
        thrd_CloseHandle(phandle);
    }

#ifndef DJGPP
    phandle = thrd_Create(NULL, 65536, (ThreadFunc) smb_ClientWaiter,
                          NULL, 0, &lpid, "smb_ClientWaiter");
    osi_assert(phandle != NULL);
    thrd_CloseHandle(phandle);
#endif /* !DJGPP */

    phandle = thrd_Create(NULL, 65536, (ThreadFunc) smb_ServerWaiter,
                          NULL, 0, &lpid, "smb_ServerWaiter");
    osi_assert(phandle != NULL);
    thrd_CloseHandle(phandle);

    for (i=0; i<smb_NumServerThreads; i++) {
        phandle = thrd_Create(NULL, 65536, (ThreadFunc) smb_Server,
                              (void *) i, 0, &lpid, "smb_Server");
        osi_assert(phandle != NULL);
        thrd_CloseHandle(phandle);
    }

    phandle = thrd_Create(NULL, 65536, (ThreadFunc) smb_Daemon,
                          NULL, 0, &lpid, "smb_Daemon");
    osi_assert(phandle != NULL);
    thrd_CloseHandle(phandle);

    phandle = thrd_Create(NULL, 65536, (ThreadFunc) smb_WaitingLocksDaemon,
                          NULL, 0, &lpid, "smb_WaitingLocksDaemon");
    osi_assert(phandle != NULL);
    thrd_CloseHandle(phandle);

#ifdef DJGPP
    smb_ListShares();
#endif

    return;
}

void smb_Shutdown(void)
{
    NCB *ncbp;
#ifdef DJGPP
    dos_ptr dos_ncb;
#endif
    long code = 0;
    int i;
    smb_vc_t *vcp;

    /*fprintf(stderr, "Entering smb_Shutdown\n");*/
        
    /* setup the NCB system */
    ncbp = GetNCB();
#ifdef DJGPP
    dos_ncb = ((smb_ncb_t *)ncbp)->dos_ncb;
#endif

    /* Block new sessions by setting shutdown flag */
    smbShutdownFlag = 1;

    /* Hang up all sessions */
    memset((char *)ncbp, 0, sizeof(NCB));
    for (i = 1; i < numSessions; i++)
    {
        if (dead_sessions[i])
            continue;
      
        /*fprintf(stderr, "NCBHANGUP session %d LSN %d\n", i, LSNs[i]);*/
        ncbp->ncb_command = NCBHANGUP;
        ncbp->ncb_lana_num = lanas[i];  /*smb_LANadapter;*/
        ncbp->ncb_lsn = LSNs[i];
#ifndef DJGPP
        code = Netbios(ncbp);
#else
        code = Netbios(ncbp, dos_ncb);
#endif
        /*fprintf(stderr, "returned from NCBHANGUP session %d LSN %d\n", i, LSNs[i]);*/
        if (code == 0) code = ncbp->ncb_retcode;
        if (code != 0) {
            osi_Log1(smb_logp, "Netbios NCBHANGUP error code %d", code);
            fprintf(stderr, "Session %d Netbios NCBHANGUP error code %d", i, code);
        }
    }

    /* Trigger the shutdown of all SMB threads */                                
    for (i = 0; i < smb_NumServerThreads; i++)                                   
        thrd_SetEvent(NCBreturns[i][0]);                                         
                                                                                 
    thrd_SetEvent(NCBevents[0]);                                                 
    thrd_SetEvent(SessionEvents[0]);                                             
    thrd_SetEvent(NCBavails[0]);                                                 
                                                                                 
    for (i = 0;i < smb_NumServerThreads; i++) {                                  
        DWORD code = thrd_WaitForSingleObject_Event(smb_ServerShutdown[i], 500); 
        if (code == WAIT_OBJECT_0) {                                             
            continue;                                                            
        } else {                                                                 
            afsi_log("smb_Shutdown thread [%d] did not stop; retry ...",i);      
            thrd_SetEvent(NCBreturns[i--][0]);                                   
        }                                                                        
    }                                                                            

    /* Delete Netbios name */
    memset((char *)ncbp, 0, sizeof(NCB));
    for (i = 0; i < lana_list.length; i++) {
        if (lana_list.lana[i] == 255) continue;
        ncbp->ncb_command = NCBDELNAME;
        ncbp->ncb_lana_num = lana_list.lana[i];
        memcpy(ncbp->ncb_name,smb_sharename,NCBNAMSZ);
#ifndef DJGPP
        code = Netbios(ncbp);
#else
        code = Netbios(ncbp, dos_ncb);
#endif
        if (code == 0) 
            code = ncbp->ncb_retcode;
        if (code != 0) {
            fprintf(stderr, "Netbios NCBDELNAME lana %d error code %d",
                     ncbp->ncb_lana_num, code);
        }       
        fflush(stderr);
    }

    /* Release the reference counts held by the VCs */
    lock_ObtainWrite(&smb_rctLock);
    for (vcp = smb_allVCsp; vcp; vcp=vcp->nextp) 
    {
        smb_fid_t *fidp;
        smb_tid_t *tidp;
     
        for (fidp = vcp->fidsp; fidp; fidp = (smb_fid_t *) osi_QNext(&fidp->q))
        {
            if (fidp->scp != NULL) {
                cm_scache_t * scp;

                lock_ObtainMutex(&fidp->mx);
                if (fidp->scp != NULL) {
                    scp = fidp->scp;
                    fidp->scp = NULL;
                    cm_ReleaseSCache(scp);
                }
                lock_ReleaseMutex(&fidp->mx);
            }
        }

        for (tidp = vcp->tidsp; tidp; tidp = tidp->nextp) {
            if (tidp->vcp)
                smb_ReleaseVCNoLock(tidp->vcp);
            if (tidp->userp) {
                cm_user_t *userp = tidp->userp;
                tidp->userp = NULL;
                lock_ReleaseWrite(&smb_rctLock);
                cm_ReleaseUser(userp);
                lock_ObtainWrite(&smb_rctLock);
            }
        }
    }
    lock_ReleaseWrite(&smb_rctLock);
}

/* Get the UNC \\<servername>\<sharename> prefix. */
char *smb_GetSharename()
{
    char *name;

    /* Make sure we have been properly initialized. */
    if (smb_localNamep == NULL)
        return NULL;

    /* Allocate space for \\<servername>\<sharename>, plus the
     * terminator.
     */
    name = malloc(strlen(smb_localNamep) + strlen("ALL") + 4);
    sprintf(name, "\\\\%s\\%s", smb_localNamep, "ALL");
    return name;
}   


#ifdef LOG_PACKET
void smb_LogPacket(smb_packet_t *packet)
{
    BYTE *vp, *cp;
    unsigned length, paramlen, datalen, i, j;
    char buf[81];
    char hex[]={'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};

    if (!packet) return;

    osi_Log0(smb_logp, "*** SMB packet dump ***");

    vp = (BYTE *) packet->data;

    datalen = *((WORD*)(vp + (paramlen = ((unsigned)*(vp+20)) << 1)));
    length = paramlen + 2 + datalen;


    for (i=0;i < length; i+=16)
    {
        memset( buf, ' ', 80 );
        buf[80] = 0;

        itoa( i, buf, 16 );

        buf[strlen(buf)] = ' ';

        cp = (BYTE*) buf + 7;

        for (j=0;j < 16 && (i+j)<length; j++)
        {
            *(cp++) = hex[vp[i+j] >> 4];
            *(cp++) = hex[vp[i+j] & 0xf];
            *(cp++) = ' ';

            if (j==7)
            {
                *(cp++) = '-';
                *(cp++) = ' ';
            }
        }

        for (j=0;j < 16 && (i+j)<length;j++)
        {
            *(cp++) = ( 32 <= vp[i+j] && 128 > vp[i+j] )? vp[i+j]:'.';
            if (j==7)
            {
                *(cp++) = ' ';
                *(cp++) = '-';
                *(cp++) = ' ';
            }
        }

        *cp = 0;

        osi_Log0( smb_logp, osi_LogSaveString(smb_logp, buf));
    }

    osi_Log0(smb_logp, "*** End SMB packet dump ***");
}
#endif /* LOG_PACKET */


int smb_DumpVCP(FILE *outputFile, char *cookie, int lock)
{
    int zilch;
    char output[1024];
  
    smb_vc_t *vcp;
  
    if (lock)
        lock_ObtainRead(&smb_rctLock);
  
    sprintf(output, "begin dumping smb_vc_t\n");
    WriteFile(outputFile, output, strlen(output), &zilch, NULL);

    for (vcp = smb_allVCsp; vcp; vcp=vcp->nextp) 
    {
        smb_fid_t *fidp;
      
        sprintf(output, "%s vcp=0x%08X, refCount=%d, flags=%d, vcID=%d, lsn=%d, uidCounter=%d, tidCounter=%d, fidCounter=%d\n",
                 cookie, vcp, vcp->refCount, vcp->flags, vcp->vcID, vcp->lsn, vcp->uidCounter, vcp->tidCounter, vcp->fidCounter);
        WriteFile(outputFile, output, strlen(output), &zilch, NULL);
      
        sprintf(output, "begin dumping smb_fid_t\n");
        WriteFile(outputFile, output, strlen(output), &zilch, NULL);

        for (fidp = vcp->fidsp; fidp; fidp = (smb_fid_t *) osi_QNext(&fidp->q))
        {
            sprintf(output, "%s -- smb_fidp=0x%08X, refCount=%d, fid=%d, vcp=0x%08X, scp=0x%08X, ioctlp=0x%08X, NTopen_pathp=%s, NTopen_wholepathp=%s\n", 
                     cookie, fidp, fidp->refCount, fidp->fid, fidp->vcp, fidp->scp, fidp->ioctlp, 
                     fidp->NTopen_pathp ? fidp->NTopen_pathp : "NULL", 
                     fidp->NTopen_wholepathp ? fidp->NTopen_wholepathp : "NULL");
            WriteFile(outputFile, output, strlen(output), &zilch, NULL);
        }
      
        sprintf(output, "done dumping smb_fid_t\n");
        WriteFile(outputFile, output, strlen(output), &zilch, NULL);
    }

    sprintf(output, "done dumping smb_vc_t\n");
    WriteFile(outputFile, output, strlen(output), &zilch, NULL);
  
    if (lock)
        lock_ReleaseRead(&smb_rctLock);
    return 0;
}
