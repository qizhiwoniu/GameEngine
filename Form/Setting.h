//---------------------------------------------------------------------------

#ifndef SettingH
#define SettingH
//---------------------------------------------------------------------------
#include <System.Classes.hpp>
#include <Vcl.Controls.hpp>
#include <Vcl.StdCtrls.hpp>
#include <Vcl.Forms.hpp>
#include <Vcl.ComCtrls.hpp>
#include <Vcl.ExtCtrls.hpp>
#include <Vcl.WinXCtrls.hpp>
#include <vector>
//---------------------------------------------------------------------------
// ---- hotkey data (shared by the settings window and the viewport) ----
enum { HKM_SHIFT = 1, HKM_CTRL = 2, HKM_ALT = 4 };		// modifier bit mask

struct THotkeyBinding
{
	String Id;			// internal id, e.g. "persp"
	String Name;		// display name
	int    VK;			// virtual key code, 0 = not assigned
	int    Mods;		// HKM_xxx mask
	int    DefVK;		// factory default
	int    DefMods;
	int    Cmd;			// TViewCmd of the viewport command (ViewHotkeys.h)
};

// The one global binding table. The settings window edits a copy and writes it back on OK.
std::vector<THotkeyBinding>& GlobalHotkeys();
// Look a command up by id (NULL if unknown). The viewport can use it instead of hard-coded keys.
const THotkeyBinding* FindHotkey(const String &Id);
// "Ctrl+Shift+T"
String HotkeyText(int VK, int Mods);
//---------------------------------------------------------------------------
class TKeyboardView;	// the keyboard picture (defined in Setting.cpp)
class TDarkTabs;		// the dark tab bar (defined in Setting.cpp)

class TForm2 : public TForm
{
__published:	// IDE-managed Components
	TButton *Button1;
	TButton *Button2;
	void __fastcall Button1Click(TObject *Sender);
private:	// User declarations
	bool FDark;		// true = dark theme active
	bool FLoading;
	std::vector<THotkeyBinding> FBind;		// working copy of the hotkeys

	TDarkTabs     *FTabs;
	TPanel        *FPage[3];		// one panel per tab
	TKeyboardView *FKb;
	TListView     *FList;
	TLabel        *FInfo;
	TButton       *btnAssign, *btnClear, *btnReset;
	TCheckBox     *cbAutoSave, *cbGrid, *cbFps;
	TEdit         *edAutoSave;

	void __fastcall ApplyTitleBar();
	void __fastcall BuildUi();
	void __fastcall RefreshList(int ASel);
	void __fastcall UpdateInfo();
	void __fastcall KbChanged(TObject *Sender);
	void __fastcall TabsChanged(TObject *Sender);
	void __fastcall ListSelect(TObject *Sender, TListItem *Item, bool Selected);
	void __fastcall AssignClick(TObject *Sender);
	void __fastcall ClearClick(TObject *Sender);
	void __fastcall ResetClick(TObject *Sender);
protected:
	virtual void __fastcall CreateWnd();	// re-applies the dark title bar whenever the handle is (re)created
	DYNAMIC void __fastcall DoShow();		// copies the public options into the controls
public:		// User declarations
	// General / viewport options (set before ShowModal, read after mrOk)
	bool AutoSave;
	int  AutoSaveMinutes;
	bool ShowGrid;
	bool ShowFps;

	__fastcall TForm2(TComponent* Owner);
	void __fastcall ApplyTheme(bool ADark);	// call it with the main form's current theme
};
//---------------------------------------------------------------------------
extern PACKAGE TForm2 *Form2;
//---------------------------------------------------------------------------
#endif
