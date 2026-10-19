// This file includes opengl whether on windows, mac, or linux.
// 10/1/2016 - Tyler J. Drake

#ifndef GLWRAPPER_H
#define GLWRAPPER_H

#if defined(CL_NO_WX) && defined(__linux__)
// The native Linux app (linux/) never draws with OpenGL: the shared model
// only needs its type names, so it doesn't need OpenGL's headers to build.
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
#elif defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#else
#ifdef _WIN32
// windows.h defines min and max as macros, which breaks std::min/std::max in
// any file that includes this before wxWidgets has had the chance to say no.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
// Linux needs GL 3.0+ for framebuffer extension functions
#ifdef __linux__
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glu.h>
#ifdef __linux__
#include <GL/glext.h>
#endif
#endif

#endif