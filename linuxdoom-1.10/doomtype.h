// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	Simple basic typedefs, isolated here to make it easier
//	 separating modules.
//    
//-----------------------------------------------------------------------------


#ifndef __DOOMTYPE__
#define __DOOMTYPE__

#include <stdint.h>


#ifndef __BYTEBOOL__
#define __BYTEBOOL__
// Fixed to use builtin bool type with C++.
#ifdef __cplusplus
typedef bool boolean;
#else
typedef enum {false, true} boolean;
#endif
typedef unsigned char byte;
#endif


// Predefined with some OS.
#include <limits.h>
#define MAXCHAR		SCHAR_MAX
#define MAXSHORT	SHRT_MAX

// Max pos 32-bit int.
#define MAXINT		INT_MAX
// MAXLONG/MINLONG intentionally kept 32-bit-valued: legacy code assumed a
// 32-bit long, and LONG_MAX is 64-bit on LP64. (Unused outside this header.)
#define MAXLONG		((long)0x7fffffff)
#define MINCHAR		SCHAR_MIN
#define MINSHORT	SHRT_MIN

// Max negative 32-bit integer.
#define MININT		INT_MIN
#define MINLONG		((long)0x80000000)




#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
