/*
 * Andrew configuration.
 */

#ifdef vax
#include "conf-bsdvax.h"
#else
#if mips && !defined(sgi)
#include "conf-mips.h"
#else
#if	defined(sun) && !defined(AFS_X86_ENV)
#include "conf-bsd-sun.h"
#else
#ifdef	AFS_AIX_ENV
#include "conf-aix-ibm.h"
#else
#ifdef mac2
#include "conf-bsd-mac.h"
#else
#ifdef AFS_HPUX_ENV
#ifdef	hp9000s300
#include "conf-hp9000s300.h"
#else
#include "conf-hp9000s700.h"
#endif
#else
#ifdef NeXT
#include "conf-next.h"
#else
#if defined(sgi)
#include "conf-sgi.h"
#else
#if defined(__alpha) && !defined(AFS_ALPHA_LINUX20_ENV)
#include "conf-bsd-alpha.h"
#else
#if	defined(AFS_X86_ENV)
#include "conf-bsd-ncr.h"
#else
#ifdef AFS_NT40_ENV
#include "conf-winnt.h"
#else
#ifdef AFS_XBSD_ENV
#include "conf-i386-obsd.h"
#else
#if defined(AFS_LINUX20_ENV) || defined(AFS_DJGPP_ENV)
#ifdef AFS_PARISC_LINUX20_ENV
#include "conf-parisc-linux.h"
#else
#ifdef AFS_PPC_LINUX20_ENV
#include "conf-ppc-linux.h"
#else
#ifdef AFS_SPARC_LINUX20_ENV
#include "conf-sparc-linux.h"
#else
#ifdef AFS_SPARC64_LINUX20_ENV
#include "conf-sparc64-linux.h"
#else
#ifdef AFS_S390_LINUX20_ENV
#include "conf-s390-linux.h"
#else
#ifdef AFS_ALPHA_LINUX20_ENV
#include "conf-alpha-linux.h"
#else
#ifdef AFS_IA64_LINUX20_ENV
#include "conf-ia64-linux.h"
#else
#include "conf-i386-linux.h"
#endif /* AFS_IA64_LINUX20_ENV */
#endif /* AFS_ALPHA_LINUX20_ENV */
#endif /* AFS_S390_LINUX20_ENV */
#endif /* AFS_SPARC64_LINUX20_ENV */
#endif /* AFS_SPARC_LINUX20_ENV */
#endif /* AFS_PPC_LINUX20_ENV */
#endif /* AFS_PARISC_LINUX24_ENV */
#else
#if defined(AFS_DARWIN_ENV) && defined(AFS_PPC_ENV)
#include "conf-ppc-darwin.h"
#else
Sorry, you lose.
Figure out what the machine looks like and fix this file to include it.
#endif
#endif /* AFS_LINUX20_ENV */
#endif /* AFS_NT40_ENV */
#endif /* AFS_XBSD_ENV */
#endif /* NCR || X86 */
#endif /* __alpha */
#endif /* SGI */
#endif /* NeXT */
#endif	/* HP/UX */
#endif /* mac */
#endif	/* aix */
#endif /* sun */
#endif /* mips */
#endif /* not vax */
