//---------------------------------------------------------------------------
// TViewport3D - 3ds Max style 3D viewport (SDL3 rendering, VCL input)
//
// Controls (Z axis up, like 3ds Max):
//   Middle drag            pan
//   Alt + Middle drag      orbit
//   Ctrl+Alt + Middle drag zoom
//   Right drag             orbit (for mice without a middle button)
//   Mouse wheel            zoom
//   T / F / L / P          Top / Front / Left / Perspective
//   Z                      reset view
//   G                      toggle grid
//
// Input is polled every frame (GetAsyncKeyState / GetCursorPos) and the wheel is
// caught with Application->OnMessage, because SDL subclasses the HWND and VCL's
// MouseDown / KeyDown / DoMouseWheel are not reliably called.
// The mouse must be over the viewport (no click-to-focus needed).
//---------------------------------------------------------------------------
#ifndef Viewport3DH
#define Viewport3DH
//---------------------------------------------------------------------------
#include "SDLPanel.h"
#include <Vcl.Forms.hpp>
#include <Vcl.StdCtrls.hpp>
#include <math.h>
#include <stdio.h>
#include <vector>

struct Vec3 { float x, y, z; };
static inline Vec3  V3(float x, float y, float z) { Vec3 r = { x, y, z }; return r; }
static inline Vec3  operator+(Vec3 a, Vec3 b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3  operator-(Vec3 a, Vec3 b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3  operator*(Vec3 a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
static inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3  Cross(Vec3 a, Vec3 b)
{
	return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline Vec3  Norm(Vec3 a)
{
	float l = sqrtf(Dot(a, a));
	return l > 1e-6f ? a * (1.0f / l) : a;
}
//---------------------------------------------------------------------------
// A scene object (axis-aligned box for now)
struct TSceneObj
{
	String Name;
	Vec3   Pos;			// center
	Vec3   Half;		// half size
	int    R, G, B;		// wire color
};
//---------------------------------------------------------------------------
class TViewport3D : public TSDLPanel
{
private:
	// ---- orbit camera (Z up) ----
	Vec3  FTarget;			// look-at point
	float FYaw, FPitch;		// radians
	float FDist;			// camera distance to target
	float FFov;				// vertical field of view (radians)
	int   FLastX, FLastY;
	bool  FShowGrid;
	bool  FWasDown, FDragging;
	int   FWheel;					// accumulated wheel delta (from OnMessage)
	bool  FKeyPrev[6];
	TMessageEvent FOldOnMessage;
	// ---- scene + selection ----
	std::vector<TSceneObj> FObjs;
	int   FSelected;				// -1 = nothing selected
	TNotifyEvent FOnSelect;
	bool  FLDown, FLDownInside;
	int   FLDownX, FLDownY;
	String FViewName;

	// camera values computed every frame
	Vec3  FPos, FRight, FUp, FFwd;
	float FFocal, FCx, FCy;

	void __fastcall ResetView()
	{
		FTarget = V3(0, 0, 1);
		FYaw    = -0.9f;
		FPitch  = 0.5f;
		FDist   = 14.0f;
		FViewName = "Perspective";
	}

	void __fastcall SetStdView(float Yaw, float Pitch, const char *Name)
	{
		FYaw = Yaw; FPitch = Pitch; FViewName = Name;
	}

	void __fastcall ClampDist()
	{
		if (FDist < 0.2f)   FDist = 0.2f;
		if (FDist > 1000.f) FDist = 1000.f;
	}

	void __fastcall UpdateCamera(float W, float H)
	{
		float cp = cosf(FPitch), sp = sinf(FPitch);
		FPos   = FTarget + V3(cp * cosf(FYaw), cp * sinf(FYaw), sp) * FDist;
		FFwd   = Norm(FTarget - FPos);
		FRight = Norm(Cross(FFwd, V3(0, 0, 1)));
		FUp    = Cross(FRight, FFwd);
		FFocal = (H * 0.5f) / tanf(FFov * 0.5f);
		FCx = W * 0.5f;
		FCy = H * 0.5f;
	}

	// 3D line -> screen (with near plane clipping)
	void __fastcall Line3(SDL_Renderer *R, Vec3 A, Vec3 B)
	{
		Vec3 a = V3(Dot(A - FPos, FRight), Dot(A - FPos, FUp), Dot(A - FPos, FFwd));
		Vec3 b = V3(Dot(B - FPos, FRight), Dot(B - FPos, FUp), Dot(B - FPos, FFwd));
		const float n = 0.05f;
		if (a.z < n && b.z < n)
			return;
		if (a.z < n)      a = a + (b - a) * ((n - a.z) / (b.z - a.z));
		else if (b.z < n) b = b + (a - b) * ((n - b.z) / (a.z - b.z));
		SDL_RenderLine(R,
			FCx + a.x / a.z * FFocal, FCy - a.y / a.z * FFocal,
			FCx + b.x / b.z * FFocal, FCy - b.y / b.z * FFocal);
	}

	void __fastcall Color(SDL_Renderer *R, int r, int g, int b)
	{
		SDL_SetRenderDrawColor(R, (Uint8)r, (Uint8)g, (Uint8)b, 255);
	}

	void __fastcall DrawBox(SDL_Renderer *R, Vec3 C, Vec3 S)	// center + half size
	{
		Vec3 p[8];
		for (int i = 0; i < 8; i++)
			p[i] = V3(C.x + ((i & 1) ? S.x : -S.x),
					  C.y + ((i & 2) ? S.y : -S.y),
					  C.z + ((i & 4) ? S.z : -S.z));
		static const int e[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		for (int i = 0; i < 12; i++)
			Line3(R, p[e[i][0]], p[e[i][1]]);
	}

	void __fastcall DrawGizmo(SDL_Renderer *R, float H)		// axis indicator, bottom left
	{
		float ox = 50, oy = H - 50, L = 32;
		Vec3 ax[3] = { V3(1,0,0), V3(0,1,0), V3(0,0,1) };
		static const int col[3][3] = { {230,60,60}, {80,200,80}, {70,120,240} };
		for (int i = 0; i < 3; i++)
		{
			Color(R, col[i][0], col[i][1], col[i][2]);
			SDL_RenderLine(R, ox, oy, ox + Dot(ax[i], FRight) * L, oy - Dot(ax[i], FUp) * L);
		}
	}

	// ray from the camera through a screen point, nearest box hit (-1 = none)
	int __fastcall PickAt(int X, int Y)
	{
		Vec3 d = Norm(FFwd + FRight * ((X - FCx) / FFocal) + FUp * (-(Y - FCy) / FFocal));
		float o[3]  = { FPos.x, FPos.y, FPos.z };
		float dd[3] = { d.x, d.y, d.z };
		int best = -1;
		float bestT = 1e30f;
		for (int i = 0; i < (int)FObjs.size(); i++)
		{
			const TSceneObj &ob = FObjs[i];
			float a[3] = { ob.Pos.x - ob.Half.x, ob.Pos.y - ob.Half.y, ob.Pos.z - ob.Half.z };
			float b[3] = { ob.Pos.x + ob.Half.x, ob.Pos.y + ob.Half.y, ob.Pos.z + ob.Half.z };
			float t0 = 0.0f, t1 = 1e30f;
			bool ok = true;
			for (int k = 0; k < 3 && ok; k++)
			{
				if (fabsf(dd[k]) < 1e-8f)
				{
					if (o[k] < a[k] || o[k] > b[k]) ok = false;
				}
				else
				{
					float ta = (a[k] - o[k]) / dd[k];
					float tb = (b[k] - o[k]) / dd[k];
					if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
					if (ta > t0) t0 = ta;
					if (tb < t1) t1 = tb;
					if (t0 > t1) ok = false;
				}
			}
			if (ok && t0 < bestT)
			{
				bestT = t0;
				best = i;
			}
		}
		return best;
	}

	void __fastcall SelectAt(int X, int Y)
	{
		int idx = PickAt(X, Y);
		if (idx != FSelected)
		{
			FSelected = idx;
			if (FOnSelect)
				FOnSelect(this);
		}
	}

	// Application-level message hook: only used to catch the mouse wheel
	void __fastcall AppMessage(tagMSG &Msg, bool &Handled)
	{
		if (FOldOnMessage)
			FOldOnMessage(Msg, Handled);
		if (!Handled && Msg.message == WM_MOUSEWHEEL && HandleAllocated() && Showing)
		{
			TPoint p = ScreenToClient(Point(Msg.pt.x, Msg.pt.y));
			if (p.x >= 0 && p.y >= 0 && p.x < ClientWidth && p.y < ClientHeight)
			{
				FWheel += (short)HIWORD(Msg.wParam);
				Handled = true;
			}
		}
	}

	static bool __fastcall KeyIsDown(int VK)
	{
		return (GetAsyncKeyState(VK) & 0x8000) != 0;
	}

	void __fastcall ApplyKey(int Key)
	{
		switch (Key)
		{
			case 'T': SetStdView(0.0f,     1.5533f, "Top");         break;
			case 'F': SetStdView(-1.5708f, 0.0f,    "Front");       break;
			case 'L': SetStdView(3.1416f,  0.0f,    "Left");        break;
			case 'P': SetStdView(-0.9f,    0.5f,    "Perspective"); break;
			case 'Z': ResetView(); break;
			case 'G': FShowGrid = !FShowGrid; break;
		}
	}

	// called once per frame, at the start of Render3D
	void __fastcall PollInput()
	{
		if (!Showing || !Application->Active)
		{
			FDragging = false; FWasDown = false; FWheel = 0; FLDown = false;
			return;
		}
		TPoint sp;
		GetCursorPos(&sp);
		TPoint cp = ScreenToClient(sp);
		bool inside = cp.x >= 0 && cp.y >= 0 && cp.x < ClientWidth && cp.y < ClientHeight;

		bool mmb = KeyIsDown(VK_MBUTTON);
		bool rmb = KeyIsDown(VK_RBUTTON);
		bool alt = KeyIsDown(VK_MENU);
		bool ctl = KeyIsDown(VK_CONTROL);

		// ---- mouse drag ----
		bool down = mmb || rmb;
		if (down && !FWasDown)
		{
			FDragging = inside;			// a drag only starts inside the viewport
			FLastX = cp.x; FLastY = cp.y;
		}
		if (!down)
			FDragging = false;
		FWasDown = down;

		if (FDragging)
		{
			int dx = cp.x - FLastX, dy = cp.y - FLastY;
			FLastX = cp.x; FLastY = cp.y;
			if (mmb && alt && ctl)					// Ctrl+Alt+MMB: zoom
			{
				FDist *= expf(dy * 0.01f);
			}
			else if ((mmb && alt) || rmb)			// Alt+MMB / RMB: orbit
			{
				FYaw   -= dx * 0.01f;
				FPitch += dy * 0.01f;
				if (FPitch >  1.553f) FPitch =  1.553f;
				if (FPitch < -1.553f) FPitch = -1.553f;
				FViewName = "Perspective";
			}
			else if (mmb)							// MMB: pan
			{
				float s = 2.0f * FDist * tanf(FFov * 0.5f) / (float)(ClientHeight > 0 ? ClientHeight : 1);
				FTarget = FTarget - FRight * (dx * s) + FUp * (dy * s);
			}
		}

		// ---- left click: select object ----
		bool lmb = KeyIsDown(VK_LBUTTON);
		if (lmb && !FLDown)
		{
			FLDown = true; FLDownInside = inside;
			FLDownX = cp.x; FLDownY = cp.y;
		}
		else if (!lmb && FLDown)
		{
			FLDown = false;
			if (FLDownInside && inside && abs(cp.x - FLDownX) < 5 && abs(cp.y - FLDownY) < 5)
				SelectAt(cp.x, cp.y);
		}

		// ---- wheel ----
		if (FWheel != 0)
		{
			FDist *= powf(0.9f, FWheel / 120.0f);
			FWheel = 0;
		}
		ClampDist();

		// ---- keys (only while the mouse is over the viewport) ----
		static const int keys[6] = { 'T', 'F', 'L', 'P', 'Z', 'G' };
		bool typing = dynamic_cast<TCustomEdit*>(Screen->ActiveControl) != NULL;
		for (int i = 0; i < 6; i++)
		{
			bool d = KeyIsDown(keys[i]) && inside && !ctl && !alt && !typing;
			if (d && !FKeyPrev[i])
				ApplyKey(keys[i]);
			FKeyPrev[i] = d;
		}
	}

	void __fastcall Render3D(TObject *Sender, SDL_Renderer *R)
	{
		int w = ClientWidth, h = ClientHeight;
		if (w < 2 || h < 2)
			return;
		PollInput();
		UpdateCamera((float)w, (float)h);

		// grid
		if (FShowGrid)
		{
			for (int i = -10; i <= 10; i++)
			{
				if (i == 0) continue;
				if (i % 5 == 0) Color(R, 95, 95, 95); else Color(R, 62, 62, 62);
				Line3(R, V3((float)i, -10, 0), V3((float)i, 10, 0));
				Line3(R, V3(-10, (float)i, 0), V3(10, (float)i, 0));
			}
		}
		// world axes
		Color(R, 200, 60, 60);  Line3(R, V3(-10, 0, 0), V3(10, 0, 0));
		Color(R, 60, 180, 60);  Line3(R, V3(0, -10, 0), V3(0, 10, 0));
		Color(R, 70, 110, 230); Line3(R, V3(0, 0, 0),   V3(0, 0, 5));

		// scene objects (the selected one is drawn white)
		for (int i = 0; i < (int)FObjs.size(); i++)
		{
			const TSceneObj &ob = FObjs[i];
			if (i == FSelected) Color(R, 255, 255, 255);
			else                Color(R, ob.R, ob.G, ob.B);
			DrawBox(R, ob.Pos, ob.Half);
		}

		DrawGizmo(R, (float)h);

		// viewport name
		SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
		SDL_RenderDebugText(R, 8, 8, AnsiString(FViewName).c_str());
	}

public:
	__fastcall TViewport3D(TComponent *Owner) : TSDLPanel(Owner)
	{
		FFov = 0.9f;				// about 51 degrees
		FShowGrid = true;
		FLastX = FLastY = 0;
		FWasDown = FDragging = false;
		FWheel = 0;
		for (int i = 0; i < 6; i++) FKeyPrev[i] = false;
		FSelected = -1;
		FOnSelect = NULL;
		FLDown = FLDownInside = false;
		FLDownX = FLDownY = 0;
		AddBox("Box001", V3(0, 0, 1),        V3(1, 1, 1),          240, 200, 80);
		AddBox("Box002", V3(4, 2, 0.5f),     V3(0.5f, 0.5f, 0.5f), 120, 200, 240);
		AddBox("Box003", V3(-3, -3, 1.5f),   V3(0.7f, 0.7f, 1.5f), 240, 120, 160);
		ResetView();
		OnRender = Render3D;
		FOldOnMessage = Application->OnMessage;		// chain to any existing hook
		Application->OnMessage = AppMessage;
	}

	void __fastcall AddBox(const String &Name, Vec3 Pos, Vec3 Half, int R, int G, int B)
	{
		TSceneObj o;
		o.Name = Name; o.Pos = Pos; o.Half = Half; o.R = R; o.G = G; o.B = B;
		FObjs.push_back(o);
	}

	int __fastcall ObjectCount() { return (int)FObjs.size(); }
	const TSceneObj& __fastcall Obj(int I) { return FObjs[I]; }

	__property int Selected = { read = FSelected };
	__property TNotifyEvent OnSelect = { read = FOnSelect, write = FOnSelect };

	__fastcall ~TViewport3D()
	{
		Application->OnMessage = FOldOnMessage;
	}
};
//---------------------------------------------------------------------------
#endif
