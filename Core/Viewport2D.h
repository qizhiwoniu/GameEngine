//---------------------------------------------------------------------------
// TViewport2D - 2D node-graph viewport (SDL3 rendering)
//
// Header-only, same pattern as Viewport3D.h: #include "Viewport2D.h" in Unit1.h.
// Shows a plain grid (pannable / zoomable) and a node editor on top of it, in the
// style of the Fusion / Unreal node editors:
//   - middle mouse button drag : pan the grid
//   - mouse wheel              : zoom (around the cursor)
//   - right click              : menu  Create -> Box / Plane / Time / Keyboard / Accumulate / Run output
//                                      Edit   -> Copy / Paste / Rename / Group -> Group, Ungroup
//     Group = a titled frame around 2+ nodes; drag its title bar to move all members (Ctrl+G / Ctrl+Shift+G)
//   - left drag on a node      : move it (Ctrl+click = multi select, drag on empty = rubber band)
//   - left drag from a pin     : create a link (output pin on the right -> input pin on the left).
//                                Pins have a type (number / object): only equal types connect.
//                                Dragging an already connected input pin picks the link up again,
//                                dropping it on empty space deletes it
//   - Delete / Ctrl+C / Ctrl+V / F2 : delete / copy / paste / rename
//   - Z / F / X                : go to the selected node / fit all nodes into the window / zoom to 100%
//   - double click a node header : fold / unfold the node;  double click the body of a Value node: edit its number
//   - double click a group title bar : fold / unfold all nodes of the group
//   - Run: boxes collide with each other (see ResolveCollisions)
//   - Box / Plane nodes are linked with the objects of the 3D view (see SceneLink.h)
//
// The graph is a small data-flow program:
//   Time / Keyboard / Accumulate produce numbers, Box / Plane turn numbers into scene objects,
//   "Run output" collects the objects of the scene. The toolbar "Run" button opens the render
//   window (RunWindow.h) which calls RunStep() every frame to evaluate the graph.
//
// Switching between 2D and 3D = showing one viewport and hiding the other
// (see TForm1::SetViewMode); every frame is cleared by TSDLPanel::RenderFrame.
//---------------------------------------------------------------------------
#ifndef Viewport2DH
#define Viewport2DH
#pragma message("Viewport2D.h rev 1525 compiled from: " __FILE__)		// shows in the IDE Messages window: proves which copy of the header the project really uses
//---------------------------------------------------------------------------
#include "SDLPanel.h"
#include <Vcl.Forms.hpp>
#include <Vcl.Menus.hpp>
#include <Vcl.Dialogs.hpp>
#include <Vcl.Graphics.hpp>
#include <Vcl.StdCtrls.hpp>
#include <memory>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <System.SysUtils.hpp>
#include <System.Classes.hpp>
#include <math.h>
#include <stdio.h>
//---------------------------------------------------------------------------
// Node kinds and their pins
//---------------------------------------------------------------------------
enum { nkBox, nkPlane, nkTime, nkKey, nkAccum, nkSub, nkValue, nkGravity, nkAdd, nkOutput, nkKeys,nkMouse,nkCamera, NK_COUNT };
enum { ptNumber = 0, ptObject = 1 };			// pin types: only equal types can be linked

struct TPinDef  { const wchar_t *Name; int Type; float Def; };		// Def = value of an unconnected number input
struct TKindDef { const wchar_t *Name; int R, G, B; int NIn; TPinDef In[5]; int NOut; TPinDef Out[8]; };

// ---- node kinds 键盘 / 鼠标 / 镜头: each has its own definition function (used by KindDef below) ----
inline const TKindDef &KindDefKeys()
{
	// nkKeys: 键盘按键（按下输出 1，反向输出 -1）
	static const TKindDef D =
		{ L"\u952E\u76D8", 40, 140, 100,
		  0, { },
		  2, { { L"\u6309\u4E0B", ptNumber, 0.0f }, { L"\u53CD\u5411", ptNumber, 0.0f } } };		// 按下 / 反向
	return D;
}

inline const TKindDef &KindDefMouse()
{
	// nkMouse: 输出鼠标 X / Y（运行窗口中，窗口中心为 0，范围 -1..1，右 / 上为正）；控制模式显示在节点主体，由 MouseMode() 读取
	static const TKindDef D =
		{ L"\u9F20\u6807", 150, 90, 110,
		  0, { },
		  2, { { L"X", ptNumber, 0.0f }, { L"Y", ptNumber, 0.0f } } };
	return D;
}

inline const TKindDef &KindDefCamera()
{
	// nkCamera: 镜头位置输入 XYZ
	static const TKindDef D =
		{ L"\u955C\u5934", 140, 110, 60,
		  3, { { L"\u4F4D\u7F6EX", ptNumber, 0.0f }, { L"\u4F4D\u7F6EY", ptNumber, 4.5f }, { L"\u4F4D\u7F6EZ", ptNumber, -11.0f } },
		  0, { } };
	return D;
}

inline const TKindDef &KindDef(int K)
{
	if (K == nkKeys)   return KindDefKeys();
	if (K == nkMouse)  return KindDefMouse();
	if (K == nkCamera) return KindDefCamera();
	static const TKindDef D[] =			// size is checked below: a missing entry would be a zeroed (blank, pin-less) node kind
	{
		// nkBox: 方块
		{ L"\u65B9\u5757", 56, 120, 170,
		  5, { { L"\u4F4D\u7F6EX", ptNumber, 0.0f }, { L"\u4F4D\u7F6EY", ptNumber, 0.5f }, { L"\u4F4D\u7F6EZ", ptNumber, 0.0f },
			   { L"\u65CB\u8F6C", ptNumber, 0.0f },  { L"\u7F29\u653E", ptNumber, 1.0f } },
		  1, { { L"\u7269\u4F53", ptObject, 0.0f } } },
		// nkPlane: 平面
		{ L"\u5E73\u9762", 170, 110, 50,
		  5, { { L"\u4F4D\u7F6EX", ptNumber, 0.0f }, { L"\u4F4D\u7F6EY", ptNumber, 0.0f }, { L"\u4F4D\u7F6EZ", ptNumber, 0.0f },
			   { L"\u65CB\u8F6C", ptNumber, 0.0f },  { L"\u7F29\u653E", ptNumber, 1.0f } },
		  1, { { L"\u7269\u4F53", ptObject, 0.0f } } },
		// nkTime: 时间
		{ L"\u65F6\u95F4", 130, 80, 170,
		  0, { },
		  4, { { L"\u65F6\u95F4", ptNumber, 0.0f }, { L"\u6B63\u5F26", ptNumber, 0.0f }, { L"\u4F59\u5F26", ptNumber, 0.0f },
			   { L"\u89D2\u5EA6", ptNumber, 0.0f } } },
		// nkKey: 键盘输入
		{ L"\u952E\u76D8\u8F93\u5165", 40, 140, 130,
		  0, { },
		  3, { { L"\u6C34\u5E73", ptNumber, 0.0f }, { L"\u5782\u76F4", ptNumber, 0.0f }, { L"\u7A7A\u683C", ptNumber, 0.0f } } },
		// nkAccum: 累加 (integrates  value * speed  over time)
		{ L"\u7D2F\u52A0", 110, 130, 60,
		  2, { { L"\u6570\u503C", ptNumber, 0.0f }, { L"\u901F\u5EA6", ptNumber, 3.0f } },
		  1, { { L"\u7D2F\u8BA1", ptNumber, 0.0f } } },
		// nkSub: 累减 (the opposite of 累加: integrates  -value * speed  over time)
		{ L"\u7D2F\u51CF", 150, 100, 60,
		  2, { { L"\u6570\u503C", ptNumber, 0.0f }, { L"\u901F\u5EA6", ptNumber, 3.0f } },
		  1, { { L"\u7D2F\u8BA1", ptNumber, 0.0f } } },
		// nkValue: 数值 (a constant; double click the body to edit it)
		{ L"\u6570\u503C", 150, 120, 60,
		  0, { },
		  1, { { L"\u6570\u503C", ptNumber, 0.0f } } },
		// nkGravity: 重力 (simulated height: a trigger sets the vertical speed, gravity pulls it down, it lands on the ground
		// height or on top of a box (collision feeds the resolved height back))
		{ L"\u91CD\u529B", 60, 110, 160,
		  4, { { L"\u89E6\u53D1", ptNumber, 0.0f }, { L"\u8D77\u8DF3\u901F\u5EA6", ptNumber, 8.0f },
			   { L"\u91CD\u529B", ptNumber, 20.0f }, { L"\u5730\u9762\u9AD8\u5EA6", ptNumber, 0.5f } },
		  1, { { L"\u9AD8\u5EA6", ptNumber, 0.0f } } },
		// nkAdd: 相加 (A + B; feed it a key's "按下" and another key's "反向" to get a -1 .. 1 axis)
		{ L"\u76F8\u52A0", 90, 140, 150,
		  2, { { L"A", ptNumber, 0.0f }, { L"B", ptNumber, 0.0f } },
		  1, { { L"\u8F93\u51FA", ptNumber, 0.0f } } },
		// nkOutput: 运行输出
		{ L"\u8FD0\u884C\u8F93\u51FA", 175, 60, 60,
		  1, { { L"\u573A\u666F", ptObject, 0.0f } },
		  0, { } },
	};
	static_assert(sizeof(D) / sizeof(D[0]) == nkKeys, "KindDef: one table entry per node kind nkBox..nkOutput");
	return D[(K >= 0 && K < nkKeys) ? K : 0];
}
// display name of a node kind; falls back to a literal if the table entry is ever empty
inline String KindName(int K)
{
	const TKindDef &D = KindDef(K);
	if (D.Name && D.Name[0]) return String(D.Name);
	switch (K)
	{
		case nkKeys:   return String(L"\u952E\u76D8");		// 键盘
		case nkMouse:  return String(L"\u9F20\u6807");		// 鼠标
		case nkCamera: return String(L"\u955C\u5934");		// 镜头
	}
	return String(L"\u8282\u70B9");							// 节点
}
//---------------------------------------------------------------------------
// One scene object produced by the graph (read by the render window)
// keys a "Key" node can read: W A S D Up Down Left Right Space Q E R F Shift Ctrl Z X C
enum { NKEYS = 14 };
inline int KeyVk(int i)
{
	static const int Vk[NKEYS] = { 'W', 'A', 'S', 'D', VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_SPACE,
								   VK_SHIFT, VK_CONTROL, 'Z', 'X', 'C' };
	return Vk[(i < 0 || i >= NKEYS) ? 0 : i];
}
//---------------------------------------------------------------------------
struct TRunObj
{
	int   Id;							// node id (collision state is kept per node)
	int   Kind;							// nkBox / nkPlane
	float X, Y, Z, Rot, Scale;			// position, rotation around Y in degrees, scale
	int   R, G, B;						// base colour
};
//---------------------------------------------------------------------------
// Text rendering: GDI -> texture, so Chinese works (SDL_RenderDebugText is ASCII only)
//---------------------------------------------------------------------------
class TSdlText
{
private:
	struct TTex { SDL_Texture *Tex; int W, H; };
	std::map<std::wstring, TTex> FCache;
	SDL_Renderer *FRenderer;

	TTex * Get(SDL_Renderer *R, const String &S, int Size, bool Bold)
	{
		if (S.IsEmpty()) return NULL;
		if (R != FRenderer) { FCache.clear(); FRenderer = R; }		// old textures died with the old renderer
		std::wstring Key = std::wstring(Bold ? L"b" : L"n") + std::to_wstring(Size) + L"|" + std::wstring(S.c_str());
		std::map<std::wstring, TTex>::iterator it = FCache.find(Key);
		if (it != FCache.end()) return it->second.Tex ? &it->second : NULL;

		if (FCache.size() > 300)
		{
			for (it = FCache.begin(); it != FCache.end(); ++it)
				if (it->second.Tex) SDL_DestroyTexture(it->second.Tex);
			FCache.clear();
		}

		TTex T; T.Tex = NULL; T.W = 0; T.H = 0;
		std::unique_ptr<TBitmap> BP(new TBitmap);
		TBitmap *B = BP.get();
		B->PixelFormat = pf32bit;
		B->SetSize(1, 1);
		B->Canvas->Font->Name    = L"Microsoft YaHei UI";
		B->Canvas->Font->Height  = -Size;
		B->Canvas->Font->Style   = Bold ? (TFontStyles() << fsBold) : TFontStyles();
		B->Canvas->Font->Quality = static_cast<TFontQuality>(4);		// fqAntialiased
		TSize Ext = B->Canvas->TextExtent(S);
		int w = Ext.cx + 2, h = Ext.cy;
		if (w > 1 && h > 0)
		{
			B->SetSize(w, h);
			B->Canvas->Font->Name    = L"Microsoft YaHei UI";
			B->Canvas->Font->Height  = -Size;
			B->Canvas->Font->Style   = Bold ? (TFontStyles() << fsBold) : TFontStyles();
			B->Canvas->Font->Quality = static_cast<TFontQuality>(4);		// fqAntialiased
			B->Canvas->Brush->Color  = clBlack;
			B->Canvas->FillRect(TRect(0, 0, w, h));
			B->Canvas->Font->Color   = clWhite;
			B->Canvas->Brush->Style  = bsClear;
			B->Canvas->TextOut(1, 0, S);

			std::vector<Uint32> Buf((size_t)w * h);
			for (int y = 0; y < h; y++)
			{
				const Uint32 *src = (const Uint32*)B->ScanLine[y];
				for (int x = 0; x < w; x++)
				{
					Uint32 p = src[x];
					Uint32 a = std::max(std::max((p >> 16) & 255, (p >> 8) & 255), p & 255);
					Buf[(size_t)y * w + x] = (a << 24) | 0x00FFFFFF;		// white, alpha = text coverage
				}
			}
			T.Tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
			if (T.Tex)
			{
				SDL_UpdateTexture(T.Tex, NULL, Buf.data(), w * 4);
				SDL_SetTextureBlendMode(T.Tex, SDL_BLENDMODE_BLEND);
				T.W = w; T.H = h;
			}
		}
		FCache[Key] = T;
		return T.Tex ? &FCache[Key] : NULL;
	}

public:
	TSdlText() : FRenderer(NULL) {}

	// Align: 0 = x is the left edge, 1 = x is the right edge, 2 = x is the centre; y is the vertical centre
	void Draw(SDL_Renderer *R, const String &S, float x, float y, int Size, bool Bold,
			  int r, int g, int b, int Align)
	{
		TTex *T = Get(R, S, Size, Bold);
		if (!T) return;
		SDL_SetTextureColorMod(T->Tex, (Uint8)r, (Uint8)g, (Uint8)b);
		SDL_FRect D;
		D.w = (float)T->W; D.h = (float)T->H;
		D.x = Align == 1 ? x - D.w : (Align == 2 ? x - D.w * 0.5f : x);
		D.y = y - D.h * 0.5f;
		SDL_RenderTexture(R, T->Tex, NULL, &D);
	}
};
//---------------------------------------------------------------------------
class TViewport2D : public TSDLPanel
{
private:
	// ---- data -----------------------------------------------------------
	// X,Y = top-left, world units; Group 0 = none
	// Obj = uid of the linked 3D scene object (0 = none); Collapsed = only the header is shown;
	// HasVal / Val = values of the 5 inputs (position X/Y/Z, rotation, scale) used while a pin is not connected
	struct TNode { int Id; int Kind; String Name; float X, Y; bool Sel; int Group;
				   int Obj; bool Collapsed; bool HasVal; float Val[5]; };
	struct TLink { int From; int FromPin; int To; int ToPin; };				// output pin of From -> input pin of To
	struct TGroup { int Id; String Name; bool Collapsed; };					// a frame around its member nodes (Collapsed = members hidden)
	enum   TDrag { dgNone, dgPan, dgMove, dgLink, dgRubber };
	enum   { hkNone, hkNode, hkIn, hkOut, hkGroup };					// hkGroup: Node = group id (title bar of the frame)
	struct THit  { int Kind; int Node; int Pin; };

	static constexpr float NODE_W = 150.0f;		// node size in world units
	static constexpr float HEAD_H = 24.0f;
	static constexpr float ROW_H  = 26.0f;
	static constexpr float PIN_R  = 5.0f;
	static constexpr float GRP_PAD   = 14.0f;	// group frame: padding around the member nodes
	static constexpr float GRP_TITLE = 22.0f;	// group frame: title bar height

	float FStep;			// pixels between two grid lines (at zoom 1)
	int   FMajorEvery;		// every N-th line is a major line

	float FZoom, FPanX, FPanY;		// view: screen = center + Pan + world * Zoom

	std::vector<TNode> FNodes;		// draw order = z order (last is on top)
	std::vector<TLink> FLinks;
	std::vector<TGroup> FGroups;
	int   FNextId;
	int   FNextGroup, FGroupCounter;
	int   FCounter[NK_COUNT];

	TDrag FDrag;
	int   FLastX, FLastY, FDownX, FDownY, FMouseX, FMouseY;
	int   FLinkNode, FLinkPin;		// link being dragged: the pin it started on
	bool  FLinkFromOut;

	TPopupMenu *FMenu;
	TMenuItem  *FmiCopy, *FmiPaste, *FmiRename, *FmiGroup, *FmiUngroup;
    TMenuItem  *FmiW,*FmiA,*FmiS,*FmiD,*FmiUp,*FmiDown,*FmiLeft,*FmiRight;
	float FMenuWX, FMenuWY;			// where the menu was opened (world)
	int   FMenuNode;				// node under the cursor when the menu was opened (0 = none)
	int   FMenuGroup;				// group title under the cursor when the menu was opened (0 = none)

	std::vector<TNode>  FClipNodes;	// clipboard
	std::vector<TLink>  FClipLinks;
	std::vector<TGroup> FClipGroups;

	TSdlText FTxt;

	// double click detection on node headers (VCL's ssDouble is not reliable here)
	DWORD FClickTick;
	int   FClickNode, FClickX, FClickY;

	// run state (see RunStep)
	std::map<int, float> FJumpT;		// seconds since the jump started (< 0 = on the ground) of every "Jump" node
	std::map<int, bool>  FJumpHeld;		// trigger state of the last frame (a jump starts on the rising edge)
	struct TColState { float Rx, Ry, Rz, Px, Py, Pz; };		// last requested (graph) and resolved position
	std::map<int, TColState> FCol;
	std::map<int, float> FReqY;
	struct TGrav { float Y, Vy; bool On, Held; };		// state of a "Gravity" node: height, vertical speed, standing on something
	std::map<int, TGrav> FGrav;
	bool FCollide;
	std::map<int, float> FAccum;	// accumulated value of every "Accumulate" node
	float FRunT, FRunH, FRunV, FRunS;
	bool FRunDown[256];				// virtual keys that are down (set by the run window)
	unsigned FRunKeys;				// bit 0..7 = W A S D Up Down Left Right
	float FRunMX, FRunMY;			// mouse position in the run window, -1..1 from the centre (right / up = +), set by the run window

	// ---- small helpers --------------------------------------------------
	void __fastcall SetColor(SDL_Renderer *R, int r, int g, int b, int a = 255)
	{
		SDL_SetRenderDrawColor(R, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
	}

	static void PinColor(int Type, int &r, int &g, int &b)
	{
		if (Type == ptObject) { r = 90;  g = 160; b = 235; }
		else                  { r = 140; g = 205; b = 90;  }
	}

	static int RowsOf(int Kind)
	{
		const TKindDef &D = KindDef(Kind);
		return std::max(1, std::max(D.NIn, D.NOut));
	}

	static float NodeH(int Kind) { return HEAD_H + ROW_H * RowsOf(Kind); }
	static float NodeHN(const TNode &N) { return N.Collapsed ? HEAD_H : NodeH(N.Kind); }	// collapsed: header only

	float SX(float wx) { return ClientWidth  * 0.5f + FPanX + wx * FZoom; }
	float SY(float wy) { return ClientHeight * 0.5f + FPanY + wy * FZoom; }
	float WX(float sx) { return (sx - ClientWidth  * 0.5f - FPanX) / FZoom; }
	float WY(float sy) { return (sy - ClientHeight * 0.5f - FPanY) / FZoom; }

	int FindNode(int Id)
	{
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Id == Id) return (int)i;
		return -1;
	}

	// key shown by a "Key" node (Val[0] = index: W A S D Up Down Left Right)
	// a key node stores the Windows virtual-key code in Val[0] (any key can be typed in: double click the node body)
	static int KeyVkOf(const TNode &N)
	{
		int k = (int)(N.Val[0] + 0.5f);
		return (k < 1 || k > 254) ? 'W' : k;
	}

	static String KeyNameOfVk(int vk)
	{
		if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return String((wchar_t)vk);
		if (vk >= VK_F1 && vk <= VK_F24) return String(L"F") + IntToStr(vk - VK_F1 + 1);
		if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return String(L"Num") + IntToStr(vk - VK_NUMPAD0);
		switch (vk)
		{
			case VK_SPACE:  return String(L"\u7A7A\u683C");
			case VK_UP:     return String(L"\u4E0A");
			case VK_DOWN:   return String(L"\u4E0B");
			case VK_LEFT:   return String(L"\u5DE6");
			case VK_RIGHT:  return String(L"\u53F3");
			case VK_RETURN: return String(L"Enter");
			case VK_TAB:    return String(L"Tab");
			case VK_ESCAPE: return String(L"Esc");
			case VK_SHIFT:  return String(L"Shift");
			case VK_CONTROL:return String(L"Ctrl");
			case VK_MENU:   return String(L"Alt");
			case VK_BACK:   return String(L"Backspace");
			case VK_DELETE: return String(L"Delete");
			case VK_INSERT: return String(L"Insert");
			case VK_HOME:   return String(L"Home");
			case VK_END:    return String(L"End");
			case VK_PRIOR:  return String(L"PageUp");
			case VK_NEXT:   return String(L"PageDown");
			case VK_CAPITAL:return String(L"CapsLock");
			case VK_OEM_COMMA:  return String(L",");
			case VK_OEM_PERIOD: return String(L".");
			case VK_OEM_MINUS:  return String(L"-");
			case VK_OEM_PLUS:   return String(L"=");
			case VK_OEM_1:      return String(L";");
			case VK_OEM_2:      return String(L"/");
			case VK_OEM_3:      return String(L"`");
			case VK_OEM_4:      return String(L"[");
			case VK_OEM_5:      return String(L"\\");
			case VK_OEM_6:      return String(L"]");
			case VK_OEM_7:      return String(L"'");
		}
		return String(L"#") + IntToStr(vk);
	}

	// text typed by the user -> virtual-key code (letters, digits, F1..F24, Num0..9, names in English or Chinese)
	static bool KeyFromText(const String &Text, int &vk)
	{
		String U = Text.Trim().UpperCase();
		if (U.IsEmpty()) return false;
		if (U.Length() == 1)
		{
			wchar_t c = U[1];
			if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) { vk = (int)c; return true; }
			switch (c)
			{
				case L',': vk = VK_OEM_COMMA; return true;
				case L'.': vk = VK_OEM_PERIOD; return true;
				case L'-': vk = VK_OEM_MINUS; return true;
				case L'=': vk = VK_OEM_PLUS; return true;
				case L';': vk = VK_OEM_1; return true;
				case L'/': vk = VK_OEM_2; return true;
				case L'`': vk = VK_OEM_3; return true;
				case L'[': vk = VK_OEM_4; return true;
				case L'\\': vk = VK_OEM_5; return true;
				case L']': vk = VK_OEM_6; return true;
				case L'\'': vk = VK_OEM_7; return true;
			}
		}
		if (U.Length() >= 2 && U[1] == L'F')				// F1 .. F24
		{
			int n = StrToIntDef(U.SubString(2, U.Length() - 1), 0);
			if (n >= 1 && n <= 24) { vk = VK_F1 + n - 1; return true; }
		}
		if (U.Length() >= 4 && U.SubString(1, 3) == L"NUM")	// NUM0 .. NUM9
		{
			int n = StrToIntDef(U.SubString(4, U.Length() - 3), -1);
			if (n >= 0 && n <= 9) { vk = VK_NUMPAD0 + n; return true; }
		}
		struct TName { const wchar_t *Name; int Vk; };
		static const TName Names[] =
		{
			{ L"\u7A7A\u683C", VK_SPACE }, { L"SPACE", VK_SPACE }, { L"\u4E0A", VK_UP }, { L"UP", VK_UP },
			{ L"\u4E0B", VK_DOWN }, { L"DOWN", VK_DOWN }, { L"\u5DE6", VK_LEFT }, { L"LEFT", VK_LEFT },
			{ L"\u53F3", VK_RIGHT }, { L"RIGHT", VK_RIGHT }, { L"\u56DE\u8F66", VK_RETURN }, { L"ENTER", VK_RETURN },
			{ L"RETURN", VK_RETURN }, { L"TAB", VK_TAB }, { L"ESC", VK_ESCAPE }, { L"ESCAPE", VK_ESCAPE },
			{ L"SHIFT", VK_SHIFT }, { L"CTRL", VK_CONTROL }, { L"CONTROL", VK_CONTROL }, { L"ALT", VK_MENU },
			{ L"\u9000\u683C", VK_BACK }, { L"BACKSPACE", VK_BACK }, { L"\u5220\u9664", VK_DELETE },
			{ L"DELETE", VK_DELETE }, { L"DEL", VK_DELETE }, { L"INSERT", VK_INSERT }, { L"INS", VK_INSERT },
			{ L"HOME", VK_HOME }, { L"END", VK_END }, { L"PAGEUP", VK_PRIOR }, { L"PGUP", VK_PRIOR },
			{ L"PAGEDOWN", VK_NEXT }, { L"PGDN", VK_NEXT }, { L"CAPSLOCK", VK_CAPITAL }, { L"CAPS", VK_CAPITAL }
		};
		for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); i++)
			if (U == Names[i].Name) { vk = Names[i].Vk; return true; }
		return false;
	}

	static String KeyNameOf(const TNode &N)
	{
		return KeyNameOfVk(KeyVkOf(N));
	}

	// true when the node belongs to a folded group (it is not drawn, not clickable)
	bool NodeHidden(const TNode &N)
	{
		if (N.Group == 0) return false;
		int g = FindGroup(N.Group);
		return g >= 0 && FGroups[g].Collapsed;
	}

	// screen position of a pin
	void PinPos(const TNode &N, bool Out, int Idx, float &px, float &py)
	{
		if (NodeHidden(N))		// inside a folded group: links end at the left / right side of the group's title bar
		{
			float x0, y0, x1, y1;
			if (GroupRect(N.Group, x0, y0, x1, y1))
			{
				px = SX(Out ? x1 : x0);
				py = SY(y0 + GRP_TITLE * 0.5f);
				return;
			}
		}
		px = SX(N.X + (Out ? NODE_W : 0.0f));
		if (N.Collapsed)
			py = SY(N.Y + HEAD_H * 0.5f);		// collapsed: every link ends at the middle of the header
		else
			py = SY(N.Y + HEAD_H + ROW_H * (Idx + 0.5f));
	}

	THit HitTest(int sx, int sy)
	{
		THit H; H.Kind = hkNone; H.Node = 0; H.Pin = 0;
		H = HitTestNodes(sx, sy);
		if (H.Kind != hkNone) return H;
		for (int g = (int)FGroups.size() - 1; g >= 0; g--)		// group title bars (below the nodes)
		{
			float x0, y0, x1, y1;
			if (!GroupRect(FGroups[g].Id, x0, y0, x1, y1)) continue;
			if (sx >= SX(x0) && sx <= SX(x1) && sy >= SY(y0) && sy <= SY(y0 + GRP_TITLE))
			{ H.Kind = hkGroup; H.Node = FGroups[g].Id; return H; }
		}
		return H;
	}

	THit HitTestNodes(int sx, int sy)
	{
		THit H; H.Kind = hkNone; H.Node = 0; H.Pin = 0;
		float pr = std::max(9.0f, PIN_R * FZoom + 4.0f);
		for (int i = (int)FNodes.size() - 1; i >= 0; i--)
		{
			TNode &N = FNodes[i];
			if (NodeHidden(N)) continue;
			const TKindDef &D = KindDef(N.Kind);
			float px, py;
			for (int k = 0; !N.Collapsed && k < D.NIn; k++)
			{
				PinPos(N, false, k, px, py);
				if ((sx - px) * (sx - px) + (sy - py) * (sy - py) <= pr * pr) { H.Kind = hkIn; H.Node = N.Id; H.Pin = k; return H; }
			}
			for (int k = 0; !N.Collapsed && k < D.NOut; k++)
			{
				PinPos(N, true, k, px, py);
				if ((sx - px) * (sx - px) + (sy - py) * (sy - py) <= pr * pr) { H.Kind = hkOut; H.Node = N.Id; H.Pin = k; return H; }
			}
			float x = SX(N.X), y = SY(N.Y);
			if (sx >= x && sx <= x + NODE_W * FZoom && sy >= y && sy <= y + NodeHN(N) * FZoom)
			{ H.Kind = hkNode; H.Node = N.Id; return H; }
		}
		return H;
	}

	// ---- groups -----------------------------------------------------------
	int FindGroup(int Id)
	{
		for (size_t i = 0; i < FGroups.size(); i++)
			if (FGroups[i].Id == Id) return (int)i;
		return -1;
	}

	// frame of a group in world units (title bar on top); false when the group has no members
	bool GroupRect(int Gid, float &x0, float &y0, float &x1, float &y1)
	{
		bool any = false;
		for (size_t i = 0; i < FNodes.size(); i++)
		{
			const TNode &N = FNodes[i];
			if (N.Group != Gid) continue;
			float ax = N.X, ay = N.Y, bx = N.X + NODE_W, by = N.Y + NodeHN(N);
			if (!any) { x0 = ax; y0 = ay; x1 = bx; y1 = by; any = true; }
			else { x0 = std::min(x0, ax); y0 = std::min(y0, ay); x1 = std::max(x1, bx); y1 = std::max(y1, by); }
		}
		if (any)
		{
			x0 -= GRP_PAD; x1 += GRP_PAD; y1 += GRP_PAD; y0 -= GRP_PAD + GRP_TITLE;
			int gi = FindGroup(Gid);
			if (gi >= 0 && FGroups[gi].Collapsed)		// folded: only a title bar of normal node width
			{
				x1 = x0 + NODE_W + 2.0f * GRP_PAD;
				y1 = y0 + GRP_TITLE;
			}
		}
		return any;
	}

	bool GroupAllSelected(int Gid)
	{
		int c = 0;
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Group == Gid) { if (!FNodes[i].Sel) return false; c++; }
		return c > 0;
	}

	void SelectGroup(int Gid, bool Add)
	{
		if (!Add) ClearSel();
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Group == Gid) FNodes[i].Sel = true;
	}

	// a group needs at least two members: dissolve the others
	void CleanGroups()
	{
		for (int g = (int)FGroups.size() - 1; g >= 0; g--)
		{
			int c = 0;
			for (size_t i = 0; i < FNodes.size(); i++)
				if (FNodes[i].Group == FGroups[g].Id) c++;
			if (c < 2)
			{
				for (size_t i = 0; i < FNodes.size(); i++)
					if (FNodes[i].Group == FGroups[g].Id) FNodes[i].Group = 0;
				FGroups.erase(FGroups.begin() + g);
			}
		}
	}

	// "Group": put the selected nodes (two or more) into one new group
	void GroupSelected()
	{
		if (SelCount() < 2) return;
		TGroup G;
		G.Id   = FNextGroup++;
		G.Name = String(L"\u7EC4") + L" " + IntToStr(++FGroupCounter);		// 组
		G.Collapsed = false;
		FGroups.push_back(G);
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Sel) FNodes[i].Group = G.Id;
		CleanGroups();
	}

	// "Ungroup": the selected nodes leave their groups
	void UngroupSelected()
	{
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Sel) FNodes[i].Group = 0;
		CleanGroups();
	}

	bool AnySelectedGrouped()
	{
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Sel && FNodes[i].Group != 0) return true;
		return false;
	}

	// ---- view commands: Z locate, F fit all, X 100% -----------------------
	// Z: centre the view on the selected node(s) (zoom stays)
	void LocateSelected()
	{
		bool any = false;
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		for (size_t i = 0; i < FNodes.size(); i++)
		{
			const TNode &N = FNodes[i];
			if (!N.Sel) continue;
			float bx = N.X + NODE_W, by = N.Y + NodeHN(N);
			if (!any) { x0 = N.X; y0 = N.Y; x1 = bx; y1 = by; any = true; }
			else { x0 = std::min(x0, N.X); y0 = std::min(y0, N.Y); x1 = std::max(x1, bx); y1 = std::max(y1, by); }
		}
		if (!any) return;
		FPanX = -(x0 + x1) * 0.5f * FZoom;
		FPanY = -(y0 + y1) * 0.5f * FZoom;
	}

	// F: zoom and pan so that every node (and group frame) fits into the window
	void FitAll()
	{
		bool any = false;
		float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		for (size_t i = 0; i < FNodes.size(); i++)
		{
			const TNode &N = FNodes[i];
			if (NodeHidden(N)) continue;			// covered by its folded group frame below
			float bx = N.X + NODE_W, by = N.Y + NodeHN(N);
			if (!any) { x0 = N.X; y0 = N.Y; x1 = bx; y1 = by; any = true; }
			else { x0 = std::min(x0, N.X); y0 = std::min(y0, N.Y); x1 = std::max(x1, bx); y1 = std::max(y1, by); }
		}
		for (size_t g = 0; g < FGroups.size(); g++)
		{
			float gx0, gy0, gx1, gy1;
			if (!GroupRect(FGroups[g].Id, gx0, gy0, gx1, gy1)) continue;
			if (!any) { x0 = gx0; y0 = gy0; x1 = gx1; y1 = gy1; any = true; continue; }
			x0 = std::min(x0, gx0); y0 = std::min(y0, gy0); x1 = std::max(x1, gx1); y1 = std::max(y1, gy1);
		}
		if (!any) { FZoom = 1.0f; FPanX = FPanY = 0.0f; return; }
		float bw = std::max(x1 - x0, 1.0f), bh = std::max(y1 - y0, 1.0f);
		float z = std::min(ClientWidth * 0.9f / bw, ClientHeight * 0.9f / bh);
		z = std::min(1.5f, std::max(0.1f, z));
		FZoom = z;
		FPanX = -(x0 + x1) * 0.5f * z;
		FPanY = -(y0 + y1) * 0.5f * z;
	}

	// X: zoom to 100%, the world point in the middle of the window stays there
	void ZoomActual()
	{
		float c = FZoom;
		FPanX /= c; FPanY /= c;
		FZoom = 1.0f;
	}

	// second click on the same node header within the system double click time = fold / unfold it
	bool ToggleOnDoubleClick(int Id, int X, int Y)
	{
		DWORD now = ::GetTickCount();
		bool dbl = (Id == FClickNode) && (now - FClickTick <= ::GetDoubleClickTime()) &&
				   abs(X - FClickX) <= 4 && abs(Y - FClickY) <= 4;
		FClickNode = Id; FClickTick = now; FClickX = X; FClickY = Y;
		if (!dbl) return false;
		int idx = FindNode(Id);
		if (idx < 0) return false;
		if ((float)Y > SY(FNodes[idx].Y) + HEAD_H * FZoom)
		{
			if (FNodes[idx].Kind == nkValue && !FNodes[idx].Collapsed)		// double click on the body of a value node: edit it
			{
				FClickNode = 0;
				EditValue(Id);
				return true;
			}
			if (FNodes[idx].Kind == nkMouse && !FNodes[idx].Collapsed)		// double click on the body of a mouse node: click / no click
			{
				FClickNode = 0;
				FNodes[idx].Val[0] = FNodes[idx].Val[0] > 0.5f ? 0.0f : 1.0f;
				return true;
			}
			if (FNodes[idx].Kind == nkKeys && !FNodes[idx].Collapsed)		// double click on the body of a key node: next key
			{
				FClickNode = 0;
				EditKey(Id);
				return true;
			}
			return false;			// only the header folds the node
		}
		FNodes[idx].Collapsed = !FNodes[idx].Collapsed;
		FClickNode = 0;
		return true;
	}

	bool GroupFolded(int Gid)		// true when the group is folded (its nodes are hidden)
	{
		int g = FindGroup(Gid);
		return g >= 0 && FGroups[g].Collapsed;
	}

	// second click on the same group title bar = fold / unfold all member nodes
	bool ToggleGroupOnDoubleClick(int Gid, int X, int Y)
	{
		DWORD now = ::GetTickCount();
		bool dbl = (-Gid == FClickNode) && (now - FClickTick <= ::GetDoubleClickTime()) &&
				   abs(X - FClickX) <= 4 && abs(Y - FClickY) <= 4;
		FClickNode = -Gid; FClickTick = now; FClickX = X; FClickY = Y;
		if (!dbl) return false;
		int g = FindGroup(Gid);
		if (g >= 0) FGroups[g].Collapsed = !FGroups[g].Collapsed;		// fold / unfold the group like a normal node
		SelectGroup(Gid, false);										// the members stay selected (a drag moves the whole group)
		FClickNode = 0;
		return true;
	}

	// ---- node operations ------------------------------------------------
	void ClearSel()
	{
		for (size_t i = 0; i < FNodes.size(); i++) FNodes[i].Sel = false;
	}

	int SelCount()
	{
		int c = 0;
		for (size_t i = 0; i < FNodes.size(); i++) if (FNodes[i].Sel) c++;
		return c;
	}

	void BringToFront(int Id)
	{
		int idx = FindNode(Id);
		if (idx < 0 || idx == (int)FNodes.size() - 1) return;
		TNode t = FNodes[idx];
		FNodes.erase(FNodes.begin() + idx);
		FNodes.push_back(t);
	}

	void AddNode(int Kind, float wx, float wy)
	{
		TNode N;
		N.Id   = FNextId++;
		N.Kind = Kind;
		N.Name = KindName(Kind) + L" " + IntToStr(++FCounter[Kind]);
		N.X = wx; N.Y = wy;
		N.Group = 0;
		N.Obj = 0; N.Collapsed = false; N.HasVal = false;
		for (int k = 0; k < 5; k++) N.Val[k] = 0.0f;
		if (Kind == nkValue) { N.HasVal = true; N.Val[0] = 1.0f; }
		if (Kind == nkKeys)  { N.HasVal = true; N.Val[0] = (float)'W'; }		// Val[0] = Windows virtual-key code (default W)
		if (Kind == nkMouse) { N.HasVal = true; N.Val[0] = 0.0f; }		// Val[0] = 0 click + drag, 1 no click
		ClearSel();
		N.Sel = true;
		FNodes.push_back(N);
	}

	void RemoveLinksOf(int Id)
	{
		for (int i = (int)FLinks.size() - 1; i >= 0; i--)
			if (FLinks[i].To == Id || FLinks[i].From == Id)
				FLinks.erase(FLinks.begin() + i);
	}

	void DeleteSelected()
	{
		for (int i = (int)FNodes.size() - 1; i >= 0; i--)
			if (FNodes[i].Sel)
			{
				RemoveLinksOf(FNodes[i].Id);
				FNodes.erase(FNodes.begin() + i);
			}
		CleanGroups();
	}

	void CopySelected()
	{
		FClipNodes.clear(); FClipLinks.clear(); FClipGroups.clear();
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Sel) FClipNodes.push_back(FNodes[i]);
		for (size_t i = 0; i < FGroups.size(); i++)			// groups that have members in the clipboard
			for (size_t k = 0; k < FClipNodes.size(); k++)
				if (FClipNodes[k].Group == FGroups[i].Id) { FClipGroups.push_back(FGroups[i]); break; }
		for (size_t i = 0; i < FLinks.size(); i++)
		{
			bool a = false, b = false;
			for (size_t k = 0; k < FClipNodes.size(); k++)
			{
				if (FClipNodes[k].Id == FLinks[i].From) a = true;
				if (FClipNodes[k].Id == FLinks[i].To)   b = true;
			}
			if (a && b) FClipLinks.push_back(FLinks[i]);
		}
	}

	void PasteAt(float wx, float wy)
	{
		if (FClipNodes.empty()) return;
		float minx = FClipNodes[0].X, miny = FClipNodes[0].Y;
		for (size_t i = 1; i < FClipNodes.size(); i++)
		{
			minx = std::min(minx, FClipNodes[i].X);
			miny = std::min(miny, FClipNodes[i].Y);
		}
		ClearSel();
		std::map<int, int> IdMap, GroupMap;
		for (size_t i = 0; i < FClipGroups.size(); i++)		// pasted nodes get their own copy of each group
		{
			TGroup G = FClipGroups[i];
			int OldG = G.Id;
			G.Id = FNextGroup++;
			GroupMap[OldG] = G.Id;
			FGroups.push_back(G);
		}
		for (size_t i = 0; i < FClipNodes.size(); i++)
		{
			TNode N = FClipNodes[i];
			int OldId = N.Id;
			N.Id   = FNextId++;
			N.Group = (N.Group != 0 && GroupMap.count(N.Group)) ? GroupMap[N.Group] : 0;
			N.X    = wx + (N.X - minx);
			N.Y    = wy + (N.Y - miny);
			N.Name = N.Name + L" \u590D\u5236";		// "复制"
			N.Obj  = 0;									// a pasted node becomes a NEW 3D object
			N.Sel  = true;
			IdMap[OldId] = N.Id;
			FNodes.push_back(N);
		}
		for (size_t i = 0; i < FClipLinks.size(); i++)
		{
			TLink L = FClipLinks[i];
			L.From = IdMap[L.From]; L.To = IdMap[L.To];
			FLinks.push_back(L);
		}
		CleanGroups();
	}

	void RenameGroup(int Gid)
	{
		int idx = FindGroup(Gid);
		if (idx < 0) return;
		String S = FGroups[idx].Name;
		if (InputQuery(L"\u91CD\u547D\u540D", L"\u540D\u79F0", S))		// 重命名 / 名称
		{
			S = S.Trim();
			if (!S.IsEmpty()) FGroups[idx].Name = S;
		}
	}

	void EditValue(int Id)
	{
		int idx = FindNode(Id);
		if (idx < 0 || FNodes[idx].Kind != nkValue) return;
		String S = FloatToStrF(FNodes[idx].Val[0], ffGeneral, 7, 0);
		if (InputQuery(L"\u6570\u503C", L"\u6570\u503C", S))		// 数值
		{
			float v = (float)StrToFloatDef(S.Trim(), FNodes[idx].Val[0]);
			FNodes[idx].Val[0] = v;
		}
	}

	void EditKey(int Id)		// double click on a key node: type the key (W, F5, 空格, Shift ...)
	{
		int idx = FindNode(Id);
		if (idx < 0 || FNodes[idx].Kind != nkKeys) return;
		String S = KeyNameOf(FNodes[idx]);
		if (InputQuery(L"\u6309\u952E", L"\u8F93\u5165\u6309\u952E\uFF08\u5982 W\u3001F5\u3001\u7A7A\u683C\u3001Shift\u3001\u4E0A\uFF09", S))
		{
			int vk = 0;
			if (KeyFromText(S, vk)) FNodes[idx].Val[0] = (float)vk;
			else ShowMessage(L"\u65E0\u6CD5\u8BC6\u522B\u7684\u6309\u952E");
		}
	}

	void RenameNode(int Id)
	{
		int idx = FindNode(Id);
		if (idx < 0) return;
		String S = FNodes[idx].Name;
		if (InputQuery(L"\u91CD\u547D\u540D", L"\u540D\u79F0", S))		// 重命名 / 名称
		{
			S = S.Trim();
			if (!S.IsEmpty()) FNodes[idx].Name = S;
		}
	}

	int SingleSelected()		// id of the selected node when exactly one is selected, else 0
	{
		int id = 0, c = 0;
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Sel) { id = FNodes[i].Id; c++; }
		return c == 1 ? id : 0;
	}

	// does anything downstream of node From (following links from outputs to inputs) reach node Target?
	bool Reaches(int From, int Target, std::set<int> &Seen)
	{
		if (From == Target) return true;
		if (!Seen.insert(From).second) return false;
		for (size_t i = 0; i < FLinks.size(); i++)
			if (FLinks[i].From == From && Reaches(FLinks[i].To, Target, Seen))
				return true;
		return false;
	}

	void TryLink(const THit &H)
	{
		int oN, oP, iN, iP;
		if (FLinkFromOut) { if (H.Kind != hkIn)  return; oN = FLinkNode; oP = FLinkPin; iN = H.Node; iP = H.Pin; }
		else              { if (H.Kind != hkOut) return; oN = H.Node;    oP = H.Pin;    iN = FLinkNode; iP = FLinkPin; }
		if (oN == iN) return;
		int io = FindNode(oN), ii = FindNode(iN);
		if (io < 0 || ii < 0) return;
		const TKindDef &DO = KindDef(FNodes[io].Kind), &DI = KindDef(FNodes[ii].Kind);
		if (oP >= DO.NOut || iP >= DI.NIn) return;
		if (DO.Out[oP].Type != DI.In[iP].Type) return;		// number <-> number, object <-> object only
		std::set<int> Seen;
		if (Reaches(iN, oN, Seen)) return;					// would create a cycle

		bool Multi = (DI.In[iP].Type == ptObject);			// object inputs take many links, number inputs one
		for (int i = (int)FLinks.size() - 1; i >= 0; i--)
		{
			const TLink &L = FLinks[i];
			if (L.To == iN && L.ToPin == iP && (!Multi || (L.From == oN && L.FromPin == oP)))
				FLinks.erase(FLinks.begin() + i);
		}
		TLink L; L.From = oN; L.FromPin = oP; L.To = iN; L.ToPin = iP;
		FLinks.push_back(L);
	}

	// ---- graph evaluation (used by the render window) -------------------
	float EvalIn(int NodeId, int Pin)
	{
		for (size_t i = 0; i < FLinks.size(); i++)
			if (FLinks[i].To == NodeId && FLinks[i].ToPin == Pin)
				return EvalOut(FLinks[i].From, FLinks[i].FromPin);
		int idx = FindNode(NodeId);
		if (idx < 0) return 0.0f;
		if (FNodes[idx].HasVal && Pin >= 0 && Pin < 5) return FNodes[idx].Val[Pin];		// value of the linked 3D object
		return KindDef(FNodes[idx].Kind).In[Pin].Def;
	}

	float EvalOut(int NodeId, int Pin)
	{
		int idx = FindNode(NodeId);
		if (idx < 0) return 0.0f;
		switch (FNodes[idx].Kind)
		{
			case nkTime:
				if (Pin == 0) return FRunT;
				if (Pin == 1) return sinf(FRunT);
				if (Pin == 2) return cosf(FRunT);
				return fmodf(FRunT * 90.0f, 360.0f);			// angle: 90 degrees per second
			case nkValue:
				return FNodes[idx].Val[0];
			case nkGravity:
			{
				std::map<int, TGrav>::iterator it = FGrav.find(NodeId);
				return it == FGrav.end() ? EvalIn(NodeId, 3) : it->second.Y;
			}
			//case nkJump:
			//{
			//	float base = EvalIn(NodeId, 1);
			//	std::map<int, float>::iterator it = FJumpT.find(NodeId);
			//	if (it == FJumpT.end() || it->second < 0.0f) return base;
			//	float dur = std::max(0.05f, EvalIn(NodeId, 3));
			//	float u = std::min(1.0f, it->second / dur);
			//	return base + EvalIn(NodeId, 2) * 4.0f * u * (1.0f - u);
			//}
			case nkKeys:
				return EvalKeysOut(idx, Pin);
			case nkAdd:
				return EvalIn(NodeId, 0) + EvalIn(NodeId, 1);
			case nkMouse:
				return EvalMouseOut(Pin);
			case nkKey:
				return Pin == 0 ? FRunH : (Pin == 1 ? FRunV : FRunS);
			case nkAccum:
			case nkSub:
			{
				std::map<int, float>::iterator it = FAccum.find(NodeId);
				return it == FAccum.end() ? 0.0f : it->second;
			}
		}
		return 0.0f;
	}

	void BuildObj(int NodeId, std::vector<TRunObj> &Out)
	{
		int idx = FindNode(NodeId);
		if (idx < 0) return;
		int K = FNodes[idx].Kind;
		if (K != nkBox && K != nkPlane) return;
		static const int Pal[6][3] = { { 90, 150, 220 }, { 220, 120, 90 }, { 110, 200, 130 },
									   { 220, 190, 90 }, { 170, 120, 220 }, { 90, 200, 200 } };
		TRunObj O;
		O.Id    = NodeId;
		O.Kind  = K;
		O.X     = EvalIn(NodeId, 0);
		O.Y     = EvalIn(NodeId, 1);
		O.Z     = -EvalIn(NodeId, 2);			// +Z of the graph = away from the run camera (W moves the object away)
		O.Rot   = EvalIn(NodeId, 3);
		O.Scale = EvalIn(NodeId, 4);
		if (K == nkPlane) { O.R = 92; O.G = 98; O.B = 108; }
		else
		{
			const int *P = Pal[(NodeId - 1) % 6];
			O.R = P[0]; O.G = P[1]; O.B = P[2];
		}
		Out.push_back(O);
	}

	// ---- 键盘 / 鼠标 / 镜头: menu entries, each written separately ----------
	enum { mtKeyBase = 200, mtMouseClick = 41, mtMouseNoClick = 42, mtCameraPos = 51 };		// menu tags

	// Val[0] of the right-clicked node (or of the selected nodes) of this kind = V; when there is none a new node is created at the menu position
	void SetKindValue(int Kind, float V)
	{
		bool done = false;
		int hit = FMenuNode ? FindNode(FMenuNode) : -1;
		if (hit >= 0 && FNodes[hit].Kind == Kind)
		{
			FNodes[hit].Val[0] = V;
			done = true;
		}
		else
			for (size_t i = 0; i < FNodes.size(); i++)
				if (FNodes[i].Sel && FNodes[i].Kind == Kind) { FNodes[i].Val[0] = V; done = true; }
		if (!done)
		{
			AddNode(Kind, FMenuWX, FMenuWY);
			FNodes.back().Val[0] = V;
		}
	}

	void MenuKeyboard(int Index)	{ SetKindValue(nkKeys, (float)KeyVk(Index)); }						// 键盘 -> W / A / S / D / ...
	void MenuMouse(bool NoClick)	{ SetKindValue(nkMouse, NoClick ? 1.0f : 0.0f); }					// 鼠标 -> 点击控制 / 不点击控制
	void MenuCamera()				{ AddNode(nkCamera, FMenuWX, FMenuWY); }							// 镜头 -> 位置 XYZ

	// true when the tag belongs to the 键盘 / 鼠标 / 镜头 menus (and it was handled)
	bool HandleDeviceMenu(int T)
	{
		if (T >= mtKeyBase && T < mtKeyBase + NKEYS)       { MenuKeyboard(T - mtKeyBase); return true; }
		if (T == mtMouseClick || T == mtMouseNoClick)      { MenuMouse(T == mtMouseNoClick); return true; }
		if (T == mtCameraPos)                              { MenuCamera(); return true; }
		return false;
	}

	TMenuItem * BuildKeyboardMenu()
	{
		TMenuItem *Mi = NewItem(L"\u952E\u76D8", 0);				// 键盘
		for (int i = 0; i < NKEYS; i++)
			Mi->Add(NewItem(KeyNameOfVk(KeyVk(i)), mtKeyBase + i));
		return Mi;
	}

	TMenuItem * BuildMouseMenu()
	{
		TMenuItem *Mi = NewItem(L"\u9F20\u6807", 0);				// 鼠标
		Mi->Add(NewItem(L"\u70B9\u51FB\u63A7\u5236", mtMouseClick));			// 点击控制
		Mi->Add(NewItem(L"\u4E0D\u70B9\u51FB\u63A7\u5236", mtMouseNoClick));	// 不点击控制
		return Mi;
	}

	TMenuItem * BuildCameraMenu()
	{
		TMenuItem *Mi = NewItem(L"\u955C\u5934", 0);				// 镜头
		Mi->Add(NewItem(L"\u4F4D\u7F6E XYZ", mtCameraPos));		// 位置 XYZ
		return Mi;
	}

	// ---- node outputs of 键盘 / 鼠标 ----
	float EvalKeysOut(int Idx, int Pin)
	{
		float down = FRunDown[KeyVkOf(FNodes[Idx])] ? 1.0f : 0.0f;
		return Pin == 0 ? down : -down;			// pin 1 = reversed direction (0 / -1)
	}

	float EvalMouseOut(int Pin) { return Pin == 0 ? FRunMX : FRunMY; }

	// ---- context menu ---------------------------------------------------
	TMenuItem * __fastcall NewItem(const String &Cap, int Tag)
	{
		TMenuItem *Mi = new TMenuItem(FMenu);
		Mi->Caption = Cap;
		Mi->Tag     = Tag;
		if (Tag != 0) Mi->OnClick = MenuClick;
		return Mi;
	}

	void __fastcall MenuClick(TObject *Sender)
	{
		int T = (int)static_cast<TMenuItem*>(Sender)->Tag;
		if (T >= 100 && T < 100 + NK_COUNT) { AddNode(T - 100, FMenuWX, FMenuWY); return; }
		if (HandleDeviceMenu(T)) return;			// 键盘 / 鼠标 / 镜头 entries (see below)
		switch (T)
		{
			case 11: CopySelected(); break;
			case 12: PasteAt(FMenuWX, FMenuWY); break;
			case 13:
			{
				if (FMenuGroup) { RenameGroup(FMenuGroup); break; }
				int id = FMenuNode ? FMenuNode : SingleSelected();
				if (id) RenameNode(id);
				break;
			}
			case 21: GroupSelected(); break;
			case 22: UngroupSelected(); break;
		}
	}

	void BuildMenu()
	{
		FMenu = new TPopupMenu(this);
		FMenu->AutoHotkeys = maManual;		// no automatic "(X)" hotkeys: with many items the letters run out and the last entries came out blank
		TMenuItem *c = NewItem(L"\u521B\u5EFA", 0);					// 创建
		for (int k = 0; k < nkKeys; k++)					// 方块 ... 运行输出 only; 键盘 / 鼠标 / 镜头 (nkKeys..) have their own sub menus
		{
			String Cap = KindName(k);
			if (Cap.IsEmpty()) continue;					// never add a blank entry
			c->Add(NewItem(Cap, 100 + k));
		}
		FMenu->Items->Add(c);
		TMenuItem *e = NewItem(L"\u7F16\u8F91", 0);					// 编辑
		FmiCopy   = NewItem(L"\u590D\u5236", 11);					// 复制
		FmiPaste  = NewItem(L"\u7C98\u8D34", 12);					// 粘贴
		FmiRename = NewItem(L"\u91CD\u547D\u540D", 13);				// 重命名
		e->Add(FmiCopy); e->Add(FmiPaste); e->Add(FmiRename);
		TMenuItem *g = NewItem(L"\u7EC4", 0);					// 组
		FmiGroup   = NewItem(L"\u6210\u7EC4", 21);				// 成组
		FmiUngroup = NewItem(L"\u89E3\u7EC4", 22);				// 解组
		g->Add(FmiGroup); g->Add(FmiUngroup);
		FMenu->Items->Add(e);
		FMenu->Items->Add(g);
		FMenu->Items->Add(NewItem(L"-", 0));			// separator: 键盘 / 鼠标 / 镜头 are written separately from 创建 / 编辑 / 组
		FMenu->Items->Add(BuildKeyboardMenu());
		FMenu->Items->Add(BuildMouseMenu());
		FMenu->Items->Add(BuildCameraMenu());
		{	// build marker (temporary): grey entry at the bottom of the right-click menu
			TMenuItem *V = NewItem(String(L"rev 1525  \u521B\u5EFA\u83DC\u5355 ") + IntToStr(c->Count) + L" \u9879", 0);
			V->Enabled = false;
			FMenu->Items->Add(NewItem(L"-", 0));
			FMenu->Items->Add(V);
		}
		PurgeBlankItems(FMenu->Items);
	}

	// delete every menu entry without a caption (also entries added from outside through ContextMenu)
	void PurgeBlankItems(TMenuItem *Parent)
	{
		for (int i = Parent->Count - 1; i >= 0; i--)
		{
			TMenuItem *Mi = Parent->Items[i];
			if (Mi->Count > 0) PurgeBlankItems(Mi);
			if (Mi->Caption.Trim().IsEmpty()) Parent->Delete(i);
		}
	}

	void ShowMenu(int X, int Y)
	{
		PurgeBlankItems(FMenu->Items);
		FMenuWX = WX((float)X);
		FMenuWY = WY((float)Y);
		FmiCopy->Enabled   = SelCount() > 0;
		FmiPaste->Enabled  = !FClipNodes.empty();
		FmiRename->Enabled = (FMenuGroup != 0) || (FMenuNode != 0) || (SingleSelected() != 0);
		FmiGroup->Enabled   = SelCount() >= 2;
		FmiUngroup->Enabled = AnySelectedGrouped();
		TPoint P = ClientToScreen(Point(X, Y));
		FMenu->Popup(P.x, P.y);
	}

	void FocusIfIdle()
	{
		// the mouse wheel / keys go to the focused control: take focus when the mouse is over the
		// viewport, unless the user is typing in an edit box
		if (Focused() || !CanFocus()) return;
		TCustomForm *F = GetParentForm(this);
		if (!F || Screen->ActiveCustomForm != F) return;
		TWinControl *A = F->ActiveControl;
		if (A && dynamic_cast<TCustomEdit*>(A)) return;
		SetFocus();
	}

	// ---- shapes ----------------------------------------------------------
	// rounded rectangle outline: rt = radius of the top corners, rb = of the bottom corners
	static void RoundPath(std::vector<SDL_FPoint> &P, float x, float y, float w, float h, float rt, float rb)
	{
		P.clear();
		const int N = 6;
		const float PI_ = 3.14159265f;
		float cx[4] = { x + rt, x + w - rt, x + w - rb, x + rb };
		float cy[4] = { y + rt, y + rt,     y + h - rb, y + h - rb };
		float rr[4] = { rt, rt, rb, rb };
		float a0[4] = { 180.0f, 270.0f, 0.0f, 90.0f };
		float qx[4] = { x, x + w, x + w, x };
		float qy[4] = { y, y,     y + h, y + h };
		for (int k = 0; k < 4; k++)
		{
			if (rr[k] < 0.5f)
			{
				SDL_FPoint p = { qx[k], qy[k] };
				P.push_back(p);
				continue;
			}
			for (int i = 0; i <= N; i++)
			{
				float a = (a0[k] + 90.0f * i / N) * PI_ / 180.0f;
				SDL_FPoint p = { cx[k] + rr[k] * cosf(a), cy[k] + rr[k] * sinf(a) };
				P.push_back(p);
			}
		}
	}

	static void CirclePath(std::vector<SDL_FPoint> &P, float cx, float cy, float r)
	{
		P.clear();
		const int N = 16;
		for (int i = 0; i < N; i++)
		{
			float a = 6.2831853f * i / N;
			SDL_FPoint p = { cx + r * cosf(a), cy + r * sinf(a) };
			P.push_back(p);
		}
	}

	void FillPoly(SDL_Renderer *R, const std::vector<SDL_FPoint> &P, int r, int g, int b, int a)
	{
		if (P.size() < 3) return;
		SDL_FColor col = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
		std::vector<SDL_Vertex> V;
		std::vector<int> I;
		float mx = 0, my = 0;
		for (size_t i = 0; i < P.size(); i++) { mx += P[i].x; my += P[i].y; }
		mx /= P.size(); my /= P.size();
		SDL_Vertex c; c.position.x = mx; c.position.y = my; c.color = col; c.tex_coord.x = 0; c.tex_coord.y = 0;
		V.push_back(c);
		for (size_t i = 0; i < P.size(); i++)
		{
			SDL_Vertex v; v.position = P[i]; v.color = col; v.tex_coord.x = 0; v.tex_coord.y = 0;
			V.push_back(v);
		}
		int n = (int)P.size();
		for (int i = 0; i < n; i++)
		{
			I.push_back(0); I.push_back(1 + i); I.push_back(1 + (i + 1) % n);
		}
		SDL_RenderGeometry(R, NULL, V.data(), (int)V.size(), I.data(), (int)I.size());
	}

	void StrokePoly(SDL_Renderer *R, const std::vector<SDL_FPoint> &P, int r, int g, int b, int a)
	{
		if (P.size() < 2) return;
		std::vector<SDL_FPoint> Q = P;
		Q.push_back(P[0]);
		SetColor(R, r, g, b, a);
		SDL_RenderLines(R, Q.data(), (int)Q.size());
	}

	void DrawLink(SDL_Renderer *R, float x0, float y0, float x3, float y3, int r, int g, int b, int a)
	{
		float d = std::max(40.0f * FZoom, fabsf(x3 - x0) * 0.5f);
		float x1 = x0 + d, y1 = y0, x2 = x3 - d, y2 = y3;
		const int N = 28;
		SDL_FPoint P[N + 1];
		for (int i = 0; i <= N; i++)
		{
			float t = (float)i / N, u = 1.0f - t;
			float c0 = u * u * u, c1 = 3 * u * u * t, c2 = 3 * u * t * t, c3 = t * t * t;
			P[i].x = c0 * x0 + c1 * x1 + c2 * x2 + c3 * x3;
			P[i].y = c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3;
		}
		SetColor(R, r, g, b, a);
		for (int k = -1; k <= 1; k++)			// ~3 px thick
		{
			SDL_FPoint Q[N + 1];
			for (int i = 0; i <= N; i++) { Q[i].x = P[i].x; Q[i].y = P[i].y + k; }
			SDL_RenderLines(R, Q, N + 1);
		}
	}

	// ---- rendering -------------------------------------------------------
	void DrawNode(SDL_Renderer *R, const TNode &N, const THit &Hover)
	{
		if (NodeHidden(N)) return;			// inside a folded group
		const TKindDef &D = KindDef(N.Kind);
		float z = FZoom;
		float x = SX(N.X), y = SY(N.Y);
		float w = NODE_W * z, hh = HEAD_H * z, bh = N.Collapsed ? 0.0f : ROW_H * RowsOf(N.Kind) * z;
		if (x > ClientWidth || x + w < 0 || y > ClientHeight || y + hh + bh < 0) return;
		float rad = std::max(2.0f, 6.0f * z);
		int hr = D.R, hg = D.G, hb = D.B;

		std::vector<SDL_FPoint> P;
		RoundPath(P, x, y, w, hh + bh, rad, rad);						// body
		FillPoly(R, P, 44, 46, 52, 245);
		RoundPath(P, x, y, w, hh, rad, N.Collapsed ? rad : 0.0f);							// header
		FillPoly(R, P, hr, hg, hb, 255);
		RoundPath(P, x, y, w, hh + bh, rad, rad);						// outline
		if (N.Sel)
		{
			StrokePoly(R, P, 255, 170, 40, 255);
			std::vector<SDL_FPoint> P2;
			RoundPath(P2, x - 1, y - 1, w + 2, hh + bh + 2, rad + 1, rad + 1);
			StrokePoly(R, P2, 255, 170, 40, 255);
		}
		else
			StrokePoly(R, P, 18, 18, 20, 255);

		int ts = (int)(12 * z + 0.5f);
		if (ts >= 7)
			FTxt.Draw(R, N.Name, x + 10 * z, y + hh * 0.5f, std::min(ts, 40), true, 255, 255, 255, 0);

		// pins
		{	// fold indicator at the right end of the header: down = expanded, right = collapsed
			std::vector<SDL_FPoint> T;
			float ax = x + w - 12 * z, ay = y + hh * 0.5f, s = 4.0f * z;
			SDL_FPoint a, b, c;
			if (N.Collapsed) { a.x = ax - s * 0.5f; a.y = ay - s; b.x = ax - s * 0.5f; b.y = ay + s; c.x = ax + s; c.y = ay; }
			else             { a.x = ax - s; a.y = ay - s * 0.5f; b.x = ax + s; b.y = ay - s * 0.5f; c.x = ax; c.y = ay + s; }
			T.push_back(a); T.push_back(b); T.push_back(c);
			FillPoly(R, T, 255, 255, 255, 200);
		}
		int ps = (int)(11 * z + 0.5f);
		if (N.Kind == nkMouse && !N.Collapsed && ps >= 7)		// the mode is shown in the body (left side; X / Y pins are on the right)
			FTxt.Draw(R, N.Val[0] > 0.5f ? String(L"\u4E0D\u70B9\u51FB\u63A7\u5236") : String(L"\u70B9\u51FB\u63A7\u5236"),
					  x + 10 * z, y + hh + ROW_H * 0.5f * z, std::min(ps, 36), true, 255, 230, 140, 0);
		for (int o = 0; o < 2; o++)
		{
			int cnt = N.Collapsed ? 0 : (o ? D.NOut : D.NIn);
			for (int k = 0; k < cnt; k++)
			{
				const TPinDef &PD = o ? D.Out[k] : D.In[k];
				float px, py;
				PinPos(N, o == 1, k, px, py);
				bool hot = (Hover.Node == N.Id) && (Hover.Kind == (o ? hkOut : hkIn)) && (Hover.Pin == k);
				float pr = PIN_R * z * (hot ? 1.35f : 1.0f);
				int cr, cg, cb;
				PinColor(PD.Type, cr, cg, cb);
				std::vector<SDL_FPoint> C;
				CirclePath(C, px, py, pr + 1.5f);
				FillPoly(R, C, 18, 18, 20, 255);
				CirclePath(C, px, py, pr);
				if (hot) FillPoly(R, C, 255, 255, 255, 255);
				else     FillPoly(R, C, cr, cg, cb, 255);
				if (ps >= 7)
				{
					if (o == 1 && N.Kind == nkValue)		// the output pin shows the value itself
						FTxt.Draw(R, FloatToStrF(N.Val[0], ffGeneral, 5, 0), px - (PIN_R + 6) * z, py, std::min(ps, 36), true, 255, 230, 140, 1);
					else if (o == 1 && N.Kind == nkKeys && k == 0)		// the first output shows which key this node reads
						FTxt.Draw(R, String(KeyNameOf(N)) + L" \u6309\u4E0B", px - (PIN_R + 6) * z, py, std::min(ps, 36), true, 255, 230, 140, 1);
					else if (o == 0) FTxt.Draw(R, PD.Name, px + (PIN_R + 6) * z, py, std::min(ps, 36), false, 220, 220, 220, 0);
					else        FTxt.Draw(R, PD.Name, px - (PIN_R + 6) * z, py, std::min(ps, 36), false, 220, 220, 220, 1);
				}
			}
		}
	}

	void __fastcall Render2D(TObject *Sender, SDL_Renderer *R)
	{
		if (!Showing)						// hidden (3D is active): nothing to draw
			return;
		int w = ClientWidth, h = ClientHeight;
		if (w < 2 || h < 2)
			return;
		SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);

		// ---- grid (all lines use the grid colours; no coloured axes) ----
		int M = FMajorEvery < 2 ? 2 : FMajorEvery;
		float s = FStep * FZoom;
		while (s < 8.0f) s *= M;			// zoomed far out: thin the grid out
		float ox = w * 0.5f + FPanX, oy = h * 0.5f + FPanY;
		int i0 = (int)floorf(-ox / s), i1 = (int)ceilf((w - ox) / s);
		for (int i = i0; i <= i1; i++)
		{
			if ((((i % M) + M) % M) == 0) SetColor(R, 95, 95, 95); else SetColor(R, 62, 62, 62);
			float x = ox + i * s;
			SDL_RenderLine(R, x, 0, x, (float)h);
		}
		int j0 = (int)floorf(-oy / s), j1 = (int)ceilf((h - oy) / s);
		for (int j = j0; j <= j1; j++)
		{
			if ((((j % M) + M) % M) == 0) SetColor(R, 95, 95, 95); else SetColor(R, 62, 62, 62);
			float y = oy + j * s;
			SDL_RenderLine(R, 0, y, (float)w, y);
		}

		// ---- group frames (behind links and nodes) ----
		for (size_t g = 0; g < FGroups.size(); g++)
		{
			float gx0, gy0, gx1, gy1;
			if (!GroupRect(FGroups[g].Id, gx0, gy0, gx1, gy1)) continue;
			float x = SX(gx0), y = SY(gy0), fw = (gx1 - gx0) * FZoom, fh = (gy1 - gy0) * FZoom, th = GRP_TITLE * FZoom;
			if (x > w || x + fw < 0 || y > h || y + fh < 0) continue;
			bool sel = GroupAllSelected(FGroups[g].Id);
			float rad = std::max(2.0f, 8.0f * FZoom);
			std::vector<SDL_FPoint> P;
			RoundPath(P, x, y, fw, fh, rad, rad);
			FillPoly(R, P, 90, 115, 160, 40);						// frame body
			bool folded = GroupFolded(FGroups[g].Id);
			RoundPath(P, x, y, fw, th, rad, folded ? rad : 0.0f);
			FillPoly(R, P, 90, 115, 160, 120);						// title bar
			RoundPath(P, x, y, fw, fh, rad, rad);
			if (sel) StrokePoly(R, P, 255, 170, 40, 255);
			else     StrokePoly(R, P, 130, 160, 210, 200);
			{	// fold arrow at the right end of the title bar (down = expanded, right = every node folded); double click the bar
				bool allFolded = GroupFolded(FGroups[g].Id);
				std::vector<SDL_FPoint> T;
				float ax = x + fw - 12 * FZoom, ay = y + th * 0.5f, as = 4.0f * FZoom;
				SDL_FPoint a, b, c;
				if (allFolded) { a.x = ax - as * 0.5f; a.y = ay - as; b.x = ax - as * 0.5f; b.y = ay + as; c.x = ax + as; c.y = ay; }
				else           { a.x = ax - as; a.y = ay - as * 0.5f; b.x = ax + as; b.y = ay - as * 0.5f; c.x = ax; c.y = ay + as; }
				T.push_back(a); T.push_back(b); T.push_back(c);
				FillPoly(R, T, 255, 255, 255, 200);
			}
			int gs = (int)(12 * FZoom + 0.5f);
			if (gs >= 7)
				FTxt.Draw(R, FGroups[g].Name, x + 10 * FZoom, y + th * 0.5f, std::min(gs, 40), true, 255, 255, 255, 0);
		}

		// ---- links ----
		for (size_t i = 0; i < FLinks.size(); i++)
		{
			int a = FindNode(FLinks[i].From), b = FindNode(FLinks[i].To);
			if (a < 0 || b < 0) continue;
			if (FNodes[a].Group != 0 && FNodes[a].Group == FNodes[b].Group && NodeHidden(FNodes[a])) continue;		// link inside a folded group
			float x0, y0, x3, y3;
			PinPos(FNodes[a], true,  FLinks[i].FromPin, x0, y0);
			PinPos(FNodes[b], false, FLinks[i].ToPin,   x3, y3);
			bool sel = FNodes[a].Sel || FNodes[b].Sel;
			int cr, cg, cb;
			PinColor(KindDef(FNodes[a].Kind).Out[FLinks[i].FromPin].Type, cr, cg, cb);
			if (sel) DrawLink(R, x0, y0, x3, y3, 255, 170, 40, 255);
			else     DrawLink(R, x0, y0, x3, y3, cr, cg, cb, 235);
		}

		// ---- nodes ----
		THit Hover; Hover.Kind = hkNone; Hover.Node = 0; Hover.Pin = 0;
		if (FDrag == dgNone || FDrag == dgLink) Hover = HitTest(FMouseX, FMouseY);
		for (size_t i = 0; i < FNodes.size(); i++)
			DrawNode(R, FNodes[i], Hover);

		// ---- link being dragged ----
		if (FDrag == dgLink)
		{
			int a = FindNode(FLinkNode);
			if (a >= 0)
			{
				const TKindDef &D = KindDef(FNodes[a].Kind);
				int cr, cg, cb;
				PinColor(FLinkFromOut ? D.Out[FLinkPin].Type : D.In[FLinkPin].Type, cr, cg, cb);
				float px, py;
				PinPos(FNodes[a], FLinkFromOut, FLinkPin, px, py);
				if (FLinkFromOut) DrawLink(R, px, py, (float)FMouseX, (float)FMouseY, cr, cg, cb, 255);
				else              DrawLink(R, (float)FMouseX, (float)FMouseY, px, py, cr, cg, cb, 255);
			}
		}

		// ---- rubber band ----
		if (FDrag == dgRubber)
		{
			SDL_FRect Rc;
			Rc.x = (float)std::min(FDownX, FMouseX); Rc.y = (float)std::min(FDownY, FMouseY);
			Rc.w = (float)abs(FMouseX - FDownX);     Rc.h = (float)abs(FMouseY - FDownY);
			SetColor(R, 80, 140, 220, 50);  SDL_RenderFillRect(R, &Rc);
			SetColor(R, 80, 140, 220, 255); SDL_RenderRect(R, &Rc);
		}

		// viewport name + zoom
		char Buf[48];
		sprintf(Buf, "2D  %d%%  [rev 1525  create-menu items: %d]", (int)(FZoom * 100.0f + 0.5f), FMenu ? FMenu->Items->Items[0]->Count : -1);		// must read 10; if not, the exe is stale
		SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
		SDL_RenderDebugText(R, 8, 8, Buf);
	}

protected:
	DYNAMIC void __fastcall MouseDown(TMouseButton Button, TShiftState Shift, int X, int Y)
	{
		TSDLPanel::MouseDown(Button, Shift, X, Y);
		if (CanFocus() && !Focused()) SetFocus();
		FDownX = FLastX = FMouseX = X;
		FDownY = FLastY = FMouseY = Y;

		if (Button == mbMiddle)
		{
			FDrag = dgPan;
		}
		else if (Button == mbLeft)
		{
			THit H = HitTest(X, Y);
			if (H.Kind == hkNode && ToggleOnDoubleClick(H.Node, X, Y))
			{
				FDrag = dgNone;			// double click on a header: folded / unfolded, no drag
			}
			else if (H.Kind == hkOut)
			{
				FDrag = dgLink; FLinkNode = H.Node; FLinkPin = H.Pin; FLinkFromOut = true;
			}
			else if (H.Kind == hkIn)
			{
				FDrag = dgLink;
				int li = -1;
				for (int i = (int)FLinks.size() - 1; i >= 0; i--)
					if (FLinks[i].To == H.Node && FLinks[i].ToPin == H.Pin) { li = i; break; }
				if (li >= 0)		// pick up the existing link by its input end
				{
					FLinkNode = FLinks[li].From; FLinkPin = FLinks[li].FromPin; FLinkFromOut = true;
					FLinks.erase(FLinks.begin() + li);
				}
				else
				{
					FLinkNode = H.Node; FLinkPin = H.Pin; FLinkFromOut = false;
				}
			}
			else if (H.Kind == hkGroup && ToggleGroupOnDoubleClick(H.Node, X, Y))
			{
				FDrag = dgNone;			// double click on a group title: fold / unfold its nodes
			}
			else if (H.Kind == hkGroup)			// title bar of a group frame: select all its nodes, drag moves them all
			{
				SelectGroup(H.Node, Shift.Contains(ssCtrl));
				FDrag = dgMove;
			}
			else if (H.Kind == hkNode)
			{
				int idx = FindNode(H.Node);
				if (Shift.Contains(ssCtrl))
					FNodes[idx].Sel = !FNodes[idx].Sel;
				else if (!FNodes[idx].Sel)
				{
					ClearSel();
					FNodes[idx].Sel = true;
				}
				BringToFront(H.Node);
				FDrag = dgMove;
			}
			else
			{
				if (!Shift.Contains(ssCtrl)) ClearSel();
				FDrag = dgRubber;
			}
		}
		else if (Button == mbRight)
		{
			THit H = HitTest(X, Y);
			FMenuGroup = (H.Kind == hkGroup) ? H.Node : 0;
			FMenuNode  = (H.Kind != hkNone && H.Kind != hkGroup) ? H.Node : 0;
			if (FMenuGroup)
			{
				if (!GroupAllSelected(FMenuGroup)) SelectGroup(FMenuGroup, false);
			}
			else if (FMenuNode)
			{
				int idx = FindNode(FMenuNode);
				if (idx >= 0 && !FNodes[idx].Sel) { ClearSel(); FNodes[idx].Sel = true; }
			}
			else
				ClearSel();
		}
	}

	DYNAMIC void __fastcall MouseMove(TShiftState Shift, int X, int Y)
	{
		TSDLPanel::MouseMove(Shift, X, Y);
		FMouseX = X; FMouseY = Y;
		if (FDrag == dgNone) FocusIfIdle();
		float dx = (float)(X - FLastX), dy = (float)(Y - FLastY);
		if (FDrag == dgPan)
		{
			FPanX += dx; FPanY += dy;
		}
		else if (FDrag == dgMove)
		{
			for (size_t i = 0; i < FNodes.size(); i++)
				if (FNodes[i].Sel) { FNodes[i].X += dx / FZoom; FNodes[i].Y += dy / FZoom; }
		}
		FLastX = X; FLastY = Y;
	}

	DYNAMIC void __fastcall MouseUp(TMouseButton Button, TShiftState Shift, int X, int Y)
	{
		TSDLPanel::MouseUp(Button, Shift, X, Y);
		FMouseX = X; FMouseY = Y;
		if (Button == mbLeft)
		{
			if (FDrag == dgLink)
			{
				THit H = HitTest(X, Y);
				if (H.Kind == hkIn || H.Kind == hkOut) TryLink(H);
			}
			else if (FDrag == dgRubber)
			{
				float x0 = WX((float)std::min(FDownX, X)), x1 = WX((float)std::max(FDownX, X));
				float y0 = WY((float)std::min(FDownY, Y)), y1 = WY((float)std::max(FDownY, Y));
				for (size_t i = 0; i < FNodes.size(); i++)
				{
					TNode &N = FNodes[i];
					if (NodeHidden(N)) continue;
					if (N.X < x1 && N.X + NODE_W > x0 && N.Y < y1 && N.Y + NodeHN(N) > y0)
						N.Sel = true;
				}
			}
			FDrag = dgNone;
		}
		else if (Button == mbMiddle)
		{
			FDrag = dgNone;
		}
		else if (Button == mbRight)
		{
			ShowMenu(X, Y);
		}
	}

	DYNAMIC bool __fastcall DoMouseWheel(TShiftState Shift, int WheelDelta, const TPoint &MousePos)
	{
		TPoint P = ScreenToClient(MousePos);
		float f  = powf(1.1f, WheelDelta / 120.0f);
		float nz = std::min(4.0f, std::max(0.1f, FZoom * f));
		f = nz / FZoom;
		float cx = ClientWidth * 0.5f, cy = ClientHeight * 0.5f;
		FPanX = (P.x - cx) - (P.x - cx - FPanX) * f;		// the world point under the cursor stays put
		FPanY = (P.y - cy) - (P.y - cy - FPanY) * f;
		FZoom = nz;
		return true;
	}

	DYNAMIC void __fastcall KeyDown(WORD &Key, TShiftState Shift)
	{
		TSDLPanel::KeyDown(Key, Shift);
		if (Key == VK_DELETE)
		{
			DeleteSelected(); Key = 0;
		}
		else if (Shift.Contains(ssCtrl) && Key == 'C')
		{
			CopySelected(); Key = 0;
		}
		else if (Shift.Contains(ssCtrl) && Key == 'V')
		{
			PasteAt(WX((float)FMouseX), WY((float)FMouseY)); Key = 0;
		}
		else if (Shift.Contains(ssCtrl) && Key == 'G')
		{
			if (Shift.Contains(ssShift)) UngroupSelected(); else GroupSelected();
			Key = 0;
		}
		else if (!Shift.Contains(ssCtrl) && !Shift.Contains(ssAlt) && Key == 'Z')
		{
			LocateSelected(); Key = 0;		// Z: go to the current node
		}
		else if (!Shift.Contains(ssCtrl) && !Shift.Contains(ssAlt) && Key == 'F')
		{
			FitAll(); Key = 0;				// F: fit everything into the window
		}
		else if (!Shift.Contains(ssCtrl) && !Shift.Contains(ssAlt) && Key == 'X')
		{
			ZoomActual(); Key = 0;			// X: 100%
		}
		else if (Key == VK_F2)
		{
			int id = SingleSelected();
			if (id) RenameNode(id);
			Key = 0;
		}
	}

public:
	__fastcall TViewport2D(TComponent *Owner)
		: TSDLPanel(Owner), FStep(20.0f), FMajorEvery(5),
		  FZoom(1.0f), FPanX(0), FPanY(0), FNextId(1), FNextGroup(1), FGroupCounter(0), FDrag(dgNone),
		  FLastX(0), FLastY(0), FDownX(0), FDownY(0), FMouseX(0), FMouseY(0),
		  FLinkNode(0), FLinkPin(0), FLinkFromOut(true), FMenu(NULL), FmiCopy(NULL), FmiPaste(NULL), FmiRename(NULL),
		  FmiGroup(NULL), FmiUngroup(NULL),
		  FMenuWX(0), FMenuWY(0), FMenuNode(0), FMenuGroup(0), FRunT(0), FRunH(0), FRunV(0), FRunS(0), FRunKeys(0), FRunMX(0), FRunMY(0), FCollide(true)
	{
		for (int k = 0; k < NK_COUNT; k++) FCounter[k] = 0;
		for (int k = 0; k < 256; k++) FRunDown[k] = false;
		FClickTick = 0; FClickNode = 0; FClickX = FClickY = 0;
		OnRender = Render2D;
		BuildMenu();
	}

	// ---- running the graph (called every frame by the render window) -----
	bool HasRunOutput()
	{
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Kind == nkOutput) return true;
		return false;
	}

	// mouse node: -1 = none (the mouse does not move the camera), 0 = click + drag, 1 = no click needed
	int MouseMode()
	{
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Kind == nkMouse) return FNodes[i].Val[0] > 0.5f ? 1 : 0;
		return -1;
	}

	// camera node: position in run-window coordinates (Y up); false when there is none. Call after RunStep.
	bool GetRunCamera(float &x, float &y, float &z)
	{
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Kind == nkCamera)
			{
				int id = FNodes[i].Id;
				x = EvalIn(id, 0); y = EvalIn(id, 1); z = -EvalIn(id, 2);		// same Z convention as the objects
				return true;
			}
		return false;
	}

	// the run window tells which single keys are down (bit 0..7 = W A S D Up Down Left Right); call before RunStep
	void SetRunKeys(unsigned Mask) { FRunKeys = Mask; }
	// mouse position in the run window: x / y in -1..1 from the window centre (right / up positive); read by the "Mouse" node outputs X / Y; call before RunStep
	void SetRunMouse(float x, float y) { FRunMX = x; FRunMY = y; }
	// Down[vk] = true while that virtual key is down (256 entries); read by the "Key" nodes
	void SetRunKeyStates(const bool *Down) { for (int k = 0; k < 256; k++) FRunDown[k] = Down[k]; }

	void RunReset()
	{
		FAccum.clear();
		FJumpT.clear();
		FCol.clear();
		FGrav.clear();
		FReqY.clear();
		FJumpHeld.clear();
	}

	// t = seconds since the run started, dt = frame time, kh / kv / ks = keyboard (horizontal, vertical, space).
	// Fills Out with the objects connected to the first "Run output" node.
	void RunStep(float t, float dt, float kh, float kv, float ks, std::vector<TRunObj> &Out)
	{
		Out.clear();
		FRunT = t; FRunH = kh; FRunV = kv; FRunS = ks;
		for (size_t i = 0; i < FNodes.size(); i++)			// integrate the accumulators
			if (FNodes[i].Kind == nkAccum || FNodes[i].Kind == nkSub)
			{
				int id = FNodes[i].Id;
				FAccum[id] += (FNodes[i].Kind == nkSub ? -1.0f : 1.0f) * EvalIn(id, 0) * EvalIn(id, 1) * dt;
			}
		//for (size_t i = 0; i < FNodes.size(); i++)			// jumps: start on the rising edge of the trigger, run to the end
		//	if (FNodes[i].Kind == nkJump)
		//	{
		//		int id = FNodes[i].Id;
		//		bool trig = EvalIn(id, 0) > 0.5f;
		//		float &T = FJumpT.insert(std::make_pair(id, -1.0f)).first->second;
		//		bool &Held = FJumpHeld[id];
		//		if (T >= 0.0f)
		//		{
		//			T += dt;
		//			if (T >= std::max(0.05f, EvalIn(id, 3))) T = -1.0f;
		//		}
		//		else if (trig && !Held)
		//			T = 0.0f;
		//		Held = trig;
		//	}
		for (size_t i = 0; i < FNodes.size(); i++)			// gravity: jump on the rising edge, fall, land on the ground
			if (FNodes[i].Kind == nkGravity)
			{
				int id = FNodes[i].Id;
				float ground = EvalIn(id, 3);
				std::map<int, TGrav>::iterator it = FGrav.find(id);
				if (it == FGrav.end())
				{
					TGrav G0; G0.Y = ground; G0.Vy = 0.0f; G0.On = true; G0.Held = false;
					it = FGrav.insert(std::make_pair(id, G0)).first;
				}
				TGrav &G = it->second;
				bool trig = EvalIn(id, 0) > 0.5f;
				if (G.On && trig && !G.Held) { G.Vy = EvalIn(id, 1); G.On = false; }
				G.Held = trig;
				G.Vy -= EvalIn(id, 2) * dt;
				G.Y  += G.Vy * dt;
				if (G.Y <= ground) { G.Y = ground; if (G.Vy < 0.0f) G.Vy = 0.0f; G.On = true; }
				else if (G.Vy != 0.0f) G.On = false;		// in the air; standing on a box is set again by the collision step
			}
		int OutId = 0;
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Kind == nkOutput) { OutId = FNodes[i].Id; break; }
		if (!OutId) return;
		for (size_t i = 0; i < FLinks.size(); i++)
			if (FLinks[i].To == OutId && FLinks[i].ToPin == 0)
				BuildObj(FLinks[i].From, Out);
		if (FCollide) ResolveCollisions(Out);
	}

	// Box-box collisions. Every frame an object moves by what the graph asks (its change since the last frame),
	// starting from where it was resolved last frame; then overlapping boxes are pushed apart along the axis of
	// least penetration. Only boxes that moved are pushed (a box nobody moves is a wall); if both moved, both give way.
	// Planes are the ground and are not solid. Boxes are treated as axis-aligned boxes around their rotated footprint.
	// the Gravity node that drives the height (pin 1) of this object node, or 0
	int GravityOf(int ObjId)
	{
		for (size_t i = 0; i < FLinks.size(); i++)
			if (FLinks[i].To == ObjId && FLinks[i].ToPin == 1)
			{
				int k = FindNode(FLinks[i].From);
				if (k >= 0 && FNodes[k].Kind == nkGravity) return FNodes[k].Id;
			}
		return 0;
	}

	void ResolveCollisions(std::vector<TRunObj> &Objs)
	{
		size_t n = Objs.size();
		std::vector<bool>  moved(n, false);
		std::vector<float> hx(n, 0), hy(n, 0);
		for (size_t i = 0; i < n; i++)
		{
			TRunObj &O = Objs[i];
			std::map<int, TColState>::iterator it = FCol.find(O.Id);
			TColState St;
			St.Rx = O.X; St.Ry = O.Y; St.Rz = O.Z;
			if (it != FCol.end())
			{
				float dx = O.X - it->second.Rx, dy = O.Y - it->second.Ry, dz = O.Z - it->second.Rz;
				moved[i] = fabsf(dx) + fabsf(dy) + fabsf(dz) > 1e-5f;
				O.X = it->second.Px + dx; O.Z = it->second.Pz + dz;
				if (GravityOf(O.Id) == 0) O.Y = it->second.Py + dy;		// a gravity node gives the absolute height
			}
			St.Px = O.X; St.Py = O.Y; St.Pz = O.Z;
			FCol[O.Id] = St;
			FReqY[O.Id] = St.Ry;
			float a = O.Rot * 3.14159265f / 180.0f, sc = fabsf(O.Scale);
			hx[i] = 0.5f * sc * (fabsf(cosf(a)) + fabsf(sinf(a)));
			hy[i] = 0.5f * sc;
		}
		for (int pass = 0; pass < 4; pass++)
			for (size_t i = 0; i < n; i++)
				for (size_t j = i + 1; j < n; j++)
				{
					TRunObj &A = Objs[i], &B = Objs[j];
					if (A.Kind != nkBox || B.Kind != nkBox) continue;
					if (!moved[i] && !moved[j]) continue;
					float d[3]  = { B.X - A.X, B.Y - A.Y, B.Z - A.Z };
					float ov[3] = { hx[i] + hx[j] - fabsf(d[0]), hy[i] + hy[j] - fabsf(d[1]), hx[i] + hx[j] - fabsf(d[2]) };
					if (ov[0] <= 0 || ov[1] <= 0 || ov[2] <= 0) continue;
					int ax = 0;
					if (ov[1] < ov[ax]) ax = 1;
					if (ov[2] < ov[ax]) ax = 2;
					float push = ov[ax] * (d[ax] < 0 ? -1.0f : 1.0f);		// B moves by +push, A by -push
					float fa = (moved[i] && !moved[j]) ? 1.0f : ((moved[j] && !moved[i]) ? 0.0f : 0.5f);
					float *pa = ax == 0 ? &A.X : (ax == 1 ? &A.Y : &A.Z);
					float *pb = ax == 0 ? &B.X : (ax == 1 ? &B.Y : &B.Z);
					*pa -= push * fa;
					*pb += push * (1.0f - fa);
				}
		for (size_t i = 0; i < n; i++)
		{
			TColState &St = FCol[Objs[i].Id];
			St.Px = Objs[i].X; St.Py = Objs[i].Y; St.Pz = Objs[i].Z;
			int g = GravityOf(Objs[i].Id);
			if (g)			// feed the resolved height back: standing on a box = landed, bumping a ceiling = stop rising
			{
				std::map<int, TGrav>::iterator it = FGrav.find(g);
				if (it != FGrav.end())
				{
					TGrav &G = it->second;
					float req = FReqY[Objs[i].Id];
					G.Y = Objs[i].Y;
					if (Objs[i].Y > req + 1e-4f)      { if (G.Vy < 0.0f) G.Vy = 0.0f; G.On = true; }
					else if (Objs[i].Y < req - 1e-4f) { if (G.Vy > 0.0f) G.Vy = 0.0f; }
				}
			}
		}
	}

	// the "Create" panel: new node of this kind in the middle of the view (selected)
	int CreateNodeAtCenter(int Kind)
	{
		float wx = WX(ClientWidth * 0.5f) - NODE_W * 0.5f, wy = WY(ClientHeight * 0.5f);
		AddNode(Kind, wx, wy);
		return FNodes.back().Id;
	}

	// ---- link to the 3D scene (used by SceneLink.h) -----------------------
	struct TLinkInfo { int Id; int Kind; int Obj; String Name; float V[5]; };

	// all Box / Plane nodes; V = value of the 5 inputs (stored value, or the pin default)
	void GetLinkNodes(std::vector<TLinkInfo> &Out)
	{
		Out.clear();
		for (size_t i = 0; i < FNodes.size(); i++)
		{
			const TNode &N = FNodes[i];
			if (N.Kind != nkBox && N.Kind != nkPlane) continue;
			TLinkInfo L;
			L.Id = N.Id; L.Kind = N.Kind; L.Obj = N.Obj; L.Name = N.Name;
			for (int k = 0; k < 5; k++) L.V[k] = N.HasVal ? N.Val[k] : KindDef(N.Kind).In[k].Def;
			Out.push_back(L);
		}
	}

	// new node for a 3D object; placed below the existing nodes, the selection is not changed
	int AddLinkedNode(int Kind, const String &Name, int Obj, const float *V)
	{
		TNode N;
		N.Id = FNextId++;
		N.Kind = Kind;
		N.Name = Name;
		N.X = 0; N.Y = 0;
		N.Group = 0; N.Sel = false; N.Collapsed = false;
		N.Obj = Obj; N.HasVal = true;
		for (int k = 0; k < 5; k++) N.Val[k] = V[k];
		float minx = -300.0f, bottom = -240.0f;
		if (!FNodes.empty())
		{
			minx = FNodes[0].X; bottom = FNodes[0].Y + NodeHN(FNodes[0]);
			for (size_t i = 1; i < FNodes.size(); i++)
			{
				minx = std::min(minx, FNodes[i].X);
				bottom = std::max(bottom, FNodes[i].Y + NodeHN(FNodes[i]));
			}
		}
		N.X = minx;
		N.Y = bottom + 40.0f;
		++FCounter[Kind];
		FNodes.push_back(N);
		return N.Id;
	}

	void SetNodeObj(int Id, int Obj)
	{
		int i = FindNode(Id);
		if (i >= 0) FNodes[i].Obj = Obj;
	}

	void SetNodeName(int Id, const String &Name)
	{
		int i = FindNode(Id);
		if (i >= 0) FNodes[i].Name = Name;
	}

	void SetNodeVals(int Id, const float *V)
	{
		int i = FindNode(Id);
		if (i < 0) return;
		FNodes[i].HasVal = true;
		for (int k = 0; k < 5; k++) FNodes[i].Val[k] = V[k];
	}

	void RemoveNode(int Id)
	{
		int i = FindNode(Id);
		if (i < 0) return;
		RemoveLinksOf(Id);
		FNodes.erase(FNodes.begin() + i);
		CleanGroups();
	}

	// select the nodes that belong to the given 3D object uids
	void SelectNodesByObj(const std::vector<int> &Uids)
	{
		for (size_t i = 0; i < FNodes.size(); i++)
			FNodes[i].Sel = FNodes[i].Obj != 0 &&
				std::find(Uids.begin(), Uids.end(), FNodes[i].Obj) != Uids.end();
	}

	// ---- .yu file: save / open the node graph ------------------------------
	// Text file, UTF-8, one record per line, fields separated by TAB:
	//   YU <version>
	//   view  zoom panX panY
	//   ids   nextNodeId nextGroupId groupCounter
	//   counters  c0 c1 ...                     (per node kind: used for the "方块 3" style names)
	//   group id collapsed name
	//   node  id kind x y group collapsed hasVal v0 v1 v2 v3 v4 name
	//   link  fromNode fromPin toNode toPin
	// The 3D objects are not stored separately: every Box / Plane object has a node (see SceneLink.h),
	// so opening a file recreates the objects from the node values (name, position, rotation, size).
	// Decimal numbers are always written with '.', whatever the Windows region is.

	int NodeCount() { return (int)FNodes.size(); }

	// empty graph, default view ("New")
	void ClearGraph()
	{
		FNodes.clear(); FLinks.clear(); FGroups.clear();
		FNextId = 1; FNextGroup = 1; FGroupCounter = 0;
		for (int k = 0; k < NK_COUNT; k++) FCounter[k] = 0;
		FZoom = 1.0f; FPanX = FPanY = 0.0f;
		FDrag = dgNone; FClickNode = 0;
		RunReset();
	}

private:
	static String YuNum(float v)
	{
		TFormatSettings FS = TFormatSettings::Create(L"en-US");
		return FloatToStrF((long double)v, ffGeneral, 9, 0, FS);
	}

	static float YuNumIn(const String &S, float Def)
	{
		TFormatSettings FS = TFormatSettings::Create(L"en-US");
		return (float)StrToFloatDef(S.Trim(), (long double)Def, FS);
	}

	static String YuEsc(const String &S)
	{
		String R;
		for (int i = 1; i <= S.Length(); i++)
		{
			wchar_t c = S[i];
			if      (c == L'\\') R += L"\\\\";
			else if (c == L'\t') R += L"\\t";
			else if (c == L'\n') R += L"\\n";
			else if (c == L'\r') R += L"\\r";
			else                 R += c;
		}
		return R;
	}

	static String YuUnesc(const String &S)
	{
		String R;
		for (int i = 1; i <= S.Length(); i++)
		{
			wchar_t c = S[i];
			if (c == L'\\' && i < S.Length())
			{
				wchar_t d = S[++i];
				R += (d == L't') ? L'\t' : (d == L'n') ? L'\n' : (d == L'r') ? L'\r' : d;
			}
			else R += c;
		}
		return R;
	}

	static void YuSplit(const String &Line, std::vector<String> &F)
	{
		F.clear();
		int start = 1;
		for (int i = 1; i <= Line.Length(); i++)
			if (Line[i] == L'\t')
			{
				F.push_back(Line.SubString(start, i - start));
				start = i + 1;
			}
		F.push_back(Line.SubString(start, Line.Length() - start + 1));
	}

public:
	bool SaveToFile(const String &Path, String &Err)
	{
		try
		{
			std::unique_ptr<TStringList> L(new TStringList);
			L->Add(L"YU\t1");
			L->Add(L"view\t" + YuNum(FZoom) + L"\t" + YuNum(FPanX) + L"\t" + YuNum(FPanY));
			L->Add(L"ids\t" + IntToStr(FNextId) + L"\t" + IntToStr(FNextGroup) + L"\t" + IntToStr(FGroupCounter));
			String C = L"counters";
			for (int k = 0; k < NK_COUNT; k++) C += L"\t" + IntToStr(FCounter[k]);
			L->Add(C);
			for (size_t i = 0; i < FGroups.size(); i++)
				L->Add(L"group\t" + IntToStr(FGroups[i].Id) + L"\t" + IntToStr(FGroups[i].Collapsed ? 1 : 0) + L"\t" + YuEsc(FGroups[i].Name));
			for (size_t i = 0; i < FNodes.size(); i++)
			{
				const TNode &N = FNodes[i];
				String S = L"node\t" + IntToStr(N.Id) + L"\t" + IntToStr(N.Kind) + L"\t" + YuNum(N.X) + L"\t" + YuNum(N.Y) +
						   L"\t" + IntToStr(N.Group) + L"\t" + IntToStr(N.Collapsed ? 1 : 0) + L"\t" + IntToStr(N.HasVal ? 1 : 0);
				for (int k = 0; k < 5; k++) S += L"\t" + YuNum(N.Val[k]);
				S += L"\t" + YuEsc(N.Name);
				L->Add(S);
			}
			for (size_t i = 0; i < FLinks.size(); i++)
				L->Add(L"link\t" + IntToStr(FLinks[i].From) + L"\t" + IntToStr(FLinks[i].FromPin) + L"\t" +
					   IntToStr(FLinks[i].To) + L"\t" + IntToStr(FLinks[i].ToPin));
			L->SaveToFile(Path, TEncoding::UTF8);
			return true;
		}
		catch (Exception &E)
		{
			Err = E.Message;
			return false;
		}
	}

	// the current graph stays untouched when the file cannot be read
	bool LoadFromFile(const String &Path, String &Err)
	{
		std::vector<TNode>  Nodes;
		std::vector<TLink>  Links;
		std::vector<TGroup> Groups;
		int nextId = 1, nextGroup = 1, groupCounter = 0;
		int counters[NK_COUNT];
		for (int k = 0; k < NK_COUNT; k++) counters[k] = 0;
		float zoom = 1.0f, panX = 0.0f, panY = 0.0f;
		int skipped = 0;
		try
		{
			std::unique_ptr<TStringList> L(new TStringList);
			L->LoadFromFile(Path, TEncoding::UTF8);
			if (L->Count == 0) { Err = L"\u6587\u4EF6\u662F\u7A7A\u7684"; return false; }
			std::vector<String> F;
			YuSplit(L->Strings[0], F);
			if (F.size() < 2 || F[0] != L"YU") { Err = L"\u4E0D\u662F\u6709\u6548\u7684 .yu \u6587\u4EF6"; return false; }
			if (StrToIntDef(F[1], 0) > 1) { Err = L"\u6587\u4EF6\u7248\u672C\u592A\u65B0\uFF0C\u8BF7\u4F7F\u7528\u65B0\u7248\u672C\u7A0B\u5E8F\u6253\u5F00"; return false; }

			for (int i = 1; i < L->Count; i++)
			{
				YuSplit(L->Strings[i], F);
				if (F.empty()) continue;
				const String &T = F[0];
				if (T == L"view" && F.size() >= 4)
				{
					zoom = std::min(4.0f, std::max(0.1f, YuNumIn(F[1], 1.0f)));
					panX = YuNumIn(F[2], 0.0f); panY = YuNumIn(F[3], 0.0f);
				}
				else if (T == L"ids" && F.size() >= 4)
				{
					nextId = StrToIntDef(F[1], 1); nextGroup = StrToIntDef(F[2], 1); groupCounter = StrToIntDef(F[3], 0);
				}
				else if (T == L"counters")
				{
					for (int k = 0; k < NK_COUNT && (size_t)(k + 1) < F.size(); k++) counters[k] = StrToIntDef(F[k + 1], 0);
				}
				else if (T == L"group" && F.size() >= 4)
				{
					TGroup G;
					G.Id = StrToIntDef(F[1], 0); G.Collapsed = StrToIntDef(F[2], 0) != 0; G.Name = YuUnesc(F[3]);
					if (G.Id > 0) Groups.push_back(G);
				}
				else if (T == L"node" && F.size() >= 13)
				{
					TNode N;
					N.Id    = StrToIntDef(F[1], 0);
					N.Kind  = StrToIntDef(F[2], -1);
					N.X = YuNumIn(F[3], 0.0f); N.Y = YuNumIn(F[4], 0.0f);
					N.Group = StrToIntDef(F[5], 0);
					N.Collapsed = StrToIntDef(F[6], 0) != 0;
					N.HasVal    = StrToIntDef(F[7], 0) != 0;
					for (int k = 0; k < 5; k++) N.Val[k] = YuNumIn(F[8 + k], 0.0f);
					N.Name = (F.size() > 13) ? YuUnesc(F[13]) : String(L"");
					N.Obj = 0; N.Sel = false;			// the 3D object is created again by SceneLink
					bool dup = false;
					for (size_t k = 0; k < Nodes.size(); k++) if (Nodes[k].Id == N.Id) dup = true;
					if (N.Id > 0 && N.Kind >= 0 && N.Kind < NK_COUNT && !dup)
					{
						if (N.Name.IsEmpty()) N.Name = KindName(N.Kind);
						Nodes.push_back(N);
					}
					else skipped++;
				}
				else if (T == L"link" && F.size() >= 5)
				{
					TLink K;
					K.From = StrToIntDef(F[1], 0); K.FromPin = StrToIntDef(F[2], -1);
					K.To   = StrToIntDef(F[3], 0); K.ToPin   = StrToIntDef(F[4], -1);
					Links.push_back(K);
				}
			}
		}
		catch (Exception &E)
		{
			Err = E.Message;
			return false;
		}

		// ---- commit ----
		ClearGraph();
		FGroups = Groups;
		FNodes  = Nodes;
		int maxId = 0, maxGroup = 0;
		for (size_t i = 0; i < FNodes.size(); i++)
		{
			maxId = std::max(maxId, FNodes[i].Id);
			if (FNodes[i].Group != 0 && FindGroup(FNodes[i].Group) < 0) FNodes[i].Group = 0;
		}
		for (size_t i = 0; i < FGroups.size(); i++) maxGroup = std::max(maxGroup, FGroups[i].Id);
		FNextId = std::max(nextId, maxId + 1);
		FNextGroup = std::max(nextGroup, maxGroup + 1);
		FGroupCounter = groupCounter;
		for (int k = 0; k < NK_COUNT; k++) FCounter[k] = counters[k];
		FZoom = zoom; FPanX = panX; FPanY = panY;

		for (size_t i = 0; i < Links.size(); i++)			// same rules as TryLink: valid pins, equal types, no cycles
		{
			const TLink &K = Links[i];
			int io = FindNode(K.From), ii = FindNode(K.To);
			if (io < 0 || ii < 0 || K.From == K.To) { skipped++; continue; }
			const TKindDef &DO = KindDef(FNodes[io].Kind), &DI = KindDef(FNodes[ii].Kind);
			if (K.FromPin < 0 || K.FromPin >= DO.NOut || K.ToPin < 0 || K.ToPin >= DI.NIn) { skipped++; continue; }
			if (DO.Out[K.FromPin].Type != DI.In[K.ToPin].Type) { skipped++; continue; }
			std::set<int> Seen;
			if (Reaches(K.To, K.From, Seen)) { skipped++; continue; }
			bool same = false, taken = false;
			for (size_t j = 0; j < FLinks.size(); j++)
			{
				if (FLinks[j].To == K.To && FLinks[j].ToPin == K.ToPin)
				{
					if (FLinks[j].From == K.From && FLinks[j].FromPin == K.FromPin) same = true;
					else if (DI.In[K.ToPin].Type == ptNumber) taken = true;		// a number input takes one link
				}
			}
			if (same || taken) { skipped++; continue; }
			FLinks.push_back(K);
		}
		CleanGroups();
		ClearSel();
		if (skipped > 0) Err = L"\u6709 " + IntToStr(skipped) + L" \u9879\u5185\u5BB9\u65E0\u6CD5\u8BC6\u522B\uFF0C\u5DF2\u8DF3\u8FC7";		// warning only: the file was opened
		else Err = L"";
		return true;
	}

	__property float GridStep   = { read = FStep,       write = FStep };
	__property int   MajorEvery = { read = FMajorEvery, write = FMajorEvery };
	__property TPopupMenu *ContextMenu = { read = FMenu };		// so the form can theme / owner-draw it
};
//---------------------------------------------------------------------------
#endif
