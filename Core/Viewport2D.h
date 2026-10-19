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
#include <math.h>
#include <stdio.h>
//---------------------------------------------------------------------------
// Node kinds and their pins
//---------------------------------------------------------------------------
enum { nkBox, nkPlane, nkTime, nkKey, nkAccum, nkOutput, NK_COUNT };
enum { ptNumber = 0, ptObject = 1 };			// pin types: only equal types can be linked

struct TPinDef  { const wchar_t *Name; int Type; float Def; };		// Def = value of an unconnected number input
struct TKindDef { const wchar_t *Name; int R, G, B; int NIn; TPinDef In[5]; int NOut; TPinDef Out[4]; };

inline const TKindDef &KindDef(int K)
{
	static const TKindDef D[NK_COUNT] =
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
		// nkOutput: 运行输出
		{ L"\u8FD0\u884C\u8F93\u51FA", 175, 60, 60,
		  1, { { L"\u573A\u666F", ptObject, 0.0f } },
		  0, { } }
	};
	return D[(K >= 0 && K < NK_COUNT) ? K : 0];
}
//---------------------------------------------------------------------------
// One scene object produced by the graph (read by the render window)
struct TRunObj
{
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
	struct TNode { int Id; int Kind; String Name; float X, Y; bool Sel; int Group; };	// X,Y = top-left, world units; Group 0 = none
	struct TLink { int From; int FromPin; int To; int ToPin; };				// output pin of From -> input pin of To
	struct TGroup { int Id; String Name; };									// a frame around its member nodes
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
	float FMenuWX, FMenuWY;			// where the menu was opened (world)
	int   FMenuNode;				// node under the cursor when the menu was opened (0 = none)
	int   FMenuGroup;				// group title under the cursor when the menu was opened (0 = none)

	std::vector<TNode>  FClipNodes;	// clipboard
	std::vector<TLink>  FClipLinks;
	std::vector<TGroup> FClipGroups;

	TSdlText FTxt;

	// run state (see RunStep)
	std::map<int, float> FAccum;	// accumulated value of every "Accumulate" node
	float FRunT, FRunH, FRunV, FRunS;

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

	// screen position of a pin
	void PinPos(const TNode &N, bool Out, int Idx, float &px, float &py)
	{
		px = SX(N.X + (Out ? NODE_W : 0.0f));
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
			const TKindDef &D = KindDef(N.Kind);
			float px, py;
			for (int k = 0; k < D.NIn; k++)
			{
				PinPos(N, false, k, px, py);
				if ((sx - px) * (sx - px) + (sy - py) * (sy - py) <= pr * pr) { H.Kind = hkIn; H.Node = N.Id; H.Pin = k; return H; }
			}
			for (int k = 0; k < D.NOut; k++)
			{
				PinPos(N, true, k, px, py);
				if ((sx - px) * (sx - px) + (sy - py) * (sy - py) <= pr * pr) { H.Kind = hkOut; H.Node = N.Id; H.Pin = k; return H; }
			}
			float x = SX(N.X), y = SY(N.Y);
			if (sx >= x && sx <= x + NODE_W * FZoom && sy >= y && sy <= y + NodeH(N.Kind) * FZoom)
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
			float ax = N.X, ay = N.Y, bx = N.X + NODE_W, by = N.Y + NodeH(N.Kind);
			if (!any) { x0 = ax; y0 = ay; x1 = bx; y1 = by; any = true; }
			else { x0 = std::min(x0, ax); y0 = std::min(y0, ay); x1 = std::max(x1, bx); y1 = std::max(y1, by); }
		}
		if (any) { x0 -= GRP_PAD; x1 += GRP_PAD; y1 += GRP_PAD; y0 -= GRP_PAD + GRP_TITLE; }
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
		N.Name = String(KindDef(Kind).Name) + L" " + IntToStr(++FCounter[Kind]);
		N.X = wx; N.Y = wy;
		N.Group = 0;
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
		return idx >= 0 ? KindDef(FNodes[idx].Kind).In[Pin].Def : 0.0f;
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
			case nkKey:
				return Pin == 0 ? FRunH : (Pin == 1 ? FRunV : FRunS);
			case nkAccum:
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
		O.Kind  = K;
		O.X     = EvalIn(NodeId, 0);
		O.Y     = EvalIn(NodeId, 1);
		O.Z     = EvalIn(NodeId, 2);
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
		TMenuItem *c = NewItem(L"\u521B\u5EFA", 0);					// 创建
		for (int k = 0; k < NK_COUNT; k++)					// 方块 / 平面 / 时间 / 键盘输入 / 累加 / 运行输出
			c->Add(NewItem(KindDef(k).Name, 100 + k));
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
		//e->Add(g);
		FMenu->Items->Add(e);
		FMenu->Items->Add(g);
	}

	void ShowMenu(int X, int Y)
	{
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
		const TKindDef &D = KindDef(N.Kind);
		float z = FZoom;
		float x = SX(N.X), y = SY(N.Y);
		float w = NODE_W * z, hh = HEAD_H * z, bh = ROW_H * RowsOf(N.Kind) * z;
		if (x > ClientWidth || x + w < 0 || y > ClientHeight || y + hh + bh < 0) return;
		float rad = std::max(2.0f, 6.0f * z);
		int hr = D.R, hg = D.G, hb = D.B;

		std::vector<SDL_FPoint> P;
		RoundPath(P, x, y, w, hh + bh, rad, rad);						// body
		FillPoly(R, P, 44, 46, 52, 245);
		RoundPath(P, x, y, w, hh, rad, 0.0f);							// header
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
		int ps = (int)(11 * z + 0.5f);
		for (int o = 0; o < 2; o++)
		{
			int cnt = o ? D.NOut : D.NIn;
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
					if (o == 0) FTxt.Draw(R, PD.Name, px + (PIN_R + 6) * z, py, std::min(ps, 36), false, 220, 220, 220, 0);
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
			RoundPath(P, x, y, fw, th, rad, 0.0f);
			FillPoly(R, P, 90, 115, 160, 120);						// title bar
			RoundPath(P, x, y, fw, fh, rad, rad);
			if (sel) StrokePoly(R, P, 255, 170, 40, 255);
			else     StrokePoly(R, P, 130, 160, 210, 200);
			int gs = (int)(12 * FZoom + 0.5f);
			if (gs >= 7)
				FTxt.Draw(R, FGroups[g].Name, x + 10 * FZoom, y + th * 0.5f, std::min(gs, 40), true, 255, 255, 255, 0);
		}

		// ---- links ----
		for (size_t i = 0; i < FLinks.size(); i++)
		{
			int a = FindNode(FLinks[i].From), b = FindNode(FLinks[i].To);
			if (a < 0 || b < 0) continue;
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
		char Buf[32];
		sprintf(Buf, "2D  %d%%", (int)(FZoom * 100.0f + 0.5f));
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
			if (H.Kind == hkOut)
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
					if (N.X < x1 && N.X + NODE_W > x0 && N.Y < y1 && N.Y + NodeH(N.Kind) > y0)
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
		float nz = std::min(4.0f, std::max(0.2f, FZoom * f));
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
		  FMenuWX(0), FMenuWY(0), FMenuNode(0), FMenuGroup(0), FRunT(0), FRunH(0), FRunV(0), FRunS(0)
	{
		for (int k = 0; k < NK_COUNT; k++) FCounter[k] = 0;
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

	void RunReset()
	{
		FAccum.clear();
	}

	// t = seconds since the run started, dt = frame time, kh / kv / ks = keyboard (horizontal, vertical, space).
	// Fills Out with the objects connected to the first "Run output" node.
	void RunStep(float t, float dt, float kh, float kv, float ks, std::vector<TRunObj> &Out)
	{
		Out.clear();
		FRunT = t; FRunH = kh; FRunV = kv; FRunS = ks;
		for (size_t i = 0; i < FNodes.size(); i++)			// integrate the accumulators
			if (FNodes[i].Kind == nkAccum)
			{
				int id = FNodes[i].Id;
				FAccum[id] += EvalIn(id, 0) * EvalIn(id, 1) * dt;
			}
		int OutId = 0;
		for (size_t i = 0; i < FNodes.size(); i++)
			if (FNodes[i].Kind == nkOutput) { OutId = FNodes[i].Id; break; }
		if (!OutId) return;
		for (size_t i = 0; i < FLinks.size(); i++)
			if (FLinks[i].To == OutId && FLinks[i].ToPin == 0)
				BuildObj(FLinks[i].From, Out);
	}

	__property float GridStep   = { read = FStep,       write = FStep };
	__property int   MajorEvery = { read = FMajorEvery, write = FMajorEvery };
	__property TPopupMenu *ContextMenu = { read = FMenu };		// so the form can theme / owner-draw it
};
//---------------------------------------------------------------------------
#endif
