/* Subroutines for insn-output.c for VAX.
   Copyright (C) 1987, 1994, 1995, 1997, 1998, 1999, 2000, 2001, 2002, 2004
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "regs.h"
#include "hard-reg-set.h"
#include "real.h"
#include "insn-config.h"
#include "conditions.h"
#include "function.h"
#include "output.h"
#include "insn-attr.h"
#include "recog.h"
#include "expr.h"
#include "optabs.h"
#include "flags.h"
#include "debug.h"
#include "toplev.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"

static void vax_output_function_prologue (FILE *, HOST_WIDE_INT);
static void vax_file_start (void);
static void vax_init_libfuncs (void);
static void vax_output_mi_thunk (FILE *, tree, HOST_WIDE_INT,
				 HOST_WIDE_INT, tree);
static int vax_address_cost_1 (rtx);
static int vax_address_cost (rtx);
static int vax_rtx_costs_1 (rtx, enum rtx_code, enum rtx_code);
static bool vax_rtx_costs (rtx, int, int, int *);
static rtx vax_struct_value_rtx (tree, int);

/* Initialize the GCC target structure.  */
#undef TARGET_ASM_ALIGNED_HI_OP
#define TARGET_ASM_ALIGNED_HI_OP "\t.word\t"

#undef TARGET_ASM_FUNCTION_PROLOGUE
#define TARGET_ASM_FUNCTION_PROLOGUE vax_output_function_prologue

#undef TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START vax_file_start
#undef TARGET_ASM_FILE_START_APP_OFF
#define TARGET_ASM_FILE_START_APP_OFF true

#undef TARGET_INIT_LIBFUNCS
#define TARGET_INIT_LIBFUNCS vax_init_libfuncs

#undef TARGET_ASM_OUTPUT_MI_THUNK
#define TARGET_ASM_OUTPUT_MI_THUNK vax_output_mi_thunk
#undef TARGET_ASM_CAN_OUTPUT_MI_THUNK
#define TARGET_ASM_CAN_OUTPUT_MI_THUNK default_can_output_mi_thunk_no_vcall

#undef TARGET_RTX_COSTS
#define TARGET_RTX_COSTS vax_rtx_costs
#undef TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST vax_address_cost

#undef TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_tree_true

#undef TARGET_STRUCT_VALUE_RTX
#define TARGET_STRUCT_VALUE_RTX vax_struct_value_rtx

struct gcc_target targetm = TARGET_INITIALIZER;

/* Set global variables as needed for the options enabled.  */

void
override_options (void)
{
  /* We're VAX floating point, not IEEE floating point.  */
  if (TARGET_G_FLOAT)
    REAL_MODE_FORMAT (DFmode) = &vax_g_format;
}

/* Generate the assembly code for function entry.  FILE is a stdio
   stream to output the code to.  SIZE is an int: how many units of
   temporary storage to allocate.

   Refer to the array `regs_ever_live' to determine which registers to
   save; `regs_ever_live[I]' is nonzero if register number I is ever
   used in the function.  This function is responsible for knowing
   which registers should not be saved even if used.  */

static void
vax_output_function_prologue (FILE * file, HOST_WIDE_INT size)
{
  register int regno;
  register int mask = 0;

  for (regno = 0; regno < FIRST_PSEUDO_REGISTER; regno++)
    if (regs_ever_live[regno] && !call_used_regs[regno])
      mask |= 1 << regno;

  fprintf (file, "\t.word 0x%x\n", mask);

  if (dwarf2out_do_frame ())
    {
      const char *label = dwarf2out_cfi_label ();
      int offset = 0;

      for (regno = FIRST_PSEUDO_REGISTER-1; regno >= 0; --regno)
	if (regs_ever_live[regno] && !call_used_regs[regno])
	  dwarf2out_reg_save (label, regno, offset -= 4);

      dwarf2out_reg_save (label, PC_REGNUM, offset -= 4);
      dwarf2out_reg_save (label, FRAME_POINTER_REGNUM, offset -= 4);
      dwarf2out_reg_save (label, ARG_POINTER_REGNUM, offset -= 4);
      dwarf2out_def_cfa (label, FRAME_POINTER_REGNUM, -(offset - 4));
    }

  size -= STARTING_FRAME_OFFSET;
  if (size >= 64)
    asm_fprintf (file, "\tmovab %wd(%Rsp),%Rsp\n", -size);
  else if (size)
    asm_fprintf (file, "\tsubl2 $%wd,%Rsp\n", size);
}

/* When debugging with stabs, we want to output an extra dummy label
   so that gas can distinguish between D_float and G_float prior to
   processing the .stabs directive identifying type double.  */
static void
vax_file_start (void)
{
  default_file_start ();

  if (write_symbols == DBX_DEBUG)
    fprintf (asm_out_file, "___vax_%c_doubles:\n", ASM_DOUBLE_CHAR);
}

/* We can use the BSD C library routines for the libgcc calls that are
   still generated, since that's what they boil down to anyways.  When
   ELF, avoid the user's namespace.  */

static void
vax_init_libfuncs (void)
{
  set_optab_libfunc (udiv_optab, SImode, TARGET_ELF ? "*__udiv" : "*udiv");
  set_optab_libfunc (umod_optab, SImode, TARGET_ELF ? "*__urem" : "*urem");
}

/* This is like nonimmediate_operand with a restriction on the type of MEM.  */

void
split_quadword_operands (rtx * operands, rtx * low, int n ATTRIBUTE_UNUSED)
{
  int i;
  /* Split operands.  */

  low[0] = low[1] = low[2] = 0;
  for (i = 0; i < 3; i++)
    {
      if (low[i])
	/* it's already been figured out */;
      else if (GET_CODE (operands[i]) == MEM
	       && (GET_CODE (XEXP (operands[i], 0)) == POST_INC))
	{
	  rtx addr = XEXP (operands[i], 0);
	  operands[i] = low[i] = gen_rtx_MEM (SImode, addr);
	  if (which_alternative == 0 && i == 0)
	    {
	      addr = XEXP (operands[i], 0);
	      operands[i+1] = low[i+1] = gen_rtx_MEM (SImode, addr);
	    }
	}
      else
	{
	  low[i] = operand_subword (operands[i], 0, 0, DImode);
	  operands[i] = operand_subword (operands[i], 1, 0, DImode);
	}
    }
}

void
print_operand_address (FILE * file, register rtx addr)
{
  register rtx reg1, breg, ireg;
  rtx offset;

 retry:
  switch (GET_CODE (addr))
    {
    case MEM:
      fprintf (file, "*");
      addr = XEXP (addr, 0);
      goto retry;

    case REG:
      fprintf (file, "(%s)", reg_names[REGNO (addr)]);
      break;

    case PRE_DEC:
      fprintf (file, "-(%s)", reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case POST_INC:
      fprintf (file, "(%s)+", reg_names[REGNO (XEXP (addr, 0))]);
      break;

    case PLUS:
      /* There can be either two or three things added here.  One must be a
	 REG.  One can be either a REG or a MULT of a REG and an appropriate
	 constant, and the third can only be a constant or a MEM.

	 We get these two or three things and put the constant or MEM in
	 OFFSET, the MULT or REG in IREG, and the REG in BREG.  If we have
	 a register and can't tell yet if it is a base or index register,
	 put it into REG1.  */

      reg1 = 0; ireg = 0; breg = 0; offset = 0;

      if (CONSTANT_ADDRESS_P (XEXP (addr, 0))
	  || GET_CODE (XEXP (addr, 0)) == MEM)
	{
	  offset = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else if (CONSTANT_ADDRESS_P (XEXP (addr, 1))
	       || GET_CODE (XEXP (addr, 1)) == MEM)
	{
	  offset = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      else if (GET_CODE (XEXP (addr, 1)) == MULT)
	{
	  ireg = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      else if (GET_CODE (XEXP (addr, 0)) == MULT)
	{
	  ireg = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else if (GET_CODE (XEXP (addr, 1)) == REG)
	{
	  reg1 = XEXP (addr, 1);
	  addr = XEXP (addr, 0);
	}
      else if (GET_CODE (XEXP (addr, 0)) == REG)
	{
	  reg1 = XEXP (addr, 0);
	  addr = XEXP (addr, 1);
	}
      else
	abort ();

      if (GET_CODE (addr) == REG)
	{
	  if (reg1)
	    ireg = addr;
	  else
	    reg1 = addr;
	}
      else if (GET_CODE (addr) == MULT)
	ireg = addr;
      else if (GET_CODE (addr) == PLUS)
	{
	  if (CONSTANT_ADDRESS_P (XEXP (addr, 0))
	      || GET_CODE (XEXP (addr, 0)) == MEM)
	    {
	      if (offset)
		{
		  if (GET_CODE (offset) == CONST_INT)
		    offset = plus_constant (XEXP (addr, 0), INTVAL (offset));
		  else if (GET_CODE (XEXP (addr, 0)) == CONST_INT)
		    offset = plus_constant (offset, INTVAL (XEXP (addr, 0)));
		  else
		    abort ();
		}
	      offset = XEXP (addr, 0);
	    }
	  else if (GET_CODE (XEXP (addr, 0)) == REG)
	    {
	      if (reg1)
		ireg = reg1, breg = XEXP (addr, 0), reg1 = 0;
	      else
		reg1 = XEXP (addr, 0);
	    }
	  else if (GET_CODE (XEXP (addr, 0)) == MULT)
	    {
	      if (ireg)
		abort ();
	      ireg = XEXP (addr, 0);
	    }
	  else
	    abort ();

	  if (CONSTANT_ADDRESS_P (XEXP (addr, 1))
	      || GET_CODE (XEXP (addr, 1)) == MEM)
	    {
	      if (offset)
		{
		  if (GET_CODE (offset) == CONST_INT)
		    offset = plus_constant (XEXP (addr, 1), INTVAL (offset));
		  else if (GET_CODE (XEXP (addr, 1)) == CONST_INT)
		    offset = plus_constant (offset, INTVAL (XEXP (addr, 1)));
		  else
		    abort ();
		}
	      offset = XEXP (addr, 1);
	    }
	  else if (GET_CODE (XEXP (addr, 1)) == REG)
	    {
	      if (reg1)
		ireg = reg1, breg = XEXP (addr, 1), reg1 = 0;
	      else
		reg1 = XEXP (addr, 1);
	    }
	  else if (GET_CODE (XEXP (addr, 1)) == MULT)
	    {
	      if (ireg)
		abort ();
	      ireg = XEXP (addr, 1);
	    }
	  else
	    abort ();
	}
      else
	abort ();

      /* If REG1 is nonzero, figure out if it is a base or index register.  */
      if (reg1)
	{
	  if (breg != 0 || (offset && GET_CODE (offset) == MEM))
	    {
	      if (ireg)
		abort ();
	      ireg = reg1;
	    }
	  else
	    breg = reg1;
	}

      if (offset != 0)
	output_address (offset);

      if (breg != 0)
	fprintf (file, "(%s)", reg_names[REGNO (breg)]);

      if (ireg != 0)
	{
	  if (GET_CODE (ireg) == MULT)
	    ireg = XEXP (ireg, 0);
	  if (GET_CODE (ireg) != REG)
	    abort ();
	  fprintf (file, "[%s]", reg_names[REGNO (ireg)]);
	}
      break;

    default:
      output_addr_const (file, addr);
    }
}

const char *
rev_cond_name (rtx op)
{
  switch (GET_CODE (op))
    {
    case EQ:
      return "neq";
    case NE:
      return "eql";
    case LT:
      return "geq";
    case LE:
      return "gtr";
    case GT:
      return "leq";
    case GE:
      return "lss";
    case LTU:
      return "gequ";
    case LEU:
      return "gtru";
    case GTU:
      return "lequ";
    case GEU:
      return "lssu";

    default:
      abort ();
    }
}

int
vax_float_literal(register rtx c)
{
  register enum machine_mode mode;
  REAL_VALUE_TYPE r, s;
  int i;

  if (GET_CODE (c) != CONST_DOUBLE)
    return 0;

  mode = GET_MODE (c);

  if (c == const_tiny_rtx[(int) mode][0]
      || c == const_tiny_rtx[(int) mode][1]
      || c == const_tiny_rtx[(int) mode][2])
    return 1;

  REAL_VALUE_FROM_CONST_DOUBLE (r, c);

  for (i = 0; i < 7; i++)
    {
      int x = 1 << i;
      REAL_VALUE_FROM_INT (s, x, 0, mode);

      if (REAL_VALUES_EQUAL (r, s))
	return 1;
      if (!exact_real_inverse (mode, &s))
	abort ();
      if (REAL_VALUES_EQUAL (r, s))
	return 1;
    }
  return 0;
}


/* Return the cost in cycles of a memory address, relative to register
   indirect.

   Each of the following adds the indicated number of cycles:

   1 - symbolic address
   1 - pre-decrement
   1 - indexing and/or offset(register)
   2 - indirect */


static int
vax_address_cost_1 (register rtx addr)
{
  int reg = 0, indexed = 0, indir = 0, offset = 0, predec = 0;
  rtx plus_op0 = 0, plus_op1 = 0;
 restart:
  switch (GET_CODE (addr))
    {
    case PRE_DEC:
      predec = 1;
    case REG:
    case SUBREG:
    case POST_INC:
      reg = 1;
      break;
    case MULT:
      indexed = 1;	/* 2 on VAX 2 */
      break;
    case CONST_INT:
      /* byte offsets cost nothing (on a VAX 2, they cost 1 cycle) */
      if (offset == 0)
	offset = (unsigned)(INTVAL(addr)+128) > 256;
      break;
    case CONST:
    case SYMBOL_REF:
      offset = 1;	/* 2 on VAX 2 */
      break;
    case LABEL_REF:	/* this is probably a byte offset from the pc */
      if (offset == 0)
	offset = 1;
      break;
    case PLUS:
      if (plus_op0)
	plus_op1 = XEXP (addr, 0);
      else
	plus_op0 = XEXP (addr, 0);
      addr = XEXP (addr, 1);
      goto restart;
    case MEM:
      indir = 2;	/* 3 on VAX 2 */
      addr = XEXP (addr, 0);
      goto restart;
    default:
      break;
    }

  /* Up to 3 things can be added in an address.  They are stored in
     plus_op0, plus_op1, and addr.  */

  if (plus_op0)
    {
      addr = plus_op0;
      plus_op0 = 0;
      goto restart;
    }
  if (plus_op1)
    {
      addr = plus_op1;
      plus_op1 = 0;
      goto restart;
    }
  /* Indexing and register+offset can both be used (except on a VAX 2)
     without increasing execution time over either one alone.  */
  if (reg && indexed && offset)
    return reg + indir + offset + predec;
  return reg + indexed + indir + offset + predec;
}

static int
vax_address_cost (rtx x)
{
  return (1 + (GET_CODE (x) == REG ? 0 : vax_address_cost_1 (x)));
}

/* Cost of an expression on a VAX.  This version has costs tuned for the
   CVAX chip (found in the VAX 3 series) with comments for variations on
   other models.  */

static int
vax_rtx_costs_1 (register rtx x, enum rtx_code code, enum rtx_code outer_code)
{
  enum machine_mode mode = GET_MODE (x);
  register int c;
  int i = 0;				/* may be modified in switch */
  const char *fmt = GET_RTX_FORMAT (code); /* may be modified in switch */

  switch (code)
    {
      /* On a VAX, constants from 0..63 are cheap because they can use the
         1 byte literal constant format.  compare to -1 should be made cheap
         so that decrement-and-branch insns can be formed more easily (if
         the value -1 is copied to a register some decrement-and-branch
	 patterns will not match).  */
    case CONST_INT:
      if (INTVAL (x) == 0)
	return 0;
      if (outer_code == AND)
        return ((unsigned HOST_WIDE_INT) ~INTVAL (x) <= 077) ? 1 : 2;
      if ((unsigned HOST_WIDE_INT) INTVAL (x) <= 077)
	return 1;
      if (outer_code == COMPARE && INTVAL (x) == -1)
        return 1;
      if (outer_code == PLUS && (unsigned HOST_WIDE_INT) -INTVAL (x) <= 077)
        return 1;
      /* FALLTHRU */

    case CONST:
    case LABEL_REF:
    case SYMBOL_REF:
      return 3;

    case CONST_DOUBLE:
      if (GET_MODE_CLASS (GET_MODE (x)) == MODE_FLOAT)
        return vax_float_literal (x) ? 5 : 8;
      else
        return (((CONST_DOUBLE_HIGH (x) == 0
		  && (unsigned HOST_WIDE_INT) CONST_DOUBLE_LOW (x) < 64)
	         || (outer_code == PLUS
		     && CONST_DOUBLE_HIGH (x) == -1		\
		     && (unsigned HOST_WIDE_INT)-CONST_DOUBLE_LOW (x) < 64))
	        ? 2 : 5);
 
    case POST_INC:
      return 2;
    case PRE_DEC:
      return 3;
    case MULT:
      switch (mode)
	{
	case DFmode:
	  c = 16;		/* 4 on VAX 9000 */
	  break;
	case SFmode:
	  c = 9;		/* 4 on VAX 9000, 12 on VAX 2 */
	  break;
	case DImode:
	  c = 16;		/* 6 on VAX 9000, 28 on VAX 2 */
	  break;
	case SImode:
	case HImode:
	case QImode:
	  c = 10;		/* 3-4 on VAX 9000, 20-28 on VAX 2 */
	  break;
	default:
	  return MAX_COST;	/* Mode is not supported.  */
	}
      break;
    case UDIV:
      if (mode != SImode)
	return MAX_COST;	/* Mode is not supported.  */
      c = 17;
      break;
    case DIV:
      if (mode == DImode)
	c = 30;	/* highly variable */
      else if (mode == DFmode)
	/* divide takes 28 cycles if the result is not zero, 13 otherwise */
	c = 24;
      else
	c = 11;			/* 25 on VAX 2 */
      break;
    case MOD:
      c = 23;
      break;
    case UMOD:
      if (mode != SImode)
	return MAX_COST;	/* Mode is not supported.  */
      c = 29;
      break;
    case FLOAT:
      c = 6 + (mode == DFmode) + (GET_MODE (XEXP (x, 0)) != SImode);
      /* 4 on VAX 9000 */
      break;
    case FIX:
      c = 7;			/* 17 on VAX 2 */
      break;
    case ASHIFT:
    case LSHIFTRT:
    case ASHIFTRT:
      if (mode == DImode)
	c = 12;
      else
	c = 10;			/* 6 on VAX 9000 */
      break;
    case ROTATE:
    case ROTATERT:
      c = 6;			/* 5 on VAX 2, 4 on VAX 9000 */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	fmt = "e";	/* all constant rotate counts are short */
      break;
    case PLUS:
      /* Check for small negative integer operand: subl2 can be used with
	 a short positive constant instead.  */
      if (GET_CODE (XEXP (x, 1)) == CONST_INT)
	if ((unsigned)(INTVAL (XEXP (x, 1)) + 63) < 127)
	  fmt = "e";
    case MINUS:
      c = (mode == DFmode) ? 13 : 8;	/* 6/8 on VAX 9000, 16/15 on VAX 2 */
    case IOR:
    case XOR:
      c = 3;
      break;
    case AND:
      /* AND is special because the first operand is complemented.  */
      c = 3;
      if (GET_CODE (XEXP (x, 0)) == CONST_INT)
	{
	  if ((unsigned)~INTVAL (XEXP (x, 0)) > 63)
	    c = 4;
	  fmt = "e";
	  i = 1;
	}
      break;
    case NEG:
      if (mode == DFmode)
	return 9;
      else if (mode == SFmode)
	return 6;
      else if (mode == DImode)
	return 4;
    case NOT:
      return 2;
    case ZERO_EXTRACT:
    case SIGN_EXTRACT:
      c = 15;
      break;
    case MEM:
      if (mode == DImode || mode == DFmode)
	c = 5;				/* 7 on VAX 2 */
      else
	c = 3;				/* 4 on VAX 2 */
      x = XEXP (x, 0);
      if (GET_CODE (x) == REG || GET_CODE (x) == POST_INC)
	return c;
      return c + vax_address_cost_1 (x);
    default:
      c = 3;
      break;
    }

  /* Now look inside the expression.  Operands which are not registers or
     short constants add to the cost.

     FMT and I may have been adjusted in the switch above for instructions
     which require special handling */

  while (*fmt++ == 'e')
    {
      register rtx op = XEXP (x, i++);
      code = GET_CODE (op);

      /* A NOT is likely to be found as the first operand of an AND
	 (in which case the relevant cost is of the operand inside
	 the not) and not likely to be found anywhere else.  */
      if (code == NOT)
	op = XEXP (op, 0), code = GET_CODE (op);

      switch (code)
	{
	case CONST_INT:
	  if ((unsigned)INTVAL (op) > 63 && GET_MODE (x) != QImode)
	    c += 1;		/* 2 on VAX 2 */
	  break;
	case CONST:
	case LABEL_REF:
	case SYMBOL_REF:
	  c += 1;		/* 2 on VAX 2 */
	  break;
	case CONST_DOUBLE:
	  if (GET_MODE_CLASS (GET_MODE (op)) == MODE_FLOAT)
	    {
	      /* Registers are faster than floating point constants -- even
		 those constants which can be encoded in a single byte.  */
	      if (vax_float_literal (op))
		c++;
	      else
		c += (GET_MODE (x) == DFmode) ? 3 : 2;
	    }
	  else
	    {
	      if (CONST_DOUBLE_HIGH (op) != 0
		  || (unsigned)CONST_DOUBLE_LOW (op) > 63)
		c += 2;
	    }
	  break;
	case MEM:
	  c += 1;		/* 2 on VAX 2 */
	  if (GET_CODE (XEXP (op, 0)) != REG)
	    c += vax_address_cost_1 (XEXP (op, 0));
	  break;
	case REG:
	case SUBREG:
	  break;
	default:
	  c += 1;
	  break;
	}
    }
  return c;
}

static bool
vax_rtx_costs (rtx x, int code, int outer_code, int * total)
{
  *total = vax_rtx_costs_1 (x, code, outer_code);
  return true;
}

/* Output code to add DELTA to the first argument, and then jump to FUNCTION.
   Used for C++ multiple inheritance.
	.mask	^m<r2,r3,r4,r5,r6,r7,r8,r9,r10,r11>  #conservative entry mask
	addl2	$DELTA, 4(ap)	#adjust first argument
	jmp	FUNCTION+2	#jump beyond FUNCTION's entry mask
*/

static void
vax_output_mi_thunk (FILE * file,
                     tree thunk ATTRIBUTE_UNUSED, 
                     HOST_WIDE_INT delta,
                     HOST_WIDE_INT vcall_offset ATTRIBUTE_UNUSED,
                     tree function)
{
  fprintf (file, "\t.word 0x0ffc\n\taddl2 $" HOST_WIDE_INT_PRINT_DEC, delta);
  asm_fprintf (file, ",4(%Rap)\n");
  fprintf (file, "\tjmp ");						
  assemble_name (file,  XSTR (XEXP (DECL_RTL (function), 0), 0));	
  fprintf (file, "+2\n");						
}

static rtx
vax_struct_value_rtx (tree fntype ATTRIBUTE_UNUSED,
		      int incoming ATTRIBUTE_UNUSED)
{
  return gen_rtx_REG (Pmode, VAX_STRUCT_VALUE_REGNUM);
}

/* Worker function for NOTICE_UPDATE_CC.  */

void
vax_notice_update_cc (rtx exp, rtx insn ATTRIBUTE_UNUSED)
{
  if (GET_CODE (exp) == SET)
    {
      if (GET_CODE (SET_SRC (exp)) == CALL)
	CC_STATUS_INIT;
      else if (GET_CODE (SET_DEST (exp)) != ZERO_EXTRACT
	       && GET_CODE (SET_DEST (exp)) != PC)
	{
	  cc_status.flags = 0;
	  /* The integer operations below don't set carry or
	     set it in an incompatible way.  That's ok though
	     as the Z bit is all we need when doing unsigned
	     comparisons on the result of these insns (since
	     they're always with 0).  Set CC_NO_OVERFLOW to
	     generate the correct unsigned branches.  */
	  switch (GET_CODE (SET_SRC (exp)))
	    {
	    case NEG:
	      if (GET_MODE_CLASS (GET_MODE (exp)) == MODE_FLOAT)
	 	break;
	    case AND:
	    case IOR:
	    case XOR:
	    case NOT:
	    case MEM:
	    case REG:
	      cc_status.flags = CC_NO_OVERFLOW;
	      break;
	    default:
	      break;
	    }
	  cc_status.value1 = SET_DEST (exp);
	  cc_status.value2 = SET_SRC (exp);
	}
    }
  else if (GET_CODE (exp) == PARALLEL
	   && GET_CODE (XVECEXP (exp, 0, 0)) == SET)
    {
      if (GET_CODE (SET_SRC (XVECEXP (exp, 0, 0))) == CALL)
	CC_STATUS_INIT;
      else if (GET_CODE (SET_DEST (XVECEXP (exp, 0, 0))) != PC)
	{
	  cc_status.flags = 0;
	  cc_status.value1 = SET_DEST (XVECEXP (exp, 0, 0));
	  cc_status.value2 = SET_SRC (XVECEXP (exp, 0, 0));
	}
      else
	/* PARALLELs whose first element sets the PC are aob,
	   sob insns.  They do change the cc's.  */
	CC_STATUS_INIT;
    }
  else
    CC_STATUS_INIT;
  if (cc_status.value1 && GET_CODE (cc_status.value1) == REG
      && cc_status.value2
      && reg_overlap_mentioned_p (cc_status.value1, cc_status.value2))
    cc_status.value2 = 0;
  if (cc_status.value1 && GET_CODE (cc_status.value1) == MEM
      && cc_status.value2
      && GET_CODE (cc_status.value2) == MEM)
    cc_status.value2 = 0;
  /* Actual condition, one line up, should be that value2's address
     depends on value1, but that is too much of a pain.  */
}
