/*****************************************************************************
   Project: CEDAR Logic Simulator
   MacScrollDevice: tell a trackpad scroll from a mouse wheel.
*****************************************************************************/

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#include "MacScrollDevice.h"

bool MacCurrentScrollIsPrecise() {
	// wx handles the native scroll synchronously, so the app's current event
	// is the one being handled.
	NSEvent* e = [NSApp currentEvent];
	if (e == nil || e.type != NSEventTypeScrollWheel) return true;
	return e.hasPreciseScrollingDeltas;
}

#endif
