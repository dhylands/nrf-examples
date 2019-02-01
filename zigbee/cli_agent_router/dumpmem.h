/**
 * dumpmem.c - memory dumper
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.*
 */

#if !defined(DUMPMEM_H)
#define DUMPMEM_H

#include <stddef.h>

void DumpMem(const char *label, const void *buf, size_t len);

#endif  // DUMPMEM_H
