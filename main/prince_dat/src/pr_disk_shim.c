/*  Princed V3 - Prince of Persia Level Editor for PC Version
    Copyright (C) 2003 Princed Development Team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    The authors of this program may be contacted at http://forum.princed.org
*/

/*
pr_disk_shim.c: ESP32 port shim for the Princed Resources DAT reader.

  This file is an ADDITION made for the ESP32 port. It replaces the file-I/O
  based disk.c (mLoadFileArray) with an in-memory loader that returns the
  DAT bytes embedded into the firmware image via the ESP-IDF EMBED_FILES
  mechanism, so the unmodified read path of dat.c can be reused verbatim.

  The DAT resources are embedded by the "main" component and looked up by
  name through the generated registry (see dat_registry.h / dat_registry.c).

 Note:
  DO NOT remove this copyright notice
*/

#include <stdlib.h>
#include <string.h>

#include "binary.h"
#include "disk.h"
#include "common.h" /* PR_RESULT_ERR_* */

#include "dat_registry.h"

/* In-memory replacement for disk.c's mLoadFileArray().

   dat.c's read path takes ownership of the returned buffer and frees it in
   mReadCloseDatFile(), so we hand back a malloc'd COPY of the embedded blob.
   The requested file is looked up by name in the embedded DAT registry. */
tBinary mLoadFileArray(const char* vFile) {
	tBinary r;
	r.data = NULL;
	r.size = PR_RESULT_ERR_FILE_DAT_NOT_OPEN_NOTFOUND;

	for (size_t i = 0; i < g_embedded_dats_count; i++) {
		if (strcmp(g_embedded_dats[i].name, vFile) != 0) {
			continue;
		}

		long size = (long)(g_embedded_dats[i].end - g_embedded_dats[i].start);
		if (size <= 0) {
			r.size = PR_RESULT_ERR_INVALID_DAT;
			return r;
		}

		r.data = malloc((size_t)size);
		if (r.data == NULL) {
			r.size = PR_RESULT_ERR_MEMORY;
			return r;
		}

		memcpy(r.data, g_embedded_dats[i].start, (size_t)size);
		r.size = size;
		return r;
	}

	/* Not an embedded DAT file. */
	return r;
}
