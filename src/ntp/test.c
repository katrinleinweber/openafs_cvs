/*
 * Copyright 2000, International Business Machines Corporation and others.
 * All Rights Reserved.
 * 
 * This software has been released under the terms of the IBM Public
 * License.  For details, see the LICENSE file in the top-level source
 * directory or online at http://www.openafs.org/dl/license10.html
 */

#include <sys/param.h>
#include <afsconfig.h>

RCSID("$Header: /cvs/openafs/src/ntp/Attic/test.c,v 1.4 2001/08/08 00:03:53 shadow Exp $");

#include <afs/stds.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>

#include "ntp.h"

#define	TRUE	1
#define	FALSE	0

int test1(), test2(), test3(), test4(), test5(), test6();

double value[8] = {5.1, -5.1, 1.5, -1.5, 0.5, -0.5, -0.05, 0.0};

#include "AFS_component_version_number.c"

main(argc, argv)
	int argc;
	char **argv;
{
	if (argc > 1 && strcmp(argv[1], "-v") == 0) {
		int error;
		error = test1(1);
		error += test2(1);
		error += test3(1);
		error += test4(1);
		error += test5(1);
		error += test6(1);
		exit (error);
	} else {
		if (test2(0))
			exit(2);
		if (test3(0))
			exit(3);
		if (test4(0))
			exit(4);
		if (test5(0))
			exit(5);
		if (test6(0))
			exit(6);
	}
	exit(0);
}


test1()
{
	int i;
	double l_fixed_to_double();
	struct l_fixedpt sample;
	double s_fixed_to_double();
	struct s_fixedpt s_sample;

	for (i = 0; i < 8; i++) {
		printf(" %4.2f ", value[i]);
		double_to_l_fixed(&sample, value[i]);
		printf(" x%#8X.%#8X ", sample.int_part, sample.fraction);
#if 0
		printf(" %4.2f", l_fixed_to_double(&sample));
#endif
		printf("\t");
		double_to_s_fixed(&s_sample, value[i]);
		printf(" x%#4X.%#4X ", s_sample.int_part, s_sample.fraction);
		printf(" %4.2f\n", s_fixed_to_double(&s_sample));
	}
	return 0;
}

test2(v)
  int v;				/* verbose */
{
	struct timeval tp;
	struct l_fixedpt time_lm;

	(void)gettimeofday(&tp, (struct timezone *) 0);
	tstamp(&time_lm, &tp);

	if (v) {
	    printf("tv_sec:  %d tv_usec:  %d \n", tp.tv_sec, tp.tv_usec);
	    printf("intpart: %lu fraction: %lu \n",
		   ntohl(time_lm.int_part), ntohl(time_lm.fraction));
	    printf("intpart: %lX fraction: %lX \n",
		   ntohl(time_lm.int_part), ntohl(time_lm.fraction));
	}
	{   extern double ul_fixed_to_double();
	    double d;
	    d = ul_fixed_to_double (&time_lm);
	    if (v) printf ("ul_fixed_to_double returns %f\n", d);
	    if (d < 0) {
		printf ("ul_ftd returned negative number\n");
		return 1;
	    }
	    d = -d;
	    double_to_l_fixed (&time_lm, d);
	    if (v) printf("intpart: %lX fraction: %lX \n",
			  ntohl(time_lm.int_part), ntohl(time_lm.fraction));
	    d = ul_fixed_to_double (&time_lm);
	    if (d < 0) {
		printf ("second ul_ftd returned negative number\n");
		return 1;
	    }
	    if (v) printf ("ul_fixed_to_double(double_to_l_fixed(-d)) returns %f\n", d);
	}
	if (v) printf("test2 passes\n");
	return 0;
}

test3(v)
	int v;
{
	afs_uint32 ul = 0x80000001;
	double dbl;

#ifdef	GENERIC_UNS_BUG
	/*
	 *  Hopefully, we can avoid the unsigned issue altogether.  Make sure
	 *  that the high-order (sign) bit is zero, and fiddle from there 
	 */
	dbl = (afs_int32)((ul >> 1) & 0x7fffffff);
	dbl *= 2.0;
	if (ul & 1)
		dbl += 1.0;
#else
	dbl = ul;
#ifdef	VAX_COMPILER_FLT_BUG
	if (dbl < 0.0) dbl += 4.294967296e9;
#endif
#endif
	if (dbl != 2147483649.0) {
		printf("test3 fails: can't convert from unsigned long to float\n");
		printf("             (%lu != %15g)\n", ul, dbl);
		printf(
  "Try defining VAX_COMPILER_FLT_BUG or GENERIC_UNS_BUG in the Makefile.\n");
		return 1;
	} else {
		if (v)
			printf("test3 passes\n");
		return 0;
	}
}

test4(v)
	int v;
{
	double dbl = 1024.0 * 1024.0 * 1024.0;	/* 2^30 */
#ifdef SUN_FLT_BUG
	int l = 1.5 * dbl;
	afs_uint32 ul = (l<<1);
#else
	afs_uint32 ul = 3.0 * dbl;			/* between 2^31 and 2^32 */
#endif
	if (v)
		printf("test4: 3.0*1024.0*1024.0*1024.0 = 0x%08x\n", ul);

	if (ul != 0xc0000000) {
		printf("test4 fails:\n");
		printf("Can't convert unsigned long to double.\n");
		printf("Try defining SUN_FLT_BUG in the Makefile\n");
		return 1;
	} else {
	    if (v)
		printf("test4 passes\n");
	    return 0;
	}
}

/* test5 - check for sign extension of int:8 in pkt precision variable. */

test5(v)
	int v;
{
    struct ntpdata pkt;
    struct ntp_peer peer;
    struct sysdata sys;
    double delay;

    memset(&peer, 0, sizeof(peer));
    memset(&sys, 0, sizeof(sys));
    pkt.precision = -6;
    peer.precision = pkt.precision;
    sys.precision = pkt.precision;
    if ((pkt.precision != -6) ||
	(peer.precision != -6) ||
	(sys.precision != -6)) {
	printf ("pkt %d, peer %d, sys %d, all should be %d\n",
		pkt.precision, peer.precision, sys.precision, -6);
	return 1;
    }
    delay = 0;
    delay += 1.0/(afs_uint32)(1L << -peer.precision)
	+ ((peer.flags&PEER_FL_REFCLOCK) ? NTP_REFMAXSKW : NTP_MAXSKW);
    if (peer.precision < 0 && -peer.precision < sizeof(afs_int32)*NBBY)
	delay += 1.0/(afs_uint32)(1L << -peer.precision);
    if (v) printf ("delay is %f\n", delay);

    if ((delay - 0.041249) > .000002) {
	printf ("delay calculation in error: delay was %d, should be %d\n",
		delay, 0.04125);
	return -1;
    }
    if (v) printf("test5 passes\n");
    return 0;
}

/* test6 - calculates the machine clock's apparent precision. */

int test6(v)
  int v;
{
    int interval = 0;
    int precision;
    int code;
#if	defined(AFS_SUN_ENV) || defined(AFS_AIX_ENV) || defined(AFS_HPUX_ENV)
#define	PRECISION	-20
#else
#define	PRECISION	-18
#endif


    code = 0;
    precision = MeasurePrecision(&interval);
    if ((precision >= 0) || (-precision >= sizeof(afs_int32)*NBBY)) {
	printf ("Couldn't measure precision\n");
	code = 1;
    }
    if (precision < PRECISION) {
	printf ("Precision (%d) seems too small (best interval measured to date: 13 usec, 10/10/94). -JPM\n", precision);
	printf ("Make certain that the reported interval is accurate (possible), and then relax\n");
	printf ("this check only if appropriate.\n");
	code = 2;
    }
    if (v || code) {
	printf
	    ("precision is %d (%f) from measured interval of %d usec (%f)\n",
	     precision, 1.0 / (afs_uint32)(1L << -precision),
	     interval, interval / 1000000.0);
	if (!code) printf ("test6 passes\n");
    }
    return code;
}
