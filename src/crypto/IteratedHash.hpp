
#ifndef INNOEXTRACT_CRYPTO_ITERATEDHASH_HPP
#define INNOEXTRACT_CRYPTO_ITERATEDHASH_HPP

// Taken from Crypto++ and modified to fit the project.

#include <stdint.h>
#include <cstring>
#include "crypto/Checksum.hpp"
#include "util/endian.hpp"
#include "util/types.hpp"
#include "util/util.hpp"

template <class T>
class IteratedHash : public ChecksumBase< IteratedHash<T> > {
	
public:
	
	typedef T Transform;
	typedef typename Transform::HashWord HashWord;
	typedef typename Transform::ByteOrder ByteOrder;
	static const size_t BlockSize = Transform::BlockSize;
	static const size_t HashSize = Transform::HashSize;
	
	inline void init() { countLo = countHi = 0; Transform::init(state); }
	
	void update(const char * data, size_t length);
	
	void finalize(char * result);
	
private:

	size_t hash(const HashWord * input, size_t length);
	void pad(unsigned int lastBlockSize, uint8_t padFirst = 0x80);
	
	inline HashWord getBitCountHi() const {
		return (countLo >> (8 * sizeof(HashWord) - 3)) + (countHi << 3);
	}
	inline HashWord getBitCountLo() const { return countLo << 3; }
	
	HashWord data[BlockSize / sizeof(HashWord)];
	HashWord state[HashSize / sizeof(HashWord)];
	
	HashWord countLo, countHi;
	
};

template <class T>
void IteratedHash<T>::update(const char * input, size_t len) {
	
	HashWord oldCountLo = countLo;
	
	if((countLo = oldCountLo + HashWord(len)) < oldCountLo) {
		countHi++; // carry from low to high
	}
	
	countHi += HashWord(safe_right_shift<8 * sizeof(HashWord)>(len));
	
	size_t num = mod_power_of_2(oldCountLo, size_t(BlockSize));
	uint8_t * d = reinterpret_cast<uint8_t *>(data);
	
	if(num != 0) { // process left over data
		if(num + len >= BlockSize) {
			std::memcpy(d + num, input, BlockSize-num);
			hash(data, BlockSize);
			input += (BlockSize - num);
			len -= (BlockSize - num);
			num = 0;
			// drop through and do the rest
		} else {
			std::memcpy(d + num, input, len);
			return;
		}
	}
	
	// now process the input data in blocks of BlockSize bytes and save the leftovers to m_data
	if(len >= BlockSize) {
		
		if(is_aligned<T>(input)) {
			size_t leftOver = hash(reinterpret_cast<const HashWord *>(input), len);
			input += (len - leftOver);
			len = leftOver;
			
		} else {
			do { // copy input first if it's not aligned correctly
				std::memcpy(d, input, BlockSize);
				hash(data, BlockSize);
				input += BlockSize;
				len -= BlockSize;
			} while(len >= BlockSize);
		}
	}

	if(len) {
		memcpy(d, input, len);
	}
}

template <class T>
size_t IteratedHash<T>::hash(const HashWord * input, size_t length) {
	
	do {
		
		if(ByteOrder::native) {
			Transform::transform(state, input);
		} else {
			byteswap(data, input, BlockSize);
			Transform::transform(state, data);
		}
		
		input += BlockSize / sizeof(HashWord);
		length -= BlockSize;
		
	} while(length >= BlockSize);
	
	return length;
}

template <class T>
void IteratedHash<T>::pad(unsigned int lastBlockSize, uint8_t padFirst) {
	
	size_t num = mod_power_of_2(countLo, size_t(BlockSize));
	
	uint8_t * d = reinterpret_cast<uint8_t *>(data);
	
	d[num++] = padFirst;
	
	if(num <= lastBlockSize) {
		memset(d + num, 0, lastBlockSize - num);
	} else {
		memset(d + num, 0, BlockSize - num);
		hash(data, BlockSize);
		memset(d, 0, lastBlockSize);
	}
}

template <class T> inline T rotlFixed(T x, unsigned int y) {
	return T((x << y) | (x >> (sizeof(T) * 8 - y)));
}

#if defined(_MSC_VER) && _MSC_VER >= 1400 && !defined(__INTEL_COMPILER)

template<> inline uint8_t rotlFixed<uint8_t>(uint8_t x, unsigned int y) {
	return y ? _rotl8(x, y) : x;
}

// Intel C++ Compiler 10.0 gives undefined externals with these
template<> inline uint16_t rotlFixed<uint16_t>(uint16_t x, unsigned int y) {
	return y ? _rotl16(x, y) : x;
}

#endif

#ifdef _MSC_VER
template<> inline uint32_t rotlFixed<uint32_t>(uint32_t x, unsigned int y) {
	return y ? _lrotl(x, y) : x;
}
#endif

#if defined(_MSC_VER) && _MSC_VER >= 1300 && !defined(__INTEL_COMPILER)
// Intel C++ Compiler 10.0 calls a function instead of using the rotate instruction when
// using these instructions
template<> inline uint64_t rotlFixed<uint64_t>(uint64_t x, unsigned int y) {
	return y ? _rotl64(x, y) : x;
}
#endif

template <class T>
void IteratedHash<T>::finalize(char * digest) {
	
	size_t order = ByteOrder::offset;
	
	pad(BlockSize - 2 * sizeof(HashWord));
	data[BlockSize / sizeof(HashWord) - 2 + order] = ByteOrder::byteswap_if_alien(getBitCountLo());
	data[BlockSize / sizeof(HashWord) - 1 - order] = ByteOrder::byteswap_if_alien(getBitCountHi());
	
	hash(data, BlockSize);
	
	if(is_aligned<HashWord>(digest) && HashSize % sizeof(HashWord) == 0) {
		ByteOrder::byteswap_if_alien(state, reinterpret_cast<HashWord *>(digest), HashSize);
	} else {
		ByteOrder::byteswap_if_alien(state, state, HashSize);
		memcpy(digest, state, HashSize);
	}
	
}

#endif // INNOEXTRACT_CRYPTO_ITERATEDHASH_HPP
