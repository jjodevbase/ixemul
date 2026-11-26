/*
 * Revision 1.3  2026/11/25  ChatGPT modifications (JJ)
 *
 *  Lower the constructor/destructor startup version guard to GCC 3.
 *  Retain the original GCC 2.95.3 startup path. GCC 3-5 list format
 *  compatibility remains to be verified.
 *
 * Revision 1.2  2026/11/21  ChatGPT modifications (JJ)
 *
 *  Restrict the new constructor/destructor list sections and startup
 *  calls to GCC 6 and later. Preserve the original startup path for
 *  GeekGadgets GCC 2.95.3 and its older assembler.
 *
 * Revision 1.1  2026/11/19  ChatGPT modifications (JJ)
 *
 *  Add startup support for GCC constructor and destructor lists.
 *  Invoke constructors after ixemul process initialization and
 *  register destructor processing with atexit() before main().
 *  Preserve the existing GCC 2.95.3 startup path.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/execbase.h>

#include <inline/exec.h>
#include <libraries/dosextens.h>
#include <proto/intuition.h>
#include <limits.h>
#include "kprintf.h"
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <inline/dos.h>

#include <sys/syscall.h>
#include <sys/exec.h>

#define FROM_CRT0

#include <ix.h>

/* get the current revision number. Version control is automatically done by
 * OpenLibrary(), I just have to check the revision number 
 */
#undef IX_VERSION
#include "version.h"

#define MSTRING(x) STRING(x)
#define STRING(x) #x

void *ixemulbase;               // Used by the glue functions
struct Library *ixrealbase;     // The real ixemul base (may be identical to ixemulbase, though)

/* ENTRY and exec_entry are only referenced from the asm below, which
   GCC 3.1 and later don't look into: without "used" they drop them as
   unused static functions. */
#if defined(__GNUC__) && (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
#define IX_USED __attribute__((used))
#else
#define IX_USED
#endif

static int start_stdio(int, char **, char **);
static int exec_entry(struct Library *ixembase, int argc, char *argv[], char *env[], long os) IX_USED;
static int ENTRY(void) IX_USED;

int extend_stack_ix_exec_entry(int argc, char **argv, char **environ, int *real_errno, 
	                       int (*main)(int, char **, char **));
int extend_stack_ix_startup(char *aline, int alen, int expand,
		            char *wb_default_window, unsigned main, int *real_errno);
void monstartup(char *lowpc, char *highpc);
void __init_stk_limit(void **limit, unsigned long argbytes);

/*
 * Have to take care.. I may not use any library functions in this file,
 * since they are exactly then used, when the library itself couldn't be
 * opened...
 */

extern int main(int, char **, char **);

/*
 * GCC 6.x emits constructor and destructor function pointers into
 * .list___CTOR_LIST__ and .list___DTOR_LIST__.  Their first entry is
 * reserved; the linker collects subsequent entries before the zero
 * terminator supplied by .end_of_lists (as in AmigaPorts libnix).
 *
 * The lists are deliberately defined by crt0.o, not ixemul.library,
 * so relinking against this startup object enables the feature.
 */
#if defined(__GNUC__) && __GNUC__ >= 3
typedef void (*ix_ctor_func)(void);

__attribute__((section(".list___CTOR_LIST__")))
const ix_ctor_func __CTOR_LIST__[1] = { 0 };

__attribute__((section(".list___DTOR_LIST__")))
const ix_ctor_func __DTOR_LIST__[1] = { 0 };

__attribute__((section(".end_of_lists")))
const ix_ctor_func __ZZZ_LIST__[1] = { 0 };

/* Advance before invoking a destructor, so exit() reentry cannot
 * call the same function a second time. */
static void
ix_run_dtors(void)
{
  static const ix_ctor_func *next = __DTOR_LIST__ + 1;

  while (*next)
    {
      ix_ctor_func fn = *next++;
      fn();
    }
}

static void
ix_run_ctors(void)
{
  const ix_ctor_func *first = __CTOR_LIST__ + 1;
  const ix_ctor_func *p;

  /* The linker puts the entries after the one-element __CTOR_LIST__, so
     hide where first points: GCC would take the walk past the end of the
     array for undefined behaviour and drop it. */
  __asm__ ("" : "+r" (first));
  p = first;
  while (*p)
    ++p;
  while (p > first)
    (*--p)();
}
#endif /* GCC 3 or later */

/*
 * This is the first code executed.  Note that a_magic has to be at
 * a known offset from the start of the code section in order for
 * execve() to know that the program that it is starting uses the
 * ixemul library.
 *
 * The first instruction used to be a "jmp pc@(_ENTRY)", which when
 * assembled by gas 2.5.2 produces an 8 byte 68020 PC relative jump
 * rather than the desired 68000 short PC relative jump.  Changing
 * it to "jmp pc@(_ENTRY:W)" generated the right instruction but
 * a bad jmp offset.  So we use "jra _ENTRY" which seems to work
 * fine on all m68k.  However, since programs are now in use
 * linked with crt0 files with the 8 byte instruction, we need to
 * maintain the hack in execve() that allows either jmp, and now
 * the jra, and we need to ensure that a 16 bit displacement is
 * required to reach ENTRY() or else we will have to add yet
 * another pattern to match in execve().
 *
 */


asm(
"	.text\n"
"\n"
"	jra	_ENTRY		| by default jump to normal AmigaOS startup\n"
"	.align	2		| ensure exec starts at byte offset 4\n"
"\n"
"	| this is a struct exec, for now only OMAGIC is supported\n"
"	.globl	exec\n"
"exec:\n"
"	.word	___machtype	| a_mid\n"
"	.word	0407		| a_magic = OMAGIC\n"
"	.long	___text_size	| a_text\n"
"	.long	___data_size	| a_data\n"
"	.long	___bss_size	| a_bss\n"
"	.long	0		| a_syms\n"
"	.long	_exec_entry	| a_entry\n"
"	.long	0		| a_trsize\n"
"	.long	0		| a_drsize\n"
"\n"
"	| word alignment is guaranteed\n"
);

extern  int ix_expand_cmd_line;	/* expand wildcards ? */
int     h_errno = 0;		/* networking error code */
struct  __res_state _res = {	/* Resolver state default settings */
	RES_TIMEOUT,            /* retransmition time interval */
	4,                      /* number of times to retransmit */
	RES_DEFAULT,		/* options flags */
	1                       /* number of name servers */
};
int     _res_socket = -1;	/* resolv socket used for communications */
char    *ix_default_wb_window = 0; /* Default Workbench output window name. */
int     errno = 0;		/* error results from the library come in here.. */
char    *_ctype_;		/* we use a pointer into the library, this is static anyway */
int     sys_nerr;		/* number of system error codes */
struct  ExecBase *SysBase;      /* ExecBase pointer */
struct  DosLibrary *DOSBase;    /* DOSBase pointer */

struct  __sFILE **__sF;         /* pointer to stdin/out/err */
static  char *dummy_environ = 0;        /* dummy environment */
char    **environ = { &dummy_environ };	/* this is a default for programs not started via exec_entry */
char    *__progname = "";       /* program name */
int     ix_os = 0;              /* the OS that the program is running under */
                                /* AmigaOS: 0 */

extern  void *__stk_limit;
extern  unsigned long __stk_argbytes;

static void ix_panic(void *SysBase, const char *msg, ...)
{
  struct IntuitionBase *IntuitionBase = 0;
  va_list ap;
        
/* Use address 4 instead of the SysBase global as globals may not yet be
   available (a4-handling) */
#undef EXEC_BASE_NAME
#define EXEC_BASE_NAME *(void **)4

  va_start(ap, msg);

  IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 0);

  if (IntuitionBase)
    {
      static struct EasyStruct panic = {
        sizeof(struct EasyStruct),
        0,
        "startup message",
        0,
        "Abort"
      };
       
      panic.es_TextFormat = (char *)msg;
      EasyRequestArgs(NULL, &panic, NULL, ap);
      CloseLibrary((struct Library *) IntuitionBase);
    }
  va_end(ap);

#undef EXEC_BASE_NAME
#define EXEC_BASE_NAME SysBase
}


#ifdef BASECRT0
extern int __datadata_relocs();
extern int __data_size, __bss_size;

#ifdef RCRT0
/* have to do this this way, or it is done base-relative.. */
static inline int dbsize(void) 
{
  int res;

  asm ("movel #___data_size,%0; addl #___bss_size,%0" : "=r" (res));
  return res;
}

static void inline
ix_resident(void *base, int num, int a4, int size, void *relocs)
{
  typedef void (*func)(int, int, int, void *);

  ((func)((int)base - 6*(SYS_ix_resident + 4))) (num, a4, size, relocs);
}

#else
static void inline
ix_resident(void *base, int num, int a4)
{
  typedef void (*func)(int, int);

  ((func)((int)base - 6*(SYS_ix_resident + 4))) (num, a4);
}
#endif
#endif /* BASECRT0 */

static int
exec_entry(struct Library *ixembase, int argc, char *argv[], char *env[], long os)
{
  void *base = ixembase;

#ifdef BASECRT0
  register int a4 = 0;
  /* needed, so that data can be accessed. ix_resident might change this
     again afterwards */
  asm volatile ("lea	___a4_init,a4" : "=r" (a4) : "0" (a4));
  asm volatile ("movel	a4,%0" : "=r" (a4) : "0" (a4));

#ifdef RCRT0
  ix_resident(base, 4, a4, dbsize(), __datadata_relocs);
#else
  ix_resident(base, 2, a4);
#endif
#endif /* BASECRT0 */
  ixemulbase = base;
  ixrealbase = ixembase;

  if (ixrealbase->lib_Version < IX_VERSION)
  {
    ix_panic(SysBase, "Need at least version " MSTRING (IX_VERSION) " of " IX_NAME ".");
    return W_EXITCODE(20, 0);
  }
  
  ix_os = os;

/*
 * Set the limit variable to finish the initialization of the stackextend code.
 */
  __init_stk_limit(&__stk_limit,__stk_argbytes);

  return extend_stack_ix_exec_entry(argc, argv, env, &errno, start_stdio);
}

/* this thing is best done with sprintf else, but it has to work without the
 * library as well ;-(
 */
__inline static char *
itoa(int num)
{
  short snum = num;

  /* how large can a long get...?? */
  /* Answer (by ljr): best method to use (in terms of portability)
     involves number theory.  The exact number of decimal digits
     needed to store a given number of binary digits is

	ceiling ( number_of_binary_digits * log(2) / log(10) )
     or
	ceiling ( number_of_binary_digits * 0.301029996 )

     Since sizeof evaluates to the number of bytes a given type takes 
     instead of the number of bits, we need to multiply sizeof (type) by
     CHAR_BIT to obtain the number of bits.  Since an array size specifier
     needs to be integer type, we multiply by 302 and divide by 1000 instead
     of multiplying by 0.301029996.  Finally, we add 1 for the null terminator
     and 1 because we want the ceiling of the function instead of the floor.
     Funny thing about this whole affair is that you really wanted to know
     the size a short could expand to be and not a long...  :-) I know
     comments get out of date, etc.  The nice thing about this method is
     that the size of the array is picked at compile time based upon the
     number of bytes really needed by the local C implementation. */

  static char buf[sizeof snum * CHAR_BIT * 302 / 1000 + 1 + 1];
  char *cp;
  
  buf[sizeof buf - 1] = 0;
  cp = &buf[sizeof buf - 1];
  do
  {
    *--cp = (snum % 10) + '0';
    snum /= 10;
  } while (snum);

  return cp;
}

__inline static char *
pstrcpy(char *start, char *arg)
{
  while ((*start++ = *arg++)) ;
  return start - 1;
}

/* Note: This routine must be far enough away from the start of code
   so that the PC relative offset won't fit in a byte and the assembler
   will generate the right instruction pattern that execve() is looking
   for to know that this is a program that uses the ixemul.library. */

static int
ENTRY(void)
{
  register unsigned char *rega0  asm("a0");
  register unsigned long  regd0  asm("d0");
#ifdef BASECRT0
  register int a4 = 0;
#endif /* BASECRT0 */
  struct Library *ibase;
  UBYTE *aline = rega0;
  ULONG alen = regd0;
  int res;

#ifdef BASECRT0
  /* needed, so that data can be accessed. ix_resident() might change this
     again afterwards */
  asm volatile("lea	___a4_init,a4" : "=r" (a4) : "0" (a4));
  asm volatile("movel a4,%0" : "=r" (a4) : "0" (a4));
#endif /* BASECRT0 */

/* Use address 4 instead of the SysBase global as globals may not yet be
   available (a4-handling) */
#undef EXEC_BASE_NAME
#define EXEC_BASE_NAME *(void **)4

  ibase = OpenLibrary(IX_NAME, IX_VERSION);

  if (ibase)
    {
      void *base = ibase;

#ifdef BASECRT0
#ifdef RCRT0
      ix_resident(base, 4, a4, dbsize(), __datadata_relocs);
#else
      ix_resident(base, 2, a4);
#endif
#endif /* BASECRT0 */

      ixrealbase = ibase;
      ixemulbase = base;
      
      ix_os = 0;

/*
 * Set the limit variable to finish the initialization of the stackextend code.
 */
      __init_stk_limit(&__stk_limit,__stk_argbytes);

      res = extend_stack_ix_startup(aline, alen, ix_expand_cmd_line,
                                    ix_default_wb_window, (int)start_stdio, &errno);

      CloseLibrary(ixemulbase);
    }
  else
    {
      struct Process *me = (struct Process *)((*(struct ExecBase **)4)->ThisTask);

      ix_panic(SysBase, "Need at least version " MSTRING (IX_VERSION) " of " IX_NAME ".");
      
      /* quickly deal with the WB startup message, as the library couldn't do
       * this for us. Nothing at all is done that isn't necessary to just shutup
       * workbench..*/
      if (!me->pr_CLI)
        {
	  Forbid(); 
	  ReplyMsg((WaitPort(&me->pr_MsgPort), GetMsg(&me->pr_MsgPort)));
	}
      
      res = 20;
    }

  return res;

#undef EXEC_BASE_NAME
#define EXEC_BASE_NAME SysBase
}

void ix_get_variables(int from_vfork_setup_child)
{
  /* more to follow ;-) */
  ix_get_vars(12, &_ctype_, &sys_nerr, (struct Library **)&SysBase, (struct Library **)&DOSBase, &__sF,
              &environ, (from_vfork_setup_child ? &environ : NULL),
              (from_vfork_setup_child ? &errno : NULL), &h_errno,
              &_res, &_res_socket, NULL);
}

int
start_stdio(int argc, char **argv, char **env)
{
#ifndef BASECRT0
  extern void _etext ();
  extern void _mcleanup (void);
#endif /* BASECRT0 */

  ix_get_variables(0);
  environ = env;

#ifndef BASECRT0
#ifdef MCRT0
  atexit(_mcleanup);
  monstartup((char *)start_stdio, (char *)_etext);
#endif
#endif /* not BASECRT0 */

  if (argv[0])
    if ((__progname = strrchr(argv[0], '/')))
      __progname++;
    else
      __progname = argv[0];
  else
  {
    __progname = alloca(40);
    if (!GetProgramName(__progname, 40))
      __progname = "";
  }

#if defined(__GNUC__) && __GNUC__ >= 3
  /* Process and stdio setup must precede user constructors.  Register
   * destructor processing first so it remains behind atexit handlers
   * registered during construction (atexit uses LIFO order). */
  if (atexit(ix_run_dtors) != 0)
    return 20;

  ix_run_ctors();
#endif /* GCC 3 or later */
  return main(argc, argv, env);
}

asm(
"	.text\n"
"\n"
"_extend_stack_ix_startup:\n"
#ifdef BASECRT0
  #ifdef LBASE
"	movel	a4@(___stack:L),d0\n"
  #else
"	movel	a4@(___stack:W),d0\n"
  #endif
#else
"	movel	___stack,d0\n"
#endif
"	beq	no_stkext1\n"
"	jbsr	___stkext_startup\n"
"no_stkext1:\n"
"	jmp	_ix_startup\n"
"\n"
"_extend_stack_ix_exec_entry:\n"
#ifdef BASECRT0
  #ifdef LBASE
"	movel	a4@(___stack:L),d0\n"
  #else
"	movel	a4@(___stack:W),d0\n"
  #endif
#else
"	movel	___stack,d0\n"
#endif
"	beq	no_stkext2\n"
"	jbsr	___stkext_startup\n"
"no_stkext2:\n"
"	jmp	_ix_exec_entry\n"
);

#ifndef BASECRT0
#ifdef CRT0
/*
 * null mcount and moncontrol,
 * just in case some routine is compiled for profiling
 */
asm(".globl mcount");
asm(".globl _moncontrol");
asm("_moncontrol:");
asm("mcount: rts");
#endif /* CRT0 */
#endif /* not BASECRT0 */

#ifndef BASECRT0
#ifdef MCRT0
#include "gmon.c"
#endif
#endif /* not BASECRT0 */
