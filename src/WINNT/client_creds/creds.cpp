/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include "afscreds.h"

extern "C" {
#include <afs\stds.h>
#include <afs\param.h>
#include <afs\auth.h>
#include <afs\kautils.h>
#include <afs\cm_config.h>
}


/*
 * DEFINITIONS ________________________________________________________________
 *
 */

#define cREALLOC_CREDS       4

#define cszLIBTOKENS         TEXT("afsauthent.dll")
#define cszLIBCONF           TEXT("libafsconf.dll")


/*
 * DYNAMIC LINKING ____________________________________________________________
 *
 */

extern "C" {
   typedef unsigned int (*initAFSDirPath_t)(void);
   typedef int (*ka_Init_t)(int flags);
   typedef int (*rx_Init_t)(int port);
   typedef int (*ktc_GetToken_t)(struct ktc_principal *server, struct ktc_token *token, int tokenLen, struct ktc_principal *client);
   typedef int (*ktc_ListTokens_t)(int cellNum, int *cellNumP, struct ktc_principal *serverName);
   typedef int (*ktc_ForgetToken_t)(struct ktc_principal *server);
   typedef int (*ka_UserAuthenticateGeneral_t)(int flags, char *name, char *instance, char *realm, char *password, int lifetime, int *password_expiresP, int spare, char **reasonP);
   typedef long (*cm_GetRootCellName_t)(char *namep);
}

static struct l
   {
   HINSTANCE hInstLibTokens;
   HINSTANCE hInstLibConf;

   initAFSDirPath_t initAFSDirPathP;
   ka_Init_t ka_InitP;
   rx_Init_t rx_InitP;
   ktc_GetToken_t ktc_GetTokenP;
   ktc_ListTokens_t ktc_ListTokensP;
   ktc_ForgetToken_t ktc_ForgetTokenP;
   ka_UserAuthenticateGeneral_t ka_UserAuthenticateGeneralP;
   cm_GetRootCellName_t cm_GetRootCellNameP;
   } l;

#define initAFSDirPath (*l.initAFSDirPathP)
#define ka_Init (*l.ka_InitP)
#define rx_Init (*l.rx_InitP)
#define ktc_GetToken (*l.ktc_GetTokenP)
#define ktc_ListTokens (*l.ktc_ListTokensP)
#define ktc_ForgetToken (*l.ktc_ForgetTokenP)
#define ka_UserAuthenticateGeneral (*l.ka_UserAuthenticateGeneralP)
#define cm_GetRootCellName (*l.cm_GetRootCellNameP)


BOOL Creds_OpenLibraries (void)
{
   if (!l.hInstLibTokens)
      {
      if ((l.hInstLibTokens = LoadLibrary (cszLIBTOKENS)) != NULL)
         {
         l.initAFSDirPathP = (initAFSDirPath_t)GetProcAddress (l.hInstLibTokens, "initAFSDirPath");
         l.ka_InitP = (ka_Init_t)GetProcAddress (l.hInstLibTokens, "ka_Init");
         l.rx_InitP = (rx_Init_t)GetProcAddress (l.hInstLibTokens, "rx_Init");
         l.ktc_GetTokenP = (ktc_GetToken_t)GetProcAddress (l.hInstLibTokens, "ktc_GetToken");
         l.ktc_ListTokensP = (ktc_ListTokens_t)GetProcAddress (l.hInstLibTokens, "ktc_ListTokens");
         l.ktc_ForgetTokenP = (ktc_ForgetToken_t)GetProcAddress (l.hInstLibTokens, "ktc_ForgetToken");
         l.ka_UserAuthenticateGeneralP = (ka_UserAuthenticateGeneral_t)GetProcAddress (l.hInstLibTokens, "ka_UserAuthenticateGeneral");

         if (!l.initAFSDirPathP ||
             !l.ka_InitP ||
             !l.rx_InitP ||
             !l.ktc_GetTokenP ||
             !l.ktc_ListTokensP ||
             !l.ktc_ForgetTokenP ||
             !l.ka_UserAuthenticateGeneralP)
            {
            FreeLibrary (l.hInstLibTokens);
            l.hInstLibTokens = NULL;
            }
         else
            {
            rx_Init(0);
	    initAFSDirPath();
            ka_Init(0);
            }
         }
      }

   if (!l.hInstLibConf)
      {
      if ((l.hInstLibConf = LoadLibrary (cszLIBCONF)) != NULL)
         {
         l.cm_GetRootCellNameP = (cm_GetRootCellName_t)GetProcAddress (l.hInstLibConf, "cm_GetRootCellName");

         if (!l.cm_GetRootCellNameP)
            {
            FreeLibrary (l.hInstLibConf);
            l.hInstLibConf = NULL;
            }
         }
      }

   return l.hInstLibTokens && l.hInstLibConf;
}


void Creds_CloseLibraries (void)
{
   if (l.hInstLibTokens)
      {
      FreeLibrary (l.hInstLibTokens);
      l.hInstLibTokens = NULL;
      }

   if (l.hInstLibConf)
      {
      FreeLibrary (l.hInstLibConf);
      l.hInstLibConf = NULL;
      }
}



/*
 * ROUTINES ___________________________________________________________________
 *
 */

void GetGatewayName (LPTSTR pszGateway)
{
   *pszGateway = TEXT('\0');
   HKEY hk;
   if (RegOpenKey (HKEY_LOCAL_MACHINE, TEXT("System\\CurrentControlSet\\Services\\TransarcAFSDaemon\\Parameters"), &hk) == 0)
      {
      DWORD dwSize = MAX_PATH;
      DWORD dwType = REG_SZ;

      if (RegQueryValueEx (hk, TEXT("Gateway"), NULL, &dwType, (PBYTE)pszGateway, &dwSize) != 0)
         *pszGateway = TEXT('\0');

      RegCloseKey (hk);
      }
}


BOOL IsServiceRunning (void)
{
   if (g.fIsWinNT)
      {
      SERVICE_STATUS Status;
      memset (&Status, 0x00, sizeof(Status));
      Status.dwCurrentState = SERVICE_STOPPED;

      SC_HANDLE hManager;
      if ((hManager = OpenSCManager (NULL, NULL, GENERIC_READ)) != NULL)
         {
         SC_HANDLE hService;
         if ((hService = OpenService (hManager, TEXT("TransarcAFSDaemon"), GENERIC_READ)) != NULL)
            {
            QueryServiceStatus (hService, &Status);
            CloseServiceHandle (hService);
            }

         CloseServiceHandle (hManager);
         }

      return (Status.dwCurrentState == SERVICE_RUNNING);
      }

   TCHAR szGateway[ MAX_PATH ];
   GetGatewayName (szGateway);
   return (szGateway[0]) ? TRUE : FALSE;
}


BOOL IsServicePersistent (void)
{
   struct {
      QUERY_SERVICE_CONFIG Config;
      BYTE buf[1024];
   } Config;
   memset (&Config, 0x00, sizeof(Config));
   Config.Config.dwStartType = SERVICE_AUTO_START;

   SC_HANDLE hManager;
   if ((hManager = OpenSCManager (NULL, NULL, GENERIC_READ)) != NULL)
      {
      SC_HANDLE hService;
      if ((hService = OpenService (hManager, TEXT("TransarcAFSDaemon"), GENERIC_READ)) != NULL)
         {
         DWORD dwSize = sizeof(Config);
         QueryServiceConfig (hService, (QUERY_SERVICE_CONFIG*)&Config, sizeof(Config), &dwSize);

         CloseServiceHandle (hService);
         }

      CloseServiceHandle (hManager);
      }

   return (Config.Config.dwStartType == SERVICE_AUTO_START) ? TRUE : FALSE;
}


BOOL IsServiceConfigured (void)
{
   BOOL rc = FALSE;
   HKEY hk;

   if (!g.fIsWinNT)
      {
      rc = TRUE;
      }
   else if (RegOpenKey (HKEY_LOCAL_MACHINE, TEXT("System\\CurrentControlSet\\Services\\TransarcAFSDaemon\\Parameters"), &hk) == 0)
      {
      TCHAR szCell[ MAX_PATH ];
      DWORD dwSize = sizeof(szCell);
      DWORD dwType = REG_SZ;

      if (RegQueryValueEx (hk, TEXT("Cell"), NULL, &dwType, (PBYTE)szCell, &dwSize) == 0)
         {
         if (szCell[0] != TEXT('\0'))
            rc = TRUE;
         }

      RegCloseKey (hk);
      }

   return rc;
}


int GetCurrentCredentials (void)
{
   int rc = KTC_NOCM;

   // Free any knowledge we currently have about the user's credentials
   //
   if (g.aCreds)
      Free (g.aCreds);
   g.aCreds = NULL;
   g.cCreds = 0;
   g.tickLastRetest = GetTickCount();

   // Start enumerating tokens.
   //
   if (!Creds_OpenLibraries())
      {
      rc = ERROR_DLL_INIT_FAILED;
      }
   else if (IsServiceRunning())
      {
      for (int iCell = 0; ; )
         {
         struct ktc_principal Principal;
         if ((rc = ktc_ListTokens (iCell, &iCell, &Principal)) != 0)
            break;

         struct ktc_token Token;
         struct ktc_principal ClientName;
         if ((rc = ktc_GetToken (&Principal, &Token, sizeof(Token), &ClientName)) != 0)
            break;

         // Translate what we found about the user's creds in this particular
         // cell into something readable.
         //
         TCHAR szCell[ 256 ];
         CopyAnsiToString (szCell, Principal.cell);
         if (!szCell[0])
            continue;

         TCHAR szUser[ 256 ];
         CopyAnsiToString (szUser, ClientName.name);
         if (ClientName.instance[0])
            {
            lstrcat (szUser, TEXT("."));
            CopyAnsiToString (&szUser[ lstrlen(szUser) ], ClientName.instance);
            }

         SYSTEMTIME stExpires;
         TimeToSystemTime (&stExpires, Token.endTime);

         // We've found out that the user has--or perhaps recently had--
         // credentials within a certain cell under the certain name.
         // Stick that knowledge in our g.aCreds array.
         //
         for (size_t iCreds = 0; iCreds < g.cCreds; ++iCreds)
            {
            if (!lstrcmpi (g.aCreds[ iCreds ].szCell, szCell))
               break;
            }
         if (iCreds == g.cCreds)
            {
            for (iCreds = 0; iCreds < g.cCreds; ++iCreds)
               {
               if (!g.aCreds[ iCreds ].szCell[0])
                  break;
               }
            if (!REALLOC (g.aCreds, g.cCreds, 1+iCreds, cREALLOC_CREDS))
               break;
            }

         lstrcpy (g.aCreds[ iCreds ].szCell, szCell);
         lstrcpy (g.aCreds[ iCreds ].szUser, szUser);
         memcpy (&g.aCreds[ iCreds ].stExpires, &stExpires, sizeof(SYSTEMTIME));
         LoadRemind (iCreds);
         }
      }

   // We've finished updating g.aCreds. Update the tray icon to reflect
   // whether the user currently has any credentials at all, and
   // re-enable the Remind timer.
   //
   ChangeTrayIcon (NIM_MODIFY);
   return rc;
}


int DestroyCurrentCredentials (LPCTSTR pszCell)
{
   int rc = KTC_NOCM;

   if (!Creds_OpenLibraries())
      {
      rc = ERROR_DLL_INIT_FAILED;
      }
   else if (IsServiceRunning())
      {
      struct ktc_principal Principal;
      memset (&Principal, 0x00, sizeof(Principal));
      CopyStringToAnsi (Principal.cell, pszCell);
      CopyStringToAnsi (Principal.name, TEXT("afs"));
      rc = ktc_ForgetToken (&Principal);
      }

   if (rc != 0)
      {
      int idsTitle = (g.fIsWinNT) ? IDS_ERROR_TITLE : IDS_ERROR_TITLE_95;
      int idsDesc = (!g.fIsWinNT) ? IDS_ERROR_DESTROY_95 : (rc == KTC_NOCM) ? IDS_ERROR_DESTROY_NOCM : IDS_ERROR_DESTROY_UNKNOWN;
      Message (MB_ICONHAND | MB_OK, idsTitle, idsDesc, TEXT("%s%ld"), pszCell, rc);
      }

   return rc;
}


int ObtainNewCredentials (LPCTSTR pszCell, LPCTSTR pszUser, LPCTSTR pszPassword)
{
   int rc = KTC_NOCM;
   char *Result = NULL;

   if (!Creds_OpenLibraries())
      {
      rc = ERROR_DLL_INIT_FAILED;
      }
   else if (IsServiceRunning())
      {
      char szCellA[ 256 ];
      CopyStringToAnsi (szCellA, pszCell);

      char szNameA[ 256 ];
      CopyStringToAnsi (szNameA, pszUser);

      char szPasswordA[ 256 ];
      CopyStringToAnsi (szPasswordA, pszPassword);

      int Expiration = 0;

      rc = ka_UserAuthenticateGeneral(KA_USERAUTH_VERSION, szNameA, "", szCellA, szPasswordA, 0, &Expiration, 0, &Result);
      }

   if (rc != 0)
      {
      int idsTitle = (g.fIsWinNT) ? IDS_ERROR_TITLE : IDS_ERROR_TITLE_95;
      int idsDesc = (g.fIsWinNT) ? IDS_ERROR_OBTAIN : IDS_ERROR_OBTAIN_95;
      Message (MB_ICONHAND | MB_OK, idsTitle, idsDesc, TEXT("%s%s%s%ld"), pszCell, pszUser, (Result) ? Result : TEXT(""), rc);
      }

   return rc;
}


int GetDefaultCell (LPTSTR pszCell)
{
   int rc = KTC_NOCM;
   *pszCell = TEXT('\0');

   if (!Creds_OpenLibraries())
      {
      rc = ERROR_DLL_INIT_FAILED;
      }
   else if (IsServiceRunning())
      {
      char szCellA[ 256 ];

      int rc;
      if ((rc = cm_GetRootCellName (szCellA)) == 0)
         CopyAnsiToString (pszCell, szCellA);
      }

   return rc;
}

