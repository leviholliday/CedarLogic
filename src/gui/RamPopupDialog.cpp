// For compilers that supports precompilation , includes "wx/wx.h"
#include "wx/wxprec.h"
 
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#include <ostream>
#include <sstream>
#include <iomanip>
#include <wx/sizer.h>
#include "RamPopupDialog.h"
#include "guiGate.h"
#include "GUICircuit.h"
#include "RenderMode.h"

//#define LIST_ID (wxID_HIGHEST + 1)
#define ID_CHECKBOX (wxID_HIGHEST + 1)
#define ID_MEMCONTENTS (wxID_HIGHEST + 2)
//#define ID_EDIT (wxID_HIGHEST + 4)


using namespace std;

DECLARE_APP(MainApp)

RamPopupDialog::RamPopupDialog( guiGateRAM* newM_guiGateRAM, 
        unsigned long bitsInAddress, GUICircuit* newGUICircuit )
	: wxDialog( wxGetApp().GetTopWindow(), -1, RAM_TITLE, 
	    wxPoint(RAM_X_POS, RAM_Y_POS), 
	    wxSize(RAM_WIDTH, RAM_HEIGHT),
	    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	m_guiGateRAM = newM_guiGateRAM;
	gUICircuit = newGUICircuit;
	
	
	int bitsInData;

	istringstream iss(m_guiGateRAM->getLogicParam("DATA_BITS"));
	iss >> bitsInData;
	
	
	int dataSize = bitsInData / 4; //convert to nibbles
	int addressSize = bitsInAddress / 4;

	wxBoxSizer* topSizer = new wxBoxSizer( wxVERTICAL );
	wxBoxSizer* buttonSizer = new wxBoxSizer( wxHORIZONTAL );
	
	closeBtn = new wxButton( this, wxID_CLOSE );
	loadBtn = new wxButton( this, wxID_OPEN );
	saveBtn = new wxButton( this, wxID_SAVE );
	
	hexOrDecCB = new wxCheckBox(this, ID_CHECKBOX, "Show Decimal");
	
	wxGridTableBase* gridTable = new virtualGrid(addressSize, dataSize, m_guiGateRAM, gUICircuit ,hexOrDecCB); 
	memContents = new wxGrid(this, ID_MEMCONTENTS);
	memContents->SetTable(gridTable , true);
		
	topSizer->Add( hexOrDecCB, wxSizerFlags(0).Align(0).Border(wxALL, 5 ));
	
	topSizer->Add( memContents, wxSizerFlags(1).Align(0).Expand().Border(wxALL, 5 ));
	
	buttonSizer->Add( loadBtn, wxSizerFlags(0).Align(wxALIGN_LEFT).Border(wxALL, 5 ));
	buttonSizer->Add( saveBtn, wxSizerFlags(0).Align(wxALIGN_LEFT).Border(wxALL, 5 ));
	buttonSizer->Add( closeBtn,wxSizerFlags(0).Align(wxALIGN_RIGHT).Border(wxALL, 5 ));
	
	topSizer->Add( buttonSizer,wxSizerFlags(0).Align(0).Border(wxALL, 5 ));
	
	SetSizer( topSizer );
	topSizer->SetSizeHints( this );

	applyTheme();
	notifyAllChanged();
	memContents->AutoSizeColumns(true);
}

// The memory grid follows the app's theme. Every colour here used to be left
// at its default except a hard-coded white cell background, so in dark mode
// the window came up with light text on white and nothing could be read.
void RamPopupDialog::applyTheme() {
	const bool dark = renderMode().darkMode;
	const wxColour paper = dark ? wxColour(30, 33, 39) : *wxWHITE;
	const wxColour ink = dark ? wxColour(228, 232, 240) : wxColour(20, 22, 28);
	const wxColour label = dark ? wxColour(44, 48, 56) : wxColour(238, 239, 242);
	const wxColour lines = dark ? wxColour(64, 69, 78) : wxColour(200, 202, 208);

	SetBackgroundColour(dark ? wxColour(38, 41, 47) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	if (hexOrDecCB) hexOrDecCB->SetForegroundColour(ink);
	if (memContents == nullptr) return;
	memContents->SetDefaultCellBackgroundColour(paper);
	memContents->SetDefaultCellTextColour(ink);
	memContents->SetLabelBackgroundColour(label);
	memContents->SetLabelTextColour(ink);
	memContents->SetGridLineColour(lines);
	memContents->ForceRefresh();
}

void RamPopupDialog::OnBtnClose( wxCommandEvent& event ){
	Close(false);
}
void RamPopupDialog::OnBtnLoad( wxCommandEvent& event ){
	
	wxString caption = "Open a memory file";
	wxString wildcard = "CEDAR Memory files (*.cdm)|*.cdm|INTEL-HEX (*.hex)|*.hex";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();
		string mempath = path.ToStdString();
		gUICircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, new klsMessage::Message_SET_GATE_PARAM(m_guiGateRAM->getID(), "READ_FILE", mempath)));
		gUICircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_UPDATE_GATES)); //make sure we get an update of the new file
	}
}
void RamPopupDialog::OnBtnSave( wxCommandEvent& event ){

	
	wxString caption = "Save a memory file";
	wxString wildcard = "CEDAR Memory files (*.cdm)|*.cdm";
	wxString defaultFilename = "";
	wxFileDialog dialog(this, caption, wxEmptyString, defaultFilename, wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	
	if (dialog.ShowModal() == wxID_OK) {
		wxString path = dialog.GetPath();
		string mempath = path.ToStdString();
		gUICircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, new klsMessage::Message_SET_GATE_PARAM(m_guiGateRAM->getID(), "WRITE_FILE", mempath)));
	}
}

void RamPopupDialog::OnChkBox ( wxCommandEvent &event) {
	notifyAllChanged();
}

void RamPopupDialog::OnSize(){
	Layout();
}

BEGIN_EVENT_TABLE(RamPopupDialog, wxDialog)
	EVT_BUTTON(wxID_CLOSE, RamPopupDialog::OnBtnClose)
	EVT_BUTTON(wxID_OPEN, RamPopupDialog::OnBtnLoad)
	EVT_BUTTON(wxID_SAVE, RamPopupDialog::OnBtnSave)
	EVT_CHECKBOX(ID_CHECKBOX, RamPopupDialog::OnChkBox)
END_EVENT_TABLE()

//This is called by the guiGateRAM when an item changes
void RamPopupDialog::updateGridDisplay(){
	memContents->Refresh();
}

//This gets called by the guiGate when the logicGate
//has been reloaded from a file
void RamPopupDialog::notifyAllChanged(){

	//if the user changes the width of some columns manually,
	//it would perterb them if we resized them again when
	//we switch to and from decimal.
	//Also, in decimal, it is full of zeros, so if they put any
	//number in besides a single digit, it will not fit :-P
	memContents->Refresh();
}




//*********************************************************************************
//virtualGrid methods

virtualGrid::virtualGrid (int addrSize, int dSize, guiGateRAM* newM_ramGuiGate, GUICircuit* newGUICircuit, wxCheckBox* hexOrDecCBArg) {
	hexOrDecCB = hexOrDecCBArg;
	m_guiGateRAM = newM_ramGuiGate;
	addressSize = addrSize;
	dataSize = dSize;
	gUICircuit = newGUICircuit;
}


int virtualGrid::GetNumberRows () {
	return (int)pow((float)2,(int)addressSize*4)/16;
}

int virtualGrid::GetNumberCols () {
	return 16;
}

bool virtualGrid::IsEmptyCell (int row, int col) {
	return false;
}

wxString virtualGrid::GetValue (int row, int col) {
	ostringstream stream;
		
	unsigned long data = m_guiGateRAM->getValueAt( row*16 + col );
	if (hexOrDecCB->IsChecked()) {
		stream << dec << data;
	} else {
		stream << hex << uppercase << setw ( dataSize ) << setfill ( '0' ) << data;
	}
	return stream.str();
}

void virtualGrid::SetValue (int row, int col, const wxString& value) {

	int newValue = 0;
	istringstream istream(value.ToStdString());
	
	//determine if we should interperate it as hex or decimal
	if( hexOrDecCB->IsChecked() ){
		istream >> newValue;
	}else{
		istream >> hex >> newValue;
	}
	
	//calculate the target address
	int address = row*16+col;

	//send the command

    // 09/27/11 - DKR: concatenate string and an integer, fixing bug in updating ROM/RAM values
    stringstream ss;
    ss << "Address:" << address;

	gUICircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_SET_GATE_PARAM, new klsMessage::Message_SET_GATE_PARAM(m_guiGateRAM->getID(), ss.str(), newValue)));
	gUICircuit->sendMessageToCore(klsMessage::Message(klsMessage::MT_UPDATE_GATES));
	
}

wxGridCellAttr* virtualGrid::GetAttr(int row, int col, wxGridCellAttr::wxAttrKind kind) {
	
	int readAddress = m_guiGateRAM->getLastRead();
	int writtenAddress = m_guiGateRAM->getLastWritten();

	int readCol = readAddress % 16;
	int readRow = readAddress / 16;
	int writtenCol = writtenAddress % 16;
	int writtenRow = writtenAddress / 16;
	
	
	const bool dark = renderMode().darkMode;
	const wxColour paper = dark ? wxColour(30, 33, 39) : *wxWHITE;
	const wxColour ink = dark ? wxColour(228, 232, 240) : wxColour(20, 22, 28);
	// Deep green/red behind light text in the dark theme; the bright pair only
	// works under black text.
	const wxColour readBg = dark ? wxColour(22, 82, 52) : *wxGREEN;
	const wxColour writeBg = dark ? wxColour(104, 34, 38) : *wxRED;

	wxGridCellAttr* returnValue = new wxGridCellAttr();
	returnValue->SetTextColour( dark ? ink : *wxBLACK );

	if( row == readRow && col == readCol ){
		returnValue->SetBackgroundColour( readBg );
	}else if( row == writtenRow && col == writtenCol ){
		returnValue->SetBackgroundColour( writeBg );
	}else{
		returnValue->SetBackgroundColour( paper );
	}

	return returnValue;
}


void virtualGrid::SetAttr(wxGridCellAttr* attr, int row, int col) {
	//ha
	return;
}

wxString virtualGrid::GetRowLabelValue(int row) {
	//set row titles
	ostringstream stream;
	stream << "0x" << hex << uppercase << setw( addressSize - 1 ) << setfill( '0' ) << row << 'X';
	return stream.str();
}

wxString virtualGrid::GetColLabelValue(int col) {
	ostringstream oss;
	oss << uppercase << hex << col;
	return oss.str();
}
