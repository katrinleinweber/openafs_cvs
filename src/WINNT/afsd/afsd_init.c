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
#include <afs/afs_args.h>

#include <windows.h>
#include <string.h>
#include <nb30.h>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <strsafe.h>

#include <osi.h>
#include "afsd.h"
#include <rx\rx.h>
#include <rx\rx_null.h>

#include <WINNT/syscfg.h>

#include "smb.h"
#include "cm_rpc.h"
#include "lanahelper.h"
#include "afsicf.h"

extern int RXAFSCB_ExecuteRequest(struct rx_call *z_call);
extern int RXSTATS_ExecuteRequest(struct rx_call *z_call);

extern afs_int32 cryptall;

char AFSConfigKeyName[] =
	"SYSTEM\\CurrentControlSet\\Services\\TransarcAFSDaemon\\Parameters";

osi_log_t *afsd_logp;

char cm_rootVolumeName[64];
DWORD cm_rootVolumeNameLen;
cm_volume_t *cm_rootVolumep = NULL;
cm_cell_t *cm_rootCellp = NULL;
cm_fid_t cm_rootFid;
cm_scache_t *cm_rootSCachep;
char cm_mountRoot[1024];
DWORD cm_mountRootLen;
int cm_logChunkSize;
int cm_chunkSize;
#ifdef AFS_FREELANCE_CLIENT
char *cm_FakeRootDir;
#endif /* freelance */

int smb_UseV3;

int LANadapter;

int numBkgD;
int numSvThreads;

int traceOnPanic = 0;

int logReady = 0;

char cm_HostName[200];
long cm_HostAddr;

char cm_NetbiosName[MAX_NB_NAME_LENGTH];

char cm_CachePath[200];
DWORD cm_CachePathLen;

BOOL isGateway = FALSE;

BOOL reportSessionStartups = FALSE;

cm_initparams_v1 cm_initParams;

char *cm_sysName = 0;
int   cm_sysNameCount = 0;
char *cm_sysNameList[MAXNUMSYSNAMES];

DWORD TraceOption = 0;

/*
 * AFSD Initialization Log
 *
 * This is distinct from the regular debug logging facility.
 * Log items go directly to a file, not to an array in memory, so that even
 * if AFSD crashes, the log can be inspected.
 */

HANDLE afsi_file;

#ifdef AFS_AFSDB_ENV
int cm_dnsEnabled = 1;
#endif

char cm_NetBiosName[32];

extern initUpperCaseTable();
void afsd_initUpperCaseTable() 
{
	initUpperCaseTable();
}

void
afsi_start()
{
	char wd[100];
	char t[100], u[100], *p, *path;
	int zilch;
	int code;
    DWORD dwLow, dwHigh;
	HKEY parmKey;
	DWORD dummyLen;
    DWORD maxLogSize = 100 * 1024;

	afsi_file = INVALID_HANDLE_VALUE;
    if (getenv("TEMP"))
    {
        StringCbCopyA(wd, sizeof(wd), getenv("TEMP"));
    }
    else
    {
        code = GetWindowsDirectory(wd, sizeof(wd));
        if (code == 0) return;
    }
	StringCbCatA(wd, sizeof(wd), "\\afsd_init.log");
	GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, NULL, NULL, t, sizeof(t));
	afsi_file = CreateFile(wd, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);

    code = RegOpenKeyEx(HKEY_LOCAL_MACHINE, AFSConfigKeyName,
                         0, KEY_QUERY_VALUE, &parmKey);
	if (code == ERROR_SUCCESS) {
        dummyLen = sizeof(maxLogSize);
        code = RegQueryValueEx(parmKey, "MaxLogSize", NULL, NULL,
                                (BYTE *) &maxLogSize, &dummyLen);
        RegCloseKey (parmKey);
	}

    if (maxLogSize) {
        dwLow = GetFileSize( afsi_file, &dwHigh );
        if ( dwHigh > 0 || dwLow >= maxLogSize ) {
            CloseHandle(afsi_file);
            afsi_file = CreateFile( wd, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                    CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, NULL);
        }
    }

    SetFilePointer(afsi_file, 0, NULL, FILE_END);
	GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, NULL, NULL, u, sizeof(u));
	StringCbCatA(t, sizeof(t), ": Create log file\n");
	StringCbCatA(u, sizeof(u), ": Created log file\n");
	WriteFile(afsi_file, t, strlen(t), &zilch, NULL);
	WriteFile(afsi_file, u, strlen(u), &zilch, NULL);
    p = "PATH=";
    path = getenv("PATH");
	WriteFile(afsi_file, p, strlen(p), &zilch, NULL);
	WriteFile(afsi_file, path, strlen(path), &zilch, NULL);
	WriteFile(afsi_file, "\n", 1, &zilch, NULL);
}

static int afsi_log_useTimestamp = 1;

void
afsi_log(char *pattern, ...)
{
	char s[256], t[100], d[100], u[512];
	int zilch;
	va_list ap;
	va_start(ap, pattern);

	StringCbVPrintfA(s, sizeof(s), pattern, ap);
    if ( afsi_log_useTimestamp ) {
        GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, NULL, NULL, t, sizeof(t));
		GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, NULL, NULL, d, sizeof(d));
		StringCbPrintfA(u, sizeof(u), "%s %s: %s\n", d, t, s);
        if (afsi_file != INVALID_HANDLE_VALUE)
            WriteFile(afsi_file, u, strlen(u), &zilch, NULL);
#ifdef NOTSERVICE
        printf("%s", u);
#endif 
    } else {
        if (afsi_file != INVALID_HANDLE_VALUE)
            WriteFile(afsi_file, s, strlen(s), &zilch, NULL);
    }
}

/*
 * Standard AFSD trace
 */

void afsd_ForceTrace(BOOL flush)
{
	HANDLE handle;
	int len;
	char buf[256];

	if (!logReady) 
        return;

	len = GetTempPath(sizeof(buf)-10, buf);
	StringCbCopyA(&buf[len], sizeof(buf)-len, "/afsd.log");
	handle = CreateFile(buf, GENERIC_WRITE, FILE_SHARE_READ,
			    NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		logReady = 0;
		osi_panic("Cannot create log file", __FILE__, __LINE__);
	}
	osi_LogPrint(afsd_logp, handle);
	if (flush)
		FlushFileBuffers(handle);
	CloseHandle(handle);
}

static void
configureBackConnectionHostNames(void)
{
    /* On Windows XP SP2, Windows 2003 SP1, and all future Windows operating systems
     * there is a restriction on the use of SMB authentication on loopback connections.
     * There are two work arounds available:
     * 
     *   (1) We can disable the check for matching host names.  This does not
     *   require a reboot:
     *   [HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Lsa]
     *     "DisableLoopbackCheck"=dword:00000001
     *
     *   (2) We can add the AFS SMB/CIFS service name to an approved list.  This
     *   does require a reboot:
     *   [HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Lsa\MSV1_0]
     *     "BackConnectionHostNames"=multi-sz
     *
     * The algorithm will be:
     *   (1) Check to see if cm_NetbiosName exists in the BackConnectionHostNames list
     *   (2a) If not, add it to the list.  (This will not take effect until the next reboot.)
     *   (2b1)    and check to see if DisableLoopbackCheck is set.
     *   (2b2)    If not set, set the DisableLoopbackCheck value to 0x1 
     *   (2b3)                and create HKLM\SOFTWARE\OpenAFS\Client  UnsetDisableLoopbackCheck
     *   (2c) else If cm_NetbiosName exists in the BackConnectionHostNames list,
     *             check for the UnsetDisableLoopbackCheck value.  
     *             If set, set the DisableLoopbackCheck flag to 0x0 
     *             and delete the UnsetDisableLoopbackCheck value
     */
    HKEY hkLsa;
    HKEY hkMSV10;
    HKEY hkClient;
    DWORD dwType;
    DWORD dwSize;
    DWORD dwValue;
    PBYTE pHostNames = NULL, pName;
    BOOL  bNameFound = FALSE;   

    if ( RegOpenKeyEx( HKEY_LOCAL_MACHINE, 
                       "SYSTEM\\CurrentControlSet\\Control\\Lsa\\MSV1_0",
                       0,
                       KEY_READ|KEY_WRITE,
                       &hkMSV10) == ERROR_SUCCESS )
    {
        if (RegQueryValueEx( hkMSV10, "BackConnectionHostNames", 0, &dwType, NULL, &dwSize) == ERROR_SUCCESS) {
            pHostNames = malloc(dwSize + strlen(cm_NetbiosName) + 1);
            RegQueryValueEx( hkMSV10, "BackConnectionHostNames", 0, &dwType, pHostNames, &dwSize);

            for (pName = pHostNames; *pName ; pName += strlen(pName) + 1)
            {
                if ( !stricmp(pName, cm_NetbiosName) ) {
                    bNameFound = TRUE;
                    break;
                }   
            }
        }
             
        if ( !bNameFound ) {
            if ( !pHostNames ) {
                pName = pHostNames = malloc(strlen(cm_NetbiosName) + 2);
                dwSize = 1;
            }
            strcpy(pName, cm_NetbiosName);
            pName += strlen(cm_NetbiosName) + 1;
            *pName = '\0';  /* add a second nul terminator */

            dwType = REG_MULTI_SZ;
            dwSize += strlen(cm_NetbiosName) + 1;
            RegSetValueEx( hkMSV10, "BackConnectionHostNames", 0, dwType, pHostNames, dwSize);

            if ( RegOpenKeyEx( HKEY_LOCAL_MACHINE, 
                               "SYSTEM\\CurrentControlSet\\Control\\Lsa",
                               0,
                               KEY_READ|KEY_WRITE,
                               &hkLsa) == ERROR_SUCCESS )
            {
                dwSize = sizeof(DWORD);
                if ( RegQueryValueEx( hkLsa, "DisableLoopbackCheck", 0, &dwType, &dwValue, &dwSize) != ERROR_SUCCESS ||
                     dwValue == 0 ) {
                    dwType = REG_DWORD;
                    dwSize = sizeof(DWORD);
                    dwValue = 1;
                    RegSetValueEx( hkLsa, "DisableLoopbackCheck", 0, dwType, &dwValue, dwSize);

                    if (RegCreateKeyEx( HKEY_LOCAL_MACHINE, 
                                        "SOFTWARE\\OpenAFS\\Client",
                                        0,
                                        NULL,
                                        REG_OPTION_NON_VOLATILE,
                                        KEY_READ|KEY_WRITE,
                                        NULL,
                                        &hkClient,
                                        NULL) == ERROR_SUCCESS) {

                        dwType = REG_DWORD;
                        dwSize = sizeof(DWORD);
                        dwValue = 1;
                        RegSetValueEx( hkClient, "RemoveDisableLoopbackCheck", 0, dwType, &dwValue, dwSize);
                        RegCloseKey(hkClient);
                    }
                    RegCloseKey(hkLsa);
                }
            }
        } else {
            if (RegCreateKeyEx( HKEY_LOCAL_MACHINE, 
                                "SOFTWARE\\OpenAFS\\Client",
                                0,
                                NULL,
                                REG_OPTION_NON_VOLATILE,
                                KEY_READ|KEY_WRITE,
                                NULL,
                                &hkClient,
                                NULL) == ERROR_SUCCESS) {

                dwSize = sizeof(DWORD);
                if ( RegQueryValueEx( hkClient, "RemoveDisableLoopbackCheck", 0, &dwType, &dwValue, &dwSize) == ERROR_SUCCESS &&
                     dwValue == 1 ) {
                    if ( RegOpenKeyEx( HKEY_LOCAL_MACHINE, 
                                       "SYSTEM\\CurrentControlSet\\Control\\Lsa",
                                       0,
                                       KEY_READ|KEY_WRITE,
                                       &hkLsa) == ERROR_SUCCESS )
                    {
                        RegDeleteValue(hkLsa, "DisableLoopbackCheck");
                        RegCloseKey(hkLsa);
                    }
                }
                RegDeleteValue(hkClient, "RemoveDisableLoopbackCheck");
                RegCloseKey(hkClient);
            }
        }
        RegCloseKey(hkMSV10);
    }
}


/*
 * AFSD Initialization
 */

int afsd_InitCM(char **reasonP)
{
	osi_uid_t debugID;
	long cacheBlocks;
	long cacheSize;
	long logChunkSize;
	long stats;
	long traceBufSize;
    long maxcpus;
	long ltt, ltto;
    long rx_mtu, rx_nojumbo;
    long virtualCache;
	char rootCellName[256];
	struct rx_service *serverp;
	static struct rx_securityClass *nullServerSecurityClassp;
	struct hostent *thp;
	char *msgBuf;
	char buf[200];
	HKEY parmKey;
	DWORD dummyLen;
	long code;
	/*int freelanceEnabled;*/
	WSADATA WSAjunk;
    lana_number_t lanaNum;
    int i;

	WSAStartup(0x0101, &WSAjunk);

    afsd_initUpperCaseTable();

	/* setup osidebug server at RPC slot 1000 */
	osi_LongToUID(1000, &debugID);
	code = osi_InitDebug(&debugID);
	afsi_log("osi_InitDebug code %d", code);

    //	osi_LockTypeSetDefault("stat");	/* comment this out for speed *
	if (code != 0) {
		*reasonP = "unknown error";
		return -1;
	}

	/* who are we ? */
	gethostname(cm_HostName, sizeof(cm_HostName));
	afsi_log("gethostname %s", cm_HostName);
	thp = gethostbyname(cm_HostName);
	memcpy(&cm_HostAddr, thp->h_addr_list[0], 4);

	/* seed random number generator */
	srand(ntohl(cm_HostAddr));

	/* Look up configuration parameters in Registry */
	code = RegOpenKeyEx(HKEY_LOCAL_MACHINE, AFSConfigKeyName,
				0, KEY_QUERY_VALUE, &parmKey);
	if (code != ERROR_SUCCESS) {
		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM
				| FORMAT_MESSAGE_ALLOCATE_BUFFER,
			      NULL, code, 0, (LPTSTR)&msgBuf, 0, NULL);
		StringCbPrintfA(buf, sizeof(buf),
			"Failure in configuration while opening Registry: %s",
			msgBuf);
		osi_panic(buf, __FILE__, __LINE__);
	}

    dummyLen = sizeof(maxcpus);
	code = RegQueryValueEx(parmKey, "MaxCPUs", NULL, NULL,
				(BYTE *) &maxcpus, &dummyLen);
	if (code == ERROR_SUCCESS) {
        HANDLE hProcess;
        DWORD_PTR processAffinityMask, systemAffinityMask;

        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_SET_INFORMATION,
                               FALSE, GetCurrentProcessId());
        if ( hProcess != NULL &&
             GetProcessAffinityMask(hProcess, &processAffinityMask, &systemAffinityMask) )
        {
            int i, n, bits;
            DWORD_PTR mask, newAffinityMask;

#if defined(_WIN64)
            bits = 64;
#else
            bits = 32;
#endif
            for ( i=0, n=0, mask=1, newAffinityMask=0; i<bits && n<maxcpus; i++ ) {
                if ( processAffinityMask & mask ) {
                    newAffinityMask |= mask;
                    n++;
                }
                mask *= 2;
            }

            SetProcessAffinityMask(hProcess, newAffinityMask);
            CloseHandle(hProcess);
            afsi_log("CPU Restrictions set to %d cpu(s); %d cpu(s) available", maxcpus, n);
        } else {
            afsi_log("CPU Restrictions set to %d cpu(s); unable to access process information", maxcpus);
        }
    }

	dummyLen = sizeof(TraceOption);
	code = RegQueryValueEx(parmKey, "TraceOption", NULL, NULL,
				(BYTE *) &TraceOption, &dummyLen);
    afsi_log("Event Log Tracing = %lX", TraceOption);

	dummyLen = sizeof(traceBufSize);
	code = RegQueryValueEx(parmKey, "TraceBufferSize", NULL, NULL,
				(BYTE *) &traceBufSize, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Trace Buffer size %d", traceBufSize);
	else {
		traceBufSize = CM_CONFIGDEFAULT_TRACEBUFSIZE;
		afsi_log("Default trace buffer size %d", traceBufSize);
	}

	/* setup and enable debug log */
	afsd_logp = osi_LogCreate("afsd", traceBufSize);
	afsi_log("osi_LogCreate log addr %x", (int)afsd_logp);
    osi_LogEnable(afsd_logp);
	logReady = 1;

    osi_Log0(afsd_logp, "Log init");

	dummyLen = sizeof(cacheSize);
	code = RegQueryValueEx(parmKey, "CacheSize", NULL, NULL,
				(BYTE *) &cacheSize, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Cache size %d", cacheSize);
	else {
		cacheSize = CM_CONFIGDEFAULT_CACHESIZE;
		afsi_log("Default cache size %d", cacheSize);
	}

	dummyLen = sizeof(logChunkSize);
	code = RegQueryValueEx(parmKey, "ChunkSize", NULL, NULL,
				(BYTE *) &logChunkSize, &dummyLen);
	if (code == ERROR_SUCCESS) {
		if (logChunkSize < 12 || logChunkSize > 30) {
			afsi_log("Invalid chunk size %d, using default",
				 logChunkSize);
			logChunkSize = CM_CONFIGDEFAULT_CHUNKSIZE;
		}
		afsi_log("Chunk size %d", logChunkSize);
	} else {
		logChunkSize = CM_CONFIGDEFAULT_CHUNKSIZE;
		afsi_log("Default chunk size %d", logChunkSize);
	}
	cm_logChunkSize = logChunkSize;
	cm_chunkSize = 1 << logChunkSize;

	dummyLen = sizeof(numBkgD);
	code = RegQueryValueEx(parmKey, "Daemons", NULL, NULL,
				(BYTE *) &numBkgD, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("%d background daemons", numBkgD);
	else {
		numBkgD = CM_CONFIGDEFAULT_DAEMONS;
		afsi_log("Defaulting to %d background daemons", numBkgD);
	}

	dummyLen = sizeof(numSvThreads);
	code = RegQueryValueEx(parmKey, "ServerThreads", NULL, NULL,
				(BYTE *) &numSvThreads, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("%d server threads", numSvThreads);
	else {
		numSvThreads = CM_CONFIGDEFAULT_SVTHREADS;
		afsi_log("Defaulting to %d server threads", numSvThreads);
	}

	dummyLen = sizeof(stats);
	code = RegQueryValueEx(parmKey, "Stats", NULL, NULL,
				(BYTE *) &stats, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Status cache size %d", stats);
	else {
		stats = CM_CONFIGDEFAULT_STATS;
		afsi_log("Default status cache size %d", stats);
	}

	dummyLen = sizeof(ltt);
	code = RegQueryValueEx(parmKey, "LogoffTokenTransfer", NULL, NULL,
				(BYTE *) &ltt, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Logoff token transfer %s",  (ltt ? "on" : "off"));
	else {
		ltt = 1;
		afsi_log("Logoff token transfer on by default");
	}
    smb_LogoffTokenTransfer = ltt;
    afsi_log("Logoff token transfer is currently ignored");

	if (ltt) {
		dummyLen = sizeof(ltto);
		code = RegQueryValueEx(parmKey, "LogoffTokenTransferTimeout",
					NULL, NULL, (BYTE *) &ltto, &dummyLen);
		if (code == ERROR_SUCCESS)
			afsi_log("Logoff token tranfer timeout %d seconds",
				 ltto);
		else {
			ltto = 10;
			afsi_log("Default logoff token transfer timeout 10 seconds");
		}
	} else {
        ltto = 0;
    }
    smb_LogoffTransferTimeout = ltto;
    afsi_log("Default logoff token is currently ignored");

	dummyLen = sizeof(cm_rootVolumeName);
	code = RegQueryValueEx(parmKey, "RootVolume", NULL, NULL,
				cm_rootVolumeName, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Root volume %s", cm_rootVolumeName);
	else {
		StringCbCopyA(cm_rootVolumeName, sizeof(cm_rootVolumeName), "root.afs");
		afsi_log("Default root volume name root.afs");
	}

	cm_mountRootLen = sizeof(cm_mountRoot);
	code = RegQueryValueEx(parmKey, "MountRoot", NULL, NULL,
				cm_mountRoot, &cm_mountRootLen);
	if (code == ERROR_SUCCESS) {
		afsi_log("Mount root %s", cm_mountRoot);
		cm_mountRootLen = strlen(cm_mountRoot);
	} else {
		StringCbCopyA(cm_mountRoot, sizeof(cm_mountRoot), "/afs");
		cm_mountRootLen = 4;
		/* Don't log */
	}

	dummyLen = sizeof(cm_CachePath);
	code = RegQueryValueEx(parmKey, "CachePath", NULL, NULL,
				cm_CachePath, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Cache path %s", cm_CachePath);
	else {
		GetWindowsDirectory(cm_CachePath, sizeof(cm_CachePath));
		cm_CachePath[2] = 0;	/* get drive letter only */
		StringCbCatA(cm_CachePath, sizeof(cm_CachePath), "\\AFSCache");
		afsi_log("Default cache path %s", cm_CachePath);
	}

    dummyLen = sizeof(virtualCache);
    code = RegQueryValueEx(parmKey, "NonPersistentCaching", NULL, NULL,
        &virtualCache, &dummyLen);
    if (code == ERROR_SUCCESS && virtualCache) {
        buf_cacheType = CM_BUF_CACHETYPE_VIRTUAL;
    } else {
        buf_cacheType = CM_BUF_CACHETYPE_FILE;
    }
    afsi_log("Cache type is %s", ((buf_cacheType == CM_BUF_CACHETYPE_FILE)?"FILE":"VIRTUAL"));

	dummyLen = sizeof(traceOnPanic);
	code = RegQueryValueEx(parmKey, "TrapOnPanic", NULL, NULL,
				(BYTE *) &traceOnPanic, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Set to %s on panic",
			 traceOnPanic ? "trap" : "not trap");
	else {
		traceOnPanic = 0;
		/* Don't log */
	}

	dummyLen = sizeof(reportSessionStartups);
	code = RegQueryValueEx(parmKey, "ReportSessionStartups", NULL, NULL,
				(BYTE *) &reportSessionStartups, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Session startups %s be recorded in the Event Log",
			 reportSessionStartups ? "will" : "will not");
	else {
		reportSessionStartups = 0;
		/* Don't log */
	}

    for ( i=0; i < MAXNUMSYSNAMES; i++ ) {
        cm_sysNameList[i] = osi_Alloc(MAXSYSNAME);
        cm_sysNameList[i][0] = '\0';
    }
    cm_sysName = cm_sysNameList[0];

	dummyLen = MAXSYSNAME;
	code = RegQueryValueEx(parmKey, "SysName", NULL, NULL,
				cm_sysName, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("Sys name %s", cm_sysName);
	else {
		StringCbCopyA(cm_sysName, MAXSYSNAME, "i386_nt40");
		afsi_log("Default sys name %s", cm_sysName);
	}
    cm_sysNameCount = 1;

	dummyLen = sizeof(cryptall);
	code = RegQueryValueEx(parmKey, "SecurityLevel", NULL, NULL,
				(BYTE *) &cryptall, &dummyLen);
	if (code == ERROR_SUCCESS)
		afsi_log("SecurityLevel is %s", cryptall?"crypt":"clear");
	else {
		cryptall = rxkad_clear;
		afsi_log("Default SecurityLevel is clear");
	}

#ifdef AFS_AFSDB_ENV
	dummyLen = sizeof(cm_dnsEnabled);
	code = RegQueryValueEx(parmKey, "UseDNS", NULL, NULL,
				(BYTE *) &cm_dnsEnabled, &dummyLen);
	if (code == ERROR_SUCCESS) {
		afsi_log("DNS %s be used to find AFS cell servers",
			 cm_dnsEnabled ? "will" : "will not");
	}
	else {
	  cm_dnsEnabled = 1;   /* default on */
	  afsi_log("Default to use DNS to find AFS cell servers");
	}
#else /* AFS_AFSDB_ENV */
	afsi_log("AFS not built with DNS support to find AFS cell servers");
#endif /* AFS_AFSDB_ENV */

#ifdef AFS_FREELANCE_CLIENT
	dummyLen = sizeof(cm_freelanceEnabled);
	code = RegQueryValueEx(parmKey, "FreelanceClient", NULL, NULL,
				(BYTE *) &cm_freelanceEnabled, &dummyLen);
	if (code == ERROR_SUCCESS) {
		afsi_log("Freelance client feature %s activated",
			 cm_freelanceEnabled ? "is" : "is not");
	}
	else {
	  cm_freelanceEnabled = 0;  /* default off */
	}
#endif /* AFS_FREELANCE_CLIENT */

    dummyLen = sizeof(buf);
    code = RegQueryValueEx(parmKey, "NetbiosName", NULL, NULL,
                           (BYTE *) &buf, &dummyLen);
    if (code == ERROR_SUCCESS) {
        DWORD len = ExpandEnvironmentStrings(buf, cm_NetBiosName, MAX_NB_NAME_LENGTH);
        if ( len > 0 && len <= MAX_NB_NAME_LENGTH ) {
            afsi_log("Explicit NetBios name is used %s", cm_NetBiosName);
        } else {
            afsi_log("Unable to Expand Explicit NetBios name: %s", buf);
            cm_NetBiosName[0] = 0;  /* turn it off */
        }
    }
    else {
        cm_NetBiosName[0] = 0;   /* default off */
    }

    dummyLen = sizeof(smb_hideDotFiles);
    code = RegQueryValueEx(parmKey, "HideDotFiles", NULL, NULL,
                           (BYTE *) &smb_hideDotFiles, &dummyLen);
    if (code != ERROR_SUCCESS) {
        smb_hideDotFiles = 1; /* default on */
    }
    afsi_log("Dot files/dirs will %sbe marked hidden",
              smb_hideDotFiles ? "" : "not ");

    dummyLen = sizeof(smb_maxMpxRequests);
    code = RegQueryValueEx(parmKey, "MaxMpxRequests", NULL, NULL,
                           (BYTE *) &smb_maxMpxRequests, &dummyLen);
    if (code != ERROR_SUCCESS) {
        smb_maxMpxRequests = 50;
    }
    afsi_log("Maximum number of multiplexed sessions is %d", smb_maxMpxRequests);

    dummyLen = sizeof(smb_maxVCPerServer);
    code = RegQueryValueEx(parmKey, "MaxVCPerServer", NULL, NULL,
                           (BYTE *) &smb_maxVCPerServer, &dummyLen);
    if (code != ERROR_SUCCESS) {
        smb_maxVCPerServer = 100;
    }
    afsi_log("Maximum number of VCs per server is %d", smb_maxVCPerServer);

	dummyLen = sizeof(smb_authType);
	code = RegQueryValueEx(parmKey, "SMBAuthType", NULL, NULL,
		(BYTE *) &smb_authType, &dummyLen);

	if (code != ERROR_SUCCESS || 
        (smb_authType != SMB_AUTH_EXTENDED && smb_authType != SMB_AUTH_NTLM && smb_authType != SMB_AUTH_NONE)) {
		smb_authType = SMB_AUTH_EXTENDED; /* default is to use extended authentication */
	}
	afsi_log("SMB authentication type is %s", ((smb_authType == SMB_AUTH_NONE)?"NONE":((smb_authType == SMB_AUTH_EXTENDED)?"EXTENDED":"NTLM")));

    dummyLen = sizeof(rx_nojumbo);
    code = RegQueryValueEx(parmKey, "RxNoJumbo", NULL, NULL,
                           (BYTE *) &rx_nojumbo, &dummyLen);
    if (code != ERROR_SUCCESS) {
        rx_nojumbo = 0;
    }
    if(rx_nojumbo)
        afsi_log("RX Jumbograms are disabled");

    dummyLen = sizeof(rx_mtu);
    code = RegQueryValueEx(parmKey, "RxMaxMTU", NULL, NULL,
                           (BYTE *) &rx_mtu, &dummyLen);
    if (code != ERROR_SUCCESS || !rx_mtu) {
        rx_mtu = -1;
    }
    if(rx_mtu != -1)
        afsi_log("RX maximum MTU is %d", rx_mtu);

    dummyLen = sizeof(ConnDeadtimeout);
    code = RegQueryValueEx(parmKey, "ConnDeadTimeout", NULL, NULL,
                           (BYTE *) &ConnDeadtimeout, &dummyLen);
    afsi_log("ConnDeadTimeout is %d", ConnDeadtimeout);

    dummyLen = sizeof(HardDeadtimeout);
    code = RegQueryValueEx(parmKey, "HardDeadTimeout", NULL, NULL,
                           (BYTE *) &HardDeadtimeout, &dummyLen);
    afsi_log("HardDeadTimeout is %d", HardDeadtimeout);

	RegCloseKey (parmKey);

    /* Call lanahelper to get Netbios name, lan adapter number and gateway flag */
    if(SUCCEEDED(code = lana_GetUncServerNameEx(cm_NetbiosName, &lanaNum, &isGateway, LANA_NETBIOS_NAME_FULL))) {
        LANadapter = (lanaNum == LANA_INVALID)? -1: lanaNum;

        if(LANadapter != -1)
            afsi_log("LAN adapter number %d", LANadapter);
        else
            afsi_log("LAN adapter number not determined");

        if(isGateway)
            afsi_log("Set for gateway service");

        afsi_log("Using >%s< as SMB server name", cm_NetbiosName);
    } else {
        /* something went horribly wrong.  We can't proceed without a netbios name */
        StringCbPrintfA(buf,sizeof(buf),"Netbios name could not be determined: %li", code);
        osi_panic(buf, __FILE__, __LINE__);
    }

	/* setup early variables */
	/* These both used to be configurable. */
	smb_UseV3 = 1;
    buf_bufferSize = CM_CONFIGDEFAULT_BLOCKSIZE;

	/* turn from 1024 byte units into memory blocks */
    cacheBlocks = (cacheSize * 1024) / buf_bufferSize;
        
	/* get network related info */
	cm_noIPAddr = CM_MAXINTERFACE_ADDR;
	code = syscfg_GetIFInfo(&cm_noIPAddr,
                            cm_IPAddr, cm_SubnetMask,
                            cm_NetMtu, cm_NetFlags);

	if ( (cm_noIPAddr <= 0) || (code <= 0 ) )
	    afsi_log("syscfg_GetIFInfo error code %d", code);
	else
	    afsi_log("First Network address %x SubnetMask %x",
                 cm_IPAddr[0], cm_SubnetMask[0]);

	/*
	 * Save client configuration for GetCacheConfig requests
	 */
	cm_initParams.nChunkFiles = 0;
	cm_initParams.nStatCaches = stats;
	cm_initParams.nDataCaches = 0;
	cm_initParams.nVolumeCaches = 0;
	cm_initParams.firstChunkSize = cm_chunkSize;
	cm_initParams.otherChunkSize = cm_chunkSize;
	cm_initParams.cacheSize = cacheSize;
	cm_initParams.setTime = 0;
	cm_initParams.memCache = 0;

    /* Set RX parameters before initializing RX */
    if ( rx_nojumbo ) {
        rx_SetNoJumbo();
        afsi_log("rx_SetNoJumbo successful");
    }

    if ( rx_mtu != -1 ) {
        rx_SetMaxMTU(rx_mtu);
        afsi_log("rx_SetMaxMTU %d successful", rx_mtu);
    }

    /* Open Microsoft Firewall to allow in port 7001 */
    icf_CheckAndAddAFSPorts(AFS_PORTSET_CLIENT);

    /* Ensure the AFS Netbios Name is registered to allow loopback access */
    configureBackConnectionHostNames();

	/* initialize RX, and tell it to listen to port 7001, which is used for
     * callback RPC messages.
     */
	code = rx_Init(htons(7001));
	afsi_log("rx_Init code %x", code);
	if (code != 0) {
		*reasonP = "afsd: failed to init rx client on port 7001";
		return -1;
	}

	/* Initialize the RPC server for session keys */
	RpcInit();

	/* create an unauthenticated service #1 for callbacks */
	nullServerSecurityClassp = rxnull_NewServerSecurityObject();
    serverp = rx_NewService(0, 1, "AFS", &nullServerSecurityClassp, 1,
                            RXAFSCB_ExecuteRequest);
	afsi_log("rx_NewService addr %x", (int)serverp);
	if (serverp == NULL) {
		*reasonP = "unknown error";
		return -1;
	}

	nullServerSecurityClassp = rxnull_NewServerSecurityObject();
    serverp = rx_NewService(0, RX_STATS_SERVICE_ID, "rpcstats",
                            &nullServerSecurityClassp, 1, RXSTATS_ExecuteRequest);
	afsi_log("rx_NewService addr %x", (int)serverp);
	if (serverp == NULL) {
		*reasonP = "unknown error";
		return -1;
	}
        
    /* start server threads, *not* donating this one to the pool */
    rx_StartServer(0);
	afsi_log("rx_StartServer");

	/* init user daemon, and other packages */
	cm_InitUser();

	cm_InitACLCache(2*stats);

	cm_InitConn();

    cm_InitCell();
        
    cm_InitServer();
        
    cm_InitVolume();

    cm_InitIoctl();
        
    smb_InitIoctl();
        
    cm_InitCallback();
        
    cm_InitSCache(stats);
        
    code = cm_InitDCache(0, cacheBlocks);
	afsi_log("cm_InitDCache code %x", code);
	if (code != 0) {
		*reasonP = "error initializing cache";
		return -1;
	}

#ifdef AFS_AFSDB_ENV
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0500)
	if (cm_InitDNS(cm_dnsEnabled) == -1)
	  cm_dnsEnabled = 0;  /* init failed, so deactivate */
	afsi_log("cm_InitDNS %d", cm_dnsEnabled);
#endif
#endif

	code = cm_GetRootCellName(rootCellName);
	afsi_log("cm_GetRootCellName code %d, cm_freelanceEnabled= %d, rcn= %s", 
             code, cm_freelanceEnabled, (code ? "<none>" : rootCellName));
	if (code != 0 && !cm_freelanceEnabled) 
    {
	    *reasonP = "can't find root cell name in afsd.ini";
	    return -1;
    }
    else if (cm_freelanceEnabled)
        cm_rootCellp = NULL;

    if (code == 0 && !cm_freelanceEnabled) 
    {
        cm_rootCellp = cm_GetCell(rootCellName, CM_FLAG_CREATE);
        afsi_log("cm_GetCell addr %x", (int)cm_rootCellp);
        if (cm_rootCellp == NULL) 
        {
            *reasonP = "can't find root cell in afsdcell.ini";
            return -1;
        }
	}

#ifdef AFS_FREELANCE_CLIENT
	if (cm_freelanceEnabled)
	  cm_InitFreelance();
#endif

	return 0;
}

int afsd_InitDaemons(char **reasonP)
{
	long code;
	cm_req_t req;

	cm_InitReq(&req);

	/* this should really be in an init daemon from here on down */

    if (!cm_freelanceEnabled) {
		osi_Log0(afsd_logp, "Loading Root Volume from cell");
        code = cm_GetVolumeByName(cm_rootCellp, cm_rootVolumeName, cm_rootUserp,
                                  &req, CM_FLAG_CREATE, &cm_rootVolumep);
        afsi_log("cm_GetVolumeByName code %x root vol %x", code,
                 (code ? (cm_volume_t *)-1 : cm_rootVolumep));
        if (code != 0) {
            *reasonP = "can't find root volume in root cell";
            return -1;
        }
    }

	/* compute the root fid */
	if (!cm_freelanceEnabled) {
        cm_rootFid.cell = cm_rootCellp->cellID;
        cm_rootFid.volume = cm_GetROVolumeID(cm_rootVolumep);
        cm_rootFid.vnode = 1;
        cm_rootFid.unique = 1;
	}
	else
        cm_FakeRootFid(&cm_rootFid);
        
    code = cm_GetSCache(&cm_rootFid, &cm_rootSCachep, cm_rootUserp, &req);
	afsi_log("cm_GetSCache code %x scache %x", code,
             (code ? (cm_scache_t *)-1 : cm_rootSCachep));
	if (code != 0) {
		*reasonP = "unknown error";
		return -1;
	}

	cm_InitDaemon(numBkgD);
	afsi_log("cm_InitDaemon");

	return 0;
}

int afsd_InitSMB(char **reasonP, void *aMBfunc)
{
	/* Do this last so that we don't handle requests before init is done.
     * Here we initialize the SMB listener.
     */
    smb_Init(afsd_logp, cm_NetbiosName, smb_UseV3, LANadapter, numSvThreads, aMBfunc);
    afsi_log("smb_Init");

	return 0;
}

#ifdef ReadOnly
#undef ReadOnly
#endif

#ifdef File
#undef File
#endif

#pragma pack( push, before_imagehlp, 8 )
#include <imagehlp.h>
#pragma pack( pop, before_imagehlp )

#define MAXNAMELEN 1024

void afsd_printStack(HANDLE hThread, CONTEXT *c)
{
    HANDLE hProcess = GetCurrentProcess();
    int frameNum;
    DWORD offset;
    DWORD symOptions;
    char functionName[MAXNAMELEN];
  
    IMAGEHLP_MODULE Module;
    IMAGEHLP_LINE Line;
  
    STACKFRAME s;
    IMAGEHLP_SYMBOL *pSym;
  
    afsi_log_useTimestamp = 0;
  
    pSym = (IMAGEHLP_SYMBOL *) GlobalAlloc(0, sizeof (IMAGEHLP_SYMBOL) + MAXNAMELEN);
  
    memset( &s, '\0', sizeof s );
    if (!SymInitialize(hProcess, NULL, 1) )
    {
        afsi_log("SymInitialize(): GetLastError() = %lu\n", GetLastError() );
      
        SymCleanup( hProcess );
        GlobalFree(pSym);
      
        return;
    }
  
    symOptions = SymGetOptions();
    symOptions |= SYMOPT_LOAD_LINES;
    symOptions &= ~SYMOPT_UNDNAME;
    SymSetOptions( symOptions );
  
    /*
     * init STACKFRAME for first call
     * Notes: AddrModeFlat is just an assumption. I hate VDM debugging.
     * Notes: will have to be #ifdef-ed for Alphas; MIPSes are dead anyway,
     * and good riddance.
     */
#if defined (_ALPHA_) || defined (_MIPS_) || defined (_PPC_)
#error The STACKFRAME initialization in afsd_printStack() for this platform
#error must be properly configured
#else
    s.AddrPC.Offset = c->Eip;
    s.AddrPC.Mode = AddrModeFlat;
    s.AddrFrame.Offset = c->Ebp;
    s.AddrFrame.Mode = AddrModeFlat;
#endif

    memset( pSym, '\0', sizeof (IMAGEHLP_SYMBOL) + MAXNAMELEN );
    pSym->SizeOfStruct = sizeof (IMAGEHLP_SYMBOL);
    pSym->MaxNameLength = MAXNAMELEN;
  
    memset( &Line, '\0', sizeof Line );
    Line.SizeOfStruct = sizeof Line;
  
    memset( &Module, '\0', sizeof Module );
    Module.SizeOfStruct = sizeof Module;
  
    offset = 0;
  
    afsi_log("\n--# FV EIP----- RetAddr- FramePtr StackPtr Symbol" );
  
    for ( frameNum = 0; ; ++ frameNum )
    {
        /*
         * get next stack frame (StackWalk(), SymFunctionTableAccess(), 
         * SymGetModuleBase()). if this returns ERROR_INVALID_ADDRESS (487) or
         * ERROR_NOACCESS (998), you can assume that either you are done, or
         * that the stack is so hosed that the next deeper frame could not be
         * found.
         */
        if ( ! StackWalk( IMAGE_FILE_MACHINE_I386, hProcess, hThread, &s, c, 
                          NULL, SymFunctionTableAccess, SymGetModuleBase, 
                          NULL ) )
            break;
      
        /* display its contents */
        afsi_log("\n%3d %c%c %08lx %08lx %08lx %08lx ",
                 frameNum, s.Far? 'F': '.', s.Virtual? 'V': '.',
                 s.AddrPC.Offset, s.AddrReturn.Offset,
                 s.AddrFrame.Offset, s.AddrStack.Offset );
      
        if ( s.AddrPC.Offset == 0 )
        {
            afsi_log("(-nosymbols- PC == 0)" );
        }
        else
        { 
            /* show procedure info from a valid PC */
            if (!SymGetSymFromAddr(hProcess, s.AddrPC.Offset, &offset, pSym))
            {
                if ( GetLastError() != ERROR_INVALID_ADDRESS )
                {
                    afsi_log("SymGetSymFromAddr(): errno = %lu", 
                             GetLastError());
                }
            }
            else
            {
                UnDecorateSymbolName(pSym->Name, functionName, MAXNAMELEN, 
                                     UNDNAME_NAME_ONLY);
                afsi_log("%s", functionName );

                if ( offset != 0 )
                {
                    afsi_log(" %+ld bytes", (long) offset);
                }
            }

            if (!SymGetLineFromAddr(hProcess, s.AddrPC.Offset, &offset, &Line))
            {
                if (GetLastError() != ERROR_INVALID_ADDRESS)
                {
                    afsi_log("Error: SymGetLineFromAddr(): errno = %lu", 
                             GetLastError());
                }
            }
            else
            {
                afsi_log("    Line: %s(%lu) %+ld bytes", Line.FileName, 
                         Line.LineNumber, offset);
            }
        }
      
        /* no return address means no deeper stackframe */
        if (s.AddrReturn.Offset == 0)
        {
            SetLastError(0);
            break;
        }
    }
  
    if (GetLastError() != 0)
    {
        afsi_log("\nStackWalk(): errno = %lu\n", GetLastError());
    }
  
    SymCleanup(hProcess);
    GlobalFree(pSym);
}

#ifdef _DEBUG
static DWORD *afsd_crtDbgBreakCurrent = NULL;
static DWORD afsd_crtDbgBreaks[256];
#endif

LONG __stdcall afsd_ExceptionFilter(EXCEPTION_POINTERS *ep)
{
    CONTEXT context;
#ifdef _DEBUG  
    BOOL allocRequestBrk = FALSE;
#endif 
  
    afsi_log("UnhandledException : code : 0x%x, address: 0x%x\n", 
             ep->ExceptionRecord->ExceptionCode, 
             ep->ExceptionRecord->ExceptionAddress);
	   
#ifdef _DEBUG
    if (afsd_crtDbgBreakCurrent && 
        *afsd_crtDbgBreakCurrent == _CrtSetBreakAlloc(*afsd_crtDbgBreakCurrent))
    { 
        allocRequestBrk = TRUE;
        afsi_log("Breaking on alloc request # %d\n", *afsd_crtDbgBreakCurrent);
    }
#endif
	   
    /* save context if we want to print the stack information */
    context = *ep->ContextRecord;
	   
    afsd_printStack(GetCurrentThread(), &context);
	   
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT)
    {
        afsi_log("\nEXCEPTION_BREAKPOINT - continue execution ...\n");
    
#ifdef _DEBUG
        if (allocRequestBrk)
        {
            afsd_crtDbgBreakCurrent++;
            _CrtSetBreakAlloc(*afsd_crtDbgBreakCurrent);
        }
#endif         
    
        ep->ContextRecord->Eip++;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    else
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
}
  
void afsd_SetUnhandledExceptionFilter()
{
    SetUnhandledExceptionFilter(afsd_ExceptionFilter);
}
  
#ifdef _DEBUG
void afsd_DbgBreakAllocInit()
{
    memset(afsd_crtDbgBreaks, -1, sizeof(afsd_crtDbgBreaks));
    afsd_crtDbgBreakCurrent = afsd_crtDbgBreaks;
}
  
void afsd_DbgBreakAdd(DWORD requestNumber)
{
    int i;
    for (i = 0; i < sizeof(afsd_crtDbgBreaks) - 1; i++)
	{
        if (afsd_crtDbgBreaks[i] == -1)
	    {
            break;
	    }
	}
    afsd_crtDbgBreaks[i] = requestNumber;

    _CrtSetBreakAlloc(afsd_crtDbgBreaks[0]);
}
#endif
