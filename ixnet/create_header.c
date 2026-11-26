/* The offsets of ix_internals.h, compiled to assembly as
   library/create_header.c is. */

#define _KERNEL
#include "ixnet.h"
#include <stddef.h>

#define DEFINE(text, val) asm volatile ("\n->" text : : "i" (val))

void create_header(void)
{
  DEFINE ("IXNETBASE_SIZEOF (IXNETBASE_C_PRIVATE + %c0)",
	  sizeof (struct ixnet_base) - offsetof (struct ixnet_base, ix_seg_list) - 4);
}
