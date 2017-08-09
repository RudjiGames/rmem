/*
 * Copyright (c) 2012-2016 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef __RMEM_UTILS_H__
#define __RMEM_UTILS_H__

#include "rmem_platform.h"

#include <string.h> // memcpy
#include <wchar.h>

#if RMEM_COMPILER_MSVC

#include <stdlib.h>
#define ROTL32(x,y)	_rotl(x,y)

#else // defined(_MSC_VER)

inline static uint32_t rotl32( uint32_t x, int8_t r )
{
  return (x << r) | (x >> (32 - r));
}
#define	ROTL32(x,y)	rotl32(x,y)

#endif // !defined(_MSC_VER)

namespace rmem {

//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.


	//--------------------------------------------------------------------------
	/// Murmur3 hashing
	//--------------------------------------------------------------------------
	static inline uint32_t hashMurmur3(const void* _key, uint32_t _len, uint32_t _seed = 0)
	{
		const uint8_t * data = (const uint8_t*)_key;
		const int nblocks = _len / 4;

		uint32_t h1 = _seed;

		uint32_t c1 = 0xcc9e2d51;
		uint32_t c2 = 0x1b873593;

		const uint32_t * blocks = (const uint32_t *)(data + nblocks*4);

		for (int i=-nblocks; i; ++i)
		{
			uint32_t k1 = blocks[i];

			k1 *= c1;
			k1 = ROTL32(k1,15);
			k1 *= c2;
    
			h1 ^= k1;
			h1 = ROTL32(h1,13); 
			h1 = h1*5+0xe6546b64;
		}

		const uint8_t * tail = (const uint8_t*)(data + nblocks*4);

		uint32_t k1 = 0;

		switch(_len & 3)
		{
		case 3: k1 ^= tail[2] << 16;
		case 2: k1 ^= tail[1] << 8;
		case 1: k1 ^= tail[0];
				k1 *= c1; k1 = ROTL32(k1,15); k1 *= c2; h1 ^= k1;
		};

		h1 ^= _len;
		h1 ^= h1 >> 16;
		h1 *= 0x85ebca6b;
		h1 ^= h1 >> 13;
		h1 *= 0xc2b2ae35;
		h1 ^= h1 >> 16;

		return h1;
	} 

	//--------------------------------------------------------------------------
	/// Calculate a string hash, suitable for short strings
	//--------------------------------------------------------------------------
	static inline uint32_t hashStr(const char* _string)
	{
	   uint32_t	h;
	   uint8_t*	p = (uint8_t*)_string;

	   h = 0;
	   while (*p != '\0')
	   {
		  h = 37 * h + *p;
		  p++;
	   }
	   return h;
	} 
	
	static inline uintptr_t hashStackTrace(uintptr_t* _backTrace, uint32_t _numEntries)
	{
		uintptr_t hash = 0;
		for (uint32_t i=0; i<_numEntries; ++i)
			hash += _backTrace[i];
		return hash;
	}

	template <typename T>
	static inline void addVarToBuffer(const T& _value, uint8_t* _bufferBase, uint32_t& _bufferPtr)
	{
		memcpy(&_bufferBase[_bufferPtr], &_value, sizeof(T));
		_bufferPtr += sizeof(T);
	}

	/// Utility function to write data to a buffer
	static inline void addPtrToBuffer(void* ininPtr, uint32_t ininSize, uint8_t* _bufferBase, uint32_t& _bufferPtr)
	{
		memcpy(&_bufferBase[_bufferPtr], ininPtr, ininSize);
		_bufferPtr += ininSize;
	}

	/// Utility function to write a string to a buffer
	static inline void addStrToBuffer(const char* _string, uint8_t* _bufferBase, uint32_t& _bufferPtr, uint8_t _xor = 0)
	{
		uint32_t _len = _string ? (uint32_t)strlen(_string) : 0;
		memcpy(&_bufferBase[_bufferPtr],&_len,sizeof(uint32_t));
		_bufferPtr += sizeof(uint32_t);
		if (_string)
		{
			memcpy(&_bufferBase[_bufferPtr],_string,sizeof(char)*_len);
			for (uint32_t i=0; i<sizeof(char)*_len; ++i)
				_bufferBase[_bufferPtr+i] = _bufferBase[_bufferPtr+i] ^ _xor;
			_bufferPtr += _len*sizeof(char);
		}
	}

	static inline void addStrToBuffer(const wchar_t* _string, uint8_t* _bufferBase, uint32_t& _bufferPtr, uint8_t _xor = 0)
	{
		uint32_t _len = _string ? (uint32_t)wcslen(_string) : 0;
		memcpy(&_bufferBase[_bufferPtr],&_len,sizeof(uint32_t));
		_bufferPtr += sizeof(uint32_t);
		if (_string)
		{
			memcpy(&_bufferBase[_bufferPtr],_string,sizeof(wchar_t)*_len);
			for (uint32_t i=0; i<sizeof(wchar_t)*_len; ++i)
				_bufferBase[_bufferPtr+i] = _bufferBase[_bufferPtr+i] ^ _xor;
			_bufferPtr += _len*sizeof(wchar_t);
		}
	}

} // namespace rmem

#endif // __RMEM_UTILS_H__
