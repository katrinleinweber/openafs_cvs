/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

/*
 * Test of the curses package, to make sure I really understand how to use it.
 */

#include <afsconfig.h>
#include <afs/param.h>

RCSID
    ("$Header: /cvs/openafs/src/gtx/curses_test.c,v 1.5 2003/07/15 23:15:12 shadow Exp $");

#if defined(AFS_HPUX110_ENV) && !defined(__HP_CURSES)
#define __HP_CURSES
#endif

#include <curses.h>

#include "AFS_component_version_number.c"

main(argc, argv)
     int argc;
     char **argv;

{				/*main */

    WINDOW *mainscr;
    char str[128];
    int i;

    mainscr = initscr();
    scrollok(stdscr, TRUE);
    clear();
    addstr("This is my first curses string ever!\n");
    refresh();
    box(stdscr, '|', '-');
    standout();
    addstr("This is a standout string\n");
    refresh();
    standend();

#if 0
    box addstr("Enter a string and a number: ");
    refresh();
    scanw(stdscr, "%s %d", str, &i);
    wprintw(stdscr, "String was '%s', number was %d\n", str, i);
    refresh();
#endif /* 0 */

    endwin();

}				/*main */
