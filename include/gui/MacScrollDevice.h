/*****************************************************************************
   Project: CEDAR Logic Simulator
   MacScrollDevice: tell a trackpad scroll from a mouse wheel.
*****************************************************************************/

#ifndef MACSCROLLDEVICE_H_
#define MACSCROLLDEVICE_H_

#ifdef __APPLE__
// True when the scroll being handled right now has pixel-precise deltas -- a
// trackpad or Magic Mouse -- and false for a notched mouse wheel. Only
// meaningful while handling a wheel event.
bool MacCurrentScrollIsPrecise();
#endif

#endif
