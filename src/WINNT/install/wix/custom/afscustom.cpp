/**************************************************************
* afscustom.cpp : Dll implementing custom action to install AFS
*
*         The functions in this file are for use as entry points
*         for calls from MSI only. The specific MSI parameters
*         are noted in the comments section of each of the
*         functions.
*
* rcsid: $Id: afscustom.cpp,v 1.1 2004/06/21 05:21:49 jaltman Exp $
**************************************************************/

// Only works for Win2k and above

#define _WIN32_WINNT 0x0500

#include "afscustom.h"
#include "tchar.h"

void ShowMsiError( MSIHANDLE hInstall, DWORD errcode, DWORD param ){
	MSIHANDLE hRecord;

	hRecord = MsiCreateRecord(3);
	MsiRecordClearData(hRecord);
	MsiRecordSetInteger(hRecord, 1, errcode);
	MsiRecordSetInteger(hRecord, 2, param);

	MsiProcessMessage( hInstall, INSTALLMESSAGE_ERROR, hRecord );
	
	MsiCloseHandle( hRecord );
}

/* Abort the installation (called as an immediate custom action) */
MSIDLLEXPORT AbortMsiImmediate( MSIHANDLE hInstall ) {
    DWORD rv;
	DWORD dwSize = 0;
	LPTSTR sReason = NULL;
	LPTSTR sFormatted = NULL;
	MSIHANDLE hRecord = NULL;
	LPTSTR cAbortReason = _T("ABORTREASON");

	rv = MsiGetProperty( hInstall, cAbortReason, _T(""), &dwSize );
	if(rv != ERROR_MORE_DATA) goto _cleanup;

	sReason = new TCHAR[ ++dwSize ];

	rv = MsiGetProperty( hInstall, cAbortReason, sReason, &dwSize );

	if(rv != ERROR_SUCCESS) goto _cleanup;

    hRecord = MsiCreateRecord(3);
	MsiRecordClearData(hRecord);
	MsiRecordSetString(hRecord, 0, sReason);

	dwSize = 0;

	rv = MsiFormatRecord(hInstall, hRecord, "", &dwSize);
	if(rv != ERROR_MORE_DATA) goto _cleanup;

	sFormatted = new TCHAR[ ++dwSize ];

	rv = MsiFormatRecord(hInstall, hRecord, sFormatted, &dwSize);

	if(rv != ERROR_SUCCESS) goto _cleanup;

	MsiCloseHandle(hRecord);

	hRecord = MsiCreateRecord(3);
	MsiRecordClearData(hRecord);
	MsiRecordSetInteger(hRecord, 1, ERR_ABORT);
	MsiRecordSetString(hRecord,2, sFormatted);
	MsiProcessMessage(hInstall, INSTALLMESSAGE_ERROR, hRecord);

_cleanup:
	if(sFormatted) delete sFormatted;
	if(hRecord) MsiCloseHandle( hRecord );
	if(sReason) delete sReason;

	return ~ERROR_SUCCESS;
}

/* Configure the client and server services */
MSIDLLEXPORT ConfigureClientService( MSIHANDLE hInstall ) {
	DWORD rv = ConfigService( 1 );
	if(rv != ERROR_SUCCESS) {
		ShowMsiError( hInstall, ERR_SCC_FAILED, rv );
	}
	return rv;
}

MSIDLLEXPORT ConfigureServerService( MSIHANDLE hInstall ) {
	DWORD rv = ConfigService( 2 );
	if(rv != ERROR_SUCCESS) {
		ShowMsiError( hInstall, ERR_SCS_FAILED, rv );
	}
	return ERROR_SUCCESS;
}

DWORD ConfigService( int svc ) {
	SC_HANDLE scm = NULL;
	SC_HANDLE hsvc = NULL;
	SC_LOCK scl = NULL;
	DWORD rv = ERROR_SUCCESS;

	scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if(scm == NULL) {rv = GetLastError(); goto _cleanup; }

	scl = LockServiceDatabase(scm);
	if(scl == NULL) {rv = GetLastError(); goto _cleanup; }

	hsvc = OpenService( scm, ((svc==1)? _T("TransarcAFSDaemon") : _T("TransarcAFSServer")), SERVICE_ALL_ACCESS);
	if(hsvc == NULL) {rv = GetLastError(); goto _cleanup; }

	SERVICE_FAILURE_ACTIONS sfa;
	SC_ACTION saact[3];

	sfa.dwResetPeriod = 3600; // one hour
	sfa.lpRebootMsg = NULL;
	sfa.lpCommand = NULL;
	sfa.cActions = 3;
	sfa.lpsaActions = saact;

	saact[0].Type = SC_ACTION_RESTART;
	saact[0].Delay = 5000;
	saact[1].Type = SC_ACTION_RESTART;
	saact[1].Delay = 5000;
	saact[2].Type = SC_ACTION_NONE;
	saact[2].Delay = 5000;

	if(!ChangeServiceConfig2(hsvc, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa))
		rv = GetLastError();

_cleanup:
	if(hsvc) CloseServiceHandle(hsvc);
	if(scl) UnlockServiceDatabase(scl);
	if(scm) CloseServiceHandle(scm);

	return rv;
}

/* Sets the registry keys required for the functioning of the network
provider */

MSIDLLEXPORT InstallNetProvider( MSIHANDLE hInstall ) {
    return InstNetProvider( hInstall, 1 );
}

MSIDLLEXPORT UninstallNetProvider( MSIHANDLE hInstall) {
    return InstNetProvider( hInstall, 0 );
}

DWORD InstNetProvider(MSIHANDLE hInstall, int bInst) {
    LPTSTR strOrder;
    HKEY hkOrder;
    LONG rv;
    DWORD dwSize;
    HANDLE hProcHeap;
    
    strOrder = (LPTSTR) 0;
    
    CHECK(rv = RegOpenKeyEx( HKEY_LOCAL_MACHINE, STR_KEY_ORDER, 0, KEY_READ | KEY_WRITE, &hkOrder ));
    
    dwSize = 0;
    CHECK(rv = RegQueryValueEx( hkOrder, STR_VAL_ORDER, NULL, NULL, NULL, &dwSize ) );
    
    strOrder = new TCHAR[ (dwSize + STR_SERVICE_LEN) * sizeof(TCHAR) ];
    
    CHECK(rv = RegQueryValueEx( hkOrder, STR_VAL_ORDER, NULL, NULL, (LPBYTE) strOrder, &dwSize));
    
    npi_CheckAndAddRemove( strOrder, STR_SERVICE , bInst);
    
    dwSize = (lstrlen( strOrder ) + 1) * sizeof(TCHAR);
    
    CHECK(rv = RegSetValueEx( hkOrder, STR_VAL_ORDER, NULL, REG_SZ, (LPBYTE) strOrder, dwSize ));
    
    /* everything else should be set by the MSI tables */
    rv = ERROR_SUCCESS;
_cleanup:
	
    if( rv != ERROR_SUCCESS ) {
        ShowMsiError( hInstall, ERR_NPI_FAILED, rv );
    }
    
    if(strOrder) delete strOrder;
    
    return rv;
}

/* Check and add or remove networkprovider key value
	str : target string
	str2: string to add/remove
	bInst: == 1 if string should be added to target if not already there, otherwise remove string from target if present.
    */
int npi_CheckAndAddRemove( LPTSTR str, LPTSTR str2, int bInst ) {

    LPTSTR target, charset, match;
    int ret=0;

    target = new TCHAR[lstrlen(str)+3];
    lstrcpy(target,_T(","));
    lstrcat(target,str);
    lstrcat(target,_T(","));
    charset = new TCHAR[lstrlen(str2)+3];
    lstrcpy(charset,_T(","));
    lstrcat(charset,str2);
    lstrcat(charset,_T(","));

    match = _tcsstr(target, charset);
    
    if ((match) && (bInst)) {
        ret = INP_ERR_PRESENT;
        goto cleanup;
    }
    
    if ((!match) && (!bInst)) {
        ret = INP_ERR_ABSENT;
        goto cleanup;
    }

    if (bInst) // && !match
    {
       lstrcat(str, _T(","));
       lstrcat(str, str2);
       ret = INP_ERR_ADDED;
       goto cleanup;
    }

    // if (!bInst) && (match)
    {
       lstrcpy(str+(match-target),match+lstrlen(str2)+2);  
       str[lstrlen(str)-1]=_T('\0');
       ret = INP_ERR_REMOVED;
       goto cleanup;
    }

cleanup:

    delete target;
    delete charset;
    return ret;
}

/* Uninstall NSIS */
MSIDLLEXPORT UninstallNsisInstallation( MSIHANDLE hInstall )
{
	DWORD rv = ERROR_SUCCESS;
	// lookup the NSISUNINSTALL property value
	LPTSTR cNsisUninstall = _T("NSISUNINSTALL");
	HANDLE hIo = NULL;
	DWORD dwSize = 0;
	LPTSTR strPathUninst = NULL;
	HANDLE hJob = NULL;
	STARTUPINFO sInfo;
	PROCESS_INFORMATION pInfo;

	pInfo.hProcess = NULL;
	pInfo.hThread = NULL;

	rv = MsiGetProperty( hInstall, cNsisUninstall, _T(""), &dwSize );
	if(rv != ERROR_MORE_DATA) goto _cleanup;

	strPathUninst = new TCHAR[ ++dwSize ];

	rv = MsiGetProperty( hInstall, cNsisUninstall, strPathUninst, &dwSize );
	if(rv != ERROR_SUCCESS) goto _cleanup;

	// Create a process for the uninstaller
	sInfo.cb = sizeof(sInfo);
	sInfo.lpReserved = NULL;
	sInfo.lpDesktop = _T("");
	sInfo.lpTitle = _T("Foo");
	sInfo.dwX = 0;
	sInfo.dwY = 0;
	sInfo.dwXSize = 0;
	sInfo.dwYSize = 0;
	sInfo.dwXCountChars = 0;
	sInfo.dwYCountChars = 0;
	sInfo.dwFillAttribute = 0;
	sInfo.dwFlags = 0;
	sInfo.wShowWindow = 0;
	sInfo.cbReserved2 = 0;
	sInfo.lpReserved2 = 0;
	sInfo.hStdInput = 0;
	sInfo.hStdOutput = 0;
	sInfo.hStdError = 0;

	if(!CreateProcess( 
		strPathUninst,
		_T("Uninstall /S"),
		NULL,
		NULL,
		FALSE,
		CREATE_SUSPENDED,
		NULL,
		NULL,
		&sInfo,
		&pInfo)) {
			pInfo.hProcess = NULL;
			pInfo.hThread = NULL;
			rv = 40;
			goto _cleanup;
		};

	// Create a job object to contain the NSIS uninstall process tree

	JOBOBJECT_ASSOCIATE_COMPLETION_PORT acp;

	acp.CompletionKey = 0;

	hJob = CreateJobObject(NULL, _T("NSISUninstallObject"));
	if(!hJob) {
		rv = 41;
		goto _cleanup;
	}

	hIo = CreateIoCompletionPort(INVALID_HANDLE_VALUE,0,0,0);
	if(!hIo) {
		rv = 42;
		goto _cleanup;
	}

	acp.CompletionPort = hIo;

	SetInformationJobObject( hJob, JobObjectAssociateCompletionPortInformation, &acp, sizeof(acp));

	AssignProcessToJobObject( hJob, pInfo.hProcess );

	ResumeThread( pInfo.hThread );

	DWORD a,b,c;
	for(;;) {
		if(!GetQueuedCompletionStatus(hIo, &a, (PULONG_PTR) &b, (LPOVERLAPPED *) &c, INFINITE)) {
			Sleep(1000);
			continue;
		}
		if(a == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) {
			break;
		}
	}

	rv = ERROR_SUCCESS;
    
_cleanup:
	if(hIo) CloseHandle(hIo);
	if(pInfo.hProcess)	CloseHandle( pInfo.hProcess );
	if(pInfo.hThread) 	CloseHandle( pInfo.hThread );
	if(hJob) CloseHandle(hJob);
	if(strPathUninst) delete strPathUninst;

	if(rv != ERROR_SUCCESS) {
        ShowMsiError( hInstall, ERR_NSS_FAILED, rv );
	}
	return rv;
}
