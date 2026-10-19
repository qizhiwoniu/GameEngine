//---------------------------------------------------------------------------

#include <vcl.h>
#pragma hdrstop

#include "Setting.h"
#include "ViewHotkeys.h"		// the shortcuts the viewport really uses (ViewHotkeys().Key[cmd])
#include <System.TypInfo.hpp>
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma resource "*.dfm"
TForm2 *Form2;
//---------------------------------------------------------------------------
// Theme colours (same values as the main form)
static inline int Rgbi(int r, int g, int b)
{
	return static_cast<int>(RGB(r, g, b));
}
static TColor MixCol(TColor A, TColor B, int PctB)
{
	COLORREF a = static_cast<COLORREF>(ColorToRGB(A));
	COLORREF b = static_cast<COLORREF>(ColorToRGB(B));
	return static_cast<TColor>(RGB(
		(GetRValue(a) * (100 - PctB) + GetRValue(b) * PctB) / 100,
		(GetGValue(a) * (100 - PctB) + GetGValue(b) * PctB) / 100,
		(GetBValue(a) * (100 - PctB) + GetBValue(b) * PctB) / 100));
}
struct TFormPalette
{
	int Bg;		// form / panels
	int Input;	// edits, combo boxes, lists
	int Text;
};
static TFormPalette GetFormPalette(bool ADark)
{
	TFormPalette P;
	if (ADark)
	{
		P.Bg    = Rgbi(32, 32, 32);
		P.Input = Rgbi(51, 51, 51);
		P.Text  = Rgbi(255, 255, 255);
	}
	else
	{
		P.Bg    = Rgbi(243, 243, 243);
		P.Input = Rgbi(255, 255, 255);
		P.Text  = Rgbi(0, 0, 0);
	}
	return P;
}
//---------------------------------------------------------------------------
// SetWindowTheme / DwmSetWindowAttribute are loaded dynamically so no extra import library is needed
typedef HRESULT (WINAPI *TSetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
typedef HRESULT (WINAPI *TDwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);

static TSetWindowThemeFn GetSetWindowTheme()
{
	static TSetWindowThemeFn Fn = NULL;
	static bool Tried = false;
	if (!Tried)
	{
		Tried = true;
		HMODULE h = LoadLibraryW(L"uxtheme.dll");
		if (h)
			Fn = reinterpret_cast<TSetWindowThemeFn>(GetProcAddress(h, "SetWindowTheme"));
	}
	return Fn;
}
//---------------------------------------------------------------------------
// ===========================  HOTKEY DATA  ===========================
//---------------------------------------------------------------------------
static void FillDefaultHotkeys(std::vector<THotkeyBinding> &V)
{
	// All viewport commands (TViewCmd in ViewHotkeys.h) and their default keys
	struct TDef { const wchar_t *Id; const wchar_t *Name; int Cmd; int VK; int Mods; };
	static const TDef D[] =
	{
		{ L"persp",  L"\u900F\u89C6\u56FE",          vcPerspective, 'P' },	// 透视图
		{ L"ortho",  L"\u6B63\u4EA4\u56FE",          vcOrtho,       'O' },	// 正交图
		{ L"top",    L"\u9876\u89C6\u56FE",          vcTop,         'T' },	// 顶视图
		{ L"front",  L"\u524D\u89C6\u56FE",          vcFront,       'F' },	// 前视图
		{ L"left",   L"\u5DE6\u89C6\u56FE",          vcLeft,        'L' },	// 左视图
		{ L"camera", L"\u76F8\u673A",                 vcCamera,      'C' },	// 相机
		{ L"stand",  L"\u9ED8\u8BA4\u7167\u660E",   vcStand,       'D' },	// 默认照明
		{ L"wire",   L"\u7EBF\u6846\u56FE",          vcWireframe,   0   },	// 线框图
		{ L"reset",  L"\u91CD\u7F6E\u89C6\u56FE",   vcResetView,   0   },	// 重置视图
		{ L"grid",   L"\u5207\u6362\u6805\u683C",   vcGrid,        'G' },	// 切换栅格
		{ L"focus",  L"\u805A\u7126\u9009\u5B9A\u7269\u4F53", vcFocus, 'Z' },	// 聚焦选定物体
		{ L"fps",    L"\u663E\u793A FPS",             vcShowFps,     '7' },	// 显示 FPS
		{ L"toolsel",   L"\u9009\u62E9\u5DE5\u5177",       vcToolSelect, 'Q' },	// 选择工具
		{ L"toolmove",  L"\u79FB\u52A8\u5DE5\u5177",       vcToolMove,   'W' },	// 移动工具
		{ L"toolrot",   L"\u65CB\u8F6C\u5DE5\u5177",       vcToolRotate, 'E' },	// 旋转工具
		{ L"toolscale", L"\u7F29\u653E\u5DE5\u5177",       vcToolScale,  'R' },	// 缩放工具
		{ L"snap",      L"\u542F\u7528\u6355\u6349",       vcSnapToggle, 'S', 0 },	// 启用捕捉
		{ L"anglesnap", L"\u89D2\u5EA6\u6355\u6349",       vcAngleSnap,  'A', 0 },	// 角度捕捉
		{ L"pctsnap",   L"\u767E\u5206\u6BD4\u6355\u6349",   vcPercentSnap,'P', HKM_SHIFT }	// 百分比捕捉
	};
	V.clear();
	for (size_t i = 0; i < sizeof(D) / sizeof(D[0]); i++)
	{
		THotkeyBinding B;
		B.Id = D[i].Id;
		B.Name = D[i].Name;
		B.Cmd = D[i].Cmd;
		B.VK = B.DefVK = D[i].VK;
		B.Mods = B.DefMods = D[i].Mods;
		V.push_back(B);
	}
}
//---------------------------------------------------------------------------
// Read the shortcuts the viewport is using right now (ViewHotkeys) into the table
static void SyncFromView(std::vector<THotkeyBinding> &V)
{
	for (size_t i = 0; i < V.size(); i++)
	{
		TShortCut Sc = ViewHotkeys().Key[V[i].Cmd];
		V[i].VK = 0;
		V[i].Mods = 0;
		if (Sc != 0)
		{
			Word vk;
			TShiftState ss;
			ShortCutToKey(Sc, vk, ss);
			V[i].VK = vk;
			V[i].Mods = (ss.Contains(ssShift) ? HKM_SHIFT : 0) | (ss.Contains(ssCtrl) ? HKM_CTRL : 0) |
						(ss.Contains(ssAlt) ? HKM_ALT : 0);
		}
	}
}
//---------------------------------------------------------------------------
// Give the edited table to the viewport: it polls ViewHotkeys().Key every frame
static void SyncToView(const std::vector<THotkeyBinding> &V)
{
	for (size_t i = 0; i < V.size(); i++)
	{
		TShortCut Sc = 0;
		if (V[i].VK > 0)
		{
			TShiftState ss;
			if (V[i].Mods & HKM_SHIFT) ss << ssShift;
			if (V[i].Mods & HKM_CTRL)  ss << ssCtrl;
			if (V[i].Mods & HKM_ALT)   ss << ssAlt;
			Sc = ShortCut(static_cast<Word>(V[i].VK), ss);
		}
		ViewHotkeys().Key[V[i].Cmd] = Sc;
	}
}
//---------------------------------------------------------------------------
std::vector<THotkeyBinding>& GlobalHotkeys()
{
	static std::vector<THotkeyBinding> V;
	static bool Init = false;
	if (!Init)
	{
		Init = true;
		FillDefaultHotkeys(V);
	}
	return V;
}
//---------------------------------------------------------------------------
const THotkeyBinding* FindHotkey(const String &Id)
{
	std::vector<THotkeyBinding> &V = GlobalHotkeys();
	for (size_t i = 0; i < V.size(); i++)
		if (V[i].Id == Id)
			return &V[i];
	return NULL;
}
//---------------------------------------------------------------------------
// ===========================  KEYBOARD LAYOUT  ===========================
// Widths are in key units; the whole board is 15 units wide.
// Cap == NULL ends a row, VK == -1 is an empty gap, VK == 0 is a key that cannot be assigned.
struct TKeyDef { const wchar_t *Cap; int VK; float W; };
#define ROW_END { NULL, -2, 0 }
static const TKeyDef KEY_LAYOUT[] =
{
	// function row
	{ L"Esc", VK_ESCAPE, 1 }, { L"", -1, 1 },
	{ L"F1", VK_F1, 1 }, { L"F2", VK_F2, 1 }, { L"F3", VK_F3, 1 }, { L"F4", VK_F4, 1 }, { L"", -1, 0.5f },
	{ L"F5", VK_F5, 1 }, { L"F6", VK_F6, 1 }, { L"F7", VK_F7, 1 }, { L"F8", VK_F8, 1 }, { L"", -1, 0.5f },
	{ L"F9", VK_F9, 1 }, { L"F10", VK_F10, 1 }, { L"F11", VK_F11, 1 }, { L"F12", VK_F12, 1 },
	ROW_END,
	// number row
	{ L"`", VK_OEM_3, 1 }, { L"1", '1', 1 }, { L"2", '2', 1 }, { L"3", '3', 1 }, { L"4", '4', 1 },
	{ L"5", '5', 1 }, { L"6", '6', 1 }, { L"7", '7', 1 }, { L"8", '8', 1 }, { L"9", '9', 1 },
	{ L"0", '0', 1 }, { L"-", VK_OEM_MINUS, 1 }, { L"=", VK_OEM_PLUS, 1 }, { L"Backspace", VK_BACK, 2 },
	ROW_END,
	// Q row
	{ L"Tab", VK_TAB, 1.5f },
	{ L"Q", 'Q', 1 }, { L"W", 'W', 1 }, { L"E", 'E', 1 }, { L"R", 'R', 1 }, { L"T", 'T', 1 },
	{ L"Y", 'Y', 1 }, { L"U", 'U', 1 }, { L"I", 'I', 1 }, { L"O", 'O', 1 }, { L"P", 'P', 1 },
	{ L"[", VK_OEM_4, 1 }, { L"]", VK_OEM_6, 1 }, { L"\\", VK_OEM_5, 1.5f },
	ROW_END,
	// A row
	{ L"Caps", VK_CAPITAL, 1.75f },
	{ L"A", 'A', 1 }, { L"S", 'S', 1 }, { L"D", 'D', 1 }, { L"F", 'F', 1 }, { L"G", 'G', 1 },
	{ L"H", 'H', 1 }, { L"J", 'J', 1 }, { L"K", 'K', 1 }, { L"L", 'L', 1 },
	{ L";", VK_OEM_1, 1 }, { L"'", VK_OEM_7, 1 }, { L"Enter", VK_RETURN, 2.25f },
	ROW_END,
	// Z row
	{ L"Shift", VK_SHIFT, 2.25f },
	{ L"Z", 'Z', 1 }, { L"X", 'X', 1 }, { L"C", 'C', 1 }, { L"V", 'V', 1 }, { L"B", 'B', 1 },
	{ L"N", 'N', 1 }, { L"M", 'M', 1 },
	{ L",", VK_OEM_COMMA, 1 }, { L".", VK_OEM_PERIOD, 1 }, { L"/", VK_OEM_2, 1 }, { L"Shift", VK_SHIFT, 2.75f },
	ROW_END,
	// bottom row
	{ L"Ctrl", VK_CONTROL, 1.5f }, { L"Win", 0, 1.5f }, { L"Alt", VK_MENU, 1.5f },
	{ L"Space", VK_SPACE, 5.5f },
	{ L"Alt", VK_MENU, 1.5f }, { L"Win", 0, 1.5f }, { L"Ctrl", VK_CONTROL, 1.5f },
	ROW_END
};
static const int KEY_COUNT = sizeof(KEY_LAYOUT) / sizeof(KEY_LAYOUT[0]);
//---------------------------------------------------------------------------
static int ModBit(int VK)
{
	if (VK == VK_SHIFT)   return HKM_SHIFT;
	if (VK == VK_CONTROL) return HKM_CTRL;
	if (VK == VK_MENU)    return HKM_ALT;
	return 0;
}
//---------------------------------------------------------------------------
static String FindCap(int VK)
{
	for (int i = 0; i < KEY_COUNT; i++)
		if (KEY_LAYOUT[i].Cap && KEY_LAYOUT[i].VK == VK)
			return String(KEY_LAYOUT[i].Cap);
	return String();
}
//---------------------------------------------------------------------------
String HotkeyText(int VK, int Mods)
{
	if (VK <= 0)
		return String();
	String S;
	if (Mods & HKM_CTRL)  S += L"Ctrl+";
	if (Mods & HKM_ALT)   S += L"Alt+";
	if (Mods & HKM_SHIFT) S += L"Shift+";
	S += FindCap(VK);
	return S;
}
//---------------------------------------------------------------------------
// ===========================  KEYBOARD VIEW  ===========================
// A Maya-style keyboard picture. Shows which keys are bound for the current modifier state
// (Shift / Ctrl / Alt keys on the board toggle it), highlights bound keys and shows the command name
// on them. A dot in the corner of a key = it has a binding with a different modifier combination.
class TKeyboardView : public TCustomControl
{
private:
	struct TKeyBox { TRect R; int VK; String Cap; };
	std::vector<TKeyBox> FKeys;
	int  FLW, FLH;			// size the layout was built for
	int  FHot;				// index of the key under the mouse
	TColor FBg, FKeyC, FKeyDis, FEdge, FFg, FBound, FAccent;
	TNotifyEvent FOnKeySelect, FOnModsChange;

	void __fastcall BuildLayout()
	{
		FKeys.clear();
		float U = ClientWidth / 15.0f;
		float U2 = ClientHeight / 6.5f;
		if (U2 < U) U = U2;
		float ox = (ClientWidth - U * 15.0f) / 2.0f, oy = 2.0f;
		float x = 0, y = 0;
		int Row = 0;
		for (int i = 0; i < KEY_COUNT; i++)
		{
			const TKeyDef &d = KEY_LAYOUT[i];
			if (!d.Cap)								// end of row
			{
				x = 0;
				y += 1.0f;
				if (Row == 0) y += 0.5f;			// gap below the function row
				Row++;
				continue;
			}
			if (d.VK != -1)
			{
				TKeyBox k;
				k.R = TRect((int)(ox + x * U + 1.5f), (int)(oy + y * U + 1.5f),
							(int)(ox + (x + d.W) * U - 1.5f), (int)(oy + (y + 1.0f) * U - 1.5f));
				k.VK = d.VK;
				k.Cap = d.Cap;
				FKeys.push_back(k);
			}
			x += d.W;
		}
		FLW = ClientWidth;
		FLH = ClientHeight;
	}

	int __fastcall HitTest(int X, int Y)
	{
		if (FLW != ClientWidth || FLH != ClientHeight || FKeys.empty())
			BuildLayout();
		for (int i = (int)FKeys.size() - 1; i >= 0; i--)
			if (FKeys[i].R.Contains(TPoint(X, Y)))
				return i;
		return -1;
	}

	const THotkeyBinding* __fastcall FindBind(int VK, int AMods)
	{
		if (!Bind)
			return NULL;
		for (size_t i = 0; i < Bind->size(); i++)
			if ((*Bind)[i].VK == VK && (*Bind)[i].Mods == AMods)
				return &(*Bind)[i];
		return NULL;
	}

	bool __fastcall AnyBind(int VK)
	{
		if (!Bind)
			return false;
		for (size_t i = 0; i < Bind->size(); i++)
			if ((*Bind)[i].VK == VK)
				return true;
		return false;
	}

	void __fastcall CMMouseLeave(TMessage &Message)
	{
		TCustomControl::Dispatch(&Message);
		if (FHot != -1)
		{
			FHot = -1;
			Invalidate();
		}
	}

protected:
	virtual void __fastcall Paint()
	{
		if (FLW != ClientWidth || FLH != ClientHeight || FKeys.empty())
			BuildLayout();
		TCanvas *C = Canvas;
		C->Brush->Style = bsSolid;
		C->Brush->Color = FBg;
		C->FillRect(ClientRect);
		C->Font->Name = L"Segoe UI";

		for (size_t i = 0; i < FKeys.size(); i++)
		{
			const TKeyBox &k = FKeys[i];
			const THotkeyBinding *B = (k.VK > 0 && !ModBit(k.VK)) ? FindBind(k.VK, Mods) : NULL;
			bool IsMod = ModBit(k.VK) != 0;
			bool ModOn = IsMod && (Mods & ModBit(k.VK));
			bool Other = !B && !IsMod && k.VK > 0 && AnyBind(k.VK);
			bool Sel   = !IsMod && k.VK > 0 && k.VK == SelVK;

			TColor Fill = FKeyC;
			TColor Txt  = FFg;
			if (k.VK == 0)   { Fill = FKeyDis; Txt = MixCol(FKeyDis, FFg, 40); }
			else if (B)        Fill = FBound;
			else if (ModOn)  { Fill = FAccent;  Txt = clWhite; }
			if (k.VK > 0 && (int)i == FHot)
				Fill = MixCol(Fill, FFg, 14);

			C->Brush->Color = Fill;
			C->Pen->Color   = Sel ? FAccent : FEdge;
			C->Pen->Width   = Sel ? 2 : 1;
			C->RoundRect(k.R.Left, k.R.Top, k.R.Right, k.R.Bottom, 6, 6);
			C->Pen->Width = 1;

			C->Brush->Style = bsClear;
			C->Font->Size  = 9;
			C->Font->Color = Txt;
			TRect T = k.R;
			T.Left += 5; T.Top += 3;
			DrawText(C->Handle, k.Cap.c_str(), -1, &T, DT_SINGLELINE | DT_LEFT | DT_TOP | DT_NOPREFIX);

			if (B)
			{
				C->Font->Size = 8;
				TRect N = k.R;
				N.Left += 2; N.Right -= 2; N.Bottom -= 3;
				DrawText(C->Handle, B->Name.c_str(), -1, &N,
					DT_SINGLELINE | DT_CENTER | DT_BOTTOM | DT_END_ELLIPSIS | DT_NOPREFIX);
			}
			C->Brush->Style = bsSolid;
			if (Other)
			{
				C->Brush->Color = FAccent;
				C->Pen->Color   = FAccent;
				C->Ellipse(k.R.Right - 12, k.R.Top + 5, k.R.Right - 6, k.R.Top + 11);
			}
		}
	}

	DYNAMIC void __fastcall MouseMove(TShiftState Shift, int X, int Y)
	{
		TCustomControl::MouseMove(Shift, X, Y);
		int h = HitTest(X, Y);
		if (h != FHot)
		{
			FHot = h;
			Invalidate();
		}
	}

	DYNAMIC void __fastcall MouseDown(TMouseButton Button, TShiftState Shift, int X, int Y)
	{
		TCustomControl::MouseDown(Button, Shift, X, Y);
		if (Button != mbLeft)
			return;
		int h = HitTest(X, Y);
		if (h < 0 || FKeys[h].VK <= 0)
			return;
		int vk = FKeys[h].VK;
		int bit = ModBit(vk);
		if (bit)
		{
			Mods ^= bit;				// Shift / Ctrl / Alt toggle the modifier state
			Invalidate();
			if (FOnModsChange)
				FOnModsChange(this);
		}
		else
		{
			SelVK = vk;
			Invalidate();
			if (FOnKeySelect)
				FOnKeySelect(this);
		}
	}

	BEGIN_MESSAGE_MAP
		VCL_MESSAGE_HANDLER(CM_MOUSELEAVE, TMessage, CMMouseLeave)
	END_MESSAGE_MAP(TCustomControl)

public:
	std::vector<THotkeyBinding> *Bind;		// the table being edited
	int Mods;								// current modifier state shown on the board
	int SelVK;								// selected key (0 = none)

	__fastcall TKeyboardView(TComponent *AOwner)
		: TCustomControl(AOwner), FLW(0), FLH(0), FHot(-1), FOnKeySelect(NULL), FOnModsChange(NULL),
		  Bind(NULL), Mods(0), SelVK(0)
	{
		ControlStyle = ControlStyle << csOpaque;
		DoubleBuffered = true;
		SetDark(true);
	}

	void __fastcall SetDark(bool ADark)
	{
		FAccent = static_cast<TColor>(RGB(0, 120, 215));
		if (ADark)
		{
			FBg = static_cast<TColor>(RGB(32, 32, 32));    FKeyC = static_cast<TColor>(RGB(62, 62, 62));
			FKeyDis = static_cast<TColor>(RGB(42, 42, 42)); FEdge = static_cast<TColor>(RGB(90, 90, 90));
			FFg = clWhite;                                  FBound = static_cast<TColor>(RGB(28, 92, 150));
		}
		else
		{
			FBg = static_cast<TColor>(RGB(243, 243, 243)); FKeyC = clWhite;
			FKeyDis = static_cast<TColor>(RGB(230, 230, 230)); FEdge = static_cast<TColor>(RGB(180, 180, 180));
			FFg = clBlack;                                  FBound = static_cast<TColor>(RGB(170, 208, 245));
		}
		Invalidate();
	}

	__property TNotifyEvent OnKeySelect  = { read = FOnKeySelect,  write = FOnKeySelect };
	__property TNotifyEvent OnModsChange = { read = FOnModsChange, write = FOnModsChange };
};
//---------------------------------------------------------------------------
// Dark tab bar (replaces the native tab strip, which cannot be made dark reliably).
// The pages are plain panels that are shown / hidden by OnChange.
class TDarkTabs : public TCustomControl
{
private:
	std::vector<String> FCaps;
	std::vector<TRect>  FRects;
	int    FActive, FHot;
	TColor FBg, FFg, FHotC, FLine, FAccent;
	TNotifyEvent FOnChange;

	void __fastcall LayoutTabs()
	{
		FRects.clear();
		Canvas->Font->Name = L"Segoe UI";
		Canvas->Font->Size = 10;
		int x = 0;
		for (size_t i = 0; i < FCaps.size(); i++)
		{
			int w = Canvas->TextWidth(FCaps[i]) + 36;
			FRects.push_back(TRect(x, 0, x + w, Height - 1));
			x += w;
		}
	}
	int __fastcall HitTest(int X, int Y)
	{
		LayoutTabs();
		for (size_t i = 0; i < FRects.size(); i++)
			if (FRects[i].Contains(TPoint(X, Y)))
				return (int)i;
		return -1;
	}
	void __fastcall CMMouseLeave(TMessage &Message)
	{
		TCustomControl::Dispatch(&Message);
		if (FHot != -1)
		{
			FHot = -1;
			Invalidate();
		}
	}
protected:
	virtual void __fastcall Paint()
	{
		LayoutTabs();
		TCanvas *C = Canvas;
		C->Brush->Style = bsSolid;
		C->Brush->Color = FBg;
		C->FillRect(ClientRect);
		C->Pen->Color = FLine;						// line under the whole bar
		C->MoveTo(0, Height - 1);
		C->LineTo(Width, Height - 1);
		for (size_t i = 0; i < FRects.size(); i++)
		{
			TRect R = FRects[i];
			if ((int)i == FHot && (int)i != FActive)
			{
				C->Brush->Color = FHotC;
				C->FillRect(R);
			}
			if ((int)i == FActive)
			{
				C->Brush->Color = FAccent;			// accent bar under the active tab
				C->FillRect(TRect(R.Left, R.Bottom - 2, R.Right, R.Bottom + 1));
			}
			C->Brush->Style = bsClear;
			C->Font->Color = FFg;
			C->Font->Style = (int)i == FActive ? (TFontStyles() << fsBold) : TFontStyles();
			DrawText(C->Handle, FCaps[i].c_str(), -1, &R, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
			C->Brush->Style = bsSolid;
		}
	}
	DYNAMIC void __fastcall MouseMove(TShiftState Shift, int X, int Y)
	{
		TCustomControl::MouseMove(Shift, X, Y);
		int h = HitTest(X, Y);
		if (h != FHot)
		{
			FHot = h;
			Invalidate();
		}
	}
	DYNAMIC void __fastcall MouseDown(TMouseButton Button, TShiftState Shift, int X, int Y)
	{
		TCustomControl::MouseDown(Button, Shift, X, Y);
		int h = (Button == mbLeft) ? HitTest(X, Y) : -1;
		if (h >= 0 && h != FActive)
		{
			FActive = h;
			Invalidate();
			if (FOnChange)
				FOnChange(this);
		}
	}
	BEGIN_MESSAGE_MAP
		VCL_MESSAGE_HANDLER(CM_MOUSELEAVE, TMessage, CMMouseLeave)
	END_MESSAGE_MAP(TCustomControl)
public:
	__fastcall TDarkTabs(TComponent *AOwner)
		: TCustomControl(AOwner), FActive(0), FHot(-1), FOnChange(NULL)
	{
		ControlStyle = ControlStyle << csOpaque;
		DoubleBuffered = true;
		SetDark(true);
	}
	void __fastcall AddTab(const String &ACaption)
	{
		FCaps.push_back(ACaption);
		Invalidate();
	}
	int __fastcall GetActive() { return FActive; }
	void __fastcall SetActive(int I)
	{
		FActive = I;
		Invalidate();
		if (FOnChange)
			FOnChange(this);
	}
	void __fastcall SetDark(bool ADark)
	{
		FAccent = static_cast<TColor>(RGB(0, 120, 215));
		if (ADark)
		{
			FBg = static_cast<TColor>(RGB(32, 32, 32));  FHotC = static_cast<TColor>(RGB(51, 51, 51));
			FFg = clWhite;                                FLine = static_cast<TColor>(RGB(70, 70, 70));
		}
		else
		{
			FBg = static_cast<TColor>(RGB(243, 243, 243)); FHotC = static_cast<TColor>(RGB(225, 225, 225));
			FFg = clBlack;                                  FLine = static_cast<TColor>(RGB(200, 200, 200));
		}
		Invalidate();
	}
	__property int Active = { read = GetActive, write = SetActive };
	__property TNotifyEvent OnChange = { read = FOnChange, write = FOnChange };
};
//---------------------------------------------------------------------------
// Recursively colour a control and all its children.
static void ThemeControl(TControl *C, bool ADark, const TFormPalette &P)
{
	// The keyboard picture draws itself
	TKeyboardView *Kb = dynamic_cast<TKeyboardView*>(C);
	if (Kb)
	{
		Kb->SetDark(ADark);
		return;
	}

	// The tab bar draws itself
	TDarkTabs *Tb = dynamic_cast<TDarkTabs*>(C);
	if (Tb)
	{
		Tb->SetDark(ADark);
		return;
	}

	// The command list: dark background, white text (not the .vsf client colour)
	TListView *Lv = dynamic_cast<TListView*>(C);
	if (Lv)
	{
		Lv->StyleElements = TStyleElements() << seBorder;
		Lv->Color = static_cast<TColor>(P.Input);
		Lv->Font->Color = static_cast<TColor>(P.Text);
		Lv->HandleNeeded();
		TSetWindowThemeFn pSetLv = GetSetWindowTheme();
		if (pSetLv && Lv->HandleAllocated())
			pSetLv(Lv->Handle, ADark ? L"DarkMode_Explorer" : L"Explorer", NULL);	// dark header / scroll bars
		Lv->Invalidate();
		return;
	}

	// Check boxes / radio buttons: glyph from the .vsf style, text colour from the theme (white in dark)
	TCheckBox *Cb = dynamic_cast<TCheckBox*>(C);
	if (Cb)
	{
		Cb->StyleElements = TStyleElements() << seClient << seBorder;
		Cb->Font->Color = static_cast<TColor>(P.Text);
		return;
	}
	TRadioButton *Rb = dynamic_cast<TRadioButton*>(C);
	if (Rb)
	{
		Rb->StyleElements = TStyleElements() << seClient << seBorder;
		Rb->Font->Color = static_cast<TColor>(P.Text);
		return;
	}

	// Buttons are drawn by the global .vsf style: do NOT clear StyleElements, set colours or themes.
	if (dynamic_cast<TCustomButton*>(C))
	{
		C->StyleElements = TStyleElements() << seFont << seClient << seBorder;
		return;
	}

	// For all other controls the .vsf style must not override the colours set here.
	C->StyleElements = TStyleElements();

	// Colour of the control itself (only if the property is published, e.g. TPanel, TEdit, TMemo ...)
	bool IsInput = dynamic_cast<TCustomEdit*>(C)      || dynamic_cast<TCustomComboBox*>(C) ||
				   dynamic_cast<TCustomListBox*>(C);
	if (IsPublishedProp(C, "ParentBackground"))
		SetOrdProp(C, "ParentBackground", 0);
	if (IsPublishedProp(C, "Color"))
		SetOrdProp(C, "Color", IsInput ? P.Input : P.Bg);

	// Text colour
	if (IsPublishedProp(C, "Font"))
	{
		TFont *F = dynamic_cast<TFont*>(GetObjectProp(C, "Font"));
		if (F)
			F->Color = static_cast<TColor>(P.Text);
	}

	// Native (themed) controls: dark scroll bars, check boxes, edits ...
	TWinControl *W = dynamic_cast<TWinControl*>(C);
	if (W)
	{
		// Child handles are normally created lazily when the form is shown. Create it now,
		// otherwise SetWindowTheme would be skipped for it.
		W->HandleNeeded();
		if (W->HandleAllocated())
		{
			TSetWindowThemeFn pSet = GetSetWindowTheme();
			if (pSet)
			{
				pSet(W->Handle, ADark ? L"DarkMode_Explorer" : L"Explorer", NULL);
			}
		}
		for (int i = 0; i < W->ControlCount; i++)
			ThemeControl(W->Controls[i], ADark, P);
		W->Invalidate();
	}
}
//---------------------------------------------------------------------------
__fastcall TForm2::TForm2(TComponent* Owner)
	: TForm(Owner), FDark(false), FLoading(false), FTabs(NULL), FKb(NULL), FList(NULL), FInfo(NULL),
	  btnAssign(NULL), btnClear(NULL), btnReset(NULL), cbAutoSave(NULL), cbGrid(NULL), cbFps(NULL),
	  edAutoSave(NULL), AutoSave(true), AutoSaveMinutes(10), ShowGrid(true), ShowFps(false)
{
	Caption  = L"\u8BBE\u7F6E";			// the word "Settings" in Chinese
	Position = poOwnerFormCenter;
	Button1->Caption = L"\u786E\u5B9A";		// 确定
	Button2->Caption = L"\u53D6\u6D88";		// 取消
	BuildUi();
}
//---------------------------------------------------------------------------
// Builds the tab pages in code (only the OK / Cancel buttons are in the .dfm).
void __fastcall TForm2::BuildUi()
{
	FBind = GlobalHotkeys();

	FTabs = new TDarkTabs(this);
	FTabs->Parent = this;
	FTabs->SetBounds(8, 8, 720, 30);
	FTabs->AddTab(L"\u5FEB\u6377\u952E");		// 快捷键
	FTabs->AddTab(L"\u5E38\u89C4");				// 常规
	FTabs->AddTab(L"\u89C6\u53E3");				// 视口
	FTabs->OnChange = TabsChanged;
	for (int i = 0; i < 3; i++)
	{
		FPage[i] = new TPanel(this);
		FPage[i]->Parent = this;
		FPage[i]->SetBounds(8, 38, 720, 536);
		FPage[i]->BevelOuter  = bvNone;
		FPage[i]->ShowCaption = false;
		FPage[i]->Visible     = (i == 0);
	}

	// ======================= page 1: hotkeys =======================
	TPanel *tsKeys = FPage[0];

	FKb = new TKeyboardView(this);
	FKb->Parent = tsKeys;
	FKb->SetBounds(6, 6, 700, 304);
	FKb->Bind = &FBind;
	FKb->OnKeySelect  = KbChanged;
	FKb->OnModsChange = KbChanged;

	FInfo = new TLabel(this);
	FInfo->Parent = tsKeys;
	FInfo->SetBounds(8, 316, 690, 20);
	FInfo->AutoSize = false;
	FInfo->Font->Style = TFontStyles() << fsBold;

	FList = new TListView(this);
	FList->Parent = tsKeys;
	FList->SetBounds(6, 342, 520, 150);
	FList->ViewStyle     = vsReport;
	FList->ReadOnly      = true;
	FList->RowSelect     = true;
	FList->HideSelection = false;
	FList->MultiSelect   = false;
	TListColumn *Col = FList->Columns->Add();
	Col->Caption = L"\u547D\u4EE4";				// 命令
	Col->Width   = 300;
	Col = FList->Columns->Add();
	Col->Caption = L"\u5FEB\u6377\u952E";		// 快捷键
	Col->Width   = 190;
	FList->OnSelectItem = ListSelect;

	btnAssign = new TButton(this);
	btnAssign->Parent = tsKeys;
	btnAssign->SetBounds(540, 342, 160, 30);
	btnAssign->Caption = L"\u6307\u6D3E";		// 指派
	btnAssign->OnClick = AssignClick;

	btnClear = new TButton(this);
	btnClear->Parent = tsKeys;
	btnClear->SetBounds(540, 380, 160, 30);
	btnClear->Caption = L"\u6E05\u9664";			// 清除
	btnClear->OnClick = ClearClick;

	btnReset = new TButton(this);
	btnReset->Parent = tsKeys;
	btnReset->SetBounds(540, 418, 160, 30);
	btnReset->Caption = L"\u6062\u590D\u9ED8\u8BA4";	// 恢复默认
	btnReset->OnClick = ResetClick;

	TLabel *Hint = new TLabel(this);
	Hint->Parent = tsKeys;
	Hint->SetBounds(8, 500, 690, 34);
	Hint->AutoSize = false;
	Hint->WordWrap = true;
	Hint->Caption = L"\u63D0\u793A\uFF1A\u5728\u5217\u8868\u4E2D\u9009\u62E9\u547D\u4EE4\uFF0C\u70B9\u51FB\u952E\u76D8\u4E0A\u7684\u6309\u952E\uFF0C"
					L"\u518D\u70B9\u201C\u6307\u6D3E\u201D\u3002\u70B9\u51FB Shift / Ctrl / Alt \u53EF\u5207\u6362\u4FEE\u9970\u952E\u3002";

	// ======================= page 2: general =======================
	TPanel *tsGen = FPage[1];

	cbAutoSave = new TCheckBox(this);
	cbAutoSave->Parent = tsGen;
	cbAutoSave->SetBounds(16, 16, 240, 22);
	cbAutoSave->Caption = L"\u81EA\u52A8\u4FDD\u5B58";	// 自动保存

	TLabel *lblMin = new TLabel(this);
	lblMin->Parent = tsGen;
	lblMin->SetBounds(16, 52, 180, 17);
	lblMin->Caption = L"\u4FDD\u5B58\u95F4\u9694(\u5206\u949F)";	// 保存间隔(分钟)

	edAutoSave = new TEdit(this);
	edAutoSave->Parent = tsGen;
	edAutoSave->SetBounds(200, 48, 90, 24);
	edAutoSave->NumbersOnly = true;

	// ======================= page 3: viewport =======================
	TPanel *tsView = FPage[2];

	cbGrid = new TCheckBox(this);
	cbGrid->Parent = tsView;
	cbGrid->SetBounds(16, 16, 240, 22);
	cbGrid->Caption = L"\u663E\u793A\u6805\u683C";	// 显示栅格

	cbFps = new TCheckBox(this);
	cbFps->Parent = tsView;
	cbFps->SetBounds(16, 48, 240, 22);
	cbFps->Caption = L"\u663E\u793A FPS";

	FTabs->Active = 0;
}
//---------------------------------------------------------------------------
void __fastcall TForm2::DoShow()
{
	cbAutoSave->Checked = AutoSave;
	edAutoSave->Text    = IntToStr(AutoSaveMinutes);
	cbGrid->Checked     = ShowGrid;
	cbFps->Checked      = ShowFps;
	FBind = GlobalHotkeys();
	SyncFromView(FBind);			// always show what the viewport really uses
	RefreshList(0);
	UpdateInfo();
	TForm::DoShow();
}
//---------------------------------------------------------------------------
void __fastcall TForm2::RefreshList(int ASel)
{
	FLoading = true;
	FList->Items->BeginUpdate();
	FList->Items->Clear();
	for (size_t i = 0; i < FBind.size(); i++)
	{
		TListItem *It = FList->Items->Add();
		It->Caption = FBind[i].Name;
		It->SubItems->Add(HotkeyText(FBind[i].VK, FBind[i].Mods));
	}
	FList->Items->EndUpdate();
	FLoading = false;
	if (ASel >= 0 && ASel < FList->Items->Count)
		FList->ItemIndex = ASel;		// triggers ListSelect -> the keyboard shows that command's key
	FKb->Invalidate();
}
//---------------------------------------------------------------------------
void __fastcall TForm2::UpdateInfo()
{
	if (FKb->SelVK <= 0)
	{
		FInfo->Caption = L"";
		return;
	}
	String Txt = HotkeyText(FKb->SelVK, FKb->Mods) + L"  :  ";
	String Who = L"\u672A\u6307\u6D3E";		// 未指派
	for (size_t i = 0; i < FBind.size(); i++)
		if (FBind[i].VK == FKb->SelVK && FBind[i].Mods == FKb->Mods)
			Who = FBind[i].Name;
	FInfo->Caption = Txt + Who;
}
//---------------------------------------------------------------------------
void __fastcall TForm2::KbChanged(TObject *Sender)
{
	UpdateInfo();
}
//---------------------------------------------------------------------------
void __fastcall TForm2::TabsChanged(TObject *Sender)
{
	for (int i = 0; i < 3; i++)
		FPage[i]->Visible = (i == FTabs->Active);
}
//---------------------------------------------------------------------------
void __fastcall TForm2::ListSelect(TObject *Sender, TListItem *Item, bool Selected)
{
	if (FLoading || !Selected || !Item)
		return;
	int I = Item->Index;
	if (I < 0 || I >= (int)FBind.size())
		return;
	if (FBind[I].VK > 0)			// show the command's current key on the board
	{
		FKb->Mods  = FBind[I].Mods;
		FKb->SelVK = FBind[I].VK;
	}
	FKb->Invalidate();
	UpdateInfo();
}
//---------------------------------------------------------------------------
void __fastcall TForm2::AssignClick(TObject *Sender)
{
	int I = FList->ItemIndex;
	if (I < 0 || I >= (int)FBind.size() || FKb->SelVK <= 0)
		return;
	// the same key combination can only belong to one command
	for (size_t k = 0; k < FBind.size(); k++)
		if (FBind[k].VK == FKb->SelVK && FBind[k].Mods == FKb->Mods)
		{
			FBind[k].VK = 0;
			FBind[k].Mods = 0;
		}
	FBind[I].VK   = FKb->SelVK;
	FBind[I].Mods = FKb->Mods;
	RefreshList(I);
	UpdateInfo();
}
//---------------------------------------------------------------------------
void __fastcall TForm2::ClearClick(TObject *Sender)
{
	int I = FList->ItemIndex;
	if (I < 0 || I >= (int)FBind.size())
		return;
	FBind[I].VK = 0;
	FBind[I].Mods = 0;
	RefreshList(I);
	UpdateInfo();
}
//---------------------------------------------------------------------------
void __fastcall TForm2::ResetClick(TObject *Sender)
{
	for (size_t k = 0; k < FBind.size(); k++)
	{
		FBind[k].VK   = FBind[k].DefVK;
		FBind[k].Mods = FBind[k].DefMods;
	}
	RefreshList(FList->ItemIndex < 0 ? 0 : FList->ItemIndex);
	UpdateInfo();
}
//---------------------------------------------------------------------------
void __fastcall TForm2::Button1Click(TObject *Sender)
{
	AutoSave = cbAutoSave->Checked;
	int N;
	if (TryStrToInt(edAutoSave->Text, N) && N > 0)
		AutoSaveMinutes = N;
	ShowGrid = cbGrid->Checked;
	ShowFps  = cbFps->Checked;
	GlobalHotkeys() = FBind;
	SyncToView(FBind);				// the viewport uses the new keys immediately
	ViewHotkeysSave();				// write <exe name>.ini
}
//---------------------------------------------------------------------------
void __fastcall TForm2::CreateWnd()
{
	TForm::CreateWnd();
	ApplyTitleBar();
}
//---------------------------------------------------------------------------
void __fastcall TForm2::ApplyTitleBar()
{
	// Dark / light caption bar of the normal Windows title bar (Windows 10 1809+ / Windows 11)
	static TDwmSetWindowAttributeFn pSet = NULL;
	static bool Tried = false;
	if (!Tried)
	{
		Tried = true;
		HMODULE h = LoadLibraryW(L"dwmapi.dll");
		if (h)
			pSet = reinterpret_cast<TDwmSetWindowAttributeFn>(GetProcAddress(h, "DwmSetWindowAttribute"));
	}
	if (!pSet || !HandleAllocated())
		return;

	BOOL V = FDark ? TRUE : FALSE;
	const DWORD kImmersiveDarkMode = 20;		// DWMWA_USE_IMMERSIVE_DARK_MODE (Windows 10 20H1+ / 11)
	const DWORD kImmersiveDarkModeOld = 19;		// same attribute on Windows 10 1809 / 1903
	if (FAILED(pSet(Handle, kImmersiveDarkMode, &V, sizeof(V))))
		pSet(Handle, kImmersiveDarkModeOld, &V, sizeof(V));
}
//---------------------------------------------------------------------------
void __fastcall TForm2::ApplyTheme(bool ADark)
{
	FDark = ADark;

	// The form itself is coloured by this code (StyleElements empty), but StyleName is left at its
	// default so that the global .vsf style still applies to the buttons / tabs / list.
	StyleElements = TStyleElements();

	TFormPalette P = GetFormPalette(ADark);
	Color      = static_cast<TColor>(P.Bg);
	Font->Color = static_cast<TColor>(P.Text);

	HandleNeeded();			// make sure the window handle exists so the native controls can be themed
	ApplyTitleBar();

	for (int i = 0; i < ControlCount; i++)
		ThemeControl(Controls[i], ADark, P);

	Invalidate();
}
//---------------------------------------------------------------------------
