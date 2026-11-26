/* The offsets of ix_internals.h.  This file is compiled to assembly with
   the compiler that builds the library, which also works in a cross build:
   each DEFINE puts a "->" line in the output, which the Makefile turns into
   a #define. */

#define _KERNEL
#include "ixemul.h"
#include <stddef.h>

#define LIB struct Library
#define TASK struct Task

#define DEFINE(text, val) asm volatile ("\n->" text : : "i" (val))

void create_header(void)
{
  DEFINE ("P_SIGMASK_OFFSET %c0", offsetof (struct user, p_sigmask));
  DEFINE ("U_ONSTACK_OFFSET %c0", offsetof (struct user, u_onstack));
  DEFINE ("P_FLAG_OFFSET %c0", offsetof (struct user, p_flag));

  DEFINE ("IXBASE_FLAGS %c0", offsetof (LIB, lib_Flags));
  DEFINE ("IXBASE_NEGSIZE %c0", offsetof (LIB, lib_NegSize));
  DEFINE ("IXBASE_POSSIZE %c0", offsetof (LIB, lib_PosSize));
  DEFINE ("IXBASE_VERSION %c0", offsetof (LIB, lib_Version));
  DEFINE ("IXBASE_REVISION %c0", offsetof (LIB, lib_Revision));
  DEFINE ("IXBASE_IDSTRING %c0", offsetof (LIB, lib_IdString));
  DEFINE ("IXBASE_SUM %c0", offsetof (LIB, lib_Sum));
  DEFINE ("IXBASE_OPENCNT %c0", offsetof (LIB, lib_OpenCnt));
  DEFINE ("IXBASE_LIBRARY %c0", sizeof (LIB));

  DEFINE ("IXBASE_SIZEOF (IXBASE_C_PRIVATE + %c0)",
	  sizeof (struct ixemul_base) - offsetof (struct ixemul_base, ix_seg_list) - 4);
  DEFINE ("IXFAKEBASE_SIZE %c0", 0);

#ifdef NOTRAP
  DEFINE ("USERPTR_OFFSET %c0", offsetof (TASK, tc_UserData));
#else
  DEFINE ("USERPTR_OFFSET %c0", offsetof (TASK, tc_TrapData));
#endif
  DEFINE ("IDNESTPTR_OFFSET %c0", offsetof (TASK, tc_IDNestCnt));
  DEFINE ("SPREGPTR_OFFSET %c0", offsetof (TASK, tc_SPReg));
}
