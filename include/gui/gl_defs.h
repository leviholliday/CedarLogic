#ifndef GL_DEFS_H_
#define GL_DEFS_H_

#include "gl_wrapper.h"

struct GLPoint2f {
	GLPoint2f( GLfloat newX = 0.0, GLfloat newY = 0.0 ) : x(newX), y(newY) {};
	GLfloat x, y;

	GLPoint2f operator+(const GLPoint2f &other) const;

	GLPoint2f operator-(const GLPoint2f &other) const;

	void operator+=(const GLPoint2f &other);

	void operator-=(const GLPoint2f &other);

	bool operator==(const GLPoint2f &other) const;

	bool operator!=(const GLPoint2f &other) const;
};

struct GLLine2f {
	GLPoint2f begin;
	GLPoint2f end;
};

#define WIRE_BBOX_THICKNESS 0.25
#define DEG2RAD 0.0174533
#define EQUALRANGE 0.00125

// How far from the origin a part may be, in world units: a thousand times the
// size of any real circuit, and where a float still holds the 0.5 grid exactly.
// A damaged file can say anything; the loader brings a part beyond this to the
// edge. Moves aren't held to it, so they stay rigid.
#define WORLD_LIMIT 1.0e6f

// Where a paste may put a part: well inside WORLD_LIMIT, so a pasted block
// (dropped where the pointer is) still lies inside it, and saves and reopens
// the same.
#define PASTE_LIMIT (WORLD_LIMIT / 10)

// A coordinate to a ten-thousandth of a grid unit: how a save writes one, and
// how one is taken in from a file or a paste, so what is in memory is what a
// save writes and a reopen reads back.
inline double savedPrecision(double v) {
	if (!(v > -1e12 && v < 1e12)) return v;   // far past WORLD_LIMIT (or NaN): held there, not here
	const double r = (double)(long long)(v * 10000.0 + (v < 0 ? -0.5 : 0.5)) / 10000.0;
	return r == 0 ? 0.0 : r;   // no "-0"
}

// A coordinate held inside +/-limit (NaN becomes 0).
inline float worldClamp(float v, float limit = WORLD_LIMIT) {
	if (v != v) return 0.0f;
	return v < -limit ? -limit : v > limit ? limit : v;
}

#endif /*GL_DEFS_H_*/
