// QR codes (ISO/IEC 18004) for the sync code's link: byte mode, a chosen error
// correction level, the smallest version that fits, and the mask with the
// lowest penalty score -- the same steps and tables as Project Nayuki's "QR
// Code generator" (MIT), written for this engine because nothing could be
// downloaded where it was built. (SYNC.md 1.2: level M, quiet zone 4.)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qrcodegen {

enum class Ecc { Low = 0, Medium, Quartile, High };

// size x size modules, row-major, true = dark; the quiet zone is not included.
// Empty (size 0) if the data doesn't fit in version 40.
std::vector<bool> encodeBinary(const std::vector<uint8_t>& data, Ecc ecc, int& size);

}  // namespace qrcodegen
