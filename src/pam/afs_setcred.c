/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>
#include <afs/param.h>
#include <afsconfig.h>

RCSID("$Header: /cvs/openafs/src/pam/afs_setcred.c,v 1.4 2001/07/05 15:20:39 shadow Exp $");

#include <sys/param.h>
#include <afs/kautils.h>
#include "afs_message.h"
#include "afs_util.h"



#define RET(x) { retcode = (x); goto out; }

#if defined(AFS_KERBEROS_ENV)
extern char *ktc_tkt_string();
#endif

extern int
pam_sm_setcred(
	pam_handle_t	*pamh,
	int		flags,
	int		argc,
	const char	**argv)
{
    int retcode = PAM_SUCCESS;
    int errcode = PAM_SUCCESS;
    int origmask;
    int logmask = LOG_UPTO(LOG_INFO);
    int nowarn = 0;
    int use_first_pass = 1; /* use the password passed in by auth */
    int try_first_pass = 0;
    int got_authtok = 0;
    int ignore_root = 0;
    int trust_root = 0;
    int set_expires = 0; /* the default is to not to set the env variable */
    int i;
    struct pam_conv *pam_convp = NULL;
    char my_password_buf[256];
    char sbuffer[100];
    char *password = NULL;
    int torch_password = 1;
    int auth_ok = 0;
    char*	lh;
    char *user = NULL;
    long password_expires= -1;
    char*	reason = NULL;
    struct passwd unix_pwd, *upwd = NULL;
    char upwd_buf[2048];        /* size is a guess. */

#ifndef AFS_SUN56_ENV
    openlog(pam_afs_ident, LOG_CONS, LOG_AUTH);
#endif
    origmask = setlogmask(logmask);

    /*
     * Parse the user options.  Log an error for any unknown options.
     */
    for (i = 0; i < argc; i++) {
	if (	   strcasecmp(argv[i], "debug"	       ) == 0) {
	    logmask |= LOG_MASK(LOG_DEBUG);
	    (void) setlogmask(logmask);
	} else if (strcasecmp(argv[i], "nowarn"	       ) == 0) {
	    nowarn = 1;
	} else if (strcasecmp(argv[i], "use_first_pass") == 0) {
	    use_first_pass = 1; /* practically redundant */
	} else if (strcasecmp(argv[i], "try_first_pass") == 0) {
	    try_first_pass = 1;
        } else if (strcasecmp(argv[i], "ignore_root"   ) == 0) {
            ignore_root = 1;
        } else if (strcasecmp(argv[i], "trust_root"   ) == 0) {
            trust_root = 1;
        } else if (strcasecmp(argv[i], "catch_su"   ) == 0) {
            use_first_pass = 0;
	} else if (strcasecmp(argv[i], "setenv_password_expires")==0) {
	    set_expires = 1;
	} else {
	    pam_afs_syslog(LOG_ERR, PAMAFS_UNKNOWNOPT, argv[i]);
	}
    }

    if (use_first_pass) try_first_pass = 0;

    pam_afs_syslog(LOG_DEBUG, PAMAFS_OPTIONS, nowarn, use_first_pass, try_first_pass);
    /* Try to get the user-interaction info, if available. */
    errcode = pam_get_item(pamh, PAM_CONV, (void **) &pam_convp);
    if (errcode != PAM_SUCCESS) {
	pam_afs_syslog(LOG_DEBUG, PAMAFS_NO_USER_INT);
	pam_convp = NULL;
    }

    /* Who are we trying to authenticate here? */
    if ((errcode = pam_get_user(pamh, &user, "AFS username:")) != PAM_SUCCESS) {
        pam_afs_syslog(LOG_ERR, PAMAFS_NOUSER, errcode);
        RET(PAM_USER_UNKNOWN);
    }
    /*
     * If the user has a "local" (or via nss, possibly nss_dce) pwent,
     * and its uid==0, and "ignore_root" was given in pam.conf,
     * ignore the user.
     */
#if	defined(AFS_HPUX_ENV)
#if     defined(AFS_HPUX110_ENV)
    i = getpwnam_r(user, &unix_pwd, upwd_buf, sizeof(upwd_buf), &upwd);
#else   /* AFS_HPUX110_ENV */
    i = getpwnam_r(user, &unix_pwd, upwd_buf, sizeof(upwd_buf));
    if ( i == 0 )                       /* getpwnam_r success */
        upwd = &unix_pwd;
#endif  /* AFS_HPUX110_ENV */
    if (ignore_root && i == 0 && upwd->pw_uid == 0) {
        pam_afs_syslog(LOG_INFO, PAMAFS_IGNORINGROOT, user);
        RET(PAM_AUTH_ERR);
    }
#else
#ifdef AFS_LINUX20_ENV
    upwd = getpwnam(user);
#else
    upwd = getpwnam_r(user, &unix_pwd, upwd_buf, sizeof(upwd_buf));
#endif
    if (upwd != NULL && upwd->pw_uid == 0) {
	if (ignore_root) { 
		pam_afs_syslog(LOG_INFO, PAMAFS_IGNORINGROOT, user);
		RET(PAM_AUTH_ERR);
	} else if (trust_root) {
		pam_afs_syslog(LOG_INFO, PAMAFS_TRUSTROOT, user);
		RET(PAM_SUCCESS);
	}
    }
#endif

    if (flags & PAM_DELETE_CRED) {
	pam_afs_syslog(LOG_DEBUG, PAMAFS_DELCRED, user);

	RET(PAM_SUCCESS);
    } else if (flags & PAM_REINITIALIZE_CRED) {

        pam_afs_syslog(LOG_DEBUG, PAMAFS_REINITCRED, user);
        RET(PAM_SUCCESS);

    } else { /* flags are PAM_REFRESH_CRED, PAM_ESTABLISH_CRED, unknown */

	pam_afs_syslog(LOG_DEBUG, PAMAFS_ESTABCRED, user);

	errcode = pam_get_data(pamh, pam_afs_lh, (const void **) &password);
	if (errcode != PAM_SUCCESS || password == NULL) {
	    if (use_first_pass) {
		pam_afs_syslog(LOG_ERR, PAMAFS_PASSWD_REQ, user);
		RET(PAM_AUTH_ERR);
	    }
	    password = NULL;	/* In case it isn't already NULL */
	    pam_afs_syslog(LOG_DEBUG, PAMAFS_NOFIRSTPASS, user);
	} else if (password[0] == '\0') {
	    /* Actually we *did* get one but it was empty. */
	    got_authtok = 1;
	    torch_password = 0;
	    /* So don't use it. */
	    password = NULL;
	    if (use_first_pass) {
		pam_afs_syslog(LOG_ERR, PAMAFS_PASSWD_REQ, user);
		RET(PAM_NEW_AUTHTOK_REQD);
	    }
	    pam_afs_syslog(LOG_DEBUG, PAMAFS_NILPASSWORD, user);
	} else {
	    pam_afs_syslog(LOG_DEBUG, PAMAFS_GOTPASS, user);
	    torch_password = 0;
	    got_authtok = 1;
	}
	if (!(use_first_pass || try_first_pass)) {
	    password = NULL;
	}

    try_auth:
	if (password == NULL) {

	    torch_password = 1;

	    if (use_first_pass)
		RET(PAM_AUTH_ERR);	/* shouldn't happen */
	    if (try_first_pass)
		try_first_pass = 0;	/* we come back if try_first_pass==1 below */
	    
	    if (pam_convp == NULL || pam_convp->conv == NULL) {
		pam_afs_syslog(LOG_ERR, PAMAFS_CANNOT_PROMPT);
		RET(PAM_AUTH_ERR);
	    }
	    
	    errcode = pam_afs_prompt(pam_convp, &password, 0, PAMAFS_PWD_PROMPT);
	    if (errcode != PAM_SUCCESS || password == NULL) {
		pam_afs_syslog(LOG_ERR, PAMAFS_GETPASS_FAILED);
		RET(PAM_AUTH_ERR);
	    }
	    if (password[0] == '\0') {
		pam_afs_syslog(LOG_DEBUG, PAMAFS_NILPASSWORD);
		RET(PAM_NEW_AUTHTOK_REQD);
	    }
	    /*
	     * We aren't going to free the password later (we will wipe it,
	     * though), because the storage for it if we get it from other
	     * paths may belong to someone else.  Since we do need to free
	     * this storage, copy it to a buffer that won't need to be freed
	     * later, and free this storage now.
	     */
	    strncpy(my_password_buf, password, sizeof(my_password_buf));
	    my_password_buf[sizeof(my_password_buf)-1] = '\0';
	    memset(password, 0, strlen(password));
	    free(password);
	    password = my_password_buf;
	}

	if ( flags & PAM_REFRESH_CRED ) {
            if ( ka_VerifyUserPassword(
                           KA_USERAUTH_VERSION + KA_USERAUTH_DOSETPAG,
                           user, /* kerberos name */
                           (char *)0, /* instance */
                           (char *)0, /* realm */
                            password, /* password */
                            0, /* spare 2 */
                            &reason /* error string */
                            )) {
	        pam_afs_syslog(LOG_ERR, PAMAFS_LOGIN_FAILED, user, reason);
	    } else {
		auth_ok = 1;
	    }	
	}
	    
	if (  flags & PAM_ESTABLISH_CRED ) {
	    if ( ka_UserAuthenticateGeneral(
                           KA_USERAUTH_VERSION + KA_USERAUTH_DOSETPAG,
                           user, /* kerberos name */
                           (char *)0, /* instance */
                           (char *)0, /* realm */
                            password, /* password */
                            0, /* default lifetime */
                            &password_expires,
                            0, /* spare 2 */
                            &reason /* error string */
                            )) {
		pam_afs_syslog(LOG_ERR, PAMAFS_LOGIN_FAILED, user, reason);
	    } else {
	       auth_ok = 1;
	    }
	}

	if (!auth_ok && try_first_pass) {
	    password = NULL;
	    goto try_auth;
	}

	if (auth_ok && !got_authtok) {
	    torch_password = 0;
	    (void) pam_set_item(pamh, PAM_AUTHTOK, password);
	}

	if (auth_ok) {
	    if (set_expires &&  (password_expires >= 0) ) {
		strcpy(sbuffer, "PASSWORD_EXPIRES=");
		strcat(sbuffer, cv2string(&sbuffer[100], password_expires));
		errcode = pam_putenv( pamh, sbuffer); 
		if ( errcode != PAM_SUCCESS )
		    pam_afs_syslog(LOG_ERR, PAMAFS_PASSEXPFAIL, user);
	    }
#if defined(AFS_KERBEROS_ENV)
    	    if (upwd)
    	    {
        	if ( chown(ktc_tkt_string(), upwd->pw_uid, upwd->pw_gid) < 0 )
		    pam_afs_syslog(LOG_ERR, PAMAFS_CHOWNKRB, user);
		sprintf(sbuffer, "KRBTKFILE=%s", ktc_tkt_string());
		errcode = pam_putenv( pamh, sbuffer);
                if ( errcode != PAM_SUCCESS )
                    pam_afs_syslog(LOG_ERR, PAMAFS_KRBFAIL, user);
    	    }
#endif

	    RET(PAM_SUCCESS);
	} else {
	    RET(PAM_CRED_ERR);
	}
    }

 out:
    if (password && torch_password) memset(password, 0, strlen(password));
    (void) setlogmask(origmask);
#ifndef AFS_SUN56_ENV
    closelog();
#endif
    return retcode;
}
