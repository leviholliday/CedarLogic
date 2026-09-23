/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsBBox: abstraction of bounding box objects
*****************************************************************************/

#ifndef KLSBBOX_H_
#define KLSBBOX_H_

// For GLfloat and GLPoint2f:
#include "gl_defs.h"

// For FLT_MAX and fabs():
#include <float.h>
#include <cmath>

// Included to use the min() and max() templates:
#include <algorithm>

class klsBBox
{
public:
	// Default constructor resets the bbox:
	klsBBox() { reset(); };

	// Copy constructor.
	klsBBox( const klsBBox& copyBox ) : bboxMin(copyBox.bboxMin), bboxMax(copyBox.bboxMax) {};

	// Clear the bbox to be ready to add points to it:
	void reset( void ) {
		bboxMin = GLPoint2f(FLT_MAX, FLT_MAX);
		bboxMax = GLPoint2f(-FLT_MAX, -FLT_MAX);
	};

	bool empty( void ) {
		return ((getLeft() > getRight()) || (getBottom() > getTop() ) );
	};

	// Insert a point into the box, expanding the box dimensions to
	// enclose the new point if needed.
	void addPoint( GLPoint2f newPt ) {
		if( newPt.x < bboxMin.x ) {
			bboxMin.x = newPt.x;
		}		
		if( newPt.y < bboxMin.y ) {
			bboxMin.y = newPt.y;
		}		
		if( newPt.x > bboxMax.x ) {
			bboxMax.x = newPt.x;
		}		
		if( newPt.y > bboxMax.y ) {
			bboxMax.y = newPt.y;
		}
	};

	// Insert a bbox into this box, to build an overall group bbox: an empty
	// inBox (reset() and never given a point -- a gate with no shape geometry
	// yet) contributes nothing, the same way empty() already makes contains()
	// and overlaps() above answer false rather than trust its corners.
	//
	// Skipping this check let one empty child poison the whole accumulation:
	// addPoint(inBox.getBottomLeft()) pulls this box's min corner out to
	// (-FLT_MAX, -FLT_MAX) since that is inBox's *max* corner in its empty
	// (min>max) state, and addPoint(inBox.getTopRight()) does the same to this
	// box's max corner at (FLT_MAX, FLT_MAX) -- so this box now spans the
	// entire representable float range instead of staying empty. Callers that
	// go on to compute a width (maxX - minX) then get 2*FLT_MAX, which
	// overflows a float to infinity, which turns into a `(long)` cast of
	// infinity -- undefined behavior -- the first time anything (a render
	// scale, a zoom-to-fit) divides by that width. Found via
	// GUICanvas::renderToScene -> drawGridInto when the circuit held one gate
	// whose shape never got populated.
	void addBBox( klsBBox inBox ) {
		if (inBox.empty()) return;
		this->addPoint( inBox.getBottomLeft() );
		this->addPoint( inBox.getTopRight() );
	};

	// Check if a point is within the bbox:
	bool contains( GLPoint2f inPt ) {
		// Make sure that the bboxes are valid:
		if( this->empty() ) return false;
		
		if( (inPt.x >= bboxMin.x) && (inPt.x <= bboxMax.x) &&
			(inPt.y >= bboxMin.y) && (inPt.y <= bboxMax.y) ) {
			return true;
		} else {
			return false;
		}
	};

	// Check if another box is completely within this bbox:
	bool contains( klsBBox inBox ) {
		if( this->contains( inBox.getBottomLeft() ) &&
			this->contains( inBox.getTopRight() ) ) {
			return true;
		} else {
			return false;
		}
	};

	// Simple overlap checking to see if two bounding boxes overlap.
	// (This is ok for a small number of bounding boxes, but it is
	// horribly slow for large groups of bboxes.)
	bool overlaps( klsBBox oBox ) {
		// Make sure that the bboxes are valid:
		if( this->empty() || oBox.empty() ) return false;

		if( oBox.getRight() < this->getLeft() ) return false;
		if( this->getRight() < oBox.getLeft() ) return false;

		if( oBox.getTop() < this->getBottom() ) return false;
		if( this->getTop() < oBox.getBottom() ) return false;

		return true;		
	};
	
	// Return the exact overlap intersection of two bounding boxes:
	klsBBox intersect( klsBBox oBox ) {
		klsBBox temp;
		if( this->overlaps( oBox ) ) {
			// For each axis, just use the largest "start" and the smallest "end"
			// coordinate as the intersection box.

			GLPoint2f a, b;
			a.x = max( this->getLeft(), oBox.getLeft() );
			a.y = max( this->getBottom(), oBox.getBottom() );
			
			b.x = min( this->getRight(), oBox.getRight() );
			b.y = min( this->getTop(), oBox.getTop() );
			
			temp.addPoint( a );
			temp.addPoint( b );
			return temp;
		} else {
			// Return an empty bbox, since they
			// don't overlap:
			return temp;
		}
	};

	// Corner access methods:
	GLPoint2f getTopLeft( void ) { return GLPoint2f(bboxMin.x, bboxMax.y); };
	GLPoint2f getTopRight( void ) { return GLPoint2f(bboxMax.x, bboxMax.y); };
	GLPoint2f getBottomLeft( void ) { return GLPoint2f(bboxMin.x, bboxMin.y); };
	GLPoint2f getBottomRight( void ) { return GLPoint2f(bboxMax.x, bboxMin.y); };

	// Edge access methods:
	GLfloat getLeft( void ) { return bboxMin.x; };
	GLfloat getRight( void ) { return bboxMax.x; };
	GLfloat getTop( void ) { return bboxMax.y; };
	GLfloat getBottom( void ) { return bboxMin.y; };
	
	// Edge modification methods:
	// They enforce that left < right, and bottom < top!
	void extendLeft( GLfloat amt ) {
		if( (amt > 0.0) ||  ( fabs(amt) < (getRight() - getLeft()) ) ) {
			bboxMin.x = getLeft() - amt;
		} else {
			bboxMin.x = getRight(); // Zero-width bbox.
		}
	};
	
	void extendRight( GLfloat amt ) {
		if( (amt > 0.0) ||  ( fabs(amt) < (getRight() - getLeft()) ) ) {
			bboxMax.x = getRight() + amt;
		} else {
			bboxMin.x = getLeft(); // Zero-width bbox.
		}
	};

	void extendBottom( GLfloat amt ) {
		if( (amt > 0.0) ||  ( fabs(amt) < (getTop() - getBottom()) ) ) {
			bboxMin.y = getBottom() - amt;
		} else {
			bboxMin.y = getTop(); // Zero-width bbox.
		}
	};
	
	void extendTop( GLfloat amt ) {
		if( (amt > 0.0) ||  ( fabs(amt) < (getTop() - getBottom()) ) ) {
			bboxMax.y = getTop() + amt;
		} else {
			bboxMin.y = getBottom(); // Zero-width bbox.
		}
	};

protected:
	GLPoint2f bboxMin, bboxMax;
};

#endif /*KLSBBOX_H_*/
