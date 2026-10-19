//---------------------------------------------------------------------------

#include <vcl.h>
#pragma hdrstop

#include "Unit1.h"
#include "Setting.h"
#include "About.h"
#include <Vcl.Themes.hpp>
#include <JvDockGlobals.hpp>		// JvGlobalDockManager (drag state)
#include <JvDockSupportControl.hpp>	// TJvDockDragDockObject
#include <Vcl.Imaging.pngimage.hpp>
#include <Vcl.ExtCtrls.hpp>
#include <Vcl.StdCtrls.hpp>
#include <Vcl.ValEdit.hpp>
#include <System.Rtti.hpp>
#include <shellapi.h>
#include <shlobj.h>
#include <memory>
#include <algorithm>
#include <vector>
#include <string>
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma link "JvDockControlForm"
#pragma link "JvDockVSNetStyle"
#pragma resource "*.dfm"
TForm1 *Form1;
//---------------------------------------------------------------------------
// ====================== CAD 风格命令行面板 ======================
// Input 面板使用 TEdit 输入命令，按 Enter 执行；执行结果输出到 Output 面板。
static TEdit *g_CommandEdit = NULL;
static TMemo *g_CommandOutput = NULL;
static bool g_CommandLineControlsCreated = false;
static TPanel *g_CommandPanel = NULL;
static TLabel *g_CommandPrompt = NULL;
static bool g_CommandDark = true;
static void ApplyCommandLineTheme(bool ADark);	// defined after SetControlTheme
static bool DockFormReallyShown(TForm *F);		// defined with the View menu code
static TMenuItem *g_DockMenuItems[4] = { NULL, NULL, NULL, NULL };	// View menu entries of the 4 dock windows (identified by pointer, not by caption)
static void ApplySnapFormTheme(TForm *F, bool ADark);	// defined after TSnapSettingsForm

class TCommandHandler : public TObject
{
public:
    TMemo *Output;

    __fastcall TCommandHandler() : TObject(), Output(NULL) {}
    void __fastcall InputKeyPress(TObject *Sender, System::WideChar &Key);
};

static TCommandHandler *g_CommandHandler = NULL;

void __fastcall TCommandHandler::InputKeyPress(TObject *Sender, System::WideChar &Key)
{
    if (Key != L'\r')
        return;

    // Enter 由命令处理器接管，不让输入框执行默认行为。
    Key = 0;

    TEdit *Edit = dynamic_cast<TEdit*>(Sender);
    if (!Edit || !Output)
        return;

    String Line = Trim(Edit->Text);
    if (Line.IsEmpty())
        return;

    // clear 清除输出历史，不保留本次命令。
    if (Line.LowerCase() == L"clear")
    {
        Output->Clear();
        Edit->Clear();
        return;
    }

    Output->Lines->Add(L"> " + Line);

    // 分离命令名称与参数，例如：echo Hello World
    int SpacePos = Line.Pos(L" ");
    String Cmd;
    String Args;
    if (SpacePos > 0)
    {
        Cmd = Trim(Line.SubString(1, SpacePos - 1));
        Args = Trim(Line.SubString(SpacePos + 1, Line.Length() - SpacePos));
    }
    else
    {
        Cmd = Line;
        Args = L"";
    }
    Cmd = Cmd.LowerCase();

    if (Cmd == L"help")
    {
        Output->Lines->Add(L"可用命令：");
        Output->Lines->Add(L"  help          显示帮助");
        Output->Lines->Add(L"  clear         清空输出");
        Output->Lines->Add(L"  echo <文字>   输出指定文字");
    }
    else if (Cmd == L"echo")
    {
        Output->Lines->Add(Args);
    }
    else
    {
        Output->Lines->Add(L"未知命令: " + Cmd);
        Output->Lines->Add(L"输入 help 查看可用命令。");
    }

    // 自动滚动到最后一行，并准备输入下一条命令。
    Output->SelStart = Output->Text.Length();
    Output->Perform(EM_SCROLLCARET, static_cast<NativeUInt>(0), static_cast<NativeInt>(0));
    Edit->Clear();
}

static void InitCommandLineControls(TForm *OutputForm)
{
    if (g_CommandLineControlsCreated || !OutputForm)
        return;

    // 类似 Rhino 的命令栏：上方是只读的命令历史/输出，下方是一行命令输入，二者在同一个窗口内。
    // Output：只读的命令历史与执行结果。
    g_CommandOutput = new TMemo(OutputForm);
    g_CommandOutput->Parent = OutputForm;
    g_CommandOutput->Align = alClient;
    g_CommandOutput->ReadOnly = true;
    g_CommandOutput->ScrollBars = ssVertical;
    g_CommandOutput->WordWrap = false;
    g_CommandOutput->BorderStyle = bsNone;
    g_CommandOutput->Font->Name = L"Consolas";
    g_CommandOutput->Lines->Add(L"命令输出窗口已就绪。输入 help 查看可用命令。");

    // Input：命令提示 + 单行编辑框，固定在窗口底部。
    TPanel *InputPanel = new TPanel(OutputForm);
    g_CommandPanel = InputPanel;
    InputPanel->Parent = OutputForm;
    InputPanel->Align = alBottom;
    InputPanel->Height = 22;
    InputPanel->BevelOuter = bvNone;
    InputPanel->Caption = L"";
    InputPanel->ShowCaption = false;

    TLabel *Prompt = new TLabel(InputPanel);
    g_CommandPrompt = Prompt;
    Prompt->Parent = InputPanel;
    Prompt->Align = alLeft;
    Prompt->Width = 56;
    Prompt->Alignment = taCenter;
    Prompt->Layout = tlCenter;
    Prompt->Caption = L"命令:";

    g_CommandEdit = new TEdit(InputPanel);
    g_CommandEdit->Parent = InputPanel;
    g_CommandEdit->Align = alClient;
    g_CommandEdit->Text = L"";
    g_CommandEdit->Font->Name = L"Consolas";
    g_CommandEdit->OnKeyPress = NULL;

    g_CommandHandler = new TCommandHandler();
    g_CommandHandler->Output = g_CommandOutput;
    g_CommandEdit->OnKeyPress = g_CommandHandler->InputKeyPress;

    g_CommandLineControlsCreated = true;
    ApplyCommandLineTheme(g_CommandDark);
}
//---------------------------------------------------------------------------
// Theme colors
static inline TColor Rgb(int r, int g, int b)
{
	return static_cast<TColor>(RGB(r, g, b));
}
static const int DARK_BAR_R = 32, DARK_BAR_G = 32, DARK_BAR_B = 32;	// title bar / menu bar background

// ---------------------------------------------------------------------------
// Theme colour presets (the row of colour swatches below the menu bar).
// The chosen preset tints the title bar, the menu bar and the theme bar itself; every preset
// has one bar colour for the dark theme and one for the light theme. Index 0 = the original look.
struct TThemePreset
{
	const wchar_t *Name;		// hint of the swatch
	COLORREF Accent;			// colour shown inside the swatch
	COLORREF DarkBar;			// bar background in the dark theme
	COLORREF LightBar;			// bar background in the light theme
};
static const TThemePreset THEME_PRESETS[] =
{
	{ L"\u9ED8\u8BA4", RGB(150, 150, 150), RGB(32, 32, 32), RGB(255, 255, 255) },	// default
	{ L"\u84DD\u8272", RGB(0, 120, 215),   RGB(22, 34, 54), RGB(222, 236, 250) },	// blue
	{ L"\u7EFF\u8272", RGB(16, 150, 80),   RGB(22, 42, 32), RGB(224, 242, 230) },	// green
	{ L"\u7D2B\u8272", RGB(136, 80, 200),  RGB(40, 28, 56), RGB(238, 230, 248) },	// purple
	{ L"\u6A59\u8272", RGB(232, 130, 30),  RGB(56, 38, 22), RGB(253, 238, 222) },	// orange
	{ L"\u7EA2\u8272", RGB(210, 60, 60),   RGB(56, 26, 26), RGB(250, 226, 226) }	// red
};
static const int THEME_PRESET_COUNT = sizeof(THEME_PRESETS) / sizeof(THEME_PRESETS[0]);
static const int THEME_SWATCH_BTN_W = 28;		// width of one swatch button (= tbrTheme->ButtonWidth)
static int g_ThemeIndex = 0;					// selected preset
static void ApplyCreateDockControlsTheme(bool ADark);

static TColor ThemeBarColor(bool ADark)
{
	const TThemePreset &P = THEME_PRESETS[g_ThemeIndex];
	return static_cast<TColor>(ADark ? P.DarkBar : P.LightBar);
}
// Mix two colours: PctB percent of B, the rest of A
static TColor MixColor(TColor A, TColor B, int PctB)
{
	COLORREF a = static_cast<COLORREF>(ColorToRGB(A));
	COLORREF b = static_cast<COLORREF>(ColorToRGB(B));
	int r = (GetRValue(a) * (100 - PctB) + GetRValue(b) * PctB) / 100;
	int g = (GetGValue(a) * (100 - PctB) + GetGValue(b) * PctB) / 100;
	int bl = (GetBValue(a) * (100 - PctB) + GetBValue(b) * PctB) / 100;
	return Rgb(r, g, bl);
}

// Colors of the drop-down (popup) menus
struct TMenuPalette
{
	TColor Bg, Hot, Text, Dim, Sep, Shortcut;
};
static TMenuPalette GetMenuPalette(bool ADark)
{
	TMenuPalette P;
	// Popup-menu background matches the currently selected theme-bar colour preset.
	P.Bg       = ThemeBarColor(ADark);
	P.Hot      = MixColor(P.Bg, ADark ? clWhite : clBlack, ADark ? 18 : 10);
	P.Text     = ADark ? clWhite : clBlack;
	P.Dim      = ADark ? Rgb(175, 175, 175) : Rgb(130, 130, 130);
	P.Sep      = MixColor(P.Bg, ADark ? clWhite : clBlack, 24);
	P.Shortcut = ADark ? Rgb(205, 205, 205) : Rgb(90, 90, 90);
	return P;
}
//---------------------------------------------------------------------------
// Ask Windows to draw its own menu chrome (the border around popup menus) in dark/light.
// Uses the undocumented uxtheme.dll ordinals 135 (SetPreferredAppMode) and 136 (FlushMenuThemes).
// If they are not available the call is silently skipped.
typedef int  (WINAPI *TSetPreferredAppModeFn)(int);
typedef void (WINAPI *TFlushMenuThemesFn)(void);
static void SetAppDarkMode(bool ADark)
{
	HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
	if (!hUx)
		return;
	TSetPreferredAppModeFn pSetMode =
		reinterpret_cast<TSetPreferredAppModeFn>(GetProcAddress(hUx, MAKEINTRESOURCEA(135)));
	TFlushMenuThemesFn pFlush =
		reinterpret_cast<TFlushMenuThemesFn>(GetProcAddress(hUx, MAKEINTRESOURCEA(136)));
	if (pSetMode)
		pSetMode(ADark ? 2 : 0);	// 2 = ForceDark, 0 = Default
	if (pFlush)
		pFlush();
	// uxtheme.dll is already loaded by the VCL, so no FreeLibrary is needed
}
//---------------------------------------------------------------------------
// Popup-menu frame
// The white frame around a drop-down menu is NOT part of the items, so OwnerDraw / hbrBack can't
// touch it. It is drawn by Windows (or by the VCL style hook when a .vsf style is active) on the
// popup window itself (window class "#32768"). A thread-local WH_CALLWNDPROCRET hook sees every
// message after the menu window has handled it, so each time a popup is shown we
//   1. remove its border styles (WS_BORDER / WS_DLGFRAME / WS_EX_*EDGE),
//   2. cut MENU_FRAME_CROP_PX pixels off every side with a window region (hides a frame that is
//      painted inside the client area),
//   3. repaint whatever non-client frame is left in the menu background colour,
//   4. on Windows 11 additionally switch off the rounded corners and the DWM border.
// Increase MENU_FRAME_CROP_PX if a thin line is still visible, set it to 0 if items get clipped.
static const int MENU_FRAME_CROP_PX = 2;
// Extra strip at the BOTTOM of the popup (menu background colour under the last item, ~3px).
// The window is always cut at least this far from the bottom edge. Raise it if a line is still
// visible, lower it (minimum 0) if the bottom of the last highlighted item gets clipped.
static const int MENU_FRAME_CROP_BOTTOM_PX = 5;

static COLORREF g_MenuFrameColor   = RGB(43, 43, 43);
static bool     g_MenuFrameActive  = false;
static HHOOK    g_MenuFrameHook    = NULL;
static HHOOK    g_MenuCbtHook      = NULL;		// strips the border while the popup is being created
static HHOOK    g_MenuPreHook      = NULL;		// applies the region BEFORE the popup is first shown
static bool     g_MenuFrameBusy    = false;		// guards against re-entrance
static HWND     g_MenuFrameLastWnd = NULL;
static HWND     g_RgnWnd = NULL;
static int      g_RgnW = 0, g_RgnH = 0;

static bool IsPopupMenuWindow(HWND hWnd)
{
	wchar_t Cls[16];
	int n = GetClassNameW(hWnd, Cls, 16);
	return n == 6 && wcscmp(Cls, L"#32768") == 0;
}
//---------------------------------------------------------------------------
typedef HRESULT (WINAPI *TDwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
static void RemoveWin11MenuBorder(HWND hWnd)
{
	static TDwmSetWindowAttributeFn pSet = NULL;
	static bool Tried = false;
	if (!Tried)
	{
		Tried = true;
		HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
		if (hDwm)
			pSet = reinterpret_cast<TDwmSetWindowAttributeFn>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
	}
	if (!pSet)
		return;

	const DWORD kCornerPreference = 33;		// DWMWA_WINDOW_CORNER_PREFERENCE (Win11 only)
	const DWORD kBorderColor      = 34;		// DWMWA_BORDER_COLOR             (Win11 only)
	DWORD Corner = 1;						// DWMWCP_DONOTROUND
	DWORD None   = 0xFFFFFFFE;				// DWMWA_COLOR_NONE
	pSet(hWnd, kCornerPreference, &Corner, sizeof(Corner));	// errors on older Windows are ignored
	pSet(hWnd, kBorderColor, &None, sizeof(None));
}
//---------------------------------------------------------------------------
// Fill the non-client frame of the window with g_MenuFrameColor.
// hdc must have its origin at the top-left corner of the WINDOW (GetWindowDC / WM_PRINT).
static void PaintMenuFrame(HWND hWnd, HDC hdc)
{
	RECT wr, cr;
	GetWindowRect(hWnd, &wr);
	GetClientRect(hWnd, &cr);
	POINT pt = { 0, 0 };
	ClientToScreen(hWnd, &pt);

	int W = wr.right - wr.left;
	int H = wr.bottom - wr.top;
	int L = pt.x - wr.left;				// frame thickness left / top / right / bottom
	int T = pt.y - wr.top;
	int R = W - L - cr.right;
	int B = H - T - cr.bottom;

	HBRUSH hb = CreateSolidBrush(g_MenuFrameColor);
	RECT r;
	if (T > 0) { r.left = 0;     r.top = 0;     r.right = W;     r.bottom = T;     FillRect(hdc, &r, hb); }
	if (B > 0) { r.left = 0;     r.top = H - B; r.right = W;     r.bottom = H;     FillRect(hdc, &r, hb); }
	if (L > 0) { r.left = 0;     r.top = T;     r.right = L;     r.bottom = H - B; FillRect(hdc, &r, hb); }
	if (R > 0) { r.left = W - R; r.top = T;     r.right = W;     r.bottom = H - B; FillRect(hdc, &r, hb); }
	DeleteObject(hb);
}
//---------------------------------------------------------------------------
static void MakeMenuBorderless(HWND hWnd)
{
	// 1. remove the border styles
	LONG_PTR St = GetWindowLongPtrW(hWnd, GWL_STYLE);
	LONG_PTR Ex = GetWindowLongPtrW(hWnd, GWL_EXSTYLE);
	LONG_PTR NewSt = St & ~static_cast<LONG_PTR>(WS_BORDER | WS_DLGFRAME);
	LONG_PTR NewEx = Ex & ~static_cast<LONG_PTR>(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
												WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
	if (NewSt != St || NewEx != Ex)
	{
		SetWindowLongPtrW(hWnd, GWL_STYLE, NewSt);
		SetWindowLongPtrW(hWnd, GWL_EXSTYLE, NewEx);
		SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}

	// 2. trim the window to exactly the area covered by the menu items (only when the popup has a new size).
	// Removing the border styles above does not resize the window, so the items move up/left and an
	// empty strip (the line you see at the bottom) is left behind. Cutting the window region to the
	// first..last item rectangle removes that strip whatever its thickness is.
	RECT wr;
	GetWindowRect(hWnd, &wr);
	int W = wr.right - wr.left;
	int H = wr.bottom - wr.top;
	int C = MENU_FRAME_CROP_PX;
	if (W > 8 && H > 8 && !(hWnd == g_RgnWnd && W == g_RgnW && H == g_RgnH))
	{
		int RL = C, RT = C, RR = W - C, RB = H - C;		// fallback: fixed crop
		const UINT kMN_GETHMENU = 0x01E1;				// undocumented "get menu handle" message
		HMENU hm = reinterpret_cast<HMENU>(SendMessageW(hWnd, kMN_GETHMENU, 0, 0));
		int n = hm ? GetMenuItemCount(hm) : 0;
		RECT First, Last;
		if (n > 0 && GetMenuItemRect(NULL, hm, 0, &First) && GetMenuItemRect(NULL, hm, n - 1, &Last))
		{
			int L2 = First.left - wr.left, T2 = First.top - wr.top;
			int R2 = Last.right - wr.left,  B2 = Last.bottom - wr.top;
			if (L2 < 0) L2 = 0;
			if (T2 < 0) T2 = 0;
			if (R2 > W) R2 = W;
			if (B2 > H) B2 = H;
			if (R2 - L2 > 8 && B2 - T2 > 8)
			{
				RL = L2; RT = T2; RR = R2; RB = B2;
			}
		}
		// always trim the bottom strip, even if the item rectangles were not available / not laid out yet
		if (RB > H - MENU_FRAME_CROP_BOTTOM_PX)
			RB = H - MENU_FRAME_CROP_BOTTOM_PX;
		if (RB - RT < 8)
			RB = H - C;
		HRGN Rgn = CreateRectRgn(RL, RT, RR, RB);
		if (SetWindowRgn(hWnd, Rgn, FALSE))		// the system owns Rgn from now on
		{
			g_RgnWnd = hWnd;
			g_RgnW = W;
			g_RgnH = H;
		}
		else
			DeleteObject(Rgn);
	}

	// 3. Windows 11 corners / DWM border
	if (hWnd != g_MenuFrameLastWnd)
	{
		g_MenuFrameLastWnd = hWnd;
		RemoveWin11MenuBorder(hWnd);
	}
}
//---------------------------------------------------------------------------
static LRESULT CALLBACK MenuFrameHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && g_MenuFrameActive && !g_MenuFrameBusy && lParam)
	{
		const CWPRETSTRUCT *M = reinterpret_cast<const CWPRETSTRUCT*>(lParam);
		switch (M->message)
		{
		case WM_SHOWWINDOW:
		case WM_WINDOWPOSCHANGED:
		case WM_NCPAINT:
		case WM_PRINT:
			if (IsPopupMenuWindow(M->hwnd))
			{
				g_MenuFrameBusy = true;
				if (M->message == WM_SHOWWINDOW || M->message == WM_WINDOWPOSCHANGED)
					MakeMenuBorderless(M->hwnd);
				else if (M->message == WM_NCPAINT)
				{
					HDC dc = GetWindowDC(M->hwnd);
					if (dc)
					{
						PaintMenuFrame(M->hwnd, dc);
						ReleaseDC(M->hwnd, dc);
					}
				}
				else if (M->lParam & PRF_NONCLIENT)		// WM_PRINT while the menu fades in
					PaintMenuFrame(M->hwnd, reinterpret_cast<HDC>(M->wParam));
				g_MenuFrameBusy = false;
			}
			break;
		}
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}
//---------------------------------------------------------------------------
// Anti-flash hooks.
// MenuFrameHookProc (above) runs AFTER the popup has handled a message, i.e. after Windows has already
// shown the menu once with its normal border/shadow frame - that is the white frame that flashes for a
// moment when a menu opens. These two hooks act earlier:
//   * MenuCbtHookProc  (WH_CBT, HCBT_CREATEWND): removes the border styles before the window exists.
//   * MenuPreHookProc  (WH_CALLWNDPROC, WM_WINDOWPOSCHANGING): sets the cropping region and switches off
//     the Win11 rounded corners / DWM border before the window becomes visible.
static LRESULT CALLBACK MenuCbtHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HCBT_CREATEWND && g_MenuFrameActive && lParam)
	{
		HWND hWnd = reinterpret_cast<HWND>(wParam);
		if (IsPopupMenuWindow(hWnd))
		{
			CBT_CREATEWNDW *Cw = reinterpret_cast<CBT_CREATEWNDW*>(lParam);
			if (Cw && Cw->lpcs)
			{
				Cw->lpcs->style   &= ~static_cast<LONG>(WS_BORDER | WS_DLGFRAME);
				Cw->lpcs->dwExStyle &= ~static_cast<DWORD>(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
															WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
			}
		}
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}
//---------------------------------------------------------------------------
static LRESULT CALLBACK MenuPreHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && g_MenuFrameActive && !g_MenuFrameBusy && lParam)
	{
		const CWPSTRUCT *M = reinterpret_cast<const CWPSTRUCT*>(lParam);
		if (M->message == WM_WINDOWPOSCHANGING && IsPopupMenuWindow(M->hwnd))
		{
			const WINDOWPOS *Wp = reinterpret_cast<const WINDOWPOS*>(M->lParam);
			if (Wp && !(Wp->flags & SWP_NOSIZE) && Wp->cx > 8 && Wp->cy > 8)
			{
				g_MenuFrameBusy = true;
				int C = MENU_FRAME_CROP_PX;
				int B = MENU_FRAME_CROP_BOTTOM_PX > C ? MENU_FRAME_CROP_BOTTOM_PX : C;
				HRGN Rgn = CreateRectRgn(C, C, Wp->cx - C, Wp->cy - B);
				if (!SetWindowRgn(M->hwnd, Rgn, FALSE))
					DeleteObject(Rgn);
				if (M->hwnd != g_MenuFrameLastWnd)
				{
					g_MenuFrameLastWnd = M->hwnd;
					RemoveWin11MenuBorder(M->hwnd);
				}
				g_MenuFrameBusy = false;
			}
		}
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}
//---------------------------------------------------------------------------
// Create dock controls. Build these AFTER the initial docking layout is established; creating
// child HWNDs before JvDocking reparents/recreates the dock form can leave controls invisible
// until the user floats the window. The custom TPanel buttons also match the dark UI theme.
class TCreateDockControls : public TPanel
{
private:
    TViewport3D *FView;
    TLabel *FTitle;
    TLabel *FHint;
    TLabel *FStatus;
    std::vector<TPanel*> FButtons;
    TPanel *FHotButton;
    bool FDark;

    void __fastcall LayoutControls(TObject *Sender)
    {
        int W = ClientWidth;
        if (W < 180) W = 180;
        int ColW = (W - 30) / 2;
        if (ColW < 70) ColW = 70;
        if (FTitle) FTitle->SetBounds(10, 8, W - 20, 24);
        if (FButtons.size() >= 2)
        {
            FButtons[0]->SetBounds(10, 38, ColW, 38);
            FButtons[1]->SetBounds(20 + ColW, 38, ColW, 38);
        }
        if (FHint) FHint->SetBounds(10, 84, W - 20, 44);
        if (FStatus) FStatus->SetBounds(10, 132, W - 20, 42);
    }

    void __fastcall UpdateButtonColors()
    {
        const bool Dark = FDark;
        for (size_t I = 0; I < FButtons.size(); ++I)
        {
            TPanel *B = FButtons[I];
            if (!B) continue;
            B->StyleElements = TStyleElements();
            B->Color = (B == FHotButton)
                ? (Dark ? Rgb(48, 78, 105) : Rgb(215, 232, 250))
                : (Dark ? Rgb(48, 48, 48) : Rgb(242, 242, 242));
            B->Font->Color = Dark ? clWhite : clBlack;
        }
    }

    void __fastcall AddButton(const String &Caption, int Action)
    {
        TPanel *B = new TPanel(this);
        B->Parent = this;
        B->Caption = Caption;
        B->Tag = Action;
        B->BevelOuter = bvNone;
        B->Alignment = taCenter;
        B->Cursor = crHandPoint;
        B->Font->Style = TFontStyles() << fsBold;
        B->OnClick = ButtonClick;
        B->OnMouseEnter = ButtonEnter;
        B->OnMouseLeave = ButtonLeave;
        B->Anchors = TAnchors() << akLeft << akTop << akRight;
        FButtons.push_back(B);
    }

    void __fastcall ButtonEnter(TObject *Sender)
    {
        FHotButton = dynamic_cast<TPanel*>(Sender);
        UpdateButtonColors();
    }

    void __fastcall ButtonLeave(TObject *Sender)
    {
        FHotButton = NULL;
        UpdateButtonColors();
    }

    void __fastcall ButtonClick(TObject *Sender)
    {
        if (!FView) return;
        TPanel *B = dynamic_cast<TPanel*>(Sender);
        if (!B) return;
        switch ((int)B->Tag)
        {
            case 0:
            {
                int Index = FView->CreateBoxObject();
                TSceneObj *Obj = FView->MutableObj(Index);
                if (FStatus)
                    FStatus->Caption = Obj ? (L"已创建：" + Obj->Name) : L"创建方体失败。";
                break;
            }
            case 1:
            {
                int Index = FView->CreatePlaneObject();
                TSceneObj *Obj = FView->MutableObj(Index);
                if (FStatus)
                    FStatus->Caption = Obj ? (L"已创建：" + Obj->Name) : L"创建平面失败。";
                break;
            }
        }
        FView->Invalidate();
        if (FView->CanFocus()) FView->SetFocus();
    }

public:
    __fastcall TCreateDockControls(TComponent *Owner, TWinControl *ParentWnd, TViewport3D *View)
        : TPanel(Owner), FView(View), FTitle(NULL), FHint(NULL), FStatus(NULL), FHotButton(NULL), FDark(true)
    {
        Parent = ParentWnd;
        Align = alClient;
        BevelOuter = bvNone;
        Caption = L"";
        DoubleBuffered = true;
        OnResize = LayoutControls;

        FTitle = new TLabel(this);
        FTitle->Parent = this;
        FTitle->Caption = L"标准基本体";
        FTitle->Font->Style = TFontStyles() << fsBold;
        FTitle->Transparent = true;
        FTitle->Anchors = TAnchors() << akLeft << akTop << akRight;

        AddButton(L"+  方体", 0);
        AddButton(L"+  平面", 1);

        FHint = new TLabel(this);
        FHint->Parent = this;
        FHint->AutoSize = false;
        FHint->WordWrap = true;
        FHint->Transparent = true;
        FHint->Caption = L"Q 选择   W 移动   E 旋转   R 缩放\nCtrl+C 复制   Ctrl+V 粘贴   Delete 删除";
        FHint->Anchors = TAnchors() << akLeft << akTop << akRight;

        FStatus = new TLabel(this);
        FStatus->Parent = this;
        FStatus->AutoSize = false;
        FStatus->WordWrap = true;
        FStatus->Transparent = true;
        FStatus->Caption = L"选中物体后，可在“属性”中修改位置和长宽高。";
        FStatus->Anchors = TAnchors() << akLeft << akTop << akRight;

        LayoutControls(NULL);
    }

    void __fastcall ApplyTheme(bool ADark)
    {
        FDark = ADark;
        StyleElements = TStyleElements();
        Color = ADark ? Rgb(DARK_BAR_R, DARK_BAR_G, DARK_BAR_B) : clWhite;
        Font->Color = ADark ? clWhite : clBlack;
        if (FTitle)
        {
            FTitle->StyleElements = TStyleElements();
            FTitle->Font->Color = ADark ? clWhite : clBlack;
        }
        if (FHint)
        {
            FHint->StyleElements = TStyleElements();
            FHint->Font->Color = ADark ? Rgb(185, 185, 185) : Rgb(90, 90, 90);
        }
        if (FStatus)
        {
            FStatus->StyleElements = TStyleElements();
            FStatus->Font->Color = ADark ? Rgb(185, 205, 225) : Rgb(70, 90, 110);
        }
        UpdateButtonColors();
        Invalidate();
    }
};

static TCreateDockControls *g_CreateDockControls = NULL;
static void ApplyCreateDockControlsTheme(bool ADark)
{
    if (g_CreateDockControls)
        g_CreateDockControls->ApplyTheme(ADark);
}

static void InitCreateDockControls(TForm *DockForm, TViewport3D *View)
{
    if (!DockForm || !View || g_CreateDockControls)
        return;
    // Owner/Parent are the dock form, but this is called only after LayoutDockForms().
    g_CreateDockControls = new TCreateDockControls(DockForm, DockForm, View);
    DockForm->Realign();
    g_CreateDockControls->ApplyTheme(true);
    g_CreateDockControls->BringToFront();
    g_CreateDockControls->Visible = true;
    DockForm->Invalidate();
}

// Editable Properties grid: edits are committed directly to the active viewport object.
class TScenePropertyEditor : public TValueListEditor
{
private:
    TViewport3D *FView;

    String __fastcall CurrentValue(TSceneObj *O, const String &Key)
    {
        if (!O) return String();
        if (Key == L"Name") return O->Name;
        if (Key == L"Type") return O->Kind;
        if (Key == L"Position X") return FloatToStrF((double)O->Pos.x, ffFixed, 7, 2);
        if (Key == L"Position Y") return FloatToStrF((double)O->Pos.y, ffFixed, 7, 2);
        if (Key == L"Position Z") return FloatToStrF((double)O->Pos.z, ffFixed, 7, 2);
        if (Key == L"Rotation X") return FloatToStrF((double)(O->Rot.x * 57.2957795131), ffFixed, 7, 2);
        if (Key == L"Rotation Y") return FloatToStrF((double)(O->Rot.y * 57.2957795131), ffFixed, 7, 2);
        if (Key == L"Rotation Z") return FloatToStrF((double)(O->Rot.z * 57.2957795131), ffFixed, 7, 2);
        if (Key == L"Length (X)") return FloatToStrF((double)(O->Half.x * 2.0f), ffFixed, 7, 2);
        if (Key == L"Width (Y)")  return FloatToStrF((double)(O->Half.y * 2.0f), ffFixed, 7, 2);
        if (Key == L"Height (Z)") return FloatToStrF((double)(O->Half.z * 2.0f), ffFixed, 7, 2);
        if (Key == L"Color R") return IntToStr(O->R);
        if (Key == L"Color G") return IntToStr(O->G);
        if (Key == L"Color B") return IntToStr(O->B);
        return String();
    }

    void __fastcall PropertyValidate(TObject *Sender, int ACol, int ARow, const String KeyName, const String KeyValue)
    {
        if (!FView || ACol != 1 || ARow < 1)
            return;
        TSceneObj *O = FView->MutableObj(FView->Selected);
        if (!O)
            return;

        String Key = KeyName;
        const String &Value = KeyValue;
        if (Key == L"Name")
        {
            String NewName = Value.Trim();
            if (!NewName.IsEmpty())
                O->Name = NewName;
            else
                Cells[1][ARow] = O->Name;
            return;
        }

        if (Key == L"Color R" || Key == L"Color G" || Key == L"Color B")
        {
            int C = 0;
            if (!TryStrToInt(Value, C) || C < 0 || C > 255)
            {
                Cells[1][ARow] = CurrentValue(O, Key);
                return;
            }
            if (Key == L"Color R") O->R = C;
            else if (Key == L"Color G") O->G = C;
            else O->B = C;
            return;
        }

        // Type is informational for now; changing it would not change the mesh geometry.
        if (Key != L"Position X" && Key != L"Position Y" && Key != L"Position Z" &&
            Key != L"Rotation X" && Key != L"Rotation Y" && Key != L"Rotation Z" &&
            Key != L"Length (X)" && Key != L"Width (Y)" && Key != L"Height (Z)")
        {
            Cells[1][ARow] = CurrentValue(O, Key);
            return;
        }

        double D = 0.0;
        if (!TryStrToFloat(Value, D) || D != D || D > 1000000.0 || D < -1000000.0)
        {
            Cells[1][ARow] = CurrentValue(O, Key);
            return;
        }
        if (Key == L"Rotation X" || Key == L"Rotation Y" || Key == L"Rotation Z")
        {
            const float Rad = (float)(D * 0.0174532925199433);
            if (Key == L"Rotation X") O->Rot.x = Rad;
            else if (Key == L"Rotation Y") O->Rot.y = Rad;
            else O->Rot.z = Rad;
        }
        else if (Key == L"Length (X)" || Key == L"Width (Y)" || Key == L"Height (Z)")
        {
            if (D <= 0.001)
            {
                Cells[1][ARow] = CurrentValue(O, Key);
                return;
            }
            if (Key == L"Length (X)") O->Half.x = (float)D * 0.5f;
            else if (Key == L"Width (Y)") O->Half.y = (float)D * 0.5f;
            else O->Half.z = (float)D * 0.5f;
        }
        else
        {
            if (Key == L"Position X") O->Pos.x = (float)D;
            else if (Key == L"Position Y") O->Pos.y = (float)D;
            else O->Pos.z = (float)D;
        }
    }

public:
    __fastcall TScenePropertyEditor(TComponent *Owner, TViewport3D *View)
        : TValueListEditor(Owner), FView(View)
    {
        OnValidate = PropertyValidate;
    }
};
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
__fastcall TForm1::TForm1(TComponent* Owner)
	: TForm(Owner), FDark(false), FTreeMenu(NULL), FBarMenu(NULL), FmiShowThemeBar(NULL), FmiShowToolBar1(NULL), FmiShowToolBar2(NULL), FmiShowToolBar3(NULL), FmiShowToolBar4(NULL), FAllowRename(false), FDockServer(NULL), FDockStyle(NULL), FExploreForm(NULL), FCreateForm(NULL), FPropsForm(NULL), FOutputForm(NULL), FDockLaidOut(false), FLayoutLocked(false), FSdl(NULL), FSdl2(NULL), FThemeImages(NULL), FToolBarImages(NULL), FProps(NULL),
	  FSnapHoldTimer(NULL), FSnapLongPress(false), FViewMode(1), FSnapForm(NULL)
{
}
//---------------------------------------------------------------------------
__fastcall TForm1::~TForm1()
{
	g_CreateDockControls = NULL;	// the dock form owns and destroys the control panel
	if (g_MenuFrameHook)
	{
		UnhookWindowsHookEx(g_MenuFrameHook);
		g_MenuFrameHook = NULL;
	}
	if (g_MenuCbtHook)
	{
		UnhookWindowsHookEx(g_MenuCbtHook);
		g_MenuCbtHook = NULL;
	}
	if (g_MenuPreHook)
	{
		UnhookWindowsHookEx(g_MenuPreHook);
		g_MenuPreHook = NULL;
	}
	g_MenuFrameActive = false;
}
//---------------------------------------------------------------------------
// Two copies of the .vsf style: they are identical except for the popup-menu frame bitmap, which is
// filled with the dark (43,43,43) or the light (252,252,252) menu background colour.
// The frame bitmap can only have one colour, so the style is switched together with the theme.
// Put YuPengDark.vsf and YuPengLight.vsf next to the .exe (or in the current directory).
static const wchar_t *STYLE_DARK_NAME  = L"YuPengDark";
static const wchar_t *STYLE_LIGHT_NAME = L"YuPengLight";

static bool IsStyleRegistered(const String &AName)
{
	DynamicArray<String> Names = TStyleManager::StyleNames;
	for (int i = 0; i < Names.Length; i++)
		if (SameText(Names[i], AName))
			return true;
	return false;
}
//---------------------------------------------------------------------------
static void EnsureStyleLoaded(const String &AName, const String &AFile)
{
	if (IsStyleRegistered(AName))
		return;
	String Candidates[2] = { ExtractFilePath(ParamStr(0)) + AFile, GetCurrentDir() + "\\" + AFile };
	for (int i = 0; i < 2; i++)
	{
		if (!FileExists(Candidates[i]))
			continue;
		try
		{
			TStyleManager::LoadFromFile(Candidates[i]);
		}
		catch (Exception &)
		{
			// invalid style file: ignore, the normal frame handling below still applies
		}
		return;
	}
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// File system tree (TreeView1 in the "Explore" dock panel)
//---------------------------------------------------------------------------
// Every node carries its full path (Node->Data); the info is freed in OnDeletion.
struct TFileNodeInfo
{
	String Path;
	bool   IsDir;
};
static TFileNodeInfo *NodeInfo(TTreeNode *Node)
{
	return Node ? static_cast<TFileNodeInfo*>(Node->Data) : NULL;
}
//---------------------------------------------------------------------------
// Index of the system icon (Explorer icon) for a path / extension in the system image list.
// With UseAttr = true the disk is not touched: the icon is derived from the attributes / extension only.
static int SysIconIndex(const String &Path, DWORD Attr, bool Open, bool UseAttr)
{
	SHFILEINFOW fi;
	ZeroMemory(&fi, sizeof(fi));
	UINT Flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
	if (Open)
		Flags |= SHGFI_OPENICON;
	if (UseAttr)
		Flags |= SHGFI_USEFILEATTRIBUTES;
	SHGetFileInfoW(Path.c_str(), Attr, &fi, sizeof(fi), Flags);
	return fi.iIcon;
}
//---------------------------------------------------------------------------
static String GetDesktopPath()
{
	wchar_t Buf[MAX_PATH] = { 0 };
	if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, Buf)))
		return String(Buf);
	return String();
}
//---------------------------------------------------------------------------
static TTreeNode *AddFileNode(TTreeView *Tree, TTreeNode *Parent, const String &Text, const String &Path,
	bool IsDir, int Icon, int OpenIcon)
{
	TTreeNode *N = Parent ? Tree->Items->AddChild(Parent, Text) : Tree->Items->Add(NULL, Text);
	TFileNodeInfo *I = new TFileNodeInfo();
	I->Path  = Path;
	I->IsDir = IsDir;
	N->Data          = I;
	N->ImageIndex    = Icon;
	N->SelectedIndex = OpenIcon;
	N->HasChildren   = IsDir;		// shows the [+] without reading the folder yet (lazy loading)
	return N;
}
//---------------------------------------------------------------------------
// Read the content of the folder behind Node and add it as child nodes (folders first, then files).
static void LoadFileNodeChildren(TTreeView *Tree, TTreeNode *Node)
{
	static int FolderIcon = -1, FolderOpenIcon = -1;
	if (FolderIcon < 0)
	{
		FolderIcon     = SysIconIndex(L"folder", FILE_ATTRIBUTE_DIRECTORY, false, true);
		FolderOpenIcon = SysIconIndex(L"folder", FILE_ATTRIBUTE_DIRECTORY, true,  true);
	}

	TFileNodeInfo *Info = NodeInfo(Node);
	if (!Info)
		return;
	String Dir = IncludeTrailingPathDelimiter(Info->Path);
	std::unique_ptr<TStringList> Folders(new TStringList());
	std::unique_ptr<TStringList> Files(new TStringList());
	Folders->Sorted = true;
	Files->Sorted   = true;

	UINT OldMode = SetErrorMode(SEM_FAILCRITICALERRORS);	// no "insert a disk" dialogs
	TSearchRec SR;
	if (FindFirst(Dir + L"*", faAnyFile, SR) == 0)
	{
		do
		{
			if (SR.Name == L"." || SR.Name == L"..")
				continue;
			if (SR.Attr & (faHidden | faSysFile))		// like Explorer: hide hidden / system items
				continue;
			if (SR.Attr & faDirectory)
				Folders->Add(SR.Name);
			else
				Files->Add(SR.Name);
		}
		while (FindNext(SR) == 0);
		FindClose(SR);
	}
	SetErrorMode(OldMode);

	Tree->Items->BeginUpdate();
	for (int i = 0; i < Folders->Count; i++)
		AddFileNode(Tree, Node, Folders->Strings[i], Dir + Folders->Strings[i], true, FolderIcon, FolderOpenIcon);
	for (int i = 0; i < Files->Count; i++)
	{
		int Ic = SysIconIndex(Files->Strings[i], FILE_ATTRIBUTE_NORMAL, false, true);
		AddFileNode(Tree, Node, Files->Strings[i], Dir + Files->Strings[i], false, Ic, Ic);
	}
	if (Node->Count == 0)
		Node->HasChildren = false;		// empty or not accessible
	Tree->Items->EndUpdate();
}
//---------------------------------------------------------------------------
// Paths of all expanded sub nodes (so a refresh can open them again)
static void CollectExpanded(TTreeNode *Node, TStringList *List)
{
	for (int i = 0; i < Node->Count; i++)
	{
		TTreeNode *C = Node->Item[i];
		if (C->Expanded && NodeInfo(C))
		{
			List->Add(NodeInfo(C)->Path);
			CollectExpanded(C, List);
		}
	}
}
//---------------------------------------------------------------------------
static void ExpandSaved(TTreeNode *Node, TStringList *List)
{
	for (int i = 0; i < Node->Count; i++)
	{
		TTreeNode *C = Node->Item[i];
		TFileNodeInfo *I = NodeInfo(C);
		if (I && I->IsDir && List->IndexOf(I->Path) >= 0)
		{
			C->Expand(false);		// OnExpanding reads the folder
			ExpandSaved(C, List);
		}
	}
}
//---------------------------------------------------------------------------
static TTreeNode *FindUnder(TTreeNode *Node, const String &Path)
{
	for (int i = 0; i < Node->Count; i++)
	{
		TTreeNode *C = Node->Item[i];
		TFileNodeInfo *I = NodeInfo(C);
		if (I && SameText(I->Path, Path))
			return C;
		TTreeNode *R = FindUnder(C, Path);
		if (R)
			return R;
	}
	return NULL;
}
//---------------------------------------------------------------------------
// Re-read a folder node from disk; expanded sub folders and the selection are kept.
static void ReloadNode(TTreeView *Tree, TTreeNode *Node)
{
	TFileNodeInfo *I = NodeInfo(Node);
	if (!I || !I->IsDir)
		return;

	std::unique_ptr<TStringList> Exp(new TStringList());
	CollectExpanded(Node, Exp.get());
	bool WasExpanded = Node->Expanded;

	bool SelInside = false;
	String SelPath;
	for (TTreeNode *P = Tree->Selected; P != NULL; P = P->Parent)
		if (P == Node)
		{
			SelInside = true;
			break;
		}
	if (SelInside && Tree->Selected != Node && NodeInfo(Tree->Selected))
		SelPath = NodeInfo(Tree->Selected)->Path;

	Tree->Items->BeginUpdate();
	Node->DeleteChildren();
	Node->HasChildren = true;
	if (WasExpanded)
	{
		LoadFileNodeChildren(Tree, Node);
		Node->Expand(false);
		ExpandSaved(Node, Exp.get());
	}
	if (SelInside)
	{
		TTreeNode *S = SelPath.IsEmpty() ? NULL : FindUnder(Node, SelPath);
		(S ? S : Node)->Selected = true;
	}
	Tree->Items->EndUpdate();
}
//---------------------------------------------------------------------------
// Refresh every node that shows the given folder (the same folder can appear twice, e.g. the desktop)
static void RefreshPath(TTreeView *Tree, const String &Path)
{
	String Key = ExcludeTrailingPathDelimiter(Path);
	std::vector<TTreeNode*> Hits;
	for (int i = 0; i < Tree->Items->Count; i++)
	{
		TTreeNode *N = Tree->Items->Item[i];
		TFileNodeInfo *I = NodeInfo(N);
		if (I && I->IsDir && SameText(ExcludeTrailingPathDelimiter(I->Path), Key))
			Hits.push_back(N);
	}
	for (size_t k = 0; k < Hits.size(); k++)
	{
		if (Hits[k]->Count > 0 || Hits[k]->Expanded)
			ReloadNode(Tree, Hits[k]);
		else
			Hits[k]->HasChildren = true;		// not read yet / was empty: read it when it is expanded
	}
}
//---------------------------------------------------------------------------
// ---- clipboard (same format as Windows Explorer: CF_HDROP + "Preferred DropEffect") ----
static const DWORD kEffectCopy = 1;		// DROPEFFECT_COPY
static const DWORD kEffectMove = 2;		// DROPEFFECT_MOVE

static bool PutFileOnClipboard(HWND hWnd, const String &Path, bool Cut)
{
	size_t Chars = static_cast<size_t>(Path.Length()) + 2;		// path + two terminating zeros
	HGLOBAL hDrop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + Chars * sizeof(wchar_t));
	HGLOBAL hEff  = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
	if (!hDrop || !hEff)
	{
		if (hDrop) GlobalFree(hDrop);
		if (hEff)  GlobalFree(hEff);
		return false;
	}
	DROPFILES *df = static_cast<DROPFILES*>(GlobalLock(hDrop));
	df->pFiles = sizeof(DROPFILES);
	df->fWide  = TRUE;
	memcpy(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES), Path.c_str(), Path.Length() * sizeof(wchar_t));
	GlobalUnlock(hDrop);

	DWORD *pe = static_cast<DWORD*>(GlobalLock(hEff));
	*pe = Cut ? kEffectMove : kEffectCopy;
	GlobalUnlock(hEff);

	if (!OpenClipboard(hWnd))
	{
		GlobalFree(hDrop);
		GlobalFree(hEff);
		return false;
	}
	EmptyClipboard();
	SetClipboardData(CF_HDROP, hDrop);
	SetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"), hEff);
	CloseClipboard();
	return true;
}
//---------------------------------------------------------------------------
static bool GetFilesFromClipboard(HWND hWnd, std::vector<String> &Files, bool &Cut)
{
	Files.clear();
	Cut = false;
	if (!IsClipboardFormatAvailable(CF_HDROP) || !OpenClipboard(hWnd))
		return false;

	HDROP hd = reinterpret_cast<HDROP>(GetClipboardData(CF_HDROP));
	if (hd)
	{
		UINT n = DragQueryFileW(hd, 0xFFFFFFFF, NULL, 0);
		for (UINT i = 0; i < n; i++)
		{
			UINT Len = DragQueryFileW(hd, i, NULL, 0);
			std::wstring Buf(Len + 1, L'\0');
			DragQueryFileW(hd, i, &Buf[0], Len + 1);
			Files.push_back(String(Buf.c_str()));
		}
	}
	UINT FmtEffect = RegisterClipboardFormatW(L"Preferred DropEffect");
	if (IsClipboardFormatAvailable(FmtEffect))
	{
		HANDLE he = GetClipboardData(FmtEffect);
		if (he)
		{
			DWORD *pe = static_cast<DWORD*>(GlobalLock(he));
			if (pe)
			{
				Cut = ((*pe & kEffectMove) != 0);
				GlobalUnlock(he);
			}
		}
	}
	CloseClipboard();
	return !Files.empty();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeView1Expanding(TObject *Sender, TTreeNode *Node, bool &AllowExpansion)
{
	if (Node->Count == 0)
		LoadFileNodeChildren(TreeView1, Node);
	AllowExpansion = (Node->Count > 0);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeView1Deletion(TObject *Sender, TTreeNode *Node)
{
	delete static_cast<TFileNodeInfo*>(Node->Data);
	Node->Data = NULL;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeView1MouseDown(TObject *Sender, TMouseButton Button, TShiftState Shift, int X, int Y)
{
	// A right click selects the node under the mouse first, so the menu works on it
	if (Button == mbRight)
	{
		TTreeNode *N = TreeView1->GetNodeAt(X, Y);
		if (N)
			N->Selected = true;
	}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeView1KeyDown(TObject *Sender, Word &Key, TShiftState Shift)
{
	if (TreeView1->IsEditing())
		return;
	if (Shift.Contains(ssCtrl) && !Shift.Contains(ssAlt))
	{
		if      (Key == L'X') { TreeMenuCut(NULL);   Key = 0; }
		else if (Key == L'C') { TreeMenuCopy(NULL);  Key = 0; }
		else if (Key == L'V') { TreeMenuPaste(NULL); Key = 0; }
	}
	else if (Key == VK_F2)     { TreeMenuRename(NULL);  Key = 0; }
	else if (Key == VK_DELETE) { TreeMenuDelete(NULL);  Key = 0; }
	else if (Key == VK_F5) { TreeMenuRefresh(NULL); Key = 0; }
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeView1Editing(TObject *Sender, TTreeNode *Node, bool &AllowEdit)
{
	AllowEdit = FAllowRename && Node->Parent != NULL;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeView1Edited(TObject *Sender, TTreeNode *Node, UnicodeString &S)
{
	TFileNodeInfo *I = NodeInfo(Node);
	if (!I)
		return;

	String OldName = ExtractFileName(ExcludeTrailingPathDelimiter(I->Path));
	String NewName = Trim(S);
	if (NewName.IsEmpty() || SameStr(NewName, OldName))
	{
		S = OldName;
		return;
	}
	if (NewName.LastDelimiter(L"\\/:*?\"<>|") > 0)
	{
		Application->MessageBox(L"\u540D\u79F0\u4E0D\u80FD\u5305\u542B\u4E0B\u5217\u5B57\u7B26\uFF1A \\ / : * ? \" < > |", L"\u91CD\u547D\u540D", MB_ICONWARNING | MB_OK);
		S = OldName;
		return;
	}

	String NewPath = IncludeTrailingPathDelimiter(ExtractFilePath(ExcludeTrailingPathDelimiter(I->Path))) + NewName;
	if (!RenameFile(I->Path, NewPath))
	{
		Application->MessageBox(L"\u91CD\u547D\u540D\u5931\u8D25\u3002\u6587\u4EF6\u53EF\u80FD\u6B63\u5728\u4F7F\u7528\uFF0C\u6216\u6CA1\u6709\u6743\u9650\u3002", L"\u91CD\u547D\u540D", MB_ICONWARNING | MB_OK);
		S = OldName;
		return;
	}

	I->Path = NewPath;
	S = NewName;
	if (I->IsDir)
	{
		// the paths of the already loaded sub nodes are stale: read them again
		bool Was = Node->Expanded;
		Node->DeleteChildren();
		Node->HasChildren = true;
		if (Was)
			Node->Expand(false);
	}
	else
	{
		Node->ImageIndex = SysIconIndex(NewName, FILE_ATTRIBUTE_NORMAL, false, true);	// the extension may have changed
		Node->SelectedIndex = Node->ImageIndex;
	}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeMenuPopup(TObject *Sender)
{
	TTreeNode *N = TreeView1->Selected;
	bool HasNode = (NodeInfo(N) != NULL);
	bool Editable = HasNode && N->Parent != NULL;		// not the desktop / drive root nodes

	FmiNewFile->Enabled = HasNode;
	FmiCut->Enabled     = Editable;
	FmiCopy->Enabled    = Editable;
	FmiRename->Enabled  = Editable;
	FmiDelete->Enabled  = Editable;
	FmiPaste->Enabled   = HasNode && IsClipboardFormatAvailable(CF_HDROP);
	FmiRefresh->Enabled = HasNode;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeMenuCut(TObject *Sender)
{
	TTreeNode *N = TreeView1->Selected;
	if (NodeInfo(N) && N->Parent != NULL)
		PutFileOnClipboard(Handle, NodeInfo(N)->Path, true);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeMenuCopy(TObject *Sender)
{
	TTreeNode *N = TreeView1->Selected;
	if (NodeInfo(N) && N->Parent != NULL)
		PutFileOnClipboard(Handle, NodeInfo(N)->Path, false);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeMenuPaste(TObject *Sender)
{
	TTreeNode *N = TreeView1->Selected;
	TFileNodeInfo *I = NodeInfo(N);
	if (!I)
		return;

	// Target folder: the selected folder, or the folder of the selected file
	String Target = I->IsDir ? I->Path : ExtractFilePath(I->Path);
	Target = IncludeTrailingPathDelimiter(Target);

	std::vector<String> Files;
	bool Cut = false;
	if (!GetFilesFromClipboard(Handle, Files, Cut))
		return;

	// Build the double-zero terminated lists for SHFileOperation
	std::wstring From;
	bool SameFolder = true;
	for (size_t k = 0; k < Files.size(); k++)
	{
		From += std::wstring(Files[k].c_str());
		From.push_back(L'\0');
		if (!SameText(IncludeTrailingPathDelimiter(ExtractFilePath(ExcludeTrailingPathDelimiter(Files[k]))), Target))
			SameFolder = false;
	}
	From.push_back(L'\0');
	if (Cut && SameFolder)
		return;		// moving into the folder it already is in: nothing to do

	std::wstring To = std::wstring(Target.c_str());
	To.push_back(L'\0');
	To.push_back(L'\0');

	SHFILEOPSTRUCTW Op;
	ZeroMemory(&Op, sizeof(Op));
	Op.hwnd   = Handle;
	Op.wFunc  = Cut ? FO_MOVE : FO_COPY;
	Op.pFrom  = From.c_str();
	Op.pTo    = To.c_str();
	Op.fFlags = FOF_ALLOWUNDO;
	if (!Cut && SameFolder)
		Op.fFlags |= FOF_RENAMEONCOLLISION;		// copy into the same folder -> "name - Copy"
	int rc = SHFileOperationW(&Op);

	// A finished "cut" is used up (like in Explorer)
	if (Cut && rc == 0 && !Op.fAnyOperationsAborted && OpenClipboard(Handle))
	{
		EmptyClipboard();
		CloseClipboard();
	}

	// Show the result: target folder first (and open it), then the folders the files were moved out of
	RefreshPath(TreeView1, Target);
	N = TreeView1->Selected;
	if (N && NodeInfo(N) && NodeInfo(N)->IsDir && N->Count == 0)
		N->Expand(false);
	if (Cut)
		for (size_t k = 0; k < Files.size(); k++)
			RefreshPath(TreeView1, ExtractFilePath(ExcludeTrailingPathDelimiter(Files[k])));
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeMenuRename(TObject *Sender)
{
	TTreeNode *N = TreeView1->Selected;
	if (!NodeInfo(N) || N->Parent == NULL)
		return;
	TreeView1->SetFocus();
	FAllowRename = true;
	N->EditText();
	FAllowRename = false;
}
//---------------------------------------------------------------------------
// "Folder\Name.ext", or "Folder\Name (2).ext", ... - the first one that does not exist yet
static String UniqueFilePath(const String &Dir, const String &Base, const String &Ext)
{
	String P = Dir + Base + Ext;
	for (int i = 2; FileExists(P) || DirectoryExists(P); i++)
		P = Dir + Base + L" (" + IntToStr(i) + L")" + Ext;
	return P;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeMenuNewFile(TObject *Sender)
{
	TTreeNode *N = TreeView1->Selected;
	TFileNodeInfo *I = NodeInfo(N);
	if (!I)
		return;

	// Folder to create the file in: the selected folder, or the folder of the selected file
	String Dir = IncludeTrailingPathDelimiter(I->IsDir ? I->Path : ExtractFilePath(I->Path));
	String NewPath = UniqueFilePath(Dir, L"\u65B0\u5EFA\u6587\u4EF6", L".txt");

	HANDLE hFile = CreateFileW(NewPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		Application->MessageBox(L"\u65E0\u6CD5\u521B\u5EFA\u6587\u4EF6\uFF0C\u53EF\u80FD\u6CA1\u6709\u6743\u9650\u3002", L"\u65B0\u5EFA\u6587\u4EF6", MB_ICONWARNING | MB_OK);
		return;
	}
	CloseHandle(hFile);

	// Show the new file: refresh the folder, open it, select the new node and let the user type its name
	RefreshPath(TreeView1, Dir);
	TTreeNode *Folder = TreeView1->Selected;
	if (Folder && NodeInfo(Folder) && !NodeInfo(Folder)->IsDir)
		Folder = Folder->Parent;
	if (!Folder)
		return;
	if (Folder->Count == 0)
		Folder->Expand(false);		// OnExpanding reads the folder
	TTreeNode *NewNode = FindUnder(Folder, NewPath);
	if (NewNode)
	{
		NewNode->Selected = true;
		NewNode->MakeVisible();
		TreeMenuRename(NULL);
	}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeMenuDelete(TObject *Sender)
{
	TTreeNode *N = TreeView1->Selected;
	TFileNodeInfo *I = NodeInfo(N);
	if (!I || N->Parent == NULL)		// the desktop / drive root nodes cannot be deleted
		return;

	String Path   = I->Path;
	String Parent = ExtractFilePath(ExcludeTrailingPathDelimiter(Path));

	// Windows asks for confirmation and moves the file / folder to the recycle bin
	std::wstring From = std::wstring(Path.c_str());
	From.push_back(L'\0');
	From.push_back(L'\0');

	SHFILEOPSTRUCTW Op;
	ZeroMemory(&Op, sizeof(Op));
	Op.hwnd   = Handle;
	Op.wFunc  = FO_DELETE;
	Op.pFrom  = From.c_str();
	Op.fFlags = FOF_ALLOWUNDO;
	SHFileOperationW(&Op);

	RefreshPath(TreeView1, Parent);		// removes the node if it is really gone
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TreeMenuRefresh(TObject *Sender)
{
	TTreeNode *N = TreeView1->Selected;
	if (!NodeInfo(N))
		return;
	if (!NodeInfo(N)->IsDir)
		N = N->Parent;		// a file: refresh the folder it is in
	if (N)
		ReloadNode(TreeView1, N);
}
//---------------------------------------------------------------------------
TMenuItem* __fastcall TForm1::AddTreeMenuItem(const String &ACaption, TNotifyEvent AHandler)
{
	TMenuItem *M = new TMenuItem(FTreeMenu);
	M->Caption = ACaption;
	if (AHandler)
		M->OnClick = AHandler;
	FTreeMenu->Items->Add(M);
	return M;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::InitFileTree()
{
	// Use the Windows system icon list (same icons as Explorer)
	SHFILEINFOW fi;
	ZeroMemory(&fi, sizeof(fi));
	HIMAGELIST hSys = reinterpret_cast<HIMAGELIST>(
		SHGetFileInfoW(L"C:\\", 0, &fi, sizeof(fi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON));
	if (hSys)
	{
		TImageList *IL = new TImageList(this);
		IL->ShareImages = true;		// the list belongs to Windows, the TImageList must not destroy it
		IL->Handle = reinterpret_cast<NativeUInt>(hSys);
		TreeView1->Images = IL;
	}

	TreeView1->ReadOnly      = false;		// needed for rename; OnEditing only allows it from the menu / F2
	TreeView1->HideSelection = false;
	TreeView1->ShowLines     = false;
	TreeView1->BorderStyle   = bsNone;
	TreeView1->OnExpanding   = TreeView1Expanding;
	TreeView1->OnDeletion    = TreeView1Deletion;
	TreeView1->OnMouseDown   = TreeView1MouseDown;
	TreeView1->OnKeyDown     = TreeView1KeyDown;
	TreeView1->OnEditing     = TreeView1Editing;
	TreeView1->OnEdited      = TreeView1Edited;

	// Right-click menu (owner-drawn like the main menus, so it follows the theme)
	FTreeMenu = new TPopupMenu(this);
	FTreeMenu->OwnerDraw = true;
	FTreeMenu->OnPopup   = TreeMenuPopup;
	FmiNewFile = AddTreeMenuItem(L"\u65B0\u5EFA\u6587\u4EF6",     TreeMenuNewFile);
	AddTreeMenuItem(L"-", NULL);
	FmiCut     = AddTreeMenuItem(L"\u526A\u5207",     TreeMenuCut);
	FmiCopy    = AddTreeMenuItem(L"\u590D\u5236",    TreeMenuCopy);
	FmiPaste   = AddTreeMenuItem(L"\u7C98\u8D34",   TreeMenuPaste);
	AddTreeMenuItem(L"-", NULL);
	FmiRename  = AddTreeMenuItem(L"\u91CD\u547D\u540D",  TreeMenuRename);
	FmiDelete  = AddTreeMenuItem(L"\u5220\u9664",  TreeMenuDelete);
	AddTreeMenuItem(L"-", NULL);
	FmiRefresh = AddTreeMenuItem(L"\u5237\u65B0", TreeMenuRefresh);
	HookMenuItems(FTreeMenu->Items);
	TreeView1->PopupMenu = FTreeMenu;

	// Root nodes: the desktop and one node per drive; the content is read when a node is expanded
	UINT OldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
	TreeView1->Items->BeginUpdate();
	TreeView1->Items->Clear();

	String Desktop = GetDesktopPath();
	if (!Desktop.IsEmpty())
	{
		int Ic = SysIconIndex(Desktop, 0, false, false);
		AddFileNode(TreeView1, NULL, L"\u684C\u9762", Desktop, true, Ic, Ic);
	}

	DWORD Mask = GetLogicalDrives();
	for (int i = 0; i < 26; i++)
	{
		if (!(Mask & (1u << i)))
			continue;
		String Root = String(static_cast<wchar_t>(L'A' + i)) + L":\\";
		int Ic = SysIconIndex(Root, 0, false, false);
		AddFileNode(TreeView1, NULL, Root, Root, true, Ic, Ic);
	}
	TreeView1->Items->EndUpdate();
	SetErrorMode(OldMode);
}
//---------------------------------------------------------------------------
// Dark / light scroll bars, selection and expand arrows of a native control
typedef HRESULT (WINAPI *TSetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
static void SetControlTheme(TWinControl *W, bool ADark)
{
	static TSetWindowThemeFn pSet = NULL;
	static bool Tried = false;
	if (!Tried)
	{
		Tried = true;
		HMODULE h = LoadLibraryW(L"uxtheme.dll");
		if (h)
			pSet = reinterpret_cast<TSetWindowThemeFn>(GetProcAddress(h, "SetWindowTheme"));
	}
	if (pSet)
		pSet(W->Handle, ADark ? L"DarkMode_Explorer" : L"Explorer", NULL);
}
//---------------------------------------------------------------------------
// Command window follows the Dark / Light theme (same colours as the property panel)
static void ApplyCommandLineTheme(bool ADark)
{
	g_CommandDark = ADark;
	if (!g_CommandLineControlsCreated)
		return;
	TColor Bg = ADark ? Rgb(DARK_BAR_R, DARK_BAR_G, DARK_BAR_B) : clWhite;
	TColor Fg = ADark ? clWhite : clBlack;
	if (g_CommandOutput)
	{
		g_CommandOutput->StyleElements = TStyleElements();
		g_CommandOutput->Color = Bg;
		g_CommandOutput->Font->Color = Fg;
		SetControlTheme(g_CommandOutput, ADark);
		g_CommandOutput->Invalidate();
	}
	if (g_CommandPanel)
	{
		g_CommandPanel->StyleElements = TStyleElements();
		g_CommandPanel->ParentBackground = false;
		g_CommandPanel->Color = Bg;
	}
	if (g_CommandPrompt)
	{
		g_CommandPrompt->StyleElements = TStyleElements();
		g_CommandPrompt->Font->Color = Fg;
	}
	if (g_CommandEdit)
	{
		g_CommandEdit->StyleElements = TStyleElements();
		g_CommandEdit->Color = Bg;
		g_CommandEdit->Font->Color = Fg;
		g_CommandEdit->BorderStyle = bsNone;
	}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::AlignHelpToolBar()
{
	// Shrink the Help toolbar to exactly fit its button, then keep it flush against the right edge.
	if (tbrHelp->ButtonCount > 0)
		tbrHelp->Width = tbrHelp->Buttons[tbrHelp->ButtonCount - 1]->BoundsRect.Right;
	tbrHelp->Left = pnlMenuBar->ClientWidth - tbrHelp->Width;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::InitToolBarVisibilityMenu()
{
    // Right-click menu for the draggable bands inside cbrTheme.
    FBarMenu = new TPopupMenu(this);
    FBarMenu->OwnerDraw = true;
    FBarMenu->AutoHotkeys = maManual;

    FmiShowThemeBar = new TMenuItem(FBarMenu);
    FmiShowThemeBar->Caption = L"\u4E3B\u9898\u914D\u8272"; // 主题配色
    FmiShowThemeBar->Tag = 0;
    FmiShowThemeBar->OnClick = ToolBarVisibilityMenuClick;
    FBarMenu->Items->Add(FmiShowThemeBar);

    FmiShowToolBar1 = new TMenuItem(FBarMenu);
    FmiShowToolBar1->Caption = L"\u5DE5\u5177\u680F 1"; // 工具栏 1
    FmiShowToolBar1->Tag = 1;
    FmiShowToolBar1->OnClick = ToolBarVisibilityMenuClick;
    FBarMenu->Items->Add(FmiShowToolBar1);

    FmiShowToolBar2 = new TMenuItem(FBarMenu);
    FmiShowToolBar2->Caption = L"\u5DE5\u5177\u680F 2"; // 工具栏 2
    FmiShowToolBar2->Tag = 2;
    FmiShowToolBar2->OnClick = ToolBarVisibilityMenuClick;
    FBarMenu->Items->Add(FmiShowToolBar2);

    FmiShowToolBar3 = new TMenuItem(FBarMenu);
    FmiShowToolBar3->Caption = L"\u6A21\u5F0F (2D/3D)";
    FmiShowToolBar3->Tag = 3;
    FmiShowToolBar3->OnClick = ToolBarVisibilityMenuClick;
    FBarMenu->Items->Add(FmiShowToolBar3);

    FmiShowToolBar4 = new TMenuItem(FBarMenu);
    FmiShowToolBar4->Caption = L"\u6355\u6349";
    FmiShowToolBar4->Tag = 4;
    FmiShowToolBar4->OnClick = ToolBarVisibilityMenuClick;
    FBarMenu->Items->Add(FmiShowToolBar4);

    HookMenuItems(FBarMenu->Items);
    FBarMenu->OnPopup = ToolBarVisibilityMenuPopup;
    cbrTheme->OnContextPopup = ThemeBarContextPopup;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ThemeBarContextPopup(TObject *Sender, const TPoint &MousePos, bool &Handled)
{
    if (!FBarMenu || !cbrTheme)
        return;

    // TPopupMenu::Popup expects SCREEN coordinates. Use the actual cursor position
    // so the menu opens beside the right-click, not at (0, 0) or below the toolbar.
    // MousePos is not used here because its coordinate space can vary by VCL event path.
    TPoint P = Mouse->CursorPos;

    Handled = true;
    FBarMenu->Popup(P.x, P.y);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ToolBarVisibilityMenuPopup(TObject *Sender)
{
    // A check mark means the toolbar is currently visible.
    if (FmiShowThemeBar)  FmiShowThemeBar->Checked  = tbrTheme->Visible;
    if (FmiShowToolBar1) FmiShowToolBar1->Checked = ToolBar1->Visible;
    if (FmiShowToolBar2) FmiShowToolBar2->Checked = ToolBar2->Visible;
    if (FmiShowToolBar3) FmiShowToolBar3->Checked = ToolBar3->Visible;
    if (FmiShowToolBar4) FmiShowToolBar4->Checked = ToolBar4->Visible;
    ApplyMenuColors();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ToolBarVisibilityMenuClick(TObject *Sender)
{
    TMenuItem *Item = dynamic_cast<TMenuItem*>(Sender);
    if (!Item)
        return;

    // Tag is a small index, not a pointer (safe on 64-bit builds).
    switch (Item->Tag)
    {
        case 0: tbrTheme->Visible = !tbrTheme->Visible; break;
        case 1: ToolBar1->Visible = !ToolBar1->Visible; break;
        case 2: ToolBar2->Visible = !ToolBar2->Visible; break;
		case 3: ToolBar3->Visible = !ToolBar3->Visible; break;
        case 4: ToolBar4->Visible = !ToolBar4->Visible; break;
        default: return;
    }

    cbrTheme->Realign();
    cbrTheme->Invalidate();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ApplyTheme(bool ADark)
{
	FDark = ADark;

	// Switch to the style whose popup-menu frame has the colour of the active theme.
	// This must happen BEFORE the colours below are set (a style change can repaint everything).
	EnsureStyleLoaded(STYLE_DARK_NAME,  String(STYLE_DARK_NAME)  + ".vsf");
	EnsureStyleLoaded(STYLE_LIGHT_NAME, String(STYLE_LIGHT_NAME) + ".vsf");
	String WantedStyle = ADark ? String(STYLE_DARK_NAME) : String(STYLE_LIGHT_NAME);
	if (IsStyleRegistered(WantedStyle) && !SameText(TStyleManager::ActiveStyle->Name, WantedStyle))
		TStyleManager::TrySetStyle(WantedStyle, false);

	SetAppDarkMode(ADark);

	const TColor DarkBg = Rgb(DARK_BAR_R, DARK_BAR_G, DARK_BAR_B);	// neutral dark (file tree, property panel)
	const TColor BarBg  = ThemeBarColor(ADark);							// title bar / menu bar / theme bar

	// ---- Title bar (values for light mode are the ones from the DFM) ----
	if (ADark)
	{
		CustomTitleBar->BackgroundColor               = BarBg;
		CustomTitleBar->ForegroundColor               = clWhite;
		CustomTitleBar->InactiveBackgroundColor       = BarBg;
		CustomTitleBar->InactiveForegroundColor       = Rgb(153, 153, 153);
		CustomTitleBar->ButtonForegroundColor         = clWhite;
		CustomTitleBar->ButtonBackgroundColor         = BarBg;
		CustomTitleBar->ButtonHoverForegroundColor    = clWhite;
		CustomTitleBar->ButtonHoverBackgroundColor    = Rgb(63, 63, 63);
		CustomTitleBar->ButtonPressedForegroundColor  = clWhite;
		CustomTitleBar->ButtonPressedBackgroundColor  = Rgb(90, 90, 90);
		CustomTitleBar->ButtonInactiveForegroundColor = Rgb(128, 128, 128);
		CustomTitleBar->ButtonInactiveBackgroundColor = BarBg;
	}
	else
	{
		CustomTitleBar->BackgroundColor               = BarBg;
		CustomTitleBar->ForegroundColor               = static_cast<TColor>(65793);
		CustomTitleBar->InactiveBackgroundColor       = BarBg;
		CustomTitleBar->InactiveForegroundColor       = static_cast<TColor>(10066329);
		CustomTitleBar->ButtonForegroundColor         = clBlack;
		CustomTitleBar->ButtonBackgroundColor         = BarBg;
		CustomTitleBar->ButtonHoverForegroundColor    = clBlack;
		CustomTitleBar->ButtonHoverBackgroundColor    = clSilver;
		CustomTitleBar->ButtonPressedForegroundColor  = clBlack;
		CustomTitleBar->ButtonPressedBackgroundColor  = clMedGray;
		CustomTitleBar->ButtonInactiveForegroundColor = clGray;
		CustomTitleBar->ButtonInactiveBackgroundColor = BarBg;
	}

	TitleBarPanel1->CustomButtons->Items[0]->Hint = ADark ? "Light" : "Dark";

	// ---- Menu bar (panel + the two toolbars) ----
	if (ADark)
	{
		pnlMenuBar->ParentBackground = false;
		pnlMenuBar->Color = BarBg;
		tbrMenu->Color = BarBg;
		tbrHelp->Color = BarBg;
	}
	else
	{
		pnlMenuBar->ParentBackground = false;	// same as in the DFM
		pnlMenuBar->Color = BarBg;
		tbrMenu->ParentColor = true;
		tbrHelp->ParentColor = true;
	}

	// ---- Theme colour bar (below the menu bar) ----
	cbrTheme->ParentBackground = false;
	cbrTheme->Color = BarBg;
	tbrTheme->ParentColor = false;
	tbrTheme->Color = BarBg;
	ToolBar1->ParentColor = false;
	ToolBar1->Color = BarBg;
	ToolBar2->ParentColor = false;
	ToolBar2->Color = BarBg;
	ToolBar3->ParentColor = false;
	ToolBar3->Color = BarBg;
	ToolBar4->ParentColor = false;
	ToolBar4->Color = BarBg;
	// ---- File tree (Explore panel): background / text follow the theme, not the .vsf style ----
	TreeView1->StyleElements = TStyleElements();
	TreeView1->Color       = ADark ? DarkBg : clWhite;
	TreeView1->Font->Color = ADark ? clWhite : clBlack;
	SetControlTheme(TreeView1, ADark);
	TreeView1->Invalidate();

	// ---- Property panel (dock window FPropsForm) ----
	ApplyPropsTheme();
	ApplyCommandLineTheme(ADark);
	if (FSnapForm)
		ApplySnapFormTheme(FSnapForm, ADark);
	ApplyCreateDockControlsTheme(ADark);

	// ---- Dock windows, zones, title strips, tabs ----
	ApplyDockTheme();

	TitleBarPanel1->Invalidate();
	pnlMenuBar->Invalidate();
	tbrMenu->Invalidate();
	tbrHelp->Invalidate();
	cbrTheme->Invalidate();
	tbrTheme->Invalidate();
	ToolBar1->Invalidate();
	ToolBar2->Invalidate();
	ToolBar3->Invalidate();
	ToolBar4->Invalidate();
	Invalidate();

	ApplyMenuColors();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TitleBarPanel1CustomButtons0Click(TObject *Sender)
{
	ApplyTheme(!FDark);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ToolBarAdvancedCustomDraw(TToolBar *Sender, const TRect &ARect,
	TCustomDrawStage Stage, bool &DefaultDraw)
{
	// Paint the toolbar background (also the empty area to the right of the buttons)
	if ((FDark || g_ThemeIndex != 0) && Stage == cdPrePaint)
	{
		Sender->Canvas->Brush->Color = ThemeBarColor(FDark);
		Sender->Canvas->FillRect(ARect);
	}
	DefaultDraw = true;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ToolBarAdvancedCustomDrawButton(TToolBar *Sender, TToolButton *Button,
	TCustomDrawState State, TCustomDrawStage Stage, TTBCustomDrawFlags &Flags, bool &DefaultDraw)
{
	// Light mode: let Windows draw the buttons as before
	if (!FDark || Stage != cdPrePaint)
		return;

	TCanvas *C = Sender->Canvas;
	TRect R = Button->BoundsRect;

	TColor Bg = ThemeBarColor(true);
	if (State.Contains(cdsSelected) || State.Contains(cdsChecked))
		Bg = Rgb(90, 90, 90);		// pressed / drop-down menu open
	else if (State.Contains(cdsHot))
		Bg = Rgb(63, 63, 63);		// mouse over

	C->Brush->Color = Bg;
	C->FillRect(R);

	C->Font->Assign(Sender->Font);
	C->Font->Color = clWhite;
	C->Brush->Style = bsClear;
	String S = Button->Caption;
	DrawText(C->Handle, S.c_str(), -1, &R,
			 DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_HIDEPREFIX);
	C->Brush->Style = bsSolid;

	DefaultDraw = false;	// we drew the button ourselves
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// Toolbar icon helpers
//---------------------------------------------------------------------------
static const int TOOLBAR_ICON_SIZE = 16;		// icon size in the toolbars (was 24, too big)
static const int MODE_BTN_W        = 34;		// width of the text buttons "2D" / "3D" (ToolBar3)
//static const int TOOLBAR_RUN_ICON_SIZE = 24;	// icon size of ToolBar2 (Start / Run), bigger than the others
static const int TOOLBAR_DRAW_SIZE = 64;		// fallback icons are drawn at 64x64 (16 grid x 4) and scaled down

typedef void (*TIconDrawProc)(TCanvas *C);

// Look for  <name>.ico / <name>.png  in the "Image" folder: beside the EXE, two levels
// above (Win32\Debug layout) and in the current directory.
static String FindImageFile(const String &Name, const String &Ext)
{
	String ExeDir = IncludeTrailingPathDelimiter(ExtractFilePath(ParamStr(0)));
	String Cand[3] = {
		ExeDir + L"Image\\" + Name + Ext,
		ExpandFileName(ExeDir + L"..\\..\\Image\\" + Name + Ext),
		IncludeTrailingPathDelimiter(GetCurrentDir()) + L"Image\\" + Name + Ext
	};
	for (int i = 0; i < 3; i++)
		if (FileExists(Cand[i]))
			return Cand[i];
	return L"";
}
//---------------------------------------------------------------------------
// Adds one icon to the list. Order: .ico (real alpha, scaled by Windows) -> .png -> drawn fallback.
static void AddToolIcon(TImageList *List, const String &Name, TIconDrawProc Fallback)
{
	const int S = List->Width;		// every list has its own icon size
	try
	{
		String F = Name.IsEmpty() ? String(L"") : FindImageFile(Name, L".ico");	// empty name = drawn icon only
		if (F != L"")
		{
			HICON H = (HICON)LoadImageW(NULL, F.c_str(), IMAGE_ICON, S, S, LR_LOADFROMFILE);
			if (H)
			{
				std::unique_ptr<TIcon> Ico(new TIcon());
				Ico->Handle = H;		// TIcon owns the handle now
				List->AddIcon(Ico.get());
				return;
			}
		}
		F = Name.IsEmpty() ? String(L"") : FindImageFile(Name, L".png");
		if (F != L"")
		{
			std::unique_ptr<TPngImage> Png(new TPngImage());
			Png->LoadFromFile(F);
			std::unique_ptr<TBitmap> Bmp(new TBitmap());
			Bmp->PixelFormat = pf24bit;
			Bmp->SetSize(S, S);
			Bmp->Canvas->Brush->Color = clFuchsia;
			Bmp->Canvas->FillRect(TRect(0, 0, S, S));
			SetStretchBltMode(Bmp->Canvas->Handle, HALFTONE);
			Bmp->Canvas->StretchDraw(TRect(0, 0, S, S), Png.get());
			List->AddMasked(Bmp.get(), clFuchsia);
			return;
		}
	}
	catch (Exception &)
	{
		// fall through to the drawn icon
	}
	// draw large (supersampled), then scale down to the icon size
	const int D = TOOLBAR_DRAW_SIZE;
	std::unique_ptr<TBitmap> Big(new TBitmap());
	Big->PixelFormat = pf24bit;
	Big->SetSize(D, D);
	Big->Canvas->Brush->Color = clFuchsia;
	Big->Canvas->FillRect(TRect(0, 0, D, D));
	Fallback(Big->Canvas);
	std::unique_ptr<TBitmap> Bmp(new TBitmap());
	Bmp->PixelFormat = pf24bit;
	Bmp->SetSize(S, S);
	Bmp->Canvas->Brush->Color = clFuchsia;
	Bmp->Canvas->FillRect(TRect(0, 0, S, S));
	SetStretchBltMode(Bmp->Canvas->Handle, HALFTONE);
	Bmp->Canvas->StretchDraw(TRect(0, 0, S, S), Big.get());
	List->AddMasked(Bmp.get(), clFuchsia);
}
//---------------------------------------------------------------------------
// The drawn fallback icons are designed on a 16x16 grid in the flat "RAD Studio" style and are
// rendered 4x larger (K) and then scaled down to the icon size, which gives smooth edges.
static const int ICON_K = 4;
static inline TPoint IcoPt(int x, int y) { return TPoint(x * ICON_K, y * ICON_K); }
static inline void IconPen(TCanvas *C, TColor Pen, TColor Fill)
{
	C->Pen->Color   = Pen;
	C->Pen->Width   = ICON_K;			// 1 px in the final icon
	C->Brush->Color = Fill;
}
//---------------------------------------------------------------------------
static void DrawNewIcon(TCanvas *C)		// white page, folded corner, green "+"
{
	IconPen(C, Rgb(95, 105, 120), Rgb(255, 255, 255));
	TPoint P[5] = { IcoPt(3, 1), IcoPt(10, 1), IcoPt(13, 4), IcoPt(13, 15), IcoPt(3, 15) };
	C->Polygon(P, 4);
	IconPen(C, Rgb(95, 105, 120), Rgb(215, 222, 232));
	TPoint F[3] = { IcoPt(10, 1), IcoPt(10, 4), IcoPt(13, 4) };
	C->Polygon(F, 2);
	C->Pen->Color   = Rgb(40, 160, 70);		// plus sign
	C->Pen->Width   = ICON_K * 2 - 1;
	C->MoveTo(8 * ICON_K, 7 * ICON_K);  C->LineTo(8 * ICON_K, 12 * ICON_K + ICON_K);
	C->MoveTo(5 * ICON_K + ICON_K / 2, 10 * ICON_K);  C->LineTo(10 * ICON_K + ICON_K / 2, 10 * ICON_K);
}
//---------------------------------------------------------------------------
static void DrawOpenIcon(TCanvas *C)	// yellow folder
{
	IconPen(C, Rgb(190, 135, 30), Rgb(245, 195, 80));
	TPoint Back[6] = { IcoPt(1, 3), IcoPt(6, 3), IcoPt(7, 5), IcoPt(14, 5), IcoPt(14, 13), IcoPt(1, 13) };
	C->Polygon(Back, 5);
	IconPen(C, Rgb(190, 135, 30), Rgb(255, 222, 130));
	TPoint Front[4] = { IcoPt(3, 7), IcoPt(16, 7), IcoPt(13, 14), IcoPt(1, 14) };
	C->Polygon(Front, 3);
}
//---------------------------------------------------------------------------
static void DrawSaveIcon(TCanvas *C)	// blue floppy disk
{
	IconPen(C, Rgb(25, 75, 135), Rgb(45, 120, 200));
	C->Rectangle(2 * ICON_K, 2 * ICON_K, 14 * ICON_K, 14 * ICON_K);
	IconPen(C, Rgb(150, 160, 175), Rgb(245, 247, 250));		// shutter
	C->Rectangle(5 * ICON_K, 2 * ICON_K, 11 * ICON_K, 6 * ICON_K);
	IconPen(C, Rgb(25, 75, 135), Rgb(225, 230, 238));		// label
	C->Rectangle(4 * ICON_K, 9 * ICON_K, 12 * ICON_K, 14 * ICON_K);
}
//---------------------------------------------------------------------------
static void DrawStartIcon(TCanvas *C)	// Start button: drawn green triangle (no resource file)
{
	IconPen(C, Rgb(25, 125, 60), Rgb(60, 185, 100));
	TPoint P[3] = { IcoPt(3, 1), IcoPt(14, 8), IcoPt(3, 15) };
	C->Polygon(P, 2);
}
//---------------------------------------------------------------------------
static void DrawPlayFallback(TCanvas *C)	// only used when play96x96.ico/.png is missing
{
	IconPen(C, Rgb(25, 125, 60), Rgb(60, 185, 100));		// green "run" triangle
	TPoint P[3] = { IcoPt(3, 1), IcoPt(14, 8), IcoPt(3, 15) };
	C->Polygon(P, 2);
}
static void DrawSnapIcon(TCanvas *C)		// magnet: red U with light tips
{
	IconPen(C, Rgb(120, 30, 30), Rgb(210, 55, 55));
	TPoint U[10] = { IcoPt(2, 2), IcoPt(6, 2), IcoPt(6, 9), IcoPt(10, 9), IcoPt(10, 2),
					 IcoPt(14, 2), IcoPt(14, 10), IcoPt(11, 14), IcoPt(5, 14), IcoPt(2, 10) };
	C->Polygon(U, 9);
	IconPen(C, Rgb(120, 125, 135), Rgb(235, 238, 245));		// pole tips
	C->Rectangle(2 * ICON_K, 2 * ICON_K, 6 * ICON_K, 5 * ICON_K);
	C->Rectangle(10 * ICON_K, 2 * ICON_K, 14 * ICON_K, 5 * ICON_K);
}
//---------------------------------------------------------------------------
// ====================  TOOLBAR ICONS - CUSTOMIZE HERE  ====================
// Everything about the toolbar icons is in this block:
//   1. TOOLBAR_ICON_SIZE / TOOLBAR_DRAW_SIZE (top of this section)  -> size
//   2. DrawXxxIcon functions above                                   -> the drawn (fallback) look
//   3. TOOLBAR_ICONS below                                           -> which file / which drawing each icon uses
// An icon is taken from  Image\<File>.ico  or  Image\<File>.png  if that file exists (File = L"" -> never uses a file),
// otherwise the Draw function is used. To change an icon: edit its row (another file name
// and/or another Draw function). To add one: add an enum entry + a row, then use the enum
// as ImageIndex of a button.
//---------------------------------------------------------------------------
enum TToolIconId { ICON_NEW, ICON_OPEN, ICON_SAVE, ICON_START, ICON_RUN, ICON_SNAP, ICON_COUNT };

struct TToolIconDef
{
	const wchar_t *File;		// file name without extension, in the "Image" folder
	TIconDrawProc  Draw;		// used when no file is found
};
static const TToolIconDef TOOLBAR_ICONS[ICON_COUNT] =
{
	/* ICON_NEW   */ { L"new",        DrawNewIcon  },
	/* ICON_OPEN  */ { L"open",       DrawOpenIcon },
	/* ICON_SAVE  */ { L"save",       DrawSaveIcon },
	/* ICON_START */ { L"",           DrawStartIcon     },	// Start: empty file name = always the drawn green triangle
	/* ICON_RUN   */ { L"play96x96",  DrawPlayFallback  },	// Run button (unchanged)
	/* ICON_SNAP  */ { L"",           DrawSnapIcon      }	// Snap toggle (ToolBar4): drawn magnet
};
//---------------------------------------------------------------------------
void __fastcall TForm1::InitThemeBar()
{
	// Same rule as the menu bar: this row is coloured by our code, not by the .vsf style
	cbrTheme->StyleName = "Platform Default";
	// tbrTheme + ToolBar1..4 share the same setup (all of them sit in cbrTheme)
	TToolBar *const Bars[] = { tbrTheme, ToolBar1, ToolBar2, ToolBar3, ToolBar4 };
	for (int i = 0; i < 5; i++)
	{
		Bars[i]->StyleName     = "Platform Default";
		Bars[i]->StyleElements = TStyleElements() << seFont << seBorder;
		Bars[i]->ShowHint      = true;
		// paint the background with the active theme-bar colour
		Bars[i]->OnAdvancedCustomDraw = ThemeBarAdvancedCustomDraw;
	}
	cbrTheme->OnBandPaint  = ThemeBarBandPaint;		// dark / light gripper + band frame
	cbrTheme->StyleElements = TStyleElements() << seFont << seBorder;
	// Button images: one 16x16 icon per preset, drawn here so no resource file is needed.
	// To use your own pictures, fill FThemeImages from a bitmap / resource instead.
	FThemeImages = new TImageList(this);
	FThemeImages->Width  = 16;
	FThemeImages->Height = 16;
	for (int i = 0; i < THEME_PRESET_COUNT; i++)
	{
		std::unique_ptr<TBitmap> Bmp(new TBitmap());
		Bmp->PixelFormat = pf24bit;
		Bmp->Width  = 16;
		Bmp->Height = 16;
		Bmp->Canvas->Brush->Color = clFuchsia;			// transparent colour
		Bmp->Canvas->FillRect(TRect(0, 0, 16, 16));
		TColor Accent = static_cast<TColor>(THEME_PRESETS[i].Accent);
		Bmp->Canvas->Pen->Color   = MixColor(Accent, clBlack, 30);
		Bmp->Canvas->Brush->Color = Accent;
		Bmp->Canvas->RoundRect(1, 1, 15, 15, 5, 5);
		Bmp->Canvas->Pen->Color   = MixColor(Accent, clWhite, 45);	// small highlight
		Bmp->Canvas->MoveTo(4, 4);
		Bmp->Canvas->LineTo(8, 4);
		FThemeImages->AddMasked(Bmp.get(), clFuchsia);
	}

	// ---- Toolbar icons (TOOLBAR_ICON_SIZE, 32-bit alpha) ----
	// icon order = TToolIconId, definitions in TOOLBAR_ICONS (see "TOOLBAR ICONS - CUSTOMIZE HERE")
	FToolBarImages = new TImageList(this);
	FToolBarImages->ColorDepth = cd32Bit;
	FToolBarImages->Width  = TOOLBAR_ICON_SIZE;
	FToolBarImages->Height = TOOLBAR_ICON_SIZE;
	for (int i = 0; i < ICON_COUNT; i++)
		AddToolIcon(FToolBarImages, TOOLBAR_ICONS[i].File, TOOLBAR_ICONS[i].Draw);
	// ToolBar2 (Start / Run) gets its own, bigger list; same order, so the same ICON_xxx indexes work
	TImageList *RunImages = new TImageList(this);
	RunImages->ColorDepth = cd32Bit;
	RunImages->Width  = TOOLBAR_ICON_SIZE;
	RunImages->Height = TOOLBAR_ICON_SIZE;
	for (int i = 0; i < ICON_COUNT; i++)
		AddToolIcon(RunImages, TOOLBAR_ICONS[i].File, TOOLBAR_ICONS[i].Draw);

	ToolBar1->Images = FToolBarImages;
	ToolBar2->Images = RunImages;
	ToolBar4->Images = FToolBarImages;

	// ToolBar1: New / Open / Save
	ToolButton1->ImageIndex = ICON_NEW;  ToolButton1->Hint = L"\u65B0\u5EFA";	// 新建
	ToolButton3->ImageIndex = ICON_OPEN;  ToolButton3->Hint = L"\u6253\u5F00";	// 打开
	ToolButton5->ImageIndex = ICON_SAVE;  ToolButton5->Hint = L"\u4FDD\u5B58";	// 保存

	// ToolBar2: start + play (ToolButton7 is the existing button, the second one is created here)
	ToolButton7->ImageIndex = ICON_START;  ToolButton7->Hint = L"\u5F00\u59CB";	// 开始
	TToolButton *PlayBtn = new TToolButton(ToolBar2);
	PlayBtn->Parent     = ToolBar2;
	PlayBtn->Style      = tbsButton;
	PlayBtn->ImageIndex = ICON_RUN;
	PlayBtn->Hint       = L"\u8FD0\u884C";								// 运行
	PlayBtn->Left       = ToolButton7->Left + ToolButton7->Width;		// to the right of ToolButton7
	PlayBtn->OnClick    = RunClick;										// Run: restart the game from the beginning
	ToolButton7->OnClick = StartClick;									// Start: open the render window

	// ToolBar3: 2D / 3D (ToolButton9 = 2D, ToolButton11 = 3D; text buttons, flat look drawn in ModeBarAdvancedCustomDrawButton)
	ToolButton9->Caption  = L"2D";  ToolButton9->Hint  = L"2D \u89C6\u56FE";	// 2D 视图
	ToolButton11->Caption = L"3D";  ToolButton11->Hint = L"3D \u89C6\u56FE";	// 3D 视图
	ToolButton9->OnClick  = ModeButtonClick;
	ToolButton11->OnClick = ModeButtonClick;

	// ToolBar4: snap toggle (ToolButton13): click = on/off, long press = settings window
	ToolButton13->ImageIndex  = ICON_SNAP;
	ToolButton13->Hint        = L"\u6355\u6349\u5F00\u5173(\u957F\u6309\u6253\u5F00\u8BBE\u7F6E)";	// 捕捉开关(长按打开设置)
	ToolButton13->OnClick     = SnapButtonClick;
	ToolButton13->OnMouseDown = SnapButtonMouseDown;
	ToolBar3->OnAdvancedCustomDrawButton = ModeBarAdvancedCustomDrawButton;
	ToolBar4->OnAdvancedCustomDrawButton = ModeBarAdvancedCustomDrawButton;

	FSnapHoldTimer = new TTimer(this);
	FSnapHoldTimer->Enabled  = false;
	FSnapHoldTimer->Interval = 500;				// long press = 0.5 s
	FSnapHoldTimer->OnTimer  = SnapHoldTimer;

	ToolBar1->ButtonWidth  = TOOLBAR_ICON_SIZE + 6;
	ToolBar2->ButtonWidth  = TOOLBAR_ICON_SIZE + 6;
	ToolBar3->ButtonWidth  = MODE_BTN_W;
	ToolBar4->ButtonWidth  = TOOLBAR_ICON_SIZE + 6;
	ToolBar1->ButtonHeight = TOOLBAR_ICON_SIZE + 8;
	ToolBar2->ButtonHeight = TOOLBAR_ICON_SIZE + 8;
	ToolBar3->ButtonHeight = TOOLBAR_ICON_SIZE + 8;
	ToolBar4->ButtonHeight = TOOLBAR_ICON_SIZE + 8;
	// ControlBar bands: size the bars to their buttons
	ToolBar1->Width = 3 * ToolBar1->ButtonWidth + 4;
	ToolBar2->Width = 2 * ToolBar2->ButtonWidth + 4;
	ToolBar3->Width = 2 * ToolBar3->ButtonWidth + 4;
	ToolBar4->Width = 1 * ToolBar4->ButtonWidth + 4;

	// ---- cbrTheme must never accept dock windows (or anything else) ----
	cbrTheme->OnGetSiteInfo = ThemeBarGetSiteInfo;
	cbrTheme->OnDockOver    = ThemeBarDockOver;
	cbrTheme->OnBandInfo    = ThemeBarBandInfo;		// closes the gaps between ToolBar1..4
	ToolBar2->Left = ToolBar1->Left + ToolBar1->Width;	// ToolBar2 directly right of ToolBar1
	ToolBar3->Left = ToolBar2->Left + ToolBar2->Width;	// ToolBar3 directly right of ToolBar2
	ToolBar4->Left = ToolBar3->Left + ToolBar3->Width;	// ToolBar4 directly right of ToolBar3

	tbrTheme->Images       = FThemeImages;
	tbrTheme->ShowCaptions = false;
	tbrTheme->ShowHint     = true;						// hints = colour names
	tbrTheme->ButtonWidth  = THEME_SWATCH_BTN_W;
	tbrTheme->ButtonHeight = 24;
	tbrTheme->OnAdvancedCustomDraw       = ThemeBarAdvancedCustomDraw;
	tbrTheme->OnAdvancedCustomDrawButton = ThemeBarAdvancedCustomDrawButton;

	// A TToolButton that gets its Parent assigned is inserted at the LEFT of the existing ones,
	// so create the buttons from the last to the first one to get them in preset order.
	for (int i = THEME_PRESET_COUNT - 1; i >= 0; i--)
	{
		TToolButton *B = new TToolButton(tbrTheme);
		B->Parent     = tbrTheme;
		B->Style      = tbsButton;
		B->ImageIndex = i;
		B->Tag        = i;							// preset index
		B->Hint       = THEME_PRESETS[i].Name;
		B->OnClick    = ThemeSwatchClick;
	}
	LayoutThemeBar();
	cbrTheme->Realign();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::LayoutThemeBar()
{
	// The toolbar sits in a TControlBar band, so its Left / Top belong to the user (drag the gripper)
	// and are never touched here - only the size is kept in sync with the buttons.
	tbrTheme->Height = tbrTheme->ButtonHeight;
	tbrTheme->Width  = THEME_PRESET_COUNT * THEME_SWATCH_BTN_W + 4;
	// once the window exists, fit exactly to the last button (same idea as AlignHelpToolBar)
	if (tbrTheme->HandleAllocated() && tbrTheme->ButtonCount > 0)
		tbrTheme->Width = tbrTheme->Buttons[tbrTheme->ButtonCount - 1]->BoundsRect.Right;
}
// cbrTheme only holds its own toolbars. Everything else (dock windows, other controls)
// is refused, otherwise dragging a dock panel over the bar docks it in there and breaks the layout.
void __fastcall TForm1::ThemeBarGetSiteInfo(TObject *Sender, TControl *DockClient,
	TRect &InfluenceRect, const TPoint &MousePos, bool &CanDock)
{
	CanDock = (DockClient == ToolBar1 || DockClient == ToolBar2 || DockClient == ToolBar3 || DockClient == ToolBar4 || DockClient == tbrTheme);
}
//---------------------------------------------------------------------------
// Band insets inside cbrTheme: the space reserved around a toolbar for the gripper / frame.
// ToolBar2 gets only a thin left inset and ToolBar1 no right inset, so the two bars touch.
// (Set the numbers back to larger values if the gripper should be easier to grab.)
void __fastcall TForm1::ThemeBarBandInfo(TObject *Sender, TControl *Control, TRect &Insets,
	int &PreferredSize, int &RowCount)
{
	if (Control == ToolBar1 || Control == ToolBar2 || Control == ToolBar3)
		Insets.Right = 1;
	if (Control == ToolBar2 || Control == ToolBar3 || Control == ToolBar4)
		Insets.Left = 8;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ThemeBarDockOver(TObject *Sender, TDragDockObject *Source,
	int X, int Y, TDragState State, bool &Accept)
{
	TControl *C = Source ? Source->Control : NULL;
	Accept = (C == ToolBar1 || C == ToolBar2 || C == ToolBar3 || C == ToolBar4 || C == tbrTheme);
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void __fastcall TForm1::ThemeSwatchClick(TObject *Sender)
{
	TToolButton *B = dynamic_cast<TToolButton*>(Sender);
	if (!B || B->Tag < 0 || B->Tag >= THEME_PRESET_COUNT || B->Tag == g_ThemeIndex)
		return;
	g_ThemeIndex = B->Tag;
	ApplyTheme(FDark);		// re-colours the title bar, the menu bar and this row
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ThemeBarBandPaint(TObject *Sender, TControl *Control, TCanvas *Canvas,
	TRect &ARect, TBandPaintOptions &Options)
{
	// Paint the band (frame + gripper) ourselves so it follows the theme, like the movable
	// toolbars of the RAD Studio IDE.
	const TColor Bar = ThemeBarColor(FDark);
	const TColor Fg  = FDark ? clWhite : clBlack;
	Options = TBandPaintOptions();		// no default grabber / frame / picture

	Canvas->Brush->Style = bsSolid;
	Canvas->Brush->Color = Bar;
	Canvas->FillRect(ARect);
	Canvas->Brush->Color = MixColor(Bar, Fg, 22);
	Canvas->FrameRect(ARect);

	// gripper: a single column of small dots on the left edge
	Canvas->Brush->Color = MixColor(Bar, Fg, 55);
	int X = ARect.Left + 4;
	for (int Y = ARect.Top + 5; Y + 2 <= ARect.Bottom - 4; Y += 4)
		Canvas->FillRect(TRect(X, Y, X + 2, Y + 2));
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ThemeBarAdvancedCustomDraw(TToolBar *Sender, const TRect &ARect,
	TCustomDrawStage Stage, bool &DefaultDraw)
{
	// Background of the swatch toolbar (both themes)
	if (Stage == cdPrePaint)
	{
		Sender->Canvas->Brush->Color = ThemeBarColor(FDark);
		Sender->Canvas->FillRect(ARect);
	}
	DefaultDraw = true;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ThemeBarAdvancedCustomDrawButton(TToolBar *Sender, TToolButton *Button,
	TCustomDrawState State, TCustomDrawStage Stage, TTBCustomDrawFlags &Flags, bool &DefaultDraw)
{
	if (Stage != cdPrePaint)
		return;
	int Idx = Button->Tag;
	if (Idx < 0 || Idx >= THEME_PRESET_COUNT)
		return;

	TCanvas *C = Sender->Canvas;
	TRect R = Button->BoundsRect;
	const TColor Bar = ThemeBarColor(FDark);
	const TColor Fg  = FDark ? clWhite : clBlack;

	// flat image button like the RAD Studio toolbars: no frame normally,
	// a light frame + fill on hover, a stronger one when pressed / when it is the selected theme
	const bool Down = State.Contains(cdsSelected) || State.Contains(cdsChecked);
	const bool Hot  = State.Contains(cdsHot);
	const bool Sel  = (Idx == g_ThemeIndex);

	C->Brush->Style = bsSolid;
	C->Brush->Color = Bar;
	C->FillRect(R);
	if (Down || Hot || Sel)
	{
		C->Brush->Color = MixColor(Bar, Fg, Down ? 28 : (Hot ? 18 : 12));
		C->Pen->Style   = psSolid;
		C->Pen->Width   = 1;
		C->Pen->Color   = MixColor(Bar, Fg, Sel ? 55 : 35);
		TRect F = R;
		F.Inflate(-1, -1);
		C->Rectangle(F.Left, F.Top, F.Right, F.Bottom);
	}

	// the image, centred in the button
	int X = R.Left + (R.Width() - FThemeImages->Width) / 2;
	int Y = R.Top  + (R.Height() - FThemeImages->Height) / 2;
	if (Down)
	{
		X++;
		Y++;
	}
	FThemeImages->Draw(C, X, Y, Button->ImageIndex, true);

	DefaultDraw = false;	// we drew the button ourselves
}
//---------------------------------------------------------------------------
void __fastcall TForm1::HookMenuItems(TMenuItem *Parent)
{
	// Recursively attach the owner-draw handlers to every menu item
	for (int i = 0; i < Parent->Count; i++)
	{
		TMenuItem *Item = Parent->Items[i];
		Item->OnAdvancedDrawItem = MenuItemAdvancedDrawItem;
		Item->OnMeasureItem      = MenuItemMeasureItem;
		HookMenuItems(Item);
	}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ApplyMenuColors()
{
	// Background brush of the popup menus (the area around / behind the items)
	static HBRUSH hDark = NULL, hLight = NULL;
	static COLORREF cDark = CLR_INVALID, cLight = CLR_INVALID;
	HBRUSH &hb = FDark ? hDark : hLight;
	COLORREF &lastColor = FDark ? cDark : cLight;
	const COLORREF wantedColor = static_cast<COLORREF>(ColorToRGB(GetMenuPalette(FDark).Bg));
	if (!hb || lastColor != wantedColor)
	{
		if (hb)
			DeleteObject(hb);
		hb = CreateSolidBrush(wantedColor);
		lastColor = wantedColor;
	}

	MENUINFO mi;
	ZeroMemory(&mi, sizeof(mi));
	mi.cbSize  = sizeof(mi);
	mi.fMask   = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
	mi.hbrBack = hb;

	// Popup-menu frame (see MenuFrameHookProc): same colour as the menu background.
	// Active in both themes; set it to "FDark" to keep the normal Windows frame in the light theme.
	g_MenuFrameColor  = static_cast<COLORREF>(ColorToRGB(GetMenuPalette(FDark).Bg));
	g_MenuFrameActive = true;

	TMainMenu *Menus[2] = { MainMenu1, MainMenu2 };
	for (int m = 0; m < 2; m++)
	{
		SetMenuInfo(Menus[m]->Handle, &mi);
		for (int i = 0; i < Menus[m]->Items->Count; i++)
		{
			TMenuItem *Top = Menus[m]->Items->Items[i];
			if (Top->Count > 0)		// only items that really are drop-downs
				SetMenuInfo(Top->Handle, &mi);
		}
	}

	// right-click menu of the file tree
	if (FTreeMenu)
		SetMenuInfo(FTreeMenu->Handle, &mi);

	// toolbar visibility menu uses the active theme palette
	if (FBarMenu)
		SetMenuInfo(FBarMenu->Handle, &mi);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::MenuItemMeasureItem(TObject *Sender, TCanvas *ACanvas, int &Width, int &Height)
{
	TMenuItem *Item = static_cast<TMenuItem*>(Sender);

	if (Item->IsLine())
	{
		Width  = 0;
		Height = 9;
		return;
	}

	String Cap = StringReplace(Item->Caption, "&", "", TReplaceFlags() << rfReplaceAll);
	int W = 28 + ACanvas->TextWidth(Cap) + 16;		// left gutter + text + right padding
	if (Item->ShortCut != 0)
		W += 32 + ACanvas->TextWidth(ShortCutToText(Item->ShortCut));
	if (Item->Count > 0)
		W += 16;									// room for the sub-menu arrow

	Width  = W;
	Height = 24;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::MenuItemAdvancedDrawItem(TObject *Sender, TCanvas *ACanvas,
	const TRect &ARect, TOwnerDrawState State)
{
	TMenuItem *Item = static_cast<TMenuItem*>(Sender);
	TMenuPalette P = GetMenuPalette(FDark);
	TRect R = ARect;

	bool Selected = State.Contains(odSelected);
	bool Disabled = State.Contains(odDisabled) || State.Contains(odGrayed) || !Item->Enabled;

	// background
	ACanvas->Brush->Style = bsSolid;
	ACanvas->Brush->Color = (Selected && !Item->IsLine()) ? P.Hot : P.Bg;
	ACanvas->FillRect(R);

	// separator
	if (Item->IsLine())
	{
		int Y = (R.Top + R.Bottom) / 2;
		ACanvas->Pen->Color = P.Sep;
		ACanvas->Pen->Width = 1;
		ACanvas->MoveTo(R.Left + 28, Y);
		ACanvas->LineTo(R.Right - 8, Y);
		return;
	}

	TColor TextColor = Disabled ? P.Dim : P.Text;
	int MidY = (R.Top + R.Bottom) / 2;

	// The dock window entries of the View menu are checked while the window is really on screen.
	// This is asked every time the item is painted, so it never shows an old state.
	bool ShowCheck = Item->Checked;
	{
		TForm *Forms[4] = { FExploreForm, FCreateForm, FPropsForm, FOutputForm };
		for (int k = 0; k < 4; k++)
			if (Item == g_DockMenuItems[k])		// dock window entry: ONLY the live window state decides
			{
				ShowCheck = DockFormReallyShown(Forms[k]);
				break;
			}
	}

	// check mark ("锁定布局": a padlock instead of the check mark)
	if (ShowCheck && Item == N8)
	{
		const int X = R.Left + 7, Y = MidY - 8;			// padlock 12 x 16
		ACanvas->Pen->Color = TextColor;
		ACanvas->Pen->Width = 2;
		ACanvas->Brush->Style = bsClear;
		ACanvas->RoundRect(X + 2, Y + 1, X + 10, Y + 10, 6, 6);	// shackle
		ACanvas->Pen->Width = 1;
		ACanvas->Brush->Style = bsSolid;
		ACanvas->Brush->Color = TextColor;
		ACanvas->RoundRect(X, Y + 7, X + 12, Y + 16, 3, 3);		// body
	}
	else if (ShowCheck)
	{
		ACanvas->Pen->Color = TextColor;
		ACanvas->Pen->Width = 2;
		ACanvas->MoveTo(R.Left + 8, MidY);
		ACanvas->LineTo(R.Left + 11, MidY + 3);
		ACanvas->LineTo(R.Left + 17, MidY - 4);
		ACanvas->Pen->Width = 1;
	}

	// sub-menu arrow
	int RightEdge = R.Right - 12;
	if (Item->Count > 0)
	{
		TPoint Arrow[3] = { TPoint(R.Right - 14, MidY - 4),
							TPoint(R.Right - 14, MidY + 4),
							TPoint(R.Right - 10, MidY) };
		ACanvas->Brush->Color = TextColor;
		ACanvas->Pen->Color   = TextColor;
		ACanvas->Polygon(Arrow, 2);
		RightEdge = R.Right - 24;
	}

	ACanvas->Brush->Style = bsClear;

	// shortcut text, right aligned
	if (Item->ShortCut != 0)
	{
		TRect SR = R;
		SR.Right = RightEdge;
		String SC = ShortCutToText(Item->ShortCut);
		ACanvas->Font->Color = Disabled ? P.Dim : P.Shortcut;
		DrawText(ACanvas->Handle, SC.c_str(), -1, &SR, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);
	}

	// caption
	TRect TR = R;
	TR.Left  += 28;
	TR.Right  = RightEdge;
	UINT Flags = DT_SINGLELINE | DT_LEFT | DT_VCENTER;
	if (State.Contains(odNoAccel))
		Flags |= DT_HIDEPREFIX;
	String Cap = Item->Caption;
	ACanvas->Font->Color = TextColor;
	DrawText(ACanvas->Handle, Cap.c_str(), -1, &TR, Flags);

	ACanvas->Brush->Style = bsSolid;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::FormResize(TObject *Sender)
{
	AlignHelpToolBar();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::FormShow(TObject *Sender)
{
	AlignHelpToolBar();
	LayoutThemeBar();
	LayoutDockForms();		// build the start layout once (docking needs the main window to exist)
	// Create command controls only after JvDocking has established the dock layout.
	InitCommandLineControls(FOutputForm);
	// Create child HWNDs after JvDocking has finished the initial reparent/handle recreation.
	if (!g_CreateDockControls && FCreateForm && FSdl)
	{
		InitCreateDockControls(FCreateForm, FSdl);
		ApplyCreateDockControlsTheme(FDark);
		FCreateForm->Invalidate();
	}
	RestoreLayoutAtStartup();		// a layout that was locked last time
}
//---------------------------------------------------------------------------
// ===========================  DOCKING (JvDocking)  ===========================
// TJvDockServer on the main form creates the four dock zones (left / right / top / bottom) around the
// form's client area. Every dock window is a small TForm (created in code, no .dfm) that carries a
// TJvDockClient - the same thing TJvDockableForm does internally. The style decides how it looks:
// TJvDockVSNetStyle = Visual Studio .NET (tab groups + auto-hide).
// To add another dock window:  F = CreateDockForm(...);  put controls on it;  add it in LayoutDockForms().
//---------------------------------------------------------------------------
TForm* __fastcall TForm1::CreateDockForm(const String &AName, const String &ACaption, int AW, int AH)
{
	TForm *F = new TForm(this, 1);			// CreateNew: this form has no .dfm resource
	F->Name         = AName;				// must be unique
	F->Caption      = ACaption;
	F->BorderStyle  = bsSizeToolWin;
	F->Position     = poDesigned;
	F->ClientWidth  = AW;
	F->ClientHeight = AH;

	TJvDockClient *C = new TJvDockClient(F);
	C->DockStyle      = FDockStyle;
	C->LRDockWidth    = AW;					// size of the zone when docked left / right
	C->TBDockHeight   = AH;					// size of the zone when docked top / bottom
	C->OnFormShow     = DockFormShown;
	return F;
}
//---------------------------------------------------------------------------
// ===========================  LAYOUT: save / load / lock  ===========================
// "保存布局" writes the JvDocking tree (every dock window, floating windows, zone sizes) to
// <exe>.layout and the main window rectangle + toolbar visibility to <exe>.ini, section [Layout].
// "加载布局" reads them back. "锁定布局" saves first, then blocks every mouse press on the dock chrome
// (title strips, tab headers, splitters, zones, floating window frames), so windows can neither be
// dragged, docked, floated, closed nor resized; a padlock replaces the check mark of the menu item. The locked state
// is stored too and is restored at the next start. Click "锁定布局" again to unlock.
//---------------------------------------------------------------------------
// Application->OnMessage hook (a static object, so it outlives the form): while the layout is locked,
// mouse presses on the dock chrome are swallowed.
class TLayoutLockHook : public TObject
{
public:
	TMessageEvent Prev;
	bool          Locked;
	TForm        *Forms[4];
	__fastcall TLayoutLockHook() : Prev(NULL), Locked(false) { Forms[0] = Forms[1] = Forms[2] = Forms[3] = NULL; }
	bool __fastcall IsChrome(HWND H)
	{
		TWinControl *C = FindControl(H);
		if (!C)
			return false;
		for (int i = 0; i < 4; i++)
			if (C == Forms[i])
				return true;					// a dock window itself (title strip / floating frame)
		return C->ClassName().Pos(L"TJvDock") == 1;	// zones, splitters, tab hosts, auto-hide strips
	}
	void __fastcall OnMsg(tagMSG &Msg, bool &Handled)
	{
		if (Prev)
			Prev(Msg, Handled);
		if (Handled || !Locked)
			return;
		switch (Msg.message)
		{
			case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
			case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
			case WM_NCLBUTTONDOWN: case WM_NCLBUTTONDBLCLK: case WM_NCRBUTTONDOWN:
				if (IsChrome(Msg.hwnd))
					Handled = true;
				break;
		}
	}
};
static TLayoutLockHook *g_LayoutHook = NULL;
//---------------------------------------------------------------------------
static String LayoutTreeFile() { return ChangeFileExt(ParamStr(0), L".layout"); }
static String LayoutIniFile()  { return ChangeFileExt(ParamStr(0), L".ini"); }
static void CmdLog(const String &Msg)
{
	if (g_CommandOutput)
		g_CommandOutput->Lines->Add(Msg);
}
//---------------------------------------------------------------------------
bool __fastcall TForm1::SaveLayoutFiles()
{
	try
	{
		SaveDockTreeToFile(LayoutTreeFile());
		std::unique_ptr<TIniFile> Ini(new TIniFile(LayoutIniFile()));
		WINDOWPLACEMENT Wp;
		ZeroMemory(&Wp, sizeof(Wp));
		Wp.length = sizeof(Wp);
		::GetWindowPlacement(Handle, &Wp);			// normal (restored) rectangle even when maximized
		Ini->WriteInteger(L"Layout", L"MainState", WindowState == wsMaximized ? (int)wsMaximized : (int)wsNormal);
		Ini->WriteInteger(L"Layout", L"MainLeft",   Wp.rcNormalPosition.left);
		Ini->WriteInteger(L"Layout", L"MainTop",    Wp.rcNormalPosition.top);
		Ini->WriteInteger(L"Layout", L"MainWidth",  Wp.rcNormalPosition.right - Wp.rcNormalPosition.left);
		Ini->WriteInteger(L"Layout", L"MainHeight", Wp.rcNormalPosition.bottom - Wp.rcNormalPosition.top);
		Ini->WriteBool(L"Layout", L"BarTheme", tbrTheme->Visible);
		Ini->WriteBool(L"Layout", L"Bar1", ToolBar1->Visible);
		Ini->WriteBool(L"Layout", L"Bar2", ToolBar2->Visible);
		Ini->WriteBool(L"Layout", L"Bar3", ToolBar3->Visible);
		Ini->WriteBool(L"Layout", L"Bar4", ToolBar4->Visible);
		Ini->WriteBool(L"Layout", L"HasLayout", true);
		return true;
	}
	catch (Exception &E)
	{
		CmdLog(L"保存布局失败: " + E.Message);		// 保存布局失败
		return false;
	}
}
//---------------------------------------------------------------------------
bool __fastcall TForm1::LoadLayoutFiles()
{
	if (!FileExists(LayoutTreeFile()))
	{
		CmdLog(L"没有找到已保存的布局");	// 没有找到已保存的布局
		return false;
	}
	try
	{
		LoadDockTreeFromFile(LayoutTreeFile());
		std::unique_ptr<TIniFile> Ini(new TIniFile(LayoutIniFile()));
		if (Ini->ReadBool(L"Layout", L"HasLayout", false))
		{
			int St = Ini->ReadInteger(L"Layout", L"MainState", (int)wsNormal);
			int L  = Ini->ReadInteger(L"Layout", L"MainLeft",   Left);
			int T  = Ini->ReadInteger(L"Layout", L"MainTop",    Top);
			int W  = Ini->ReadInteger(L"Layout", L"MainWidth",  Width);
			int H  = Ini->ReadInteger(L"Layout", L"MainHeight", Height);
			if (St == (int)wsMaximized)
				WindowState = wsMaximized;
			else
			{
				WindowState = wsNormal;
				SetBounds(L, T, W, H);
			}
			tbrTheme->Visible = Ini->ReadBool(L"Layout", L"BarTheme", tbrTheme->Visible);
			ToolBar1->Visible = Ini->ReadBool(L"Layout", L"Bar1", ToolBar1->Visible);
			ToolBar2->Visible = Ini->ReadBool(L"Layout", L"Bar2", ToolBar2->Visible);
			ToolBar3->Visible = Ini->ReadBool(L"Layout", L"Bar3", ToolBar3->Visible);
			ToolBar4->Visible = Ini->ReadBool(L"Layout", L"Bar4", ToolBar4->Visible);
			cbrTheme->Realign();
		}
		DockFormShown(NULL);		// docking re-created window handles: dark scroll bars / colours again
		return true;
	}
	catch (Exception &E)
	{
		CmdLog(L"加载布局失败: " + E.Message);		// 加载布局失败
		return false;
	}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::SaveLayoutClick(TObject *Sender)
{
	if (SaveLayoutFiles())
		CmdLog(L"布局已保存");		// 布局已保存
}
//---------------------------------------------------------------------------
void __fastcall TForm1::LoadLayoutClick(TObject *Sender)
{
	if (FLayoutLocked)
	{
		CmdLog(L"布局已锁定，请先解锁再加载");	// 布局已锁定，请先解锁再加载
		return;
	}
	if (LoadLayoutFiles())
		CmdLog(L"布局已加载");		// 布局已加载
}
//---------------------------------------------------------------------------
void __fastcall TForm1::SetLayoutLocked(bool ALocked, bool ASave)
{
	if (ALocked && ASave && !SaveLayoutFiles())		// remember the current positions first
		return;
	FLayoutLocked = ALocked;
	if (g_LayoutHook)
		g_LayoutHook->Locked = ALocked;
	if (N8)
		N8->Checked = ALocked;
	try
	{
		std::unique_ptr<TIniFile> Ini(new TIniFile(LayoutIniFile()));
		Ini->WriteBool(L"Layout", L"Locked", ALocked);
	}
	catch (Exception &) {}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::LockLayoutClick(TObject *Sender)
{
	SetLayoutLocked(!FLayoutLocked, true);
	CmdLog(FLayoutLocked ? L"布局已锁定" : L"布局已解锁");	// 布局已锁定 / 布局已解锁
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
// start of the program: a layout that was locked last time is loaded and locked again
void __fastcall TForm1::RestoreLayoutAtStartup()
{
	try
	{
		if (!FileExists(LayoutIniFile()) || !FileExists(LayoutTreeFile()))
			return;
		std::unique_ptr<TIniFile> Ini(new TIniFile(LayoutIniFile()));
		if (!Ini->ReadBool(L"Layout", L"Locked", false))
			return;
	}
	catch (Exception &) { return; }
	if (LoadLayoutFiles())
		SetLayoutLocked(true, false);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::InitDockLayout()
{
	FDockStyle  = new TJvDockVSNetStyle(this);
	FDockServer = new TJvDockServer(this);
	FDockServer->DockStyle = FDockStyle;	// creates the four dock panels + splitters (all start with size 0)

	FExploreForm = CreateDockForm(L"dockExplore", L"Explore", 220, 400);
	FCreateForm  = CreateDockForm(L"dockCreate",  L"Create",  220, 200);
	FPropsForm   = CreateDockForm(L"dockProps",   L"属性", 220, 200);	// "Properties"
	FOutputForm  = CreateDockForm(L"dockOutput",  L"命令",  600, 90);	// 命令历史 + 输入行，合并为一个窗口
	// the file tree was designed on the main form (hidden): move it into its dock window
	TreeView1->Parent  = FExploreForm;
	TreeView1->Align   = alClient;
	TreeView1->Visible = true;

	// View menu: open a dock window again after it was closed
	TMenuItem *Sep = new TMenuItem(this);
	Sep->Caption = L"-";
	mnuView->Add(Sep);
	TForm *DockForms[4] = { FExploreForm, FCreateForm, FPropsForm, FOutputForm };
	for (int i = 0; i < 4; i++)
	{
		TMenuItem *Mi = new TMenuItem(this);
		Mi->Caption = DockForms[i]->Caption;
		Mi->Tag     = i;
		Mi->Checked = false;		// the check mark is drawn by MenuItemAdvancedDrawItem from the live window state only
		g_DockMenuItems[i] = Mi;
		Mi->OnClick = DockWindowMenuClick;
		mnuView->Add(Mi);
	}
	// layout items: 保存布局 / 加载布局 / 锁定布局 (N7 / N8 come from the .dfm)
	N7->OnClick = SaveLayoutClick;
	N8->OnClick = LockLayoutClick;
	TMenuItem *Ld = new TMenuItem(this);
	Ld->Caption = L"\u52A0\u8F7D\u5E03\u5C40";			// 加载布局
	Ld->OnClick = LoadLayoutClick;
	mnuView->Insert(mnuView->IndexOf(N7) + 1, Ld);
	mnuView->OnClick = ViewMenuOpen;		// refresh the check marks each time the menu opens
	HookMenuItems(mnuView);		// the menus are owner-drawn: attach the draw handlers to the new items too

	// the mouse filter that enforces the lock (the padlock is drawn in front of the menu item)
	FLayoutLocked = false;
	if (!g_LayoutHook)
	{
		g_LayoutHook = new TLayoutLockHook();
		g_LayoutHook->Prev = Application->OnMessage;
		Application->OnMessage = g_LayoutHook->OnMsg;
	}
	g_LayoutHook->Forms[0] = FExploreForm;
	g_LayoutHook->Forms[1] = FCreateForm;
	g_LayoutHook->Forms[2] = FPropsForm;
	g_LayoutHook->Forms[3] = FOutputForm;
}
//---------------------------------------------------------------------------
// Is the dock window really on screen right now? Asked from Windows itself, not from VCL properties
// (JvDocking hides windows by hiding / collapsing the zone or host around them): the window must be
// visible, and what is left of it after clipping with every parent window must not be empty.
// Windows that were hidden through the View menu. JvDocking may collapse the zone without hiding the
// form's HWND (or do it later, animated), so the geometry test alone can still say "visible" right after
// HideDockForm. This list is authoritative until the window is shown again (menu click or OnFormShow).
struct TMenuHiddenInfo { TForm *F; DWORD Tick; };
static std::vector<TMenuHiddenInfo> g_MenuHiddenForms;
static bool IsMenuHidden(TForm *F)
{
	for (size_t i = 0; i < g_MenuHiddenForms.size(); i++)
		if (g_MenuHiddenForms[i].F == F)
			return true;
	return false;
}
// AFromEvent = true: called from an OnFormShow event. JvDocking can fire that event as a side effect of
// HideDockForm itself, so events arriving right after the hide are ignored.
static void SetMenuHidden(TForm *F, bool AHidden, bool AFromEvent = false)
{
	for (size_t i = 0; i < g_MenuHiddenForms.size(); i++)
	{
		if (g_MenuHiddenForms[i].F != F)
			continue;
		if (!AHidden)
		{
			if (AFromEvent && ::GetTickCount() - g_MenuHiddenForms[i].Tick < 1500)
				return;
			g_MenuHiddenForms.erase(g_MenuHiddenForms.begin() + i);
		}
		else
			g_MenuHiddenForms[i].Tick = ::GetTickCount();
		return;
	}
	if (AHidden)
	{
		TMenuHiddenInfo Info;
		Info.F = F;
		Info.Tick = ::GetTickCount();
		g_MenuHiddenForms.push_back(Info);
	}
}
//---------------------------------------------------------------------------
static bool DockFormReallyShown(TForm *F)
{
	if (!F || !F->HandleAllocated())
		return false;
	if (IsMenuHidden(F))
		return false;
	if (!F->Visible)
		return false;
	HWND H = F->Handle;
	if (!::IsWindowVisible(H))
		return false;
	RECT R;
	::GetWindowRect(H, &R);
	HWND Desktop = ::GetDesktopWindow();
	for (HWND P = ::GetAncestor(H, GA_PARENT); P && P != Desktop; P = ::GetAncestor(P, GA_PARENT))
	{
		RECT C, PR;
		::GetClientRect(P, &C);
		POINT TL = { C.left, C.top };
		::ClientToScreen(P, &TL);
		PR.left = TL.x;  PR.top = TL.y;
		PR.right = TL.x + (C.right - C.left);
		PR.bottom = TL.y + (C.bottom - C.top);
		RECT X;
		if (!::IntersectRect(&X, &R, &PR))
			return false;
		R = X;
	}
	return (R.right - R.left) > 8 && (R.bottom - R.top) > 8;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::DockWindowMenuClick(TObject *Sender)
{
	TMenuItem *Mi = dynamic_cast<TMenuItem*>(Sender);
	if (!Mi)
		return;
	TForm *F = NULL;
	switch ((int)Mi->Tag)
	{
		case 0: F = FExploreForm; break;
		case 1: F = FCreateForm;  break;
		case 2: F = FPropsForm;   break;
		case 3: F = FOutputForm;  break;
	}
	if (!F)
		return;
	int Idx = (int)Mi->Tag;
	if (DockFormReallyShown(F))
	{
		if (FLayoutLocked)
		{
			CmdLog(L"\u5E03\u5C40\u5DF2\u9501\u5B9A\uFF0C\u4E0D\u80FD\u9690\u85CF\u7A97\u53E3");	// 布局已锁定，不能隐藏窗口
			return;
		}
		HideDockForm(F);		// second click: hide it
		SetMenuHidden(F, true);	// remember it: the check mark must go away at once
	}
	else
	{
		SetMenuHidden(F, false);
		ShowDockForm(F);
	}
	ViewMenuOpen(NULL);
}
//---------------------------------------------------------------------------
// View menu opens: a check mark in front of every dock window that is open
void __fastcall TForm1::ViewMenuOpen(TObject *Sender)
{
	// Dock window entries never use TMenuItem::Checked (nothing else may draw a check for them);
	// the owner-draw handler asks the window state itself every time the item is painted.
	for (int i = 0; i < 4; i++)
		if (g_DockMenuItems[i])
			g_DockMenuItems[i]->Checked = false;
}
//---------------------------------------------------------------------------
// Dock a (hidden) dock window into a zone and open it the JvDocking way.
// ShowDockForm() - not Form->Show() - is what opens the zone: every dock zone starts with size 0 and is
// only sized by TJvDockPanel.ShowDockPanel, which ShowDockForm calls (TJvDockPanel.DockDrop does it only
// for a form that is already visible, i.e. a real mouse drop). The library's own ManualTabDock does the same.
static void DockAndShow(TForm *F, TWinControl *Site, TControl *Drop, TAlign Side)
{
	F->ManualDock(Site, Drop, Side);
	ShowDockForm(F);
}
//---------------------------------------------------------------------------
// Start layout (like the old LMD layout): Explore left, Create above Properties on the right,
// Output at the bottom. The user can drag the windows anywhere afterwards.
void __fastcall TForm1::LayoutDockForms()
{
	if (FDockLaidOut || !FDockServer)
		return;
	FDockLaidOut = true;

	// size of a zone when it opens (ShowDockForm takes it from the dock panel itself)
	FDockServer->LeftDockPanel->LRDockWidth    = 240;
	FDockServer->RightDockPanel->LRDockWidth   = 260;
	FDockServer->BottomDockPanel->TBDockHeight = 240;

	DockAndShow(FExploreForm, FDockServer->LeftDockPanel,   NULL,        alClient);
	DockAndShow(FCreateForm,  FDockServer->RightDockPanel,  NULL,        alClient);
	DockAndShow(FPropsForm,   FDockServer->RightDockPanel,  FCreateForm, alBottom);		// stacked under "Create"
	DockAndShow(FOutputForm,  FDockServer->BottomDockPanel, NULL,        alClient);	// 命令栏：历史 + 输入行
	// docking re-created the window handles of the children: set the dark scroll bars again
	DockFormShown(NULL);
}
//---------------------------------------------------------------------------
// Docking / floating a window makes VCL re-create the handles of its children, which resets the
// SetWindowTheme() dark scroll bars - apply them again.
void __fastcall TForm1::DockFormShown(TObject *Sender)
{
	// A TJvDockClient fires OnFormShow when its window is shown again (menu, drag, layout load):
	// it is no longer "hidden by the menu". The client is owned by its dock form.
	TJvDockClient *Dc = dynamic_cast<TJvDockClient*>(Sender);
	if (Dc)
	{
		TForm *Owner = dynamic_cast<TForm*>(Dc->Owner);
		if (Owner)
			SetMenuHidden(Owner, false, true);
	}
	if (TreeView1)
		SetControlTheme(TreeView1, FDark);
	ApplyPropsTheme();
	ApplyDockTheme();
}
//---------------------------------------------------------------------------
// TControl.Color / TWinControl.ParentBackground are protected; JvDocking paints its dock zones and the
// splitters between docked windows with the zone's Color, so make them reachable.
struct TDockCtlAccess : public TWinControl
{
public:
	__property Color;
	__property ParentBackground;
	__property Font;
};
// TCustomControl.Canvas is protected too; the tab text of the auto-hide strip is drawn with Canvas.Font
struct TCtlCanvasAccess : public TCustomControl
{
public:
	__property Canvas;
};
static void SetDockCtlColor(TWinControl *C, TColor AColor)
{
	if (!C)
		return;
	TDockCtlAccess *A = static_cast<TDockCtlAccess*>(C);
	if (!A->ParentBackground && A->Color == AColor)
		return;							// already themed
	A->ParentBackground = false;		// draw with Color, not with the parent's background
	A->Color            = AColor;
	C->Invalidate();
}
//---------------------------------------------------------------------------
// TCustomPanel 的斜边属性是 protected，开个口子
struct TPanelAccess : public TCustomPanel
{
public:
	__property BevelInner;
	__property BevelOuter;
	__property BevelWidth;
	__property BorderWidth;
};
// 自动隐藏用到的标签条 / 弹出面板 / 分隔条：去白边、用主题色、字体白色
//
// JVCL 的弹出分隔条 TJvDockVSPopupPanelSplitter 在 Paint 里用 DrawFrameControl(按钮凸边) 画了一圈白边，
// 画完才触发 OnPaint —— 所以在 OnPaint 里用主题色把整个条再盖一遍即可，不用改 JVCL 源码。
//
// 可选：JVCL 里还有两处写死的 clGray 边框（标签条的小方框、未激活标题条的圆角框）。
// 如果按「JVCL补丁说明.txt」改了 JvDockVSNetStyle.pas 并重编了 JVCL 包，就打开下面这行：
#define JVDOCK_BORDER_PATCH
static const TColor kNoColor = (TColor)0x1FFFFFFF;	// same value as clNone (clNone is ambiguous: Vcl.Graphics vs System.ZLib)
static TColor g_DockBar = kNoColor;	// zones / splitters / channel colour (set by ApplyDockTheme)
static TColor g_DockTxt = kNoColor;	// font colour of the auto-hide tabs

//---------------------------------------------------------------------------
// Docking guides ("浮动归位时的引导图标", like LMD DockingPack / Visual Studio 2005-2012).
// While a dock window is being dragged, small click-through icons are shown:
//   * four arrows at the edges of the dock area,
//   * a cross (centre + 4 arrows) in the middle of the dock window / zone under the cursor.
// They are only an indication - the real docking is still done by JvDocking (preview rectangle).
// The icons never take mouse or focus (WS_EX_TRANSPARENT + HTTRANSPARENT + NOACTIVATE).
static TJvDockServer *g_DockServerPtr = NULL;		// set by ApplyDockTheme
static bool           g_DockDark      = true;
static const int      kGuideSize      = 32;
static HWND           g_GuideWnd[9]   = { 0 };		// 0 centre, 1-4 cross L/T/R/B, 5-8 edge L/T/R/B
static int            g_GuideHot      = -1;
static const wchar_t *kGuideClass     = L"JvDockGuideWnd";
// direction of each icon: 0 = centre, 1 = left, 2 = top, 3 = right, 4 = bottom
static const int      kGuideDir[9]    = { 0, 1, 2, 3, 4, 1, 2, 3, 4 };

static LRESULT CALLBACK GuideWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m)
	{
	case WM_NCHITTEST:
		return HTTRANSPARENT;						// let JvDocking's hit-testing see what is below
	case WM_MOUSEACTIVATE:
		return MA_NOACTIVATE;
	case WM_ERASEBKGND:
		return 1;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC dc = ::BeginPaint(h, &ps);
		int idx = (int)::GetWindowLongPtr(h, GWLP_USERDATA);
		bool hot = (idx == g_GuideHot);
		RECT rc;
		::GetClientRect(h, &rc);

		COLORREF bg   = hot ? RGB(0, 122, 204) : (g_DockDark ? RGB(45, 45, 48)  : RGB(238, 238, 242));
		COLORREF edge = g_DockDark ? RGB(0, 122, 204) : RGB(130, 130, 140);
		COLORREF fg   = (hot || g_DockDark) ? RGB(255, 255, 255) : RGB(40, 40, 40);

		HBRUSH hb = ::CreateSolidBrush(bg);
		HPEN   hp = ::CreatePen(PS_SOLID, 1, edge);
		HGDIOBJ ob = ::SelectObject(dc, hb);
		HGDIOBJ op = ::SelectObject(dc, hp);
		::RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);

		HBRUSH fb = ::CreateSolidBrush(fg);
		HPEN   fp = ::CreatePen(PS_SOLID, 1, fg);
		::SelectObject(dc, fb);
		::SelectObject(dc, fp);
		int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
		POINT t[3];
		switch (kGuideDir[idx])
		{
		case 1: t[0].x = cx - 6; t[0].y = cy;     t[1].x = cx + 4; t[1].y = cy - 7; t[2].x = cx + 4; t[2].y = cy + 7; ::Polygon(dc, t, 3); break;
		case 3: t[0].x = cx + 6; t[0].y = cy;     t[1].x = cx - 4; t[1].y = cy - 7; t[2].x = cx - 4; t[2].y = cy + 7; ::Polygon(dc, t, 3); break;
		case 2: t[0].x = cx;     t[0].y = cy - 6; t[1].x = cx - 7; t[1].y = cy + 4; t[2].x = cx + 7; t[2].y = cy + 4; ::Polygon(dc, t, 3); break;
		case 4: t[0].x = cx;     t[0].y = cy + 6; t[1].x = cx - 7; t[1].y = cy - 4; t[2].x = cx + 7; t[2].y = cy - 4; ::Polygon(dc, t, 3); break;
		default:									// centre: a small window with a title bar
			::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
			::Rectangle(dc, cx - 9, cy - 8, cx + 10, cy + 9);
			::Rectangle(dc, cx - 9, cy - 8, cx + 10, cy - 3);
			break;
		}
		::SelectObject(dc, ob);
		::SelectObject(dc, op);
		::DeleteObject(hb); ::DeleteObject(hp); ::DeleteObject(fb); ::DeleteObject(fp);
		::EndPaint(h, &ps);
		return 0;
	}
	}
	return ::DefWindowProc(h, m, w, l);
}

static void EnsureGuideWindows()
{
	if (g_GuideWnd[0])
		return;
	WNDCLASSW wc;
	::ZeroMemory(&wc, sizeof(wc));
	wc.lpfnWndProc   = GuideWndProc;
	wc.hInstance     = HInstance;
	wc.lpszClassName = kGuideClass;
	wc.hCursor       = ::LoadCursor(NULL, IDC_ARROW);
	::RegisterClassW(&wc);						// fails harmlessly if already registered
	for (int i = 0; i < 9; i++)
	{
		HWND h = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
			kGuideClass, L"", WS_POPUP, 0, 0, kGuideSize, kGuideSize, NULL, NULL, HInstance, NULL);
		if (h)
		{
			::SetWindowLongPtr(h, GWLP_USERDATA, i);
			::SetLayeredWindowAttributes(h, 0, 225, LWA_ALPHA);
		}
		g_GuideWnd[i] = h;
	}
}

static void PlaceGuide(int i, int x, int y, bool Show)
{
	HWND h = g_GuideWnd[i];
	if (!h)
		return;
	if (!Show)
	{
		if (::IsWindowVisible(h))
			::ShowWindow(h, SW_HIDE);
		return;
	}
	::SetWindowPos(h, HWND_TOPMOST, x - kGuideSize / 2, y - kGuideSize / 2, kGuideSize, kGuideSize,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static void HideGuides()
{
	for (int i = 0; i < 9; i++)
		PlaceGuide(i, 0, 0, false);
	g_GuideHot = -1;
}

// union of the screen rectangles of the four dock panels = the dock area
static bool GetDockAreaRect(TRect &Out)
{
	if (!g_DockServerPtr)
		return false;
	TWinControl *P[4] = { g_DockServerPtr->LeftDockPanel, g_DockServerPtr->RightDockPanel,
	                      g_DockServerPtr->TopDockPanel,  g_DockServerPtr->BottomDockPanel };
	bool Any = false;
	for (int i = 0; i < 4; i++)
	{
		if (!P[i] || !P[i]->Parent)
			continue;
		TPoint a = P[i]->Parent->ClientToScreen(Point(P[i]->Left, P[i]->Top));
		TRect  r(a.x, a.y, a.x + P[i]->Width, a.y + P[i]->Height);
		if (!Any) { Out = r; Any = true; }
		else
		{
			Out.Left   = std::min((int)Out.Left,   (int)r.Left);
			Out.Top    = std::min((int)Out.Top,    (int)r.Top);
			Out.Right  = std::max((int)Out.Right,  (int)r.Right);
			Out.Bottom = std::max((int)Out.Bottom, (int)r.Bottom);
		}
	}
	return Any && Out.Right > Out.Left && Out.Bottom > Out.Top;
}

// ---- full version: the icons are selectable --------------------------------------------------------------
// While a drag is running the icons only exist as click-through windows, so the hit test is done by position.
// A thread mouse hook sees the WM_LBUTTONUP *before* JvDocking handles it: if the cursor is over an icon we
// remember what was chosen and make JvDocking just float the window (DragTarget = NULL); the next timer tick
// then docks it to the chosen place with ManualDock.
static TPoint       g_GPos[9];
static bool         g_GVis[9] = { false };
static TWinControl *g_GTarget = NULL;			// window / zone under the cursor when the cross was shown
static TControl    *g_GDropOn = NULL;
struct TPendingDock { TControl *Ctl; int Idx; TWinControl *Site; TControl *DropOn; bool Valid; };
static TPendingDock g_Pending = { NULL, -1, NULL, NULL, false };
static HHOOK        g_MouseHook = NULL;

static LRESULT CALLBACK GuideMouseHook(int Code, WPARAM W, LPARAM L)
{
	if (Code >= 0 && W == WM_LBUTTONUP && g_GuideWnd[0] && JvGlobalDockManager)
	{
		TJvDockDragDockObject *DO = JvGlobalDockManager->DragObject;
		MOUSEHOOKSTRUCT *mh = (MOUSEHOOKSTRUCT*)L;
		if (DO && mh)
		{
			int Hot = -1;
			for (int i = 0; i < 9; i++)
				if (g_GVis[i] && ::IsWindowVisible(g_GuideWnd[i]) &&
				    abs(mh->pt.x - g_GPos[i].x) <= kGuideSize / 2 && abs(mh->pt.y - g_GPos[i].y) <= kGuideSize / 2)
					Hot = i;
			if (Hot >= 0 && DO->Control)
			{
				g_Pending.Ctl    = DO->Control;
				g_Pending.Idx    = Hot;
				g_Pending.Site   = g_GTarget;
				g_Pending.DropOn = g_GDropOn;
				g_Pending.Valid  = true;
				DO->DragTarget   = NULL;		// JvDocking: just float it, we dock it right after
			}
		}
	}
	return ::CallNextHookEx(g_MouseHook, Code, W, L);
}

static void DoPendingDock()
{
	if (!g_Pending.Valid)
		return;
	if (JvGlobalDockManager && JvGlobalDockManager->DragObject)
		return;									// JvDocking is still finishing the drop
	g_Pending.Valid = false;
	const int Dir = kGuideDir[g_Pending.Idx];
	TAlign Al = (Dir == 1) ? alLeft : (Dir == 2) ? alTop : (Dir == 3) ? alRight : (Dir == 4) ? alBottom : alClient;
	TWinControl *Site = NULL;
	TControl    *DropOn = NULL;
	if (g_Pending.Idx >= 5)						// edge arrows: the dock zones of the form
	{
		if (!g_DockServerPtr)
			return;
		Site = (Dir == 1) ? g_DockServerPtr->LeftDockPanel  : (Dir == 2) ? g_DockServerPtr->TopDockPanel
		     : (Dir == 3) ? g_DockServerPtr->RightDockPanel : g_DockServerPtr->BottomDockPanel;
	}
	else										// cross: relative to the window / zone that was under the cursor
	{
		Site   = g_Pending.Site;
		DropOn = g_Pending.DropOn;
	}
	if (!Site || !g_Pending.Ctl || g_Pending.Ctl == Site)
		return;
	try
	{
		g_Pending.Ctl->ManualDock(Site, DropOn, Al);
		g_Pending.Ctl->Show();
	}
	catch (Exception &)
	{
		// the target went away while the window was floating - leave the window floating
	}
}

// called by a fast timer: shows / hides / highlights the guides
static void UpdateDockGuides(TForm *Form)
{
	TJvDockDragDockObject *DO = JvGlobalDockManager ? JvGlobalDockManager->DragObject : NULL;
	static bool  s_seen = false;
	static bool  s_shown = false;
	static POINT s_start = { 0, 0 };
	POINT cur;
	::GetCursorPos(&cur);

	if (!DO || !(::GetAsyncKeyState(VK_LBUTTON) & 0x8000))
	{
		s_seen  = false;
		s_shown = false;
		if (g_GuideWnd[0])
			HideGuides();
		return;
	}
	if (!s_seen)
	{
		s_seen  = true;
		s_start = cur;
	}
	if (!s_shown && abs(cur.x - s_start.x) < 8 && abs(cur.y - s_start.y) < 8)
		return;									// just a click on the title strip, not a drag yet
	s_shown = true;

	EnsureGuideWindows();
	g_DockDark = (g_DockBar != kNoColor) && (GetRValue(ColorToRGB(g_DockBar)) < 128);

	TPoint Pos[9];
	bool   Vis[9] = { false };

	// edges of the dock area
	TRect Area;
	if (GetDockAreaRect(Area))
	{
		const int m = kGuideSize / 2 + 8;
		int mx = (Area.Left + Area.Right) / 2, my = (Area.Top + Area.Bottom) / 2;
		Pos[5] = Point(Area.Left  + m, my);  Vis[5] = true;
		Pos[6] = Point(mx, Area.Top    + m); Vis[6] = true;
		Pos[7] = Point(Area.Right - m, my);  Vis[7] = true;
		Pos[8] = Point(mx, Area.Bottom - m); Vis[8] = true;
	}

	// cross in the middle of the window / zone under the cursor
	TWinControl *Tg = DO->TargetControl;
	if (Tg && Tg->HandleAllocated() && Tg->Visible && Tg->ClientWidth > 3 * kGuideSize && Tg->ClientHeight > 3 * kGuideSize)
	{
		TPoint o = Tg->ClientToScreen(Point(0, 0));
		int cx = o.x + Tg->ClientWidth / 2, cy = o.y + Tg->ClientHeight / 2;
		const int d = kGuideSize + 4;
		Pos[0] = Point(cx, cy);     Vis[0] = true;
		Pos[1] = Point(cx - d, cy); Vis[1] = true;
		Pos[2] = Point(cx, cy - d); Vis[2] = true;
		Pos[3] = Point(cx + d, cy); Vis[3] = true;
		Pos[4] = Point(cx, cy + d); Vis[4] = true;
	}

	for (int i = 0; i < 9; i++)
	{
		g_GPos[i] = Pos[i];
		g_GVis[i] = Vis[i];
	}
	g_GTarget = Vis[0] ? Tg : NULL;
	g_GDropOn = Vis[0] ? DO->DropOnControl : NULL;

	int Hot = -1;
	for (int i = 0; i < 9; i++)
		if (Vis[i] && abs(cur.x - Pos[i].x) <= kGuideSize / 2 && abs(cur.y - Pos[i].y) <= kGuideSize / 2)
			Hot = i;

	for (int i = 0; i < 9; i++)
		PlaceGuide(i, Pos[i].x, Pos[i].y, Vis[i]);

	if (Hot != g_GuideHot)
	{
		int Old = g_GuideHot;
		g_GuideHot = Hot;
		if (Old >= 0 && g_GuideWnd[Old]) ::InvalidateRect(g_GuideWnd[Old], NULL, FALSE);
		if (Hot >= 0 && g_GuideWnd[Hot]) ::InvalidateRect(g_GuideWnd[Hot], NULL, FALSE);
	}
}

class TDockFlatHelper : public TObject
{
public:
	TForm *Form;
	__fastcall TDockFlatHelper(TForm *AForm) : TObject(), Form(AForm) {}
	void __fastcall SplitterPaint(TObject *Sender);
	void __fastcall Tick(TObject *Sender);
	void __fastcall GuideTick(TObject *Sender);
};
static TDockFlatHelper *g_DockFlat = NULL;

void __fastcall TDockFlatHelper::SplitterPaint(TObject *Sender)
{
	TJvDockVSPopupPanelSplitter *S = dynamic_cast<TJvDockVSPopupPanelSplitter*>(Sender);
	if (!S || g_DockBar == kNoColor)
		return;
	S->Canvas->Brush->Color = g_DockBar;
	S->Canvas->FillRect(S->ClientRect);
}

static void FlattenAutoHideCtl(TControl *C, TColor AColor)
{
	if (!C)
		return;
	if (C->ClassName().Pos(L"JvDockVS") > 0)
	{
		C->StyleElements = TStyleElements();

		TCustomPanel *P = dynamic_cast<TCustomPanel*>(C);
		if (P)
		{
			TPanelAccess *A = static_cast<TPanelAccess*>(P);
			A->BevelInner  = bvNone;
			A->BevelOuter  = bvNone;
			A->BorderWidth = 0;
		}

		TWinControl *W1 = dynamic_cast<TWinControl*>(C);
		if (W1)
			SetDockCtlColor(W1, AColor);

		// splitter of the pop-up (auto-hidden) window: cover its 3D frame
		TJvDockVSPopupPanelSplitter *Sp = dynamic_cast<TJvDockVSPopupPanelSplitter*>(C);
		if (Sp && g_DockFlat && !Sp->OnPaint)
		{
			Sp->OnPaint = g_DockFlat->SplitterPaint;
			Sp->Invalidate();
		}

		// tab strip of the auto-hidden windows: white text
		TJvDockVSChannel *Ch = dynamic_cast<TJvDockVSChannel*>(C);
		if (Ch && g_DockTxt != kNoColor)
		{
			TDockCtlAccess *FA = static_cast<TDockCtlAccess*>(static_cast<TWinControl*>(Ch));
			TCtlCanvasAccess *CA = static_cast<TCtlCanvasAccess*>(static_cast<TCustomControl*>(Ch));
			bool Changed = false;
			if (FA->Font->Color != g_DockTxt)
			{
				FA->Font->Color = g_DockTxt;
				Changed = true;
			}
			// JVCL replaces Canvas.Font (rotated text on the left/right strip), so set the canvas font too
			if (CA->Canvas->Font->Color != g_DockTxt)
			{
				CA->Canvas->Font->Color = g_DockTxt;
				Changed = true;
			}
			if (Changed)
				Ch->Invalidate();
		}
	}
	TWinControl *W = dynamic_cast<TWinControl*>(C);
	if (W)
		for (int i = 0; i < W->ControlCount; i++)
			FlattenAutoHideCtl(W->Controls[i], AColor);
}

void __fastcall TDockFlatHelper::GuideTick(TObject *Sender)
{
	DoPendingDock();
	UpdateDockGuides(Form);
}

void __fastcall TDockFlatHelper::Tick(TObject *Sender)
{
	// the pop-up panel / splitter are created lazily on the first auto-hide; catch them here
	if (Form && g_DockBar != kNoColor)
		FlattenAutoHideCtl(Form, g_DockBar);
}
//---------------------------------------------------------------------------
// Dock windows follow the Dark / Light theme like the rest of the window.
// Dark = the same colours as the "VS2012Dark" style of LMD DockingPack (Visual Studio 2012 dark theme):
//   body of a dock window ........... #252526
//   dock zones + splitters ........... #2D2D30
//   title strip ...................... inactive #2D2D30 / grey text, active #007ACC / white text
//   tabs of a tab group .............. active #007ACC, inactive #2D2D30, hot-track #1C97EA
void __fastcall TForm1::ApplyDockTheme()
{
	if (!FDockServer || !FDockStyle)
		return;

	const bool D = FDark;
	const TColor Bar    = D ? Rgb(45, 45, 48)    : ThemeBarColor(false);	// dock zones, splitters, auto-hide strip
	const TColor Body   = D ? Rgb(37, 37, 38)    : clWhite;
	const TColor Strip  = D ? Rgb(45, 45, 48)    : Rgb(238, 238, 242);		// inactive title strip / inactive tab
	const TColor Active = D ? Rgb(0, 122, 204)   : Rgb(206, 220, 240);		// active title strip / active tab
	const TColor TxtOn  = D ? clWhite            : clBlack;
	const TColor TxtOff = D ? clWhite            : Rgb(60, 60, 60);		// inactive text: white on dark

	// the dock windows themselves (the VCL style must not override Color)
	TForm *DockForms[4] = { FExploreForm, FCreateForm, FPropsForm, FOutputForm };
	for (int i = 0; i < 4; i++)
	{
		if (!DockForms[i])
			continue;
		DockForms[i]->StyleElements = TStyleElements();
		DockForms[i]->Color         = Body;
		DockForms[i]->Font->Color   = TxtOn;
	}

	// the four dock zones and their splitters
	TWinControl *Zones[8] = {
		FDockServer->LeftDockPanel,  FDockServer->RightDockPanel,
		FDockServer->TopDockPanel,   FDockServer->BottomDockPanel,
		FDockServer->LeftSplitter,   FDockServer->RightSplitter,
		FDockServer->TopSplitter,    FDockServer->BottomSplitter };
	for (int i = 0; i < 8; i++)
		SetDockCtlColor(Zones[i], Bar);

	// tab-host / conjoin forms that JvDocking creates when windows are dropped onto each other
	for (int i = 0; i < Screen->FormCount; i++)
	{
		TForm *H = Screen->Forms[i];
		if (dynamic_cast<TJvDockTabHostForm*>(H) || dynamic_cast<TJvDockConjoinHostForm*>(H))
		{
			H->StyleElements = TStyleElements();
			H->Color         = Body;
		}
	}

	// title strip of a docked window ("grabber"); setting a colour switches off "use system colours"
	TJvDockVIDConjoinServerOption *Co = dynamic_cast<TJvDockVIDConjoinServerOption*>(FDockStyle->ConjoinServerOption);
	if (Co)
	{
		Co->BeginUpdate();
		Co->ActiveTitleStartColor   = Active;
		Co->ActiveTitleEndColor     = Active;
		Co->InactiveTitleStartColor = Strip;
		Co->InactiveTitleEndColor   = Strip;
		Co->ActiveFont->Color       = TxtOn;
		Co->InactiveFont->Color     = TxtOff;
		Co->EndUpdate();
	}

	// tabs of a tab group
	TJvDockVIDTabServerOption *Ta = dynamic_cast<TJvDockVIDTabServerOption*>(FDockStyle->TabServerOption);
	if (Ta)
	{
		Ta->BeginUpdate();
		Ta->ActiveSheetColor   = Active;
		Ta->InactiveSheetColor = Strip;
		Ta->HotTrackColor      = D ? Rgb(28, 151, 234) : clBlue;
		Ta->ActiveFont->Color  = TxtOn;
		Ta->InactiveFont->Color = TxtOff;
		Ta->EndUpdate();
	}

	// strip of auto-hidden windows (VS.NET style)
	if (FDockStyle->ChannelOption)
		FDockStyle->ChannelOption->TabColor = Bar;

	g_DockBar = Bar;
	g_DockTxt = TxtOn;
	g_DockServerPtr = FDockServer;
#ifdef JVDOCK_BORDER_PATCH
	Jvdockvsnetstyle::JvDockVSBorderColor = Bar;	// JVCL 补丁：标签框 / 标题条圆角框的颜色
#endif
	FlattenAutoHideCtl(this, Bar);	// 自动隐藏的标签条 / 弹出面板 / 分隔条

	// keep it applied after pop-up panels get created later
	if (!g_DockFlat)
	{
		g_DockFlat = new TDockFlatHelper(this);
		TTimer *T = new TTimer(this);
		T->Interval = 300;
		T->OnTimer  = g_DockFlat->Tick;
		T->Enabled  = true;

		TTimer *G = new TTimer(this);			// drag-and-drop docking guides
		G->Interval = 40;
		G->OnTimer  = g_DockFlat->GuideTick;
		G->Enabled  = true;

		if (!g_MouseHook)
			g_MouseHook = ::SetWindowsHookEx(WH_MOUSE, GuideMouseHook, NULL, ::GetCurrentThreadId());
	}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::FormCreate(TObject *Sender)
{
	Caption = "3DGameEngine-Project1-Document1";

	// Click on the first custom title-bar button ("Dark") toggles the theme
	TitleBarPanel1->CustomButtons->Items[0]->OnClick = TitleBarPanel1CustomButtons0Click;

	// The menu bar (panel + both toolbars) is coloured by this code, not by the .vsf style.
	// "Platform Default" = the same value tbrMenu already has in the DFM: ignore the active VCL style.
	pnlMenuBar->StyleName = "Platform Default";
	tbrMenu->StyleName    = "Platform Default";
	tbrHelp->StyleName    = "Platform Default";
	pnlMenuBar->StyleElements = TStyleElements() << seFont << seBorder;
	tbrMenu->StyleElements    = TStyleElements() << seFont << seBorder;
	tbrHelp->StyleElements    = TStyleElements() << seFont << seBorder;

	// Owner-draw the drop-down menus so they can follow the theme
	MainMenu1->OwnerDraw = true;
	MainMenu2->OwnerDraw = true;
	HookMenuItems(MainMenu1->Items);
	HookMenuItems(MainMenu2->Items);

	// Repaint the frame of the popup menus (needs to exist before the first ApplyTheme call)
	if (!g_MenuFrameHook)
		g_MenuFrameHook = SetWindowsHookExW(WH_CALLWNDPROCRET, MenuFrameHookProc, NULL, GetCurrentThreadId());
	// Anti-flash hooks (see MenuCbtHookProc / MenuPreHookProc)
	if (!g_MenuCbtHook)
		g_MenuCbtHook = SetWindowsHookExW(WH_CBT, MenuCbtHookProc, NULL, GetCurrentThreadId());
	if (!g_MenuPreHook)
		g_MenuPreHook = SetWindowsHookExW(WH_CALLWNDPROC, MenuPreHookProc, NULL, GetCurrentThreadId());

	// Custom drawing of the menu toolbars (used only while the dark theme is active)
	tbrMenu->OnAdvancedCustomDraw       = ToolBarAdvancedCustomDraw;
	tbrMenu->OnAdvancedCustomDrawButton = ToolBarAdvancedCustomDrawButton;
	tbrHelp->OnAdvancedCustomDraw       = ToolBarAdvancedCustomDraw;
	tbrHelp->OnAdvancedCustomDrawButton = ToolBarAdvancedCustomDrawButton;

	// Row of theme-colour swatches below the menu bar
	InitThemeBar();
	InitToolBarVisibilityMenu();

	// Dynamically create the TSearchBox. This way we can avoid having it get in the way during design-time,
	// and it allows us to define the Right Margin for the control so that we can use the Align property.

	//FSearchBox = new TSearchBox(this);
	//FSearchBox->Parent = TitleBarPanel1;
	//FSearchBox->Align = alRight;
	//FSearchBox->AlignWithMargins = true;
	//FSearchBox->Width = 250;

	int W = 0;
	for (int I = 0; I < TitleBarPanel1->CustomButtons->Count; I++)
		W += TitleBarPanel1->CustomButtons->Items[I]->Width;
	//FSearchBox->Margins->Right = W + (CustomTitleBar->CaptionButtonsRect.Right - CustomTitleBar->CaptionButtonsRect.Left) + 8;
	(void)W;	// only used by the commented-out search box code above
	// Dock server + dock windows first (they own the parents of the tree and the property list)
	InitDockLayout();
	// The document area is the centre of the form: the dock zones sit around it
	FSdl = new TViewport3D(this);
	FSdl->Parent = this;
	FSdl->Align  = alClient;
	FSdl->OnSelect = ViewportSelect;
	FSdl->OnSnapToggled = ViewportSnapToggled;		// S key <-> toolbar snap button
	SnapSettingsChanged(NULL);						// give the viewport the snap settings
	FSdl2 = new TViewport2D(this);   // 2D grid view, same parent; only one of FSdl / FSdl2 is visible
	FSdl2->Parent = this;
	FSdl2->Align  = alClient;
	FSdl2->ContextMenu->OwnerDraw = true;		// right-click menu of the 2D node editor follows the theme like the other menus
	HookMenuItems(FSdl2->ContextMenu->Items);
	SetViewMode(FViewMode);          // show the view that matches the toolbar (3D at start)
	InitPropertyPanel();
	InitFileTree();

	// Start with the dark theme (the Dark/Light button in the title bar toggles it)
	ApplyTheme(true);
}
//---------------------------------------------------------------------------
// Property panel (dock window FPropsForm): a two-column list "property / value"
void __fastcall TForm1::InitPropertyPanel()
{
	FProps = new TScenePropertyEditor(this, FSdl);
	FProps->Parent = FPropsForm;
	FProps->Align  = alClient;
	FProps->Options = FProps->Options << goEditing >> goVertLine >> goHorzLine
										>> goFixedVertLine >> goFixedHorzLine;	// values editable; custom cell drawing retained
	FProps->GridLineWidth     = 0;
	FProps->BorderStyle       = bsNone;
	FProps->DefaultRowHeight  = 22;
	FProps->DisplayOptions    = FProps->DisplayOptions << doAutoColResize;
	FProps->DefaultDrawing    = false;
	FProps->OnDrawCell        = PropsDrawCell;
	FProps->TitleCaptions->Clear();
	FProps->TitleCaptions->Add(L"\u5C5E\u6027");		// property
	FProps->TitleCaptions->Add(L"\u503C");			// value
	FProps->ColWidths[0] = 90;
	ApplyPropsTheme();
	ShowProperties();
}
//---------------------------------------------------------------------------
// Property panel colours follow the Dark / Light theme (same approach as the file tree)
void __fastcall TForm1::ApplyPropsTheme()
{
	if (!FProps)
		return;
	FProps->StyleElements = TStyleElements();		// colours below, not the .vsf style
	FProps->Color       = FDark ? Rgb(DARK_BAR_R, DARK_BAR_G, DARK_BAR_B) : clWhite;
	FProps->FixedColor  = FDark ? Rgb(45, 45, 45) : Rgb(240, 240, 240);
	FProps->Font->Color = FDark ? clWhite : clBlack;
	SetControlTheme(FProps, FDark);					// dark scroll bars
	FProps->Invalidate();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::PropsDrawCell(TObject *Sender, int ACol, int ARow,
	const TRect &Rect, TGridDrawState State)
{
	TCanvas *C = FProps->Canvas;
	bool Hdr = (ARow == 0);
	bool Sel = !Hdr && ARow == FProps->Row;

	TColor Bg, Fg;
	if (Hdr)
	{
		Bg = FDark ? Rgb(45, 45, 45) : Rgb(240, 240, 240);
		Fg = FDark ? Rgb(200, 200, 200) : Rgb(80, 80, 80);
	}
	else if (Sel)
	{
		Bg = FDark ? Rgb(9, 71, 113) : Rgb(204, 232, 255);
		Fg = FDark ? clWhite : clBlack;
	}
	else
	{
		Bg = FDark ? Rgb(DARK_BAR_R, DARK_BAR_G, DARK_BAR_B) : clWhite;
		Fg = FDark ? clWhite : clBlack;
		if (ACol == 0)								// property names a bit dimmer
			Fg = FDark ? Rgb(170, 170, 170) : Rgb(90, 90, 90);
	}

	C->Brush->Color = Bg;
	C->FillRect(Rect);

	C->Font->Assign(FProps->Font);
	C->Font->Color  = Fg;
	C->Brush->Style = bsClear;
	TRect T = Rect;
	T.Left += 6;
	T.Right -= 4;
	String S = FProps->Cells[ACol][ARow];
	DrawText(C->Handle, S.c_str(), -1, &T,
			 DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
	C->Brush->Style = bsSolid;

	// thin grid lines
	C->Pen->Color = FDark ? Rgb(60, 60, 60) : Rgb(220, 220, 220);
	C->MoveTo(Rect.Left,      Rect.Bottom - 1);
	C->LineTo(Rect.Right,     Rect.Bottom - 1);
	C->MoveTo(Rect.Right - 1, Rect.Top);
	C->LineTo(Rect.Right - 1, Rect.Bottom);
}
//---------------------------------------------------------------------------
static String F2(float V)
{
	return FloatToStrF((double)V, ffFixed, 7, 2);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ShowProperties()
{
	FProps->Strings->BeginUpdate();
	try
	{
		FProps->Strings->Clear();
		int I = FSdl->Selected;
		if (I < 0 || I >= FSdl->ObjectCount())
		{
			FProps->Strings->Add(L"\u9009\u62E9=\u672A\u9009\u62E9\u7269\u4F53");	// selection = none
		}
		else
		{
			const TSceneObj &O = FSdl->Obj(I);
			FProps->Strings->Add(L"Name=" + O.Name);
			FProps->Strings->Add(L"Type=" + (O.Kind.IsEmpty() ? String(L"Box") : O.Kind));
			FProps->Strings->Add(L"Position X=" + F2(O.Pos.x));
			FProps->Strings->Add(L"Position Y=" + F2(O.Pos.y));
			FProps->Strings->Add(L"Position Z=" + F2(O.Pos.z));
			FProps->Strings->Add(L"Rotation X=" + F2(O.Rot.x * 57.2957795131f));
			FProps->Strings->Add(L"Rotation Y=" + F2(O.Rot.y * 57.2957795131f));
			FProps->Strings->Add(L"Rotation Z=" + F2(O.Rot.z * 57.2957795131f));
			FProps->Strings->Add(L"Length (X)=" + F2(O.Half.x * 2));
			FProps->Strings->Add(L"Width (Y)=" + F2(O.Half.y * 2));
			FProps->Strings->Add(L"Height (Z)=" + F2(O.Half.z * 2));
			FProps->Strings->Add(L"Color R=" + IntToStr(O.R));
			FProps->Strings->Add(L"Color G=" + IntToStr(O.G));
			FProps->Strings->Add(L"Color B=" + IntToStr(O.B));
		}
	}
	__finally
	{
		FProps->Strings->EndUpdate();
	}

	// Nothing selected: the placeholder row must not be editable / keep the keyboard focus,
	// otherwise Q W E R (tool shortcuts) are typed into it.
	bool HasSel = FSdl->Selected >= 0 && FSdl->Selected < FSdl->ObjectCount();
	if (HasSel)
		FProps->Options = FProps->Options << goEditing;
	else
	{
		FProps->EditorMode = false;
		FProps->Options = FProps->Options >> goEditing;
	}
	// Selection came from the viewport: give the keyboard back to the viewport
	if (!FProps->EditorMode && (ActiveControl == FProps || FProps->Focused()))
	{
		if (FSdl->CanFocus())
			FSdl->SetFocus();
		else
			ActiveControl = NULL;
	}
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ViewportSelect(TObject *Sender)
{
	ShowProperties();
}
//---------------------------------------------------------------------------
static bool GetButtonSurface(TObject *Sender, TCanvas *&ACanvas, TRect &ARect)
{
	TRttiContext ctx;
	TRttiType *rt = ctx.GetType(Sender->ClassType());
	if (!rt)
		return false;

	TRttiProperty *pCanvas = rt->GetProperty("Canvas");
	TRttiProperty *pRect   = rt->GetProperty("ClientRect");
	if (!pCanvas || !pRect)
		return false;

	ACanvas = static_cast<TCanvas*>(pCanvas->GetValue(Sender).AsObject());
	TValue v = pRect->GetValue(Sender);
	ARect = *static_cast<TRect*>(v.GetReferenceToRawData());
	return ACanvas != nullptr;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TitleBarPanel1CustomButtons0Paint(TObject *Sender)
{
	// This method "paints" the first Custom Button by drawing the appropriate image
	// from the image list
	int Index;
	if (Active)
		Index = 0;
	else
		Index = 1;

	// The important piece here is that the Sender is actually an instance of
	// TSystemTitleBarButton and has important properties-namely, ClientRect and Canvas.
	TCanvas *Cvs; TRect CR;
	if (!GetButtonSurface(Sender, Cvs, CR))
		return;

	int X = CR.Left + ((CR.Right - CR.Left) - 16) / 2;
	int Y = CR.Top + ((CR.Bottom - CR.Top) - 16) / 2;
	ImageList->Draw(Cvs, X, Y, Index);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TitleBarPanel1CustomButtons1Paint(TObject *Sender)
{
	// This method "paints" the second Custom Button by drawing the appropriate image
	// from the image list
	int Index;
	if (Active)
		Index = 2;
	else
		Index = 3;

	// The important piece here is that the Sender is actually an instance of
	// TSystemTitleBarButton and has important properties-namely, ClientRect and Canvas.
	TCanvas *Cvs; TRect CR;
	if (!GetButtonSurface(Sender, Cvs, CR))
		return;

	int X = CR.Left + ((CR.Right - CR.Left) - 16) / 2;
	int Y = CR.Top + ((CR.Bottom - CR.Top) - 16) / 2;
	ImageList->Draw(Cvs, X, Y, Index);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::TitleBarPanel1Paint(TObject *Sender, TCanvas *Canvas, TRect &ARect)
{
	// Delphi: AlignStyles: array [TAlignment] of TTextFormats = (tfLeft, tfRight, tfCenter);
	static const TTextFormats AlignStyles[3] = { tfLeft, tfRight, tfCenter };

	// Since our title bar has many more items on it, displaying the caption directly in the center is not
	// the best approach. Instead, we'll draw the form caption precisely where we want

	TStyleTextOptions TextOptions;
	TextOptions.Flags.Clear();
	TextOptions.Flags << stfTextColor;
	if (Active)
		TextOptions.TextColor = CustomTitleBar->ForegroundColor;
	else
		TextOptions.TextColor = CustomTitleBar->InactiveForegroundColor;

	// Calculate R to be the rectangle that is between the right-most edge of the toolbar (tbrMain) and
	// the left edge of the search box (FSearchBox).
	// Fill the panel with the title bar background so it follows the light/dark theme
	Canvas->Brush->Color = Active ? CustomTitleBar->BackgroundColor
								  : CustomTitleBar->InactiveBackgroundColor;
	Canvas->FillRect(ARect);

	TRect R = ARect;
	//R.Left = tbrMain->Left + tbrMain->Width;
	//R.Right = FSearchBox->Left;
	int iconSize = 16; // 设定系统标准标题栏图标尺寸
	int iconX = 3;    // 左侧边距
	int iconY = R.Top + (R.Height() - iconSize) / 2;
	if (Icon && !Icon->Empty) {
		::DrawIconEx(Canvas->Handle, iconX, iconY, Icon->Handle, iconSize, iconSize, 0, NULL, DI_NORMAL);
	}
	R.Left = iconX + iconSize + iconX;
	TTextFormat TextFormat;
	TextFormat << tfSingleLine << tfVerticalCenter << tfEndEllipsis << tfComposited;
	TextFormat << tfLeft;
	//居中TextFormat << AlignStyles[static_cast<int>(taCenter)];

	// Use the TStyleManager (and thus Style Services) to render the text appropriately on the
	// title bar surface
	TStyleManager::SystemStyle->DrawText(Canvas->Handle,
										 TStyleManager::SystemStyle->GetElementDetails(twCaptionActive),
										 Caption, R, TextFormat, TextOptions);
}
//---------------------------------------------------------------------------



void __fastcall TForm1::AboutClick(TObject *Sender)
{
	std::unique_ptr<TForm3> Dlg(new TForm3(this));
	Dlg->ApplyTheme(FDark);
	Dlg->ShowModal();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::N9Click(TObject *Sender)
{
std::unique_ptr<TForm2> Dlg(new TForm2(this));
	Dlg->ApplyTheme(FDark);
	if (Dlg->ShowModal() == mrOk)
	{
		// 用户点了"确定"，在这里读取设置，例如：
		// bool X = Dlg->chkSomething->Checked;
	}
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// ToolBar3 (2D / 3D) and ToolBar4 (snap toggle). Both are designed in the .dfm like ToolBar1/2;
// their buttons are set up in InitThemeBar().
//---------------------------------------------------------------------------
// "Grid and Snap Settings" window (like the one in 3ds Max): three pages, changes apply at once.
class TSnapSettingsForm : public TForm
{
public:
	__fastcall TSnapSettingsForm(TComponent *AOwner, TSnapSettings *S, TNotifyEvent AOnChanged);
	void __fastcall RefreshFromSettings();
	void __fastcall ApplyTheme(bool ADark);			// dark: theme bar colour + white text
private:
	TPanel *FStrip, *FPage[3];						// own tab strip + pages (fully themeable)
	TLabel *FTab[3];
	int     FPageIdx;
	void __fastcall SelectPage(int I);
	void __fastcall TabClick(TObject *Sender);
	void __fastcall CheckLabelClick(TObject *Sender);
	TSnapSettings *FS;
	TNotifyEvent   FOnChanged;
	bool           FLoading;
	TCheckBox *cbEnable, *cbGrid, *cbVertex, *cbEdge, *cbMid, *cbCenter, *cbBBox, *cbAxis, *cbMarker;
	TEdit     *edRadius, *edAngle, *edPercent, *edGrid, *edMajor;
	TButton   *btnClose;
	TCheckBox* __fastcall AddCheck(TWinControl *P, const String &Cap, int X, int Y, int W);
	TEdit*     __fastcall AddEdit(TWinControl *P, const String &Cap, int Y);
	void __fastcall ControlChanged(TObject *Sender);
	void __fastcall CloseClick(TObject *Sender);
	void __fastcall FormCloseEvent(TObject *Sender, TCloseAction &Action);
};
//---------------------------------------------------------------------------
__fastcall TSnapSettingsForm::TSnapSettingsForm(TComponent *AOwner, TSnapSettings *S, TNotifyEvent AOnChanged)
	: TForm(AOwner, 1), FS(S), FOnChanged(AOnChanged), FLoading(true)
{
	Caption     = L"\u6805\u683C\u548C\u6355\u6349\u8BBE\u7F6E";
	BorderStyle = bsToolWindow;
	Position    = poDesigned;
	ClientWidth  = 340;
	ClientHeight = 372;
	OnClose     = FormCloseEvent;

	cbEnable = AddCheck(this, L"\u542F\u7528\u6355\u6349", 12, 10, 200);

	// own tab strip + three pages instead of TPageControl (a native tab control cannot be darkened)
	static const wchar_t *const TabCaps[3] = { L"\u6355\u6349", L"\u9009\u9879", L"\u4E3B\u6805\u683C" };
	FStrip = new TPanel(this);
	FStrip->Parent = this;
	FStrip->SetBounds(8, 38, 324, 26);
	FStrip->BevelOuter = bvNone;
	FStrip->ShowCaption = false;
	FStrip->ParentBackground = false;
	for (int i = 0; i < 3; i++)
	{
		FTab[i] = new TLabel(this);
		FTab[i]->Parent = FStrip;
		FTab[i]->AutoSize = false;
		FTab[i]->SetBounds(i * 90, 0, 88, 26);
		FTab[i]->Alignment = taCenter;
		FTab[i]->Layout = tlCenter;
		FTab[i]->Caption = TabCaps[i];
		FTab[i]->Tag = i;
		FTab[i]->Cursor = crHandPoint;
		FTab[i]->OnClick = TabClick;
		FPage[i] = new TPanel(this);
		FPage[i]->Parent = this;
		FPage[i]->SetBounds(8, 64, 324, 254);
		FPage[i]->BevelOuter = bvNone;
		FPage[i]->ShowCaption = false;
		FPage[i]->ParentBackground = false;
	}
	FPageIdx = 0;

	// ---- page 1: snap types ----
	TWinControl *tsSnap = FPage[0];
	cbGrid   = AddCheck(tsSnap, L"\u6805\u683C\u70B9",     16, 14,  200);
	cbVertex = AddCheck(tsSnap, L"\u9876\u70B9",   16, 44,  200);
	cbEdge   = AddCheck(tsSnap, L"\u8FB9",     16, 74,  200);
	cbMid    = AddCheck(tsSnap, L"\u4E2D\u70B9",      16, 104, 200);
	cbCenter = AddCheck(tsSnap, L"\u4E2D\u5FC3",   16, 134, 200);
	cbBBox   = AddCheck(tsSnap, L"\u5305\u56F4\u76D2",     16, 164, 200);

	// ---- page 2: options ----
	TWinControl *tsOpt = FPage[1];
	edRadius  = AddEdit(tsOpt, L"\u6355\u6349\u534A\u5F84(\u50CF\u7D20)",  16);
	edAngle   = AddEdit(tsOpt, L"\u89D2\u5EA6\u6355\u6349(\u5EA6)",   50);
	edPercent = AddEdit(tsOpt, L"\u767E\u5206\u6BD4\u6355\u6349(%)", 84);
	edRadius->NumbersOnly = true;
	cbAxis   = AddCheck(tsOpt, L"\u542F\u7528\u8F74\u7EA6\u675F",   16, 130, 280);
	cbMarker = AddCheck(tsOpt, L"\u663E\u793A\u6355\u6349\u6807\u8BB0", 16, 160, 280);

	// ---- page 3: home grid ----
	TWinControl *tsGrid = FPage[2];
	edGrid  = AddEdit(tsGrid, L"\u6805\u683C\u95F4\u8DDD", 16);
	edMajor = AddEdit(tsGrid, L"\u4E3B\u7EBF\u95F4\u9694(\u6BCF N \u6761)",   50);
	edMajor->NumbersOnly = true;

	btnClose = new TButton(this);
	btnClose->Parent = this;
	btnClose->SetBounds(ClientWidth - 92, ClientHeight - 40, 80, 28);
	btnClose->Caption = L"\u5173\u95ED";
	btnClose->OnClick = CloseClick;

	SelectPage(0);
	ApplyTheme(true);
	RefreshFromSettings();
}
//---------------------------------------------------------------------------
void __fastcall TSnapSettingsForm::SelectPage(int I)
{
	FPageIdx = I;
	for (int k = 0; k < 3; k++)
	{
		FPage[k]->Visible = (k == I);
		FTab[k]->Font->Style = (k == I) ? (TFontStyles() << fsBold) : TFontStyles();
	}
	for (int k = 0; k < 3; k++)		// selected tab = page colour, the others = a bit lighter / darker
		FTab[k]->Color = (k == I) ? FPage[k]->Color : FStrip->Color;
}
//---------------------------------------------------------------------------
void __fastcall TSnapSettingsForm::TabClick(TObject *Sender)
{
	SelectPage(static_cast<TControl*>(Sender)->Tag);
}
//---------------------------------------------------------------------------
// Dark: the neutral theme-bar colour (same as the property panel / command window) with white text.
void __fastcall TSnapSettingsForm::ApplyTheme(bool ADark)
{
	const TColor Bg    = ADark ? Rgb(DARK_BAR_R, DARK_BAR_G, DARK_BAR_B) : clWhite;
	const TColor Strip = ADark ? Rgb(45, 45, 45) : Rgb(240, 240, 240);
	const TColor Edit  = ADark ? Rgb(45, 45, 45) : clWhite;
	const TColor Fg    = ADark ? clWhite : clBlack;

	// dark title bar of the tool window
	if (HandleAllocated())
	{
		BOOL Dark = ADark ? TRUE : FALSE;
		HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
		if (hDwm)
		{
			typedef HRESULT (WINAPI *TFn)(HWND, DWORD, LPCVOID, DWORD);
			TFn pSet = reinterpret_cast<TFn>(GetProcAddress(hDwm, "DwmSetWindowAttribute"));
			if (pSet)
				pSet(Handle, 20, &Dark, sizeof(Dark));		// DWMWA_USE_IMMERSIVE_DARK_MODE
		}
	}

	StyleElements = TStyleElements();			// our colours, not the .vsf style
	Color = Bg;
	Font->Color = Fg;
	FStrip->StyleElements = TStyleElements();
	FStrip->Color = Strip;
	for (int i = 0; i < 3; i++)
	{
		FPage[i]->StyleElements = TStyleElements();
		FPage[i]->Color = Bg;
		FTab[i]->StyleElements = TStyleElements();
		FTab[i]->Font->Color = Fg;
		FTab[i]->Transparent = false;
	}
	for (int i = 0; i < ComponentCount; i++)
	{
		TComponent *C = Components[i];
		if (TCheckBox *Cb = dynamic_cast<TCheckBox*>(C))
		{
			Cb->StyleElements = TStyleElements();
			Cb->Color = Bg;
			Cb->Font->Color = Fg;
		}
		else if (TEdit *Ed = dynamic_cast<TEdit*>(C))
		{
			Ed->StyleElements = TStyleElements();
			Ed->Color = Edit;
			Ed->Font->Color = Fg;
		}
		else if (TLabel *Lb = dynamic_cast<TLabel*>(C))
		{
			Lb->StyleElements = TStyleElements();
			Lb->Font->Color = Fg;
		}
		else if (TButton *Bt = dynamic_cast<TButton*>(C))
		{
			if (Bt->HandleAllocated() || Bt->Parent)
				SetControlTheme(Bt, ADark);			// dark button
		}
	}
	SelectPage(FPageIdx);
	Invalidate();
}
//---------------------------------------------------------------------------
static void ApplySnapFormTheme(TForm *F, bool ADark)
{
	static_cast<TSnapSettingsForm*>(F)->ApplyTheme(ADark);
}
//---------------------------------------------------------------------------
TCheckBox* __fastcall TSnapSettingsForm::AddCheck(TWinControl *P, const String &Cap, int X, int Y, int W)
{
	// A themed native check box always draws its text in the system colour (black), so the box has no
	// caption and a separate TLabel (white in the dark theme) carries the text.
	TCheckBox *C = new TCheckBox(this);
	C->Parent = P;
	C->SetBounds(X, Y, 20, 22);
	C->Caption = L"";
	C->OnClick = ControlChanged;
	TLabel *L = new TLabel(this);
	L->Parent  = P;
	L->Left    = X + 22;
	L->Top     = Y + 3;
	L->Caption = Cap;
	L->Tag     = reinterpret_cast<NativeInt>(C);
	L->OnClick = CheckLabelClick;
	return C;
}
//---------------------------------------------------------------------------
void __fastcall TSnapSettingsForm::CheckLabelClick(TObject *Sender)
{
	TCheckBox *C = reinterpret_cast<TCheckBox*>(static_cast<TLabel*>(Sender)->Tag);
	if (C)
	{
		C->Checked = !C->Checked;
		ControlChanged(C);		// (harmless if setting Checked already fired OnClick)
	}
}
//---------------------------------------------------------------------------
TEdit* __fastcall TSnapSettingsForm::AddEdit(TWinControl *P, const String &Cap, int Y)
{
	TLabel *L = new TLabel(this);
	L->Parent  = P;
	L->Left    = 16;
	L->Top     = Y + 4;
	L->Caption = Cap;
	TEdit *E = new TEdit(this);
	E->Parent = P;
	E->SetBounds(200, Y, 90, 24);
	E->OnChange = ControlChanged;
	return E;
}
//---------------------------------------------------------------------------
void __fastcall TSnapSettingsForm::RefreshFromSettings()
{
	FLoading = true;
	cbEnable->Checked = FS->Enabled;
	cbGrid->Checked   = FS->Grid;
	cbVertex->Checked = FS->Vertex;
	cbEdge->Checked   = FS->Edge;
	cbMid->Checked    = FS->Midpoint;
	cbCenter->Checked = FS->Center;
	cbBBox->Checked   = FS->BBox;
	cbAxis->Checked   = FS->AxisConstraint;
	cbMarker->Checked = FS->ShowMarker;
	edRadius->Text    = IntToStr(FS->Radius);
	edAngle->Text     = FloatToStr(FS->AngleDeg);
	edPercent->Text   = FloatToStr(FS->Percent);
	edGrid->Text      = FloatToStr(FS->GridSpacing);
	edMajor->Text     = IntToStr(FS->MajorEvery);
	FLoading = false;
}
//---------------------------------------------------------------------------
void __fastcall TSnapSettingsForm::ControlChanged(TObject *Sender)
{
	if (FLoading)
		return;
	FS->Enabled        = cbEnable->Checked;
	FS->Grid           = cbGrid->Checked;
	FS->Vertex         = cbVertex->Checked;
	FS->Edge           = cbEdge->Checked;
	FS->Midpoint       = cbMid->Checked;
	FS->Center         = cbCenter->Checked;
	FS->BBox           = cbBBox->Checked;
	FS->AxisConstraint = cbAxis->Checked;
	FS->ShowMarker     = cbMarker->Checked;
	int n;
	double v;
	if (TryStrToInt(edRadius->Text, n) && n > 0)    FS->Radius = n;
	if (TryStrToFloat(edAngle->Text, v) && v > 0)   FS->AngleDeg = v;
	if (TryStrToFloat(edPercent->Text, v) && v > 0) FS->Percent = v;
	if (TryStrToFloat(edGrid->Text, v) && v > 0)    FS->GridSpacing = v;
	if (TryStrToInt(edMajor->Text, n) && n > 0)     FS->MajorEvery = n;
	if (FOnChanged)
		FOnChanged(this);
}
//---------------------------------------------------------------------------
void __fastcall TSnapSettingsForm::CloseClick(TObject *Sender)
{
	Close();
}
//---------------------------------------------------------------------------
void __fastcall TSnapSettingsForm::FormCloseEvent(TObject *Sender, TCloseAction &Action)
{
	Action = caHide;		// keep the window (owned by Form1) and reuse it next time
}
//---------------------------------------------------------------------------
void __fastcall TForm1::SetViewMode(int AMode)
{
	FViewMode = AMode;			// 0 = 2D, 1 = 3D
	// show the new view first, then hide the old one (no flash of the empty panel)
	if (AMode == 0)
	{
		if (FSdl2) FSdl2->Visible = true;
		if (FSdl)  FSdl->Visible  = false;
	}
	else
	{
		if (FSdl)  FSdl->Visible  = true;
		if (FSdl2) FSdl2->Visible = false;
	}
	ToolBar3->Invalidate();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::StartClick(TObject *Sender)
{
	// "Start": open (or bring to front) the render window; it evaluates the node graph of the 2D view every frame
	if (FSdl2)
		ShowStartWindow(this, FSdl2, false);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::RunClick(TObject *Sender)
{
	// "Run": restart the animation from the beginning (opens the render window first if it is not open)
	if (FSdl2)
		ShowRunWindow(this, FSdl2, true);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ModeButtonClick(TObject *Sender)
{
	if (Sender == ToolButton9)
		SetViewMode(0);
	else if (Sender == ToolButton11)
		SetViewMode(1);
}
//---------------------------------------------------------------------------
void __fastcall TForm1::SnapButtonMouseDown(TObject *Sender, TMouseButton Button, TShiftState Shift, int X, int Y)
{
	if (Button != mbLeft)
		return;
	FSnapLongPress = false;
	FSnapHoldTimer->Enabled = false;		// (re)start the long-press timer
	FSnapHoldTimer->Enabled = true;
}
//---------------------------------------------------------------------------
void __fastcall TForm1::SnapHoldTimer(TObject *Sender)
{
	FSnapHoldTimer->Enabled = false;
	// still held down, and the cursor is still on the button?
	if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
		return;
	TPoint P = ToolBar4->ScreenToClient(Mouse->CursorPos);
	TRect BR = ToolButton13->BoundsRect;
	if (!BR.Contains(P))
		return;
	FSnapLongPress = true;			// the click that follows the release must not toggle
	ShowSnapSettings();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::SnapButtonClick(TObject *Sender)
{
	FSnapHoldTimer->Enabled = false;
	if (FSnapLongPress)				// release of a long press: only opened the settings
	{
		FSnapLongPress = false;
		return;
	}
	FSnap.Enabled = !FSnap.Enabled;
	SnapSettingsChanged(NULL);
	if (FSnapForm)
		static_cast<TSnapSettingsForm*>(FSnapForm)->RefreshFromSettings();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::SnapSettingsChanged(TObject *Sender)
{
	// the settings window changed something: refresh the button state
	if (FSdl)
	{
		TViewport3D::TViewSnap V;
		V.Enabled = FSnap.Enabled;   V.Grid = FSnap.Grid;       V.Vertex = FSnap.Vertex;
		V.Edge = FSnap.Edge;         V.Midpoint = FSnap.Midpoint; V.Center = FSnap.Center;
		V.BBox = FSnap.BBox;         V.ShowMarker = FSnap.ShowMarker;
		V.Radius = FSnap.Radius;     V.MajorEvery = FSnap.MajorEvery;
		V.AngleDeg = FSnap.AngleDeg; V.Percent = FSnap.Percent; V.GridSpacing = FSnap.GridSpacing;
		FSdl->SetSnapSettings(V);
	}
	ToolBar4->Invalidate();
}
//---------------------------------------------------------------------------
// The S key in the viewport switched snap on / off: the toolbar button and the settings window follow
void __fastcall TForm1::ViewportSnapToggled(TObject *Sender)
{
	FSnap.Enabled = FSdl->SnapEnabled;
	if (FSnapForm)
		static_cast<TSnapSettingsForm*>(FSnapForm)->RefreshFromSettings();
	ToolBar4->Invalidate();
}
//---------------------------------------------------------------------------
void __fastcall TForm1::ShowSnapSettings()
{
	if (!FSnapForm)
	{
		FSnapForm = new TSnapSettingsForm(this, &FSnap, SnapSettingsChanged);
		FSnapForm->PopupParent = this;
	}
	static_cast<TSnapSettingsForm*>(FSnapForm)->RefreshFromSettings();

	// open just below the snap button, kept inside the monitor
	TPoint P = ToolBar4->ClientToScreen(TPoint(ToolButton13->Left, ToolBar4->Height + 2));
	TRect WA = Screen->MonitorFromPoint(P)->WorkareaRect;
	if (P.x + FSnapForm->Width  > WA.Right)  P.x = WA.Right  - FSnapForm->Width;
	if (P.y + FSnapForm->Height > WA.Bottom) P.y = WA.Bottom - FSnapForm->Height;
	FSnapForm->Left = P.x;
	FSnapForm->Top  = P.y;
	static_cast<TSnapSettingsForm*>(FSnapForm)->ApplyTheme(FDark);
	FSnapForm->Show();
}
//---------------------------------------------------------------------------
// Flat button drawing for ToolBar3 / ToolBar4 (same look as the colour swatches): text buttons
// "2D" / "3D" with the active one highlighted, and the snap button (highlighted while snap is on).
void __fastcall TForm1::ModeBarAdvancedCustomDrawButton(TToolBar *Sender, TToolButton *Button,
	TCustomDrawState State, TCustomDrawStage Stage, TTBCustomDrawFlags &Flags, bool &DefaultDraw)
{
	if (Stage != cdPrePaint)
		return;

	TCanvas *C = Sender->Canvas;
	TRect R = Button->BoundsRect;
	const TColor Bar    = ThemeBarColor(FDark);
	const TColor Fg     = FDark ? clWhite : clBlack;
	const TColor Accent = static_cast<TColor>(THEME_PRESETS[g_ThemeIndex].Accent);

	const bool Down = State.Contains(cdsSelected) || State.Contains(cdsChecked);
	const bool Hot  = State.Contains(cdsHot);
	bool Active = false;
	if (Button == ToolButton9)        Active = (FViewMode == 0);
	else if (Button == ToolButton11)   Active = (FViewMode == 1);
	else if (Button == ToolButton13) Active = FSnap.Enabled;

	C->Brush->Style = bsSolid;
	C->Brush->Color = Bar;
	C->FillRect(R);
	if (Down || Hot || Active)
	{
		C->Brush->Color = Active ? MixColor(Bar, Accent, Down ? 55 : 42) : MixColor(Bar, Fg, Down ? 28 : 18);
		C->Pen->Style   = psSolid;
		C->Pen->Width   = 1;
		C->Pen->Color   = Active ? MixColor(Bar, Accent, 85) : MixColor(Bar, Fg, 35);
		TRect F = R;
		F.Inflate(-1, -1);
		C->Rectangle(F.Left, F.Top, F.Right, F.Bottom);
	}

	if (Button == ToolButton13)
	{
		int X = R.Left + (R.Width()  - FToolBarImages->Width)  / 2;
		int Y = R.Top  + (R.Height() - FToolBarImages->Height) / 2;
		if (Down) { X++; Y++; }
		FToolBarImages->Draw(C, X, Y, Button->ImageIndex, true);
	}
	else
	{
		TRect T = R;
		if (Down) T.Offset(1, 1);
		C->Font->Assign(Sender->Font);
		C->Font->Color = Fg;
		C->Font->Style = Active ? (TFontStyles() << fsBold) : TFontStyles();
		C->Brush->Style = bsClear;
		DrawText(C->Handle, Button->Caption.c_str(), -1, &T, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
		C->Brush->Style = bsSolid;
	}
	DefaultDraw = false;		// drawn here
}
//---------------------------------------------------------------------------
