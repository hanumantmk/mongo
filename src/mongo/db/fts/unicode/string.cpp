/**
 *    Copyright (C) 2015 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/db/fts/unicode/string.h"

#include <altivec.h>
#undef vector
#undef bool

#include <algorithm>
#include <boost/algorithm/searching/boyer_moore.hpp>
#include <boost/predef.h>

#include "mongo/platform/bits.h"
#include "mongo/shell/linenoise_utf8.h"
#include "mongo/util/assert_util.h"

#if BOOST_HW_SIMD_X86 >= BOOST_HW_SIMD_X86_SSE2_VERSION
#include "emmintrin.h"
#endif

namespace mongo {
namespace unicode {

using linenoise_utf8::copyString32to8;
using linenoise_utf8::copyString8to32;

using std::u32string;

String::String(const StringData utf8_src) {
    // Convert UTF-8 input to UTF-32 data.
    setData(utf8_src);
}

void String::resetData(const StringData utf8_src) {
    // Convert UTF-8 input to UTF-32 data.
    setData(utf8_src);
}

void String::setData(const StringData utf8_src) {
    // _data is the target, resize it so that it's guaranteed to fit all of the input characters,
    // plus a null character if there isn't one.
    _data.resize(utf8_src.size() + 1);

    int result = 0;
    size_t resultSize = 0;

    // Although utf8_src.rawData() is not guaranteed to be null-terminated, copyString8to32 won't
    // access bad memory because it is limited by the size of its output buffer, which is set to the
    // size of utf8_src.
    copyString8to32(&_data[0],
                    reinterpret_cast<const unsigned char*>(&utf8_src.rawData()[0]),
                    _data.size(),
                    resultSize,
                    result);

    uassert(28755, "text contains invalid UTF-8", result == 0);

    // Resize _data so it is only as big as what it contains.
    _data.resize(resultSize);
    _needsOutputConversion = true;
}

std::string String::toString() {
    // _outputBuf is the target, resize it so that it's guaranteed to fit all of the input
    // characters, plus a null character if there isn't one.
    if (_needsOutputConversion) {
        _outputBuf.resize(_data.size() * 4 + 1);
        size_t resultSize = copyString32to8(
            reinterpret_cast<unsigned char*>(&_outputBuf[0]), &_data[0], _outputBuf.size());

        // Resize output so it is only as large as what it contains.
        _outputBuf.resize(resultSize);
        _needsOutputConversion = false;
    }
    return _outputBuf;
}

String String::substr(size_t pos, size_t len) const {
    unicode::String buf;
    substrToBuf(pos, len, buf);
    return buf;
}

String String::toLower(CaseFoldMode mode) const {
    unicode::String buf;
    toLowerToBuf(mode, buf);
    return buf;
}

String String::removeDiacritics() const {
    unicode::String buf;
    removeDiacriticsToBuf(buf);
    return buf;
}

void String::copyToBuf(String& buffer) const {
    buffer._data = _data;
    buffer._data.resize(_data.size());
    auto index = 0;
    for (auto codepoint : _data) {
        buffer._data[index++] = codepoint;
    }
    buffer._needsOutputConversion = true;
}

void String::substrToBuf(size_t pos, size_t len, String& buffer) const {
    buffer._data.resize(len + 1);
    for (size_t index = 0, src_pos = pos; index < len;) {
        buffer._data[index++] = _data[src_pos++];
    }
    buffer._data[len] = '\0';
    buffer._needsOutputConversion = true;
}

void String::toLowerToBuf(CaseFoldMode mode, String& buffer) const {
    buffer._data.resize(_data.size());
    auto outIt = buffer._data.begin();
    for (auto codepoint : _data) {
        *outIt++ = codepointToLower(codepoint, mode);
    }
    buffer._needsOutputConversion = true;
}

void String::removeDiacriticsToBuf(String& buffer) const {
    buffer._data.resize(_data.size());
    auto outIt = buffer._data.begin();
    for (auto codepoint : _data) {
        if (codepoint <= 0x7f) {
            // ASCII only has two diacritics so they are hard-coded here.
            if (codepoint != '^' && codepoint != '`') {
                *outIt++ = codepoint;
            }
        } else if (auto clean = codepointRemoveDiacritics(codepoint)) {
            *outIt++ = clean;
        } else {
            // codepoint was a pure diacritic mark, so skip it.
        }
    }
    buffer._data.resize(outIt - buffer._data.begin());
    buffer._needsOutputConversion = true;
}

namespace {
#define HAVE_FAST_BYTES
/**
 * A sequence of bytes that can be manipulated using vectorized instructions.
 *
 * This is specific to the use-case below and not intended as a general purpose vector class.
 */
class Bytes {
public:
    using Native = __vector signed char;
    using Mask = uint64_t;
    using Scalar = int8_t;
    static const int size = sizeof(Native);

    /**
     * Sets all bytes to 0.
     */
    Bytes() {
        _data = vec_splat_s8(0);
    }

    /**
     * Sets all bytes to val.
     */
    explicit Bytes(Scalar val) {
	_data = vec_splats(val);
    }

    /**
     * Load a vector from a potentially unaligned location.
     */
    static Bytes load(const void* ptr) {
        // This function is documented as taking an unaligned pointer.
        return vec_vsx_ld(0, reinterpret_cast<const Native*>(ptr));
    }

    /**
     * Store this vector to a potentially unaligned location.
     */
    void store(void* ptr) const {
        // This function is documented as taking an unaligned pointer.
        vec_vsx_st(_data, 0, reinterpret_cast<Native*>(ptr));
    }

    /**
     * Returns a bitmask with the high bit from each byte.
     */
    Mask maskHigh() const {
	const Native bits = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120};
	return vec_extract(vec_vbpermq(_data, bits), 0);
    }

    /**
     * Returns a bitmask with any bit from each byte.
     *
     * This operation only makes sense if all bytes are either 0x00 or 0xff, such as the result from
     * comparison operations.
     */
    Mask maskAny() const {
        return maskHigh();  // Other archs may be more efficient here.
    }

    /**
     * Counts zero bits in mask from whichever side corresponds to the lowest memory address.
     */
    static uint32_t countInitialZeros(Mask mask) {
        return mask == 0 ? size : countLeadingZeros64(mask << (64 - sizeof(Native)));
    }

    /**
     * Sets each byte to 0xff if it is ==(EQ), <(LT), or >(GT), otherwise 0x00.
     *
     * May use either signed or unsigned comparisons since this use case doesn't care about bytes
     * with high bit set.
     */
    Bytes compareEQ(Scalar val) const {
        return (Native)vec_cmpeq(_data, Bytes(val)._data);
    }
    Bytes compareLT(Scalar val) const {
        return (Native)vec_cmplt(_data, Bytes(val)._data);
    }
    Bytes compareGT(Scalar val) const {
        return (Native)vec_cmpgt(_data, Bytes(val)._data);
    }

    Bytes operator|(Bytes other) const {
        return (Native)vec_or(_data, other._data);
    }

    Bytes& operator|=(Bytes other) {
        return (*this = (*this | other));
    }

    Bytes operator&(Bytes other) const {
        return (Native)vec_and(_data, other._data);
    }

    Bytes& operator&=(Bytes other) {
        return (*this = (*this & other));
    }

private:
    Bytes(Native data) : _data(data) {}

    Native _data;
};
}

std::pair<std::unique_ptr<char[]>, char*> String::prepForSubstrMatch(StringData utf8,
                                                                     SubstrMatchOptions options,
                                                                     CaseFoldMode mode) {
    // This function should only be called when casefolding or stripping diacritics.
    dassert(!(options & kCaseSensitive) || !(options & kDiacriticSensitive));

    // Allocate space for up to 2x growth which is the worst possible case for stripping diacritics
    // and casefolding. Proof: the only case where 1 byte goes to >1 is 'I' in Turkish going to 2
    // bytes. The biggest codepoint is 4 bytes which is also 2x 2 bytes. This holds as long as we
    // don't map a single code point to more than one.
    std::unique_ptr<char[]> buffer(new char[utf8.size() * 2]);
    auto outputIt = buffer.get();

    for (auto inputIt = utf8.begin(), endIt = utf8.end(); inputIt != endIt;) {
#ifdef HAVE_FAST_BYTES
        if (size_t(endIt - inputIt) >= Bytes::size) {
            // Try the fast path for 16 contiguous bytes of ASCII.
            auto word = Bytes::load(&*inputIt);

            // Count the bytes of ASCII.
            uint32_t usableBytes = Bytes::countInitialZeros(word.maskHigh());
            if (usableBytes) {
                if (!(options & kCaseSensitive)) {
                    // 0xFF for each byte in word that is uppercase, 0x00 for all others.
                    Bytes uppercaseMask = word.compareGT('A' - 1) & word.compareLT('Z' + 1);
                    word |= (uppercaseMask & Bytes(0x20));
                }

                if (!(options & kDiacriticSensitive)) {
                    Bytes::Mask diacriticMask =
                        word.compareEQ('^').maskAny() | word.compareEQ('`').maskAny();
                    if (diacriticMask) {
                        usableBytes =
                            std::min(usableBytes, Bytes::countInitialZeros(diacriticMask));
                    }
                }

                word.store(&*outputIt);
                outputIt += usableBytes;
                inputIt += usableBytes;
                if (usableBytes == Bytes::size)
                    continue;
            }
            // If we get here, inputIt is positioned on a byte that we know needs special handling.
            // Either it isn't ASCII or it is a diacritic that needs to be stripped.
        }
#endif
        const uint8_t firstByte = *inputIt++;
        char32_t codepoint = 0;
        if (firstByte <= 0x7f) {
            // ASCII special case. Can use faster operations.
            if ((!(options & kCaseSensitive)) && (firstByte >= 'A' && firstByte <= 'Z')) {
                codepoint = (mode == CaseFoldMode::kTurkish && firstByte == 'I')
                    ? 0x131                // In Turkish, I -> ı (i with no dot).
                    : (firstByte | 0x20);  // Set the ascii lowercase bit on the character.
            } else {
                // ASCII has two pure diacritics that should be skipped and no characters that
                // change when removing diacritics.
                if ((options & kDiacriticSensitive) || !(firstByte == '^' || firstByte == '`')) {
                    *outputIt++ = (firstByte);
                }
                continue;
            }
        } else {
            // firstByte indicates that it is not an ASCII char.
            int leadingOnes = countLeadingZeros64(~(uint64_t(firstByte) << (64 - 8)));

            // Only checking enough to ensure that this code doesn't crash in the face of malformed
            // utf-8. We make no guarantees about what results will be returned in this case.
            uassert(ErrorCodes::BadValue,
                    "text contains invalid UTF-8",
                    leadingOnes > 1 && leadingOnes <= 4 && inputIt + leadingOnes - 1 <= endIt);

            codepoint = firstByte & (0xff >> leadingOnes);  // mask off the size indicator.
            for (int subByteIx = 1; subByteIx < leadingOnes; subByteIx++) {
                const uint8_t subByte = *inputIt++;
                codepoint <<= 6;
                codepoint |= subByte & 0x3f;  // mask off continuation bits.
            }

            if (!(options & kCaseSensitive)) {
                codepoint = codepointToLower(codepoint, mode);
            }

            if (!(options & kDiacriticSensitive)) {
                codepoint = codepointRemoveDiacritics(codepoint);
                if (!codepoint)
                    continue;  // codepoint is a pure diacritic.
            }
        }

        // Back to utf-8.
        if (codepoint <= 0x7f /* max 1-byte codepoint */) {
            *outputIt++ = (codepoint);
        } else if (codepoint <= 0x7ff /* max 2-byte codepoint*/) {
            *outputIt++ = ((codepoint >> (6 * 1)) | 0xc0);  // 2 leading 1s.
            *outputIt++ = (((codepoint >> (6 * 0)) & 0x3f) | 0x80);
        } else if (codepoint <= 0xffff /* max 3-byte codepoint*/) {
            *outputIt++ = ((codepoint >> (6 * 2)) | 0xe0);  // 3 leading 1s.
            *outputIt++ = (((codepoint >> (6 * 1)) & 0x3f) | 0x80);
            *outputIt++ = (((codepoint >> (6 * 0)) & 0x3f) | 0x80);
        } else {
            *outputIt++ = ((codepoint >> (6 * 3)) | 0xf0);  // 4 leading 1s.
            *outputIt++ = (((codepoint >> (6 * 2)) & 0x3f) | 0x80);
            *outputIt++ = (((codepoint >> (6 * 1)) & 0x3f) | 0x80);
            *outputIt++ = (((codepoint >> (6 * 0)) & 0x3f) | 0x80);
        }
    }

    return {std::move(buffer), outputIt};
}

bool String::substrMatch(const std::string& str,
                         const std::string& find,
                         SubstrMatchOptions options,
                         CaseFoldMode cfMode) {
    if (cfMode == CaseFoldMode::kTurkish) {
        // Turkish comparisons are always case insensitive due to their handling of I/i.
        options &= ~kCaseSensitive;
    }

    if ((options & kCaseSensitive) && (options & kDiacriticSensitive)) {
        // No transformation needed. Just do the search on the input strings.
        return boost::algorithm::boyer_moore_search(
                   str.cbegin(), str.cend(), find.cbegin(), find.cend()) != str.cend();
    }

    auto haystack = prepForSubstrMatch(str, options, cfMode);
    auto needle = prepForSubstrMatch(find, options, cfMode);

    // Case sensitive and diacritic sensitive.
    return boost::algorithm::boyer_moore_search(
               haystack.first.get(), haystack.second, needle.first.get(), needle.second) !=
        haystack.second;
}

}  // namespace unicode
}  // namespace mongo
