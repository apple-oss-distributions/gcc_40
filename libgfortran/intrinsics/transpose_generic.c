/* Implementation of the TRANSPOSE intrinsic
   Copyright 2003 Free Software Foundation, Inc.
   Contributed by Tobias Schl?ter

This file is part of the GNU Fortran 95 runtime library (libgfor).

Libgfor is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

Ligbfor is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with libgfor; see the file COPYING.LIB.  If not,
write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "libgfortran.h"

void
__transpose (gfc_array_char * ret, gfc_array_char * source)
{
  /* r.* indicates the return array.  */
  index_type rxstride, rystride;
  char *rptr;
  /* s.* indicates the source array.  */
  index_type sxstride, systride;
  const char *sptr;

  index_type xcount, ycount;
  index_type x, y;
  index_type size;

  assert (GFC_DESCRIPTOR_RANK (source) == 2
          && GFC_DESCRIPTOR_RANK (ret) == 2);

  size = GFC_DESCRIPTOR_SIZE (source);

  if (ret->data == NULL)
    {
      assert (ret->dtype == source->dtype);

      ret->dim[0].lbound = 0;
      ret->dim[0].ubound = source->dim[1].ubound - source->dim[1].lbound;
      ret->dim[0].stride = 1;

      ret->dim[1].lbound = 0;
      ret->dim[1].ubound = source->dim[0].ubound - source->dim[0].lbound;
      ret->dim[1].stride = ret->dim[0].ubound+1;

      ret->data = internal_malloc (size * size0 ((array_t*)ret));
      ret->base = 0;
    }

  sxstride = source->dim[0].stride * size;
  if (sxstride == 0)
    sxstride = size;
  systride = source->dim[1].stride * size;
  xcount = source->dim[0].ubound + 1 - source->dim[0].lbound;
  ycount = source->dim[1].ubound + 1 - source->dim[1].lbound;

  rxstride = ret->dim[0].stride * size;
  if (rxstride == 0)
    rxstride = size;
  rystride = ret->dim[1].stride * size;

  rptr = ret->data;
  sptr = source->data;

  for (y = 0; y < ycount; y++)
    {
      for (x = 0; x < xcount; x++)
        {
          memcpy (rptr, sptr, size);

          sptr += sxstride;
          rptr += rystride;
        }
      sptr += systride - (sxstride * xcount);
      rptr += rxstride - (rystride * xcount);
    }
}

