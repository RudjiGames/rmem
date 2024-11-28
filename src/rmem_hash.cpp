//--------------------------------------------------------------------------//
/// Copyright 2024 Milos Tosic. All Rights Reserved.                       ///
/// License: http://www.opensource.org/licenses/BSD-2-Clause               ///
//--------------------------------------------------------------------------//

#include "rmem_platform.h"

#include <algorithm>
#include <string.h> // memcpy

static uint64_t UNALIGNED_LOAD64(const char* p)
{
	uint64_t result;
	memcpy(&result, p, sizeof(result));
	return result;
}

static uint32_t UNALIGNED_LOAD32(const char* p)
{
	uint32_t result;
	memcpy(&result, p, sizeof(result));
	return result;
}

#ifdef _MSC_VER

#include <stdlib.h>
#define bswap_32(x) _byteswap_ulong(x)
#define bswap_64(x) _byteswap_uint64(x)

#elif defined(__APPLE__)

// Mac OS X / Darwin features
#include <libkern/OSByteOrder.h>
#define bswap_32(x) OSSwapInt32(x)
#define bswap_64(x) OSSwapInt64(x)

#elif defined(__sun) || defined(sun)

#include <sys/byteorder.h>
#define bswap_32(x) BSWAP_32(x)
#define bswap_64(x) BSWAP_64(x)

#elif defined(__FreeBSD__)

#if !defined(__ORBIS__) && !defined(__PROSPERO__)
#include <sys/endian.h>
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#else
#include <machine/endian.h>
#define bswap_32(x) __bswap32(x)
#define bswap_64(x) __bswap64(x)
#endif

#elif defined(__OpenBSD__)

#include <sys/types.h>
#define bswap_32(x) swap32(x)
#define bswap_64(x) swap64(x)

#elif defined(__NetBSD__)

#include <sys/types.h>
#include <machine/bswap.h>
#if defined(__BSWAP_RENAME) && !defined(__bswap_32)
#define bswap_32(x) bswap32(x)
#define bswap_64(x) bswap64(x)
#endif

#else

#include <byteswap.h>

#endif

#ifdef WORDS_BIGENDIAN
#define uint32_in_expected_order(x) (bswap_32(x))
#define uint64_in_expected_order(x) (bswap_64(x))
#else
#define uint32_in_expected_order(x) (x)
#define uint64_in_expected_order(x) (x)
#endif

static uint64_t Fetch64(const char* p) {
	return uint64_in_expected_order(UNALIGNED_LOAD64(p));
}

static uint32_t Fetch32(const char* p) {
	return uint32_in_expected_order(UNALIGNED_LOAD32(p));
}


// Some primes between 2^63 and 2^64 for various uses.
static const uint64_t k0 = 0xc3a5c85c97cb3127ULL;
static const uint64_t k1 = 0xb492b66fbe98f273ULL;
static const uint64_t k2 = 0x9ae16a3b2f90404fULL;

// Magic numbers for 32-bit hashing.  Copied from Murmur3.
static const uint32_t c1 = 0xcc9e2d51;
static const uint32_t c2 = 0x1b873593;

// A 32-bit to 32-bit integer hash copied from Murmur3.
static uint32_t fmix(uint32_t h)
{
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;
	return h;
}

static uint32_t Rotate32(uint32_t val, int shift)
{
	// Avoid shifting by 32: doing so yields an undefined result.
	return shift == 0 ? val : ((val >> shift) | (val << (32 - shift)));
}

#undef PERMUTE3
#define PERMUTE3(a, b, c) do { std::swap(a, b); std::swap(a, c); } while (0)

static uint32_t Mur(uint32_t a, uint32_t h)
{
	// Helper from Murmur3 for combining two 32-bit values.
	a *= c1;
	a = Rotate32(a, 17);
	a *= c2;
	h ^= a;
	h = Rotate32(h, 19);
	return h * 5 + 0xe6546b64;
}

static uint32_t Hash32Len13to24(const char* s, size_t len) {
	uint32_t a = Fetch32(s - 4 + (len >> 1));
	uint32_t b = Fetch32(s + 4);
	uint32_t c = Fetch32(s + len - 8);
	uint32_t d = Fetch32(s + (len >> 1));
	uint32_t e = Fetch32(s);
	uint32_t f = Fetch32(s + len - 4);
	uint32_t h = static_cast<uint32_t>(len);

	return fmix(Mur(f, Mur(e, Mur(d, Mur(c, Mur(b, Mur(a, h)))))));
}

static uint32_t Hash32Len0to4(const char* s, size_t len) {
	uint32_t b = 0;
	uint32_t c = 9;
	for (size_t i = 0; i < len; i++) {
		signed char v = static_cast<signed char>(s[i]);
		b = b * c1 + static_cast<uint32_t>(v);
		c ^= b;
	}
	return fmix(Mur(b, Mur(static_cast<uint32_t>(len), c)));
}

static uint32_t Hash32Len5to12(const char* s, size_t len) {
	uint32_t a = static_cast<uint32_t>(len), b = a * 5, c = 9, d = b;
	a += Fetch32(s);
	b += Fetch32(s + len - 4);
	c += Fetch32(s + ((len >> 1) & 4));
	return fmix(Mur(c, Mur(b, Mur(a, d))));
}

// Bitwise right rotate.  Normally this will compile to a single
// instruction, especially if the shift is a manifest constant.
static uint64_t Rotate(uint64_t val, int shift) {
	// Avoid shifting by 64: doing so yields an undefined result.
	return shift == 0 ? val : ((val >> shift) | (val << (64 - shift)));
}

static uint64_t ShiftMix(uint64_t val) {
	return val ^ (val >> 47);
}

typedef std::pair<uint64_t, uint64_t> uint128_t;

inline uint64_t Uint128Low64(const uint128_t& x) { return x.first; }
inline uint64_t Uint128High64(const uint128_t& x) { return x.second; }

// Hash 128 input bits down to 64 bits of output.
// This is intended to be a reasonably good hash function.
inline uint64_t Hash128to64(const uint128_t& x)
{
	// Murmur-inspired hashing.
	const uint64_t kMul = 0x9ddfea08eb382d69ULL;
	uint64_t a = (Uint128Low64(x) ^ Uint128High64(x)) * kMul;
	a ^= (a >> 47);
	uint64_t b = (Uint128High64(x) ^ a) * kMul;
	b ^= (b >> 47);
	b *= kMul;
	return b;
}

static uint64_t HashLen16(uint64_t u, uint64_t v) {
	return Hash128to64(uint128_t(u, v));
}

static uint64_t HashLen16(uint64_t u, uint64_t v, uint64_t mul) {
	// Murmur-inspired hashing.
	uint64_t a = (u ^ v) * mul;
	a ^= (a >> 47);
	uint64_t b = (v ^ a) * mul;
	b ^= (b >> 47);
	b *= mul;
	return b;
}

static uint64_t HashLen0to16(const char* s, size_t len) {
	if (len >= 8) {
		uint64_t mul = k2 + len * 2;
		uint64_t a = Fetch64(s) + k2;
		uint64_t b = Fetch64(s + len - 8);
		uint64_t c = Rotate(b, 37) * mul + a;
		uint64_t d = (Rotate(a, 25) + b) * mul;
		return HashLen16(c, d, mul);
	}
	if (len >= 4) {
		uint64_t mul = k2 + len * 2;
		uint64_t a = Fetch32(s);
		return HashLen16(len + (a << 3), Fetch32(s + len - 4), mul);
	}
	if (len > 0) {
		uint8_t a = static_cast<uint8_t>(s[0]);
		uint8_t b = static_cast<uint8_t>(s[len >> 1]);
		uint8_t c = static_cast<uint8_t>(s[len - 1]);
		uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
		uint32_t z = static_cast<uint32_t>(len) + (static_cast<uint32_t>(c) << 2);
		return ShiftMix(y * k2 ^ z * k0) * k2;
	}
	return k2;
}

// This probably works well for 16-byte strings as well, but it may be overkill
// in that case.
static uint64_t HashLen17to32(const char* s, size_t len) {
	uint64_t mul = k2 + len * 2;
	uint64_t a = Fetch64(s) * k1;
	uint64_t b = Fetch64(s + 8);
	uint64_t c = Fetch64(s + len - 8) * mul;
	uint64_t d = Fetch64(s + len - 16) * k2;
	return HashLen16(Rotate(a + b, 43) + Rotate(c, 30) + d,
		a + Rotate(b + k2, 18) + c, mul);
}

// Return a 16-byte hash for 48 bytes.  Quick and dirty.
// Callers do best to use "random-looking" values for a and b.
static std::pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(
	uint64_t w, uint64_t x, uint64_t y, uint64_t z, uint64_t a, uint64_t b) {
	a += w;
	b = Rotate(b + a + z, 21);
	uint64_t c = a;
	a += x;
	a += y;
	b += Rotate(a, 44);
	return std::make_pair(a + z, b + c);
}

// Return a 16-byte hash for s[0] ... s[31], a, and b.  Quick and dirty.
static std::pair<uint64_t, uint64_t> WeakHashLen32WithSeeds(
	const char* s, uint64_t a, uint64_t b) {
	return WeakHashLen32WithSeeds(Fetch64(s),
		Fetch64(s + 8),
		Fetch64(s + 16),
		Fetch64(s + 24),
		a,
		b);
}

// Return an 8-byte hash for 33 to 64 bytes.
static uint64_t HashLen33to64(const char* s, size_t len) {
	uint64_t mul = k2 + len * 2;
	uint64_t a = Fetch64(s) * k2;
	uint64_t b = Fetch64(s + 8);
	uint64_t c = Fetch64(s + len - 24);
	uint64_t d = Fetch64(s + len - 32);
	uint64_t e = Fetch64(s + 16) * k2;
	uint64_t f = Fetch64(s + 24) * 9;
	uint64_t g = Fetch64(s + len - 8);
	uint64_t h = Fetch64(s + len - 16) * mul;
	uint64_t u = Rotate(a + g, 43) + (Rotate(b, 30) + c) * 9;
	uint64_t v = ((a + g) ^ d) + f + 1;
	uint64_t w = bswap_64((u + v) * mul) + h;
	uint64_t x = Rotate(e + f, 42) + c;
	uint64_t y = (bswap_64((v + w) * mul) + g) * mul;
	uint64_t z = e + f + c;
	a = bswap_64((x + z) * mul + y) + b;
	b = ShiftMix((z + a) * mul + d + h) * mul;
	return b + x;
}

namespace rmem {

uint32_t CityHash32(const void* _key, uint32_t _len)
{
	const char* s = (const char*)_key;
	if (_len <= 24) {
		return _len <= 12 ?
			(_len <= 4 ? Hash32Len0to4(s, _len) : Hash32Len5to12(s, _len)) :
			Hash32Len13to24(s, _len);
	}

	// _len > 24
	uint32_t h = static_cast<uint32_t>(_len), g = c1 * h, f = g;
	uint32_t a0 = Rotate32(Fetch32(s + _len - 4) * c1, 17) * c2;
	uint32_t a1 = Rotate32(Fetch32(s + _len - 8) * c1, 17) * c2;
	uint32_t a2 = Rotate32(Fetch32(s + _len - 16) * c1, 17) * c2;
	uint32_t a3 = Rotate32(Fetch32(s + _len - 12) * c1, 17) * c2;
	uint32_t a4 = Rotate32(Fetch32(s + _len - 20) * c1, 17) * c2;
	h ^= a0;
	h = Rotate32(h, 19);
	h = h * 5 + 0xe6546b64;
	h ^= a2;
	h = Rotate32(h, 19);
	h = h * 5 + 0xe6546b64;
	g ^= a1;
	g = Rotate32(g, 19);
	g = g * 5 + 0xe6546b64;
	g ^= a3;
	g = Rotate32(g, 19);
	g = g * 5 + 0xe6546b64;
	f += a4;
	f = Rotate32(f, 19);
	f = f * 5 + 0xe6546b64;
	size_t iters = (_len - 1) / 20;
	do {
		uint32_t a00 = Rotate32(Fetch32(s) * c1, 17) * c2;
		uint32_t a11 = Fetch32(s + 4);
		uint32_t a22 = Rotate32(Fetch32(s + 8) * c1, 17) * c2;
		uint32_t a33 = Rotate32(Fetch32(s + 12) * c1, 17) * c2;
		uint32_t a44 = Fetch32(s + 16);
		h ^= a00;
		h = Rotate32(h, 18);
		h = h * 5 + 0xe6546b64;
		f += a11;
		f = Rotate32(f, 19);
		f = f * c1;
		g += a22;
		g = Rotate32(g, 18);
		g = g * 5 + 0xe6546b64;
		h ^= a33 + a11;
		h = Rotate32(h, 19);
		h = h * 5 + 0xe6546b64;
		g ^= a44;
		g = bswap_32(g) * 5;
		h += a44 * 5;
		h = bswap_32(h);
		f += a00;
		PERMUTE3(f, h, g);
		s += 20;
	} while (--iters != 0);
	g = Rotate32(g, 11) * c1;
	g = Rotate32(g, 17) * c1;
	f = Rotate32(f, 11) * c1;
	f = Rotate32(f, 17) * c1;
	h = Rotate32(h + g, 19);
	h = h * 5 + 0xe6546b64;
	h = Rotate32(h, 17) * c1;
	h = Rotate32(h + f, 19);
	h = h * 5 + 0xe6546b64;
	h = Rotate32(h, 17) * c1;
	return h;
}

uint64_t CityHash64(const void* _key, uint32_t _len)
{
	const char* s = (const char*)_key;
	if (_len <= 32)
	{
		if (_len <= 16)
		{
			return HashLen0to16(s, _len);
		}
		else
		{
			return HashLen17to32(s, _len);
		}
	}
	else if (_len <= 64)
	{
		return HashLen33to64(s, _len);
	}

	// For strings over 64 bytes we hash the end first, and then as we
	// loop we keep 56 bytes of state: v, w, x, y, and z.
	uint64_t x = Fetch64(s + _len - 40);
	uint64_t y = Fetch64(s + _len - 16) + Fetch64(s + _len - 56);
	uint64_t z = HashLen16(Fetch64(s + _len - 48) + _len, Fetch64(s + _len - 24));
	std::pair<uint64_t, uint64_t> v = WeakHashLen32WithSeeds(s + _len - 64, _len, z);
	std::pair<uint64_t, uint64_t> w = WeakHashLen32WithSeeds(s + _len - 32, y + k1, x);
	x = x * k1 + Fetch64(s);

	// Decrease _len to the nearest multiple of 64, and operate on 64-byte chunks.
	_len = (_len - 1) & ~static_cast<size_t>(63);
	do {
		x = Rotate(x + y + v.first + Fetch64(s + 8), 37) * k1;
		y = Rotate(y + v.second + Fetch64(s + 48), 42) * k1;
		x ^= w.second;
		y += v.first + Fetch64(s + 40);
		z = Rotate(z + w.first, 33) * k1;
		v = WeakHashLen32WithSeeds(s, v.second * k1, x + w.first);
		w = WeakHashLen32WithSeeds(s + 32, z + w.second, y + Fetch64(s + 16));
		std::swap(z, x);
		s += 64;
		_len -= 64;
	} while (_len != 0);
	return HashLen16(HashLen16(v.first, w.first) + ShiftMix(y) * k1 + z,
		HashLen16(v.second, w.second) + x);
}

static uint64_t CityHash64WithSeeds(const char* s, uint32_t _len, uint64_t seed0, uint64_t seed1)
{
	return HashLen16(CityHash64(s, _len) - seed0, seed1);
}

uint64_t CityHash64(const void* _key, uint32_t _len, uint64_t seed)
{
	const char* s = (const char*)_key;
	return CityHash64WithSeeds(s, _len, k2, seed);
}

} // namespace rmem
