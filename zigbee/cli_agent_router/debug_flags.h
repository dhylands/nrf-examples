/**
 * debug_flags.h - debug command line interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.*
 */

#if !defined(DEBUG_FLAG)
#define DEBUG_FLAG(flag)  extern bool DEBUG_ ## flag;
#endif

DEBUG_FLAG(raw)
DEBUG_FLAG(slip)

#undef DEBUG_FLAG
