/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

extern "C" {
#include <afs/param.h>
#include <afs/stds.h>
}

#include "afscreds.h"


/*
 * PROTOTYPES _________________________________________________________________
 *
 */

void Creds_OnUpdate (HWND hDlg);
void Creds_OnCheckRemind (HWND hDlg);
void Creds_OnClickObtain (HWND hDlg);
void Creds_OnClickDestroy (HWND hDlg);

BOOL CALLBACK NewCreds_DlgProc (HWND hDlg, UINT msg, WPARAM wp, LPARAM lp);
void NewCreds_OnInitDialog (HWND hDlg);
void NewCreds_OnEnable (HWND hDlg);
BOOL NewCreds_OnOK (HWND hDlg);
void NewCreds_OnCancel (HWND hDlg);


/*
 * ROUTINES ___________________________________________________________________
 *
 */

BOOL CALLBACK Creds_DlgProc (HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
   switch (msg)
      {
      case WM_INITDIALOG:
         RECT rTab;
         GetClientRect (GetParent(hDlg), &rTab);
         TabCtrl_AdjustRect (GetParent (hDlg), FALSE, &rTab); 
         SetWindowPos (hDlg, NULL, rTab.left, rTab.top, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);

         SetWindowLong (hDlg, DWL_USER, lp);
         Creds_OnUpdate (hDlg);
         break;

      case WM_COMMAND:
         switch (LOWORD(wp))
            {
            case IDC_CREDS_REMIND:
               Creds_OnCheckRemind (hDlg);
               break;

            case IDC_CREDS_OBTAIN:
               Creds_OnClickObtain (hDlg);
               break;

            case IDC_CREDS_DESTROY:
               Creds_OnClickDestroy (hDlg);
               break;

            case IDHELP:
               Creds_DlgProc (hDlg, WM_HELP, 0, 0);
               break;
            }
         break;

      case WM_HELP:
         if (IsWindow (GetDlgItem (hDlg, IDC_CREDS_REMIND)))
            WinHelp (hDlg, g.szHelpFile, HELP_CONTEXT, IDH_AFSCREDS_TAB_TOKENS);
         else if (IsServiceRunning())
            WinHelp (hDlg, g.szHelpFile, HELP_CONTEXT, IDH_AFSCREDS_TAB_NOTOKENS_RUNNING);
         else // (!IsServiceRunning())
            WinHelp (hDlg, g.szHelpFile, HELP_CONTEXT, IDH_AFSCREDS_TAB_NOTOKENS_STOPPED);
         break;
      }

   return FALSE;
}


void Creds_OnCheckRemind (HWND hDlg)
{
   LPTSTR pszCell = (LPTSTR)GetWindowLong (hDlg, DWL_USER);
   for (size_t iCreds = 0; iCreds < g.cCreds; ++iCreds)
      {
      if (!lstrcmpi (g.aCreds[ iCreds ].szCell, pszCell))
         break;
      }

   if (iCreds != g.cCreds)
      {
      g.aCreds[ iCreds ].fRemind = IsDlgButtonChecked (hDlg, IDC_CREDS_REMIND);
      SaveRemind (iCreds);
      }
}


void Creds_OnUpdate (HWND hDlg)
{
   LPTSTR pszCell = (LPTSTR)GetWindowLong (hDlg, DWL_USER);
   if (!pszCell || !*pszCell)
      {
      BOOL fRunning = IsServiceRunning();
      ShowWindow (GetDlgItem (hDlg, IDC_RUNNING), fRunning);
      ShowWindow (GetDlgItem (hDlg, IDC_STOPPED), !fRunning);
      ShowWindow (GetDlgItem (hDlg, IDC_CREDS_OBTAIN), fRunning);
      return;
      }

   for (size_t iCreds = 0; iCreds < g.cCreds; ++iCreds)
      {
      if (!lstrcmpi (g.aCreds[ iCreds ].szCell, pszCell))
         break;
      }

   TCHAR szGateway[cchRESOURCE] = TEXT("");
   if (!g.fIsWinNT)
      GetGatewayName (szGateway);

   if (!szGateway[0])
      {
      SetDlgItemText (hDlg, IDC_CREDS_CELL, pszCell);
      }
   else
      {
      TCHAR szCell[ cchRESOURCE ];
      TCHAR szFormat[ cchRESOURCE ];
      GetString (szFormat, IDS_CELL_GATEWAY);
      wsprintf (szCell, szFormat, pszCell, szGateway);
      SetDlgItemText (hDlg, IDC_CREDS_CELL, szCell);
      }

   if (iCreds == g.cCreds)
      {
      TCHAR szText[cchRESOURCE];
      GetString (szText, IDS_NO_CREDS);
      SetDlgItemText (hDlg, IDC_CREDS_INFO, szText);
      }
   else
      {
      // FormatString(%t) expects a date in GMT, not the local time zone...
      //
      FILETIME ftLocal;
      SystemTimeToFileTime (&g.aCreds[ iCreds ].stExpires, &ftLocal);

      FILETIME ftGMT;
      LocalFileTimeToFileTime (&ftLocal, &ftGMT);

      SYSTEMTIME stGMT;
      FileTimeToSystemTime (&ftGMT, &stGMT);

      LPTSTR pszCreds = FormatString (IDS_CREDS, TEXT("%s%t"), g.aCreds[ iCreds ].szUser, &stGMT);
      SetDlgItemText (hDlg, IDC_CREDS_INFO, pszCreds);
      FreeString (pszCreds);
      }

   CheckDlgButton (hDlg, IDC_CREDS_REMIND, (iCreds == g.cCreds) ? FALSE : g.aCreds[iCreds].fRemind);

   EnableWindow (GetDlgItem (hDlg, IDC_CREDS_OBTAIN), IsServiceRunning());
   EnableWindow (GetDlgItem (hDlg, IDC_CREDS_REMIND), (iCreds != g.cCreds));
   EnableWindow (GetDlgItem (hDlg, IDC_CREDS_DESTROY), (iCreds != g.cCreds));
}


void Creds_OnClickObtain (HWND hDlg)
{
   LPTSTR pszCell = (LPTSTR)GetWindowLong (hDlg, DWL_USER);

   InterlockedIncrement (&g.fShowingMessage);
   ShowObtainCreds (FALSE, pszCell);
}


void Creds_OnClickDestroy (HWND hDlg)
{
   LPTSTR pszCell = (LPTSTR)GetWindowLong (hDlg, DWL_USER);
   if (pszCell && *pszCell)
      {
      DestroyCurrentCredentials (pszCell);
      Main_RepopulateTabs (FALSE);
      Creds_OnUpdate (hDlg);
      }
}


void ShowObtainCreds (BOOL fExpiring, LPTSTR pszCell)
{
   HWND hParent = (IsWindowVisible (g.hMain)) ? g.hMain : NULL;

   if (fExpiring)
      {
      ModalDialogParam (IDD_NEWCREDS_EXPIRE, hParent, (DLGPROC)NewCreds_DlgProc, (LPARAM)pszCell);
      }
   else // (!fExpiring)
      {
      ModalDialogParam (IDD_NEWCREDS, hParent, (DLGPROC)NewCreds_DlgProc, (LPARAM)pszCell);
      }
}


BOOL CALLBACK NewCreds_DlgProc (HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
   switch (msg)
      {
      case WM_INITDIALOG:
         SetWindowLong (hDlg, DWL_USER, lp);
         NewCreds_OnInitDialog (hDlg);
         break;

      case WM_DESTROY:
         InterlockedDecrement (&g.fShowingMessage);
         Main_EnableRemindTimer (TRUE);
         break;

      case WM_COMMAND:
         switch (LOWORD(wp))
            {
            case IDOK:
               if (NewCreds_OnOK (hDlg))
                  EndDialog (hDlg, IDOK);
               break;

            case IDCANCEL:
               NewCreds_OnCancel (hDlg);
               EndDialog (hDlg, IDCANCEL);
               break;

            case IDC_NEWCREDS_USER:
            case IDC_NEWCREDS_PASSWORD:
               NewCreds_OnEnable (hDlg);
               break;

            case IDHELP:
               NewCreds_DlgProc (hDlg, WM_HELP, 0, 0);
               break;
            }
         break;

      case WM_HELP:
         WinHelp (hDlg, g.szHelpFile, HELP_CONTEXT, IDH_AFSCREDS_NEWTOKENS);
         break;
      }
   return FALSE;
}


void NewCreds_OnInitDialog (HWND hDlg)
{
   LPTSTR pszCell = (LPTSTR)GetWindowLong (hDlg, DWL_USER);
   if (!pszCell)
      pszCell = TEXT("");

   if (GetDlgItem (hDlg, IDC_NEWCREDS_TITLE))
      {
      TCHAR szText[ cchRESOURCE ];
      GetDlgItemText (hDlg, IDC_NEWCREDS_TITLE, szText, cchRESOURCE);
      LPTSTR pszText = FormatString (szText, TEXT("%s"), pszCell);
      SetDlgItemText (hDlg, IDC_NEWCREDS_TITLE, pszText);
      FreeString (pszText);
      }

   if (pszCell && *pszCell)
      {
      SetDlgItemText (hDlg, IDC_NEWCREDS_CELL, pszCell);
      }
   else
      {
      TCHAR szCell[ cchRESOURCE ] = TEXT("");
      (void)GetDefaultCell (szCell);
      SetDlgItemText (hDlg, IDC_NEWCREDS_CELL, szCell);
      }

   for (size_t iCreds = 0; iCreds < g.cCreds; ++iCreds)
      {
      if (*pszCell && !lstrcmpi (g.aCreds[ iCreds ].szCell, pszCell))
         break;
      }
   if ((iCreds == g.cCreds) || (!g.aCreds[ iCreds ].szUser[0]))
      {
      PostMessage (hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem (hDlg, IDC_NEWCREDS_USER), TRUE);
      }
   else // (we have a valid username already)
      {
      SetDlgItemText (hDlg, IDC_NEWCREDS_USER, g.aCreds[ iCreds ].szUser);
      PostMessage (hDlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem (hDlg, IDC_NEWCREDS_PASSWORD), TRUE);
      }

   NewCreds_OnEnable (hDlg);
   KillTimer (g.hMain, ID_SERVICE_TIMER);
}


void NewCreds_OnEnable (HWND hDlg)
{
   BOOL fEnable = TRUE;

   TCHAR szUser[ cchRESOURCE ];
   GetDlgItemText (hDlg, IDC_NEWCREDS_USER, szUser, cchRESOURCE);
   if (!szUser[0])
      fEnable = FALSE;

   TCHAR szPassword[ cchRESOURCE ];
   GetDlgItemText (hDlg, IDC_NEWCREDS_PASSWORD, szPassword, cchRESOURCE);
   if (!szPassword[0])
      fEnable = FALSE;

   EnableWindow (GetDlgItem (hDlg, IDOK), fEnable);
}


BOOL NewCreds_OnOK (HWND hDlg)
{
   TCHAR szCell[ cchRESOURCE ];
   GetDlgItemText (hDlg, IDC_NEWCREDS_CELL, szCell, cchRESOURCE);

   TCHAR szUser[ cchRESOURCE ];
   GetDlgItemText (hDlg, IDC_NEWCREDS_USER, szUser, cchRESOURCE);

   TCHAR szPassword[ cchRESOURCE ];
   GetDlgItemText (hDlg, IDC_NEWCREDS_PASSWORD, szPassword, cchRESOURCE);

   int rc;
   if ((rc = ObtainNewCredentials (szCell, szUser, szPassword)) != 0)
      {
      EnableWindow (GetDlgItem (hDlg, IDOK), TRUE);
      EnableWindow (GetDlgItem (hDlg, IDCANCEL), TRUE);
      EnableWindow (GetDlgItem (hDlg, IDC_NEWCREDS_CELL), TRUE);
      EnableWindow (GetDlgItem (hDlg, IDC_NEWCREDS_USER), TRUE);
      EnableWindow (GetDlgItem (hDlg, IDC_NEWCREDS_PASSWORD), TRUE);
      return FALSE;
      }

   Main_RepopulateTabs (FALSE);
   return TRUE;
}


void NewCreds_OnCancel (HWND hDlg)
{
   LPTSTR pszCell = (LPTSTR)GetWindowLong (hDlg, DWL_USER);
   if (pszCell)
      {
      for (size_t iCreds = 0; iCreds < g.cCreds; ++iCreds)
         {
         if (!lstrcmpi (g.aCreds[ iCreds ].szCell, pszCell))
            {
            g.aCreds[ iCreds ].fRemind = FALSE;
            SaveRemind (iCreds);

            // Check the active tab, and fix its checkbox if necessary
            //
            HWND hTab = GetDlgItem (g.hMain, IDC_TABS);
            LPTSTR pszTab = (LPTSTR)GetTabParam (hTab, TabCtrl_GetCurSel(hTab));
            if (pszTab && HIWORD(pszTab) && (!lstrcmpi (pszTab, pszCell)))
               {
               HWND hDlg = GetTabChild (hTab);
               if (hDlg)
                  CheckDlgButton (hDlg, IDC_CREDS_REMIND, FALSE);
               }
            }
         }
      }
}

