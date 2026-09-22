/*****************************************************************************
   Project: CEDAR Logic Simulator
   Copyright 2006 Cedarville University, Benjamin Sprague,
                     Matt Lewellyn, and David Knierim
   All rights reserved.
   For license information see license.txt included with distribution.   

   klsClipboard: handles copy and paste of blocks
*****************************************************************************/

#ifndef KLSCLIPBOARD_H_
#define KLSCLIPBOARD_H_

#include <vector>
using namespace std;

class cmdPasteBlock;
class GUICircuit;
class GUICanvas;

class klsClipboard {
public:
	klsClipboard() { return; };
	virtual ~klsClipboard() { return; };
	
	cmdPasteBlock* pasteBlock( GUICircuit* gCircuit, GUICanvas* gCanvas );
	void copyBlock( GUICircuit* gCircuit, GUICanvas* gCanvas, vector < unsigned long > gates, vector < unsigned long > wires );

	// The two halves of copy/paste without the system clipboard -- for
	// Duplicate, which shouldn't overwrite what the user last copied.
	// `useClipboard` lets paste keep its JUNCTION_ID auto-increment, which
	// rewrites the clipboard text so the next paste keeps counting.
	string serializeBlock( GUICircuit* gCircuit, GUICanvas* gCanvas, vector < unsigned long > gates, vector < unsigned long > wires );
	cmdPasteBlock* pasteText( GUICircuit* gCircuit, GUICanvas* gCanvas, const string& pasteText, bool useClipboard );
};

#endif /*KLSCLIPBOARD_H_*/
