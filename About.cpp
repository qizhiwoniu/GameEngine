//---------------------------------------------------------------------------

#include <vcl.h>
#pragma hdrstop

#include "About.h"
//---------------------------------------------------------------------------
#pragma package(smart_init)
#pragma resource "*.dfm"
TForm3 *Form3;
//---------------------------------------------------------------------------
// ==== Edit the program info here (no extra version.h needed) ====
static const wchar_t *APP_NAME      = L"游戏引擎";			// 我的程序
static const wchar_t *APP_VERSION   = L"1.0.0.0";
static const wchar_t *APP_COPYRIGHT = L"\u00A9 2026 七只蜗牛";	// (c) 2026 你的公司
//---------------------------------------------------------------------------
// Same colours as the Settings window (Setting.cpp)
static const TColor DARK_BG  = static_cast<TColor>(RGB(32, 32, 32));
static const TColor LIGHT_BG = static_cast<TColor>(RGB(243, 243, 243));
//---------------------------------------------------------------------------
typedef HRESULT (WINAPI *TDwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
//---------------------------------------------------------------------------
__fastcall TForm3::TForm3(TComponent* Owner)
	: TForm(Owner), FDark(false)
{
	Caption            = L"\u5173\u4E8E";					// 关于
	lblName->Caption   = APP_NAME;
	lblVersion->Caption   = String(L"\u7248\u672C ") + APP_VERSION;	// 版本 x.x.x
	lblCopyright->Caption = APP_COPYRIGHT;
	btnOk->Caption     = L"\u786E\u5B9A";					// 确定
}
//---------------------------------------------------------------------------
void __fastcall TForm3::CreateWnd()
{
	TForm::CreateWnd();
	ApplyTitleBar();
}
//---------------------------------------------------------------------------
void __fastcall TForm3::ApplyTitleBar()
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
	if (FAILED(pSet(Handle, 20, &V, sizeof(V))))	// DWMWA_USE_IMMERSIVE_DARK_MODE
		pSet(Handle, 19, &V, sizeof(V));			// same attribute on Windows 10 1809 / 1903
}
//---------------------------------------------------------------------------
void __fastcall TForm3::ApplyTheme(bool ADark)
{
	FDark = ADark;

	// The form and the labels are coloured here; the button keeps the global .vsf style.
	StyleElements = TStyleElements();
	const TColor Fg = ADark ? clWhite : clBlack;
	Color       = ADark ? DARK_BG : LIGHT_BG;
	Font->Color = Fg;
	lblName->StyleElements      = TStyleElements();
	lblVersion->StyleElements   = TStyleElements();
	lblCopyright->StyleElements = TStyleElements();
	lblName->Font->Color      = Fg;
	lblVersion->Font->Color   = Fg;
	lblCopyright->Font->Color = Fg;

	HandleNeeded();
	ApplyTitleBar();
	Invalidate();
}
//---------------------------------------------------------------------------
