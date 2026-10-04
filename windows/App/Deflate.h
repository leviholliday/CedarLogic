// Raw deflate (RFC 1951: no zlib or gzip header), the compression Share Link's
// links use (ShareLink.h). Windows has no zlib to lean on, so this is a small
// one of its own: standard C++ only, so it builds anywhere (and is checked on
// a Mac against python's zlib, windows/Tools/deflate-check.sh).
//
// The compressor finds matches with hash chains (lazily, as zlib's top level
// does) and writes each block as stored, fixed-code or dynamic-code,
// whichever is smallest; the result is about what zlib makes. The
// decompressor takes any valid stream and refuses a damaged one.

#ifndef CL_WINDOWS_DEFLATE_H
#define CL_WINDOWS_DEFLATE_H

#include <cstddef>
#include <string>

namespace deflate {

// `in` as a raw deflate stream (an empty input makes the empty stream).
std::string compress(const std::string& in);

// What a raw deflate stream holds, in `out`. False -- with `out` cleared --
// if the stream is damaged or cut short, or would be more than `limit` bytes
// (it says which in `tooBig` when given). Bytes after the stream's last block
// are ignored.
bool decompress(const std::string& in, std::string& out, size_t limit, bool* tooBig = nullptr);

}  // namespace deflate

#endif  // CL_WINDOWS_DEFLATE_H
