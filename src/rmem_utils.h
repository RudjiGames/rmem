//--------------------------------------------------------------------------//
/// Copyright 2025 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#ifndef RMEM_UTILS_H
#define RMEM_UTILS_H

#include "rmem_platform.h"

#include <string.h> // memcpy, strlen
#include <wchar.h>  // wcslen

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>	// _umul128
#endif

namespace rmem {

	//------------------------------------------------------------------------
	// wyhash (public domain, https://github.com/wangyi-fudan/wyhash) - very fast for the short
	// stack-trace buffers we hash. The resulting value is written into the capture and read back
	// verbatim by the loader (never recomputed there), so it only has to be self-consistent
	// within a single run - no cross-platform/endian value matching is required.
	//------------------------------------------------------------------------
	static inline void _wymum(uint64_t* _a, uint64_t* _b)
	{
#if defined(__SIZEOF_INT128__)
		__uint128_t r = *_a; r *= *_b;
		*_a = (uint64_t)r; *_b = (uint64_t)(r >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
		*_a = _umul128(*_a, *_b, _b);
#else
		uint64_t ha = *_a >> 32, hb = *_b >> 32, la = (uint32_t)*_a, lb = (uint32_t)*_b;
		uint64_t rh = ha * hb, rm0 = ha * lb, rm1 = hb * la, rl = la * lb;
		uint64_t t = rl + (rm0 << 32), c = (t < rl);
		uint64_t lo = t + (rm1 << 32); c += (lo < t);
		*_a = lo;
		*_b = rh + (rm0 >> 32) + (rm1 >> 32) + c;
#endif
	}

	static inline uint64_t _wymix(uint64_t _a, uint64_t _b)	{ _wymum(&_a, &_b); return _a ^ _b; }
	static inline uint64_t _wyr8(const uint8_t* _p)			{ uint64_t v; memcpy(&v, _p, 8); return v; }
	static inline uint64_t _wyr4(const uint8_t* _p)			{ uint32_t v; memcpy(&v, _p, 4); return v; }
	static inline uint64_t _wyr3(const uint8_t* _p, uint32_t _k) { return (((uint64_t)_p[0]) << 16) | (((uint64_t)_p[_k >> 1]) << 8) | _p[_k - 1]; }

	static inline uint64_t wyhash64(const void* _key, uint32_t _len)
	{
		const uint64_t s0 = 0x2d358dccaa6c78a5ull, s1 = 0x8bb84b93962eacc9ull, s2 = 0x4b33a62ed433d4a3ull, s3 = 0x4d5a2da51de1aa47ull;
		const uint8_t* p = (const uint8_t*)_key;
		uint64_t seed = _wymix(s0, s1);
		uint64_t a, b;
		if (_len <= 16)
		{
			if (_len >= 4)
			{
				a = (_wyr4(p) << 32) | _wyr4(p + ((_len >> 3) << 2));
				b = (_wyr4(p + _len - 4) << 32) | _wyr4(p + _len - 4 - ((_len >> 3) << 2));
			}
			else if (_len > 0) { a = _wyr3(p, _len); b = 0; }
			else { a = 0; b = 0; }
		}
		else
		{
			uint32_t i = _len;
			if (i > 48)
			{
				uint64_t see1 = seed, see2 = seed;
				do {
					seed = _wymix(_wyr8(p)      ^ s1, _wyr8(p + 8)  ^ seed);
					see1 = _wymix(_wyr8(p + 16) ^ s2, _wyr8(p + 24) ^ see1);
					see2 = _wymix(_wyr8(p + 32) ^ s3, _wyr8(p + 40) ^ see2);
					p += 48; i -= 48;
				} while (i > 48);
				seed ^= see1 ^ see2;
			}
			while (i > 16) { seed = _wymix(_wyr8(p) ^ s1, _wyr8(p + 8) ^ seed); i -= 16; p += 16; }
			a = _wyr8(p + i - 16);
			b = _wyr8(p + i - 8);
		}
		a ^= s1; b ^= seed;
		_wymum(&a, &b);
		return _wymix(a ^ s0 ^ _len, b ^ s1);
	}

	inline uint32_t uint32_cnttzl(uint32_t _val)
	{
#if RMEM_COMPILER_MSVC && RMEM_PLATFORM_WINDOWS
		if (!_val) return 32;
		unsigned long index;
		_BitScanForward(&index, _val);
		return index;
#elif RMEM_COMPILER_GCC || RMEM_COMPILER_CLANG
		return _val ? __builtin_ctz(_val) : sizeof(_val) * 8;
#else
		// slow but should almost never be used
		for (int i = 0; i < sizeof(_val) * 8; ++i)
			if (_val & (1 << i))
				return i;
		return 32;
#endif
	}

	static inline uint32_t hashStr(const char* _string)
	{
		int hash = 0;
		uint8_t* p = (uint8_t*)_string;

		while (*p != '\0')
		{
			hash = hash + ((hash) << 5) + *(p) + ((*(p)) << 7);
			p++;
		}
		return ((hash) ^ (hash >> 16)) & 0xffff;
	} 

	static inline uint64_t hashStackTrace(uintptr_t* _backTrace, uint32_t _numEntries)
	{
		return wyhash64(_backTrace, _numEntries * sizeof(uintptr_t));
	}

	template <typename T>
	static inline void addVarToBuffer(const T& _value, uint8_t* _bufferBase, size_t& _bufferPtr)
	{
		memcpy(&_bufferBase[_bufferPtr], &_value, sizeof(T));
		_bufferPtr += sizeof(T);
	}

	/// Utility function to write data to a buffer
	static inline void addPtrToBuffer(void* ininPtr, uint32_t ininSize, uint8_t* _bufferBase, size_t& _bufferPtr)
	{
		memcpy(&_bufferBase[_bufferPtr], ininPtr, ininSize);
		_bufferPtr += ininSize;
	}

	/// Utility function to write a string to a buffer
	static inline void addStrToBuffer(const char* _string, uint8_t* _bufferBase, size_t& _bufferPtr, uint8_t _xor = 0)
	{
		uint32_t _len = _string ? (uint32_t)strlen(_string) : 0;
		memcpy(&_bufferBase[_bufferPtr],&_len,sizeof(uint32_t));
		_bufferPtr += sizeof(uint32_t);
		if (_string)
		{
			memcpy(&_bufferBase[_bufferPtr],_string,sizeof(char)*_len);
			for (uint32_t i=0; i<sizeof(char)*_len; ++i)
			{
				_bufferBase[_bufferPtr+i] = _bufferBase[_bufferPtr+i] ^ _xor;
			}
			_bufferPtr += _len*sizeof(char);
		}
	}

	static inline void addStrToBuffer(const wchar_t* _string, uint8_t* _bufferBase, size_t& _bufferPtr, uint8_t _xor = 0)
	{
		uint32_t _len = _string ? (uint32_t)wcslen(_string) : 0;
		memcpy(&_bufferBase[_bufferPtr],&_len,sizeof(uint32_t));
		_bufferPtr += sizeof(uint32_t);
		if (_string)
		{
			memcpy(&_bufferBase[_bufferPtr],_string,sizeof(wchar_t)*_len);
			for (uint32_t i=0; i<sizeof(wchar_t)*_len; ++i)
			{
				_bufferBase[_bufferPtr+i] = _bufferBase[_bufferPtr+i] ^ _xor;
			}
			_bufferPtr += _len*sizeof(wchar_t);
		}
	}

} // namespace rmem

#endif // RMEM_UTILS_H
