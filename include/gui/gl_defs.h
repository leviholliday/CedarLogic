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
// size of any real circuit, and well inside a float's precision. A damaged file
// or clipboard text can say anything; a part beyond this is brought to the edge.
#define WORLD_LIMIT 1.0e6f

// A coordinate held inside +/-WORLD_LIMIT (NaN becomes 0).
inline float worldClamp(float v) {
	if (v != v) return 0.0f;
	return v < -WORLD_LIMIT ? -WORLD_LIMIT : v > WORLD_LIMIT ? WORLD_LIMIT : v;
}

#endif /*GL_DEFS_H_*/
