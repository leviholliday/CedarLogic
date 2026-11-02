// A small PDF writer for the lab report: US Letter pages that are each one
// picture (8-bit gray or RGB), compressed here with Deflate, so there is no
// PDF library to ship and nothing to install. Plain standard C++: no Windows
// calls (it builds and is tried anywhere).

#ifndef CL_WINDOWS_PDF_H
#define CL_WINDOWS_PDF_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pdf {

// zlib (RFC 1950) data for `size` bytes: Deflate with LZ77 matches and
// Huffman codes made for the data.
std::vector<uint8_t> zlibCompress(const uint8_t* data, size_t size);

class Document {
public:
	// `title` and `producer` are UTF-8; `date` is "YYYYMMDDHHMMSS" local time (or empty).
	Document(const std::string& title, const std::string& producer, const std::string& date);

	// A page `widthPt` x `heightPt` points showing a picture `w` x `h` pixels,
	// top row first: `components` 1 is gray (w*h bytes), 3 is RGB (w*h*3).
	void addPage(float widthPt, float heightPt, int w, int h, int components, const uint8_t* pixels);
	int pageCount() const { return (int)pages.size(); }
	// The finished file's bytes (the document is closed: no more pages).
	const std::string& finish();

private:
	std::string out, title, producer, date;
	std::vector<size_t> offsets;   // of object n + 1
	std::vector<int> pages;        // each page object's number
	bool finished = false;
	int begin();                   // starts the next object, returning its number
	void end();
};

}  // namespace pdf

#endif  // CL_WINDOWS_PDF_H
