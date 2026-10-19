//---------------------------------------------------------------------------
// ViewHotkeys.h - keyboard shortcuts of the viewport view commands (header-only)
//
// One shared table: the settings window (Setting.cpp) edits it, the viewport (Viewport3D.h) reads it
// every frame. Stored in <exe name>.ini next to the .exe, section [Hotkeys], as TShortCut integers
// (0 = not bound).
//
// Just put this file next to Viewport3D.h / Setting.cpp, it does not have to be added to the project.
//---------------------------------------------------------------------------
#ifndef ViewHotkeysH
#define ViewHotkeysH
//---------------------------------------------------------------------------
#include <System.Classes.hpp>
#include <System.SysUtils.hpp>
#include <System.IniFiles.hpp>
#include <Vcl.Menus.hpp>
#include <memory>
//---------------------------------------------------------------------------
// All viewport commands: the "+" menu in the top-left corner of the viewport and the keyboard-only ones
enum TViewCmd
{
	vcPerspective,
	vcOrtho,
	vcStand,
	vcWireframe,
	vcWire = vcWireframe, // compatibility alias for older settings code
	vcCamera,
	vcShowFps,
	vcTop,			// keyboard-only commands (no menu item)
	vcFront,
	vcLeft,
	vcResetView,
	vcGrid,
	vcFocus,		// focus (zoom to) the selected object
	vcToolSelect,	// transform tools (Q / W / E / R by default, like 3ds Max)
	vcToolMove,
	vcToolRotate,
	vcToolScale,
	vcSnapToggle,	// snaps (S / A / Shift+P by default, like 3ds Max)
	vcAngleSnap,
	vcPercentSnap,
	vcCount
};

// Keep this array in exactly the same order as TViewCmd; these names are saved in the INI file.
static const wchar_t *const kViewCmdNames[vcCount] =
	{ L"Perspective", L"Orthographic", L"Stand", L"Wireframe", L"Camera", L"ShowFps",
	  L"Top", L"Front", L"Left", L"ResetView", L"ToggleGrid", L"FocusSelected",
	  L"ToolSelect", L"ToolMove", L"ToolRotate", L"ToolScale",
	  L"SnapToggle", L"AngleSnap", L"PercentSnap" };
//---------------------------------------------------------------------------
struct TViewHotkeys
{
	TShortCut Key[vcCount];
	bool      Loaded;

	TViewHotkeys() : Loaded(false)
	{
		SetDefaults();
	}
	void SetDefaults()
	{
		Key[vcPerspective] = ShortCut(L'P', TShiftState());		// P
		Key[vcOrtho]       = ShortCut(L'O', TShiftState());		// O
		Key[vcStand]       = ShortCut(L'D', TShiftState());		// D
		Key[vcWireframe]   = 0;											// not bound (W is the Move tool)
		Key[vcCamera]      = ShortCut(L'C', TShiftState());		// C
		Key[vcShowFps]     = ShortCut(L'7', TShiftState());		// 7 (statistics, like 3ds Max)
		Key[vcTop]         = ShortCut(L'T', TShiftState());		// T
		Key[vcFront]       = ShortCut(L'F', TShiftState());		// F
		Key[vcLeft]        = ShortCut(L'L', TShiftState());		// L
		Key[vcResetView]   = 0;											// not bound (Z focuses the selection)
		Key[vcGrid]        = ShortCut(L'G', TShiftState());		// G
		Key[vcFocus]       = ShortCut(L'Z', TShiftState());		// Z
		Key[vcToolSelect]  = ShortCut(L'Q', TShiftState());		// Q
		Key[vcToolMove]    = ShortCut(L'W', TShiftState());		// W
		Key[vcToolRotate]  = ShortCut(L'E', TShiftState());		// E
		Key[vcToolScale]   = ShortCut(L'R', TShiftState());		// R
		Key[vcSnapToggle]  = ShortCut(L'S', TShiftState());		// S  (3D snap on / off)
		Key[vcAngleSnap]   = ShortCut(L'A', TShiftState());		// A  (angle snap)
		Key[vcPercentSnap] = ShortCut(L'P', TShiftState() << ssShift);	// Shift+P (percent snap)
	}
};
//---------------------------------------------------------------------------
// "inline" (not "static"): every .cpp that includes this header must see the SAME table
inline TViewHotkeys& ViewHotkeys()
{
	static TViewHotkeys H;
	return H;
}
//---------------------------------------------------------------------------
inline String ViewHotkeysFile()
{
	return ChangeFileExt(ParamStr(0), L".ini");
}
//---------------------------------------------------------------------------
// Reads the ini file once (later calls do nothing)
inline void ViewHotkeysLoad()
{
	TViewHotkeys &H = ViewHotkeys();
	if (H.Loaded)
		return;
	H.Loaded = true;
	String F = ViewHotkeysFile();
	if (!FileExists(F))
		return;
	std::unique_ptr<TIniFile> Ini(new TIniFile(F));
	for (int i = 0; i < vcCount; i++)
		H.Key[i] = static_cast<TShortCut>(Ini->ReadInteger(L"Hotkeys", kViewCmdNames[i], H.Key[i]));
}
//---------------------------------------------------------------------------
inline void ViewHotkeysSave()
{
	TViewHotkeys &H = ViewHotkeys();
	try
	{
		std::unique_ptr<TIniFile> Ini(new TIniFile(ViewHotkeysFile()));
		for (int i = 0; i < vcCount; i++)
			Ini->WriteInteger(L"Hotkeys", kViewCmdNames[i], H.Key[i]);
	}
	catch (Exception &)
	{
		// the folder of the .exe is not writable: the shortcuts still work until the program is closed
	}
}
//---------------------------------------------------------------------------
#endif
