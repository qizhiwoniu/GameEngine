//---------------------------------------------------------------------------
// RunWindow.h - the "Run" window: renders the animated scene described by the node graph
//
// Header-only: #include "RunWindow.h" in Unit1.h (after Viewport2D.h) and call
//     ShowRunWindow(this, FSdl2);
// from the toolbar's Run button. The window evaluates the graph of the 2D viewport every frame
// (TViewport2D::RunStep) and draws the resulting boxes / planes with a tiny software 3D pipeline
// on top of SDL_RenderGeometry (fixed perspective camera, flat shading, painter's algorithm).
// The graph can still be edited while the window is open: the change shows up immediately.
//
// Keyboard (read while the run window is the foreground window):
//     A / D or Left / Right  -> "Keyboard input" output Horizontal  (-1 .. 1)
//     W / S or Up / Down     -> "Keyboard input" output Vertical    (-1 .. 1)
//     Space                  -> "Keyboard input" output Space       (0 / 1)
//     W A S D Up Down Left Right -> the "Keys" node, one output per key (0 / 1)
// Mouse: left drag orbits the camera, the wheel zooms; right click = menu (mouse control on / off, reset camera)
//---------------------------------------------------------------------------
#ifndef StartWindowH
#define StartWindowH
//---------------------------------------------------------------------------
#include "Viewport2D.h"
#include <vector>
#include <algorithm>
#include <math.h>
#include <stdio.h>
//---------------------------------------------------------------------------
class TRunView2 : public TSDLPanel
{
private:
	struct TFace { SDL_FPoint P[10]; bool Edge[10]; int N; float Depth; int Layer; float R, G, B; };	// Edge[k]: P[k] -> P[k+1] is a real edge of the face (not the cut at the near plane)

	TViewport2D *FGraph;
	ULONGLONG    FStart, FLast;
	// FPS 统计
	ULONGLONG    FFpsLastTime;
	int          FFpsFrameCount;
	int          FFps;
	std::vector<TRunObj> FObjs;
	std::vector<TFace>   FFaces;
	TSdlText     FTxt;

	// camera
	float FEx, FEy, FEz;						// eye position
	float FRx, FRy, FRz, FUx, FUy, FUz, FFx, FFy, FFz;		// right / up / forward
	float FCx, FCy, FFocal;						// screen centre, focal length in pixels

	// mouse-controlled orbit camera around (0, 0.8, 0): left drag = orbit, wheel = zoom (right-click menu: on / off, reset)
	bool  FMouseLook, FOrbit;
	int   FMouseMode;					// from the "鼠标" node: -1 none (mouse does nothing), 0 click + drag, 1 no click
	bool  FCamOn, FHasLast;				// FCamOn: a "镜头" node sets the eye position (first-person look)
	float FCamX, FCamY, FCamZ;
	float FYaw, FPitch, FDist;
	int   FOx, FOy;
	TPopupMenu *FMenu;
	TMenuItem  *FmiLook;

	static void Norm(float &x, float &y, float &z)
	{
		float l = sqrtf(x * x + y * y + z * z);
		if (l > 1e-6f) { x /= l; y /= l; z /= l; }
	}

	static bool IsDown(int Vk)
	{
		return (::GetAsyncKeyState(Vk) & 0x8000) != 0;
	}

	void SetupCamera(int w, int h)
	{
		float cp = cosf(FPitch), sp = sinf(FPitch);
		if (FCamOn)			// camera node: eye at its position, the mouse turns the view direction
		{
			FEx = FCamX; FEy = FCamY; FEz = FCamZ;
			FFx = -sinf(FYaw) * cp; FFy = -sp; FFz = -cosf(FYaw) * cp;
		}
		else
		{
			FEx = FDist * sinf(FYaw) * cp; FEy = 0.8f + FDist * sp; FEz = FDist * cosf(FYaw) * cp;
			FFx = 0.0f - FEx; FFy = 0.8f - FEy; FFz = 0.0f - FEz;		// looking at (0, 0.8, 0)
		}
		Norm(FFx, FFy, FFz);
		FRx = -FFz; FRy = 0.0f; FRz = FFx;							// right = forward x up(0,1,0)
		Norm(FRx, FRy, FRz);
		FUx = FRy * FFz - FRz * FFy;								// up = right x forward
		FUy = FRz * FFx - FRx * FFz;
		FUz = FRx * FFy - FRy * FFx;
		FCx = w * 0.5f; FCy = h * 0.5f;
		FFocal = (float)h * 1.0f;
	}

	void ToView(float x, float y, float z, float &vx, float &vy, float &vz)
	{
		float dx = x - FEx, dy = y - FEy, dz = z - FEz;
		vx = dx * FRx + dy * FRy + dz * FRz;
		vy = dx * FUx + dy * FUy + dz * FUz;
		vz = dx * FFx + dy * FFy + dz * FFz;
	}

	SDL_FPoint Project(float vx, float vy, float vz)
	{
		SDL_FPoint p;
		p.x = FCx + vx / vz * FFocal;
		p.y = FCy - vy / vz * FFocal;
		return p;
	}

	// world-space line, clipped against the near plane
	void DrawWorldLine(SDL_Renderer *R, float ax, float ay, float az, float bx, float by, float bz)
	{
		const float Near = 0.3f;
		float a[3], b[3];
		ToView(ax, ay, az, a[0], a[1], a[2]);
		ToView(bx, by, bz, b[0], b[1], b[2]);
		if (a[2] < Near && b[2] < Near) return;
		if (a[2] < Near)
		{
			float t = (Near - a[2]) / (b[2] - a[2]);
			for (int i = 0; i < 3; i++) a[i] = a[i] + (b[i] - a[i]) * t;
		}
		else if (b[2] < Near)
		{
			float t = (Near - b[2]) / (a[2] - b[2]);
			for (int i = 0; i < 3; i++) b[i] = b[i] + (a[i] - b[i]) * t;
		}
		SDL_FPoint p = Project(a[0], a[1], a[2]), q = Project(b[0], b[1], b[2]);
		SDL_RenderLine(R, p.x, p.y, q.x, q.y);
	}

	// Quad in view space cut at the near plane z = Near (Sutherland-Hodgman). A face that is only partly behind the camera
	// keeps its visible part, it is no longer dropped as a whole (the big ground plane used to vanish when one of its
	// corners got behind the camera, e.g. when the camera follows a box to the edge of the ground).
	// Returns the number of corners (0 .. 6). CE[k]: the edge from corner k to k+1 belongs to the original quad.
	static int ClipNear(const float VW[4][3], float Near, float CV[6][3], bool CE[6])
	{
		int n = 0;
		for (int i = 0; i < 4; i++)
		{
			const float *a = VW[i];
			const float *b = VW[(i + 1) & 3];
			const bool ain = a[2] >= Near, bin = b[2] >= Near;
			if (ain)
			{
				CV[n][0] = a[0]; CV[n][1] = a[1]; CV[n][2] = a[2];
				CE[n] = true;
				n++;
			}
			if (ain != bin)
			{
				const float t = (Near - a[2]) / (b[2] - a[2]);
				CV[n][0] = a[0] + (b[0] - a[0]) * t;
				CV[n][1] = a[1] + (b[1] - a[1]) * t;
				CV[n][2] = Near;
				CE[n] = !ain;			// entry point: the next edge is original again; exit point: the next edge is the cut
				n++;
			}
		}
		return n;
	}

	// The same cut in screen space, against one side of a rectangle: Side 0: x >= Lim, 1: x <= Lim, 2: y >= Lim, 3: y <= Lim.
	// Used with a guard band around the window: the part of the near-plane cut that lies far outside the window has
	// corners tens of thousands of pixels away, and not every SDL renderer draws triangles that large correctly.
	// A flat colour polygon stays exact when it is cut after the projection.
	static bool SideInside(const SDL_FPoint &p, int Side, float Lim)
	{
		switch (Side)
		{
			case 0:  return p.x >= Lim;
			case 1:  return p.x <= Lim;
			case 2:  return p.y >= Lim;
			default: return p.y <= Lim;
		}
	}
	static int ClipSide(const SDL_FPoint *In, const bool *InE, int n, int Side, float Lim, SDL_FPoint *Out, bool *OutE)
	{
		int m = 0;
		for (int i = 0; i < n && m < 14; i++)
		{
			const SDL_FPoint &a = In[i];
			const SDL_FPoint &b = In[(i + 1) % n];
			const bool ain = SideInside(a, Side, Lim), bin = SideInside(b, Side, Lim);
			if (ain)
			{
				Out[m] = a;
				OutE[m] = InE[i];
				m++;
			}
			if (ain != bin)
			{
				const float t = (Side < 2) ? (Lim - a.x) / (b.x - a.x) : (Lim - a.y) / (b.y - a.y);
				SDL_FPoint r;
				r.x = a.x + (b.x - a.x) * t;
				r.y = a.y + (b.y - a.y) * t;
				if (Side < 2) r.x = Lim; else r.y = Lim;
				Out[m] = r;
				OutE[m] = ain ? false : InE[i];		// exit point: the next edge is the cut; entry point: the original edge goes on
				m++;
			}
		}
		return m;
	}

	// ---- scene -> faces -------------------------------------------------
	void AddFace(const float W[4][3], float nx, float ny, float nz, const TRunObj &O, int Layer, bool Cull)
	{
		float cx = 0, cy = 0, cz = 0;
		for (int i = 0; i < 4; i++) { cx += W[i][0]; cy += W[i][1]; cz += W[i][2]; }
		cx *= 0.25f; cy *= 0.25f; cz *= 0.25f;
		float ex = FEx - cx, ey = FEy - cy, ez = FEz - cz;
		float facing = nx * ex + ny * ey + nz * ez;
		if (Cull && facing <= 0.0f) return;							// back face of a box

		TFace F;
		const float Near = 0.3f;
		float VW[4][3], CV[6][3];
		bool  CE[6];
		for (int i = 0; i < 4; i++)
			ToView(W[i][0], W[i][1], W[i][2], VW[i][0], VW[i][1], VW[i][2]);
		const int n = ClipNear(VW, Near, CV, CE);
		if (n < 3) return;											// completely behind the camera
		float depthSum = 0;
		SDL_FPoint S1[16], S2[16];
		bool       E1[16], E2[16];
		for (int i = 0; i < n; i++)
		{
			S1[i] = Project(CV[i][0], CV[i][1], CV[i][2]);
			E1[i] = CE[i];
			depthSum += CV[i][2];
		}
		F.Depth = depthSum / n;
		// guard band: 1024 px around the window (everything outside the window is invisible anyway)
		const float G = 1024.0f;
		int m = n;
		m = ClipSide(S1, E1, m, 0, -G,                     S2, E2); if (m < 3) return;
		m = ClipSide(S2, E2, m, 1, ClientWidth  + G,       S1, E1); if (m < 3) return;
		m = ClipSide(S1, E1, m, 2, -G,                     S2, E2); if (m < 3) return;
		m = ClipSide(S2, E2, m, 3, ClientHeight + G,       S1, E1); if (m < 3) return;
		if (m > 10) return;
		for (int i = 0; i < m; i++)
		{
			F.P[i]    = S1[i];
			F.Edge[i] = E1[i];
		}
		F.N = m;
		F.Layer = Layer;
		float lx = 0.45f, ly = 0.85f, lz = 0.55f;					// light direction
		Norm(lx, ly, lz);
		float d = nx * lx + ny * ly + nz * lz;
		float br = 0.40f + 0.60f * (Cull ? std::max(0.0f, d) : fabsf(d));
		F.R = std::min(1.0f, O.R * br / 255.0f);
		F.G = std::min(1.0f, O.G * br / 255.0f);
		F.B = std::min(1.0f, O.B * br / 255.0f);
		FFaces.push_back(F);
	}

	void AddObject(const TRunObj &O)
	{
		float a = O.Rot * 3.14159265f / 180.0f;
		float c = cosf(a), s = sinf(a);
		float sc = fabsf(O.Scale);
		if (sc < 0.01f) return;

		if (O.Kind == nkPlane)
		{
			float h = 2.5f * sc;
			const float L[4][2] = { { -h, -h }, { h, -h }, { h, h }, { -h, h } };
			float W[4][3];
			for (int i = 0; i < 4; i++)
			{
				W[i][0] = O.X + L[i][0] * c + L[i][1] * s;
				W[i][1] = O.Y;
				W[i][2] = O.Z - L[i][0] * s + L[i][1] * c;
			}
			AddFace(W, 0.0f, 1.0f, 0.0f, O, 0, false);
			return;
		}

		float h = 0.5f * sc;
		static const float V[8][3] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
									   { -1, -1,  1 }, { 1, -1,  1 }, { 1, 1,  1 }, { -1, 1,  1 } };
		static const int   Fi[6][4] = { { 0, 1, 2, 3 }, { 5, 4, 7, 6 }, { 4, 0, 3, 7 },
										{ 1, 5, 6, 2 }, { 3, 2, 6, 7 }, { 4, 5, 1, 0 } };
		static const float Nm[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
		float WV[8][3];
		for (int i = 0; i < 8; i++)
		{
			float lx = V[i][0] * h, ly = V[i][1] * h, lz = V[i][2] * h;
			WV[i][0] = O.X + lx * c + lz * s;
			WV[i][1] = O.Y + ly;
			WV[i][2] = O.Z - lx * s + lz * c;
		}
		for (int f = 0; f < 6; f++)
		{
			float W[4][3];
			for (int i = 0; i < 4; i++)
				for (int k = 0; k < 3; k++) W[i][k] = WV[Fi[f][i]][k];
			float nx = Nm[f][0] * c + Nm[f][2] * s;
			float ny = Nm[f][1];
			float nz = -Nm[f][0] * s + Nm[f][2] * c;
			AddFace(W, nx, ny, nz, O, 1, true);
		}
	}

	static bool FaceFarther(const TFace &A, const TFace &B)
	{
		if (A.Layer != B.Layer) return A.Layer < B.Layer;			// planes first, then boxes
		return A.Depth > B.Depth;									// far to near
	}

	void __fastcall RenderRun(TObject *Sender, SDL_Renderer *R)
	{
		if (!Showing)
			return;
		int w = ClientWidth, h = ClientHeight;
		if (w < 2 || h < 2)
			return;
		SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(R, 34, 38, 46, 255);
		SDL_RenderClear(R);

		if (!FGraph->HasRunOutput())
		{
			FTxt.Draw(R, L"\u6CA1\u6709\u201C\u8FD0\u884C\u8F93\u51FA\u201D\u8282\u70B9", w * 0.5f, h * 0.5f - 14, 20, true, 235, 235, 235, 2);		// 没有“运行输出”节点
			FTxt.Draw(R, L"\u5728 2D \u89C6\u56FE\u4E2D\u53F3\u952E \u521B\u5EFA \u2192 \u8FD0\u884C\u8F93\u51FA\uFF0C\u5E76\u628A\u7269\u4F53\u8FDE\u5230\u5B83\u7684\u201C\u573A\u666F\u201D\u4E0A", w * 0.5f, h * 0.5f + 16, 14, false, 180, 180, 180, 2);
			return;
		}

		// ---- time + keyboard (only while this window is in front) ----
		ULONGLONG now = ::GetTickCount64();
		float t  = (float)(now - FStart) / 1000.0f;
		float dt = (float)(now - FLast) / 1000.0f;
		FLast = now;
		if (dt > 0.1f) dt = 0.1f;
		float kh = 0, kv = 0, ks = 0;
		unsigned km = 0;
		bool Down[256];
		for (int k = 0; k < 256; k++) Down[k] = false;
		TCustomForm *F = GetParentForm(this);
		if (F && ::GetForegroundWindow() == F->Handle)
		{
			if (IsDown('D') || IsDown(VK_RIGHT)) kh += 1.0f;
			if (IsDown('A') || IsDown(VK_LEFT))  kh -= 1.0f;
			if (IsDown('W') || IsDown(VK_UP))    kv += 1.0f;
			if (IsDown('S') || IsDown(VK_DOWN))  kv -= 1.0f;
			if (IsDown(VK_SPACE))                 ks = 1.0f;
			if (IsDown('W')) km |= 1;    if (IsDown('A')) km |= 2;     if (IsDown('S')) km |= 4;       if (IsDown('D')) km |= 8;
			for (int k = 1; k < 255; k++) Down[k] = IsDown(k);		// every key, for the "键盘" nodes
			if (IsDown(VK_UP)) km |= 16; if (IsDown(VK_DOWN)) km |= 32; if (IsDown(VK_LEFT)) km |= 64; if (IsDown(VK_RIGHT)) km |= 128;
		}

		FGraph->SetRunKeys(km);
		FGraph->SetRunKeyStates(Down);		// the "键盘" nodes read this (any key, not only WASD / arrows)
		FGraph->RunStep(t, dt, kh, kv, ks, FObjs);
		FMouseMode = FGraph->MouseMode();
		FCamOn = FGraph->GetRunCamera(FCamX, FCamY, FCamZ);
		if (FMouseMode < 0) FOrbit = false;

		// ---- ground grid ----
		SetupCamera(w, h);
		SDL_SetRenderDrawColor(R, 58, 62, 70, 255);
		for (int i = -12; i <= 12; i++)
		{
			DrawWorldLine(R, (float)i, 0, -12, (float)i, 0, 12);
			DrawWorldLine(R, -12, 0, (float)i, 12, 0, (float)i);
		}

		// ---- objects ----
		FFaces.clear();
		for (size_t i = 0; i < FObjs.size(); i++)
			AddObject(FObjs[i]);
		std::sort(FFaces.begin(), FFaces.end(), FaceFarther);
		for (size_t i = 0; i < FFaces.size(); i++)
		{
			const TFace &Fc = FFaces[i];
			SDL_FColor col = { Fc.R, Fc.G, Fc.B, 1.0f };
			SDL_Vertex V[10];
			for (int k = 0; k < Fc.N; k++)
			{
				V[k].position = Fc.P[k]; V[k].color = col; V[k].tex_coord.x = 0; V[k].tex_coord.y = 0;
			}
			int Idx[24], NIdx = 0;										// triangle fan (3 .. 10 corners)
			for (int k = 1; k + 1 < Fc.N; k++)
			{
				Idx[NIdx++] = 0; Idx[NIdx++] = k; Idx[NIdx++] = k + 1;
			}
			SDL_RenderGeometry(R, NULL, V, Fc.N, Idx, NIdx);
			SDL_SetRenderDrawColor(R, (Uint8)(Fc.R * 120), (Uint8)(Fc.G * 120), (Uint8)(Fc.B * 120), 255);
			for (int k = 0; k < Fc.N; k++)								// outline: real edges only, not the cut at the near plane
			{
				if (!Fc.Edge[k]) continue;
				const SDL_FPoint &p = Fc.P[k], &q = Fc.P[(k + 1) % Fc.N];
				SDL_RenderLine(R, p.x, p.y, q.x, q.y);
			}
		}

		// ---- HUD: 时间、物体数量、FPS ----
		ULONGLONG fpsNow = ::GetTickCount64();
		FFpsFrameCount++;
		ULONGLONG fpsElapsed = fpsNow - FFpsLastTime;
		if (FFpsLastTime == 0)
		{
			FFpsLastTime = fpsNow;
			FFpsFrameCount = 0;
		}
		else if (fpsElapsed >= 500)
		{
			// 每 500 毫秒更新一次 FPS
			FFps = (int)(FFpsFrameCount * 1000.0 / fpsElapsed);

			FFpsFrameCount = 0;
			FFpsLastTime = fpsNow;
		}
		char Buf[128];
		sprintf(Buf,"t = %.2f s   objects: %d   FPS: %d",t,(int)FObjs.size(),FFps);
		SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
		SDL_RenderDebugText(R, 8, 8, Buf);
		FTxt.Draw(R, L"A/D\u3001\u5DE6\u53F3\u65B9\u5411\u952E\uFF1A\u6C34\u5E73    W/S\u3001\u4E0A\u4E0B\u65B9\u5411\u952E\uFF1A\u5782\u76F4    \u7A7A\u683C\u952E\uFF1A\u7A7A\u683C", 10, (float)h - 14, 12, false, 170, 170, 170, 0);
	}

public:
	__fastcall TRunView2(TComponent *Owner, TViewport2D *Graph)
		: TSDLPanel(Owner), FGraph(Graph), FStart(0), FLast(0),
		  FFpsLastTime(0),
		  FFpsFrameCount(0),
		  FFps(0),
		  FEx(0), FEy(0), FEz(0), FRx(0), FRy(0), FRz(0), FUx(0), FUy(0), FUz(0), FFx(0), FFy(0), FFz(0),
		  FCx(0), FCy(0), FFocal(1),
		  FMouseLook(true), FOrbit(false), FYaw(0.0f), FPitch(0.3243f), FDist(11.605f), FOx(0), FOy(0), FMenu(NULL), FmiLook(NULL)
	{
		OnRender = RenderRun;
		FMenu = new TPopupMenu(this);		// right click: mouse / camera control
		FMouseMode = -1; FCamOn = false; FHasLast = false; FCamX = 0; FCamY = 4.5f; FCamZ = 11.0f;
		TMenuItem *Reset = new TMenuItem(FMenu);
		Reset->Caption = L"\u91CD\u7F6E\u955C\u5934";							// 重置镜头
		Reset->OnClick = MenuReset;
		FMenu->Items->Add(Reset);
		PopupMenu = FMenu;
		Restart();
	}

	void __fastcall MenuLook(TObject *Sender)
	{
		FMouseLook = !FMouseLook;
		FmiLook->Checked = FMouseLook;
		FOrbit = false;
	}

	void __fastcall MenuReset(TObject *Sender)
	{
		FYaw = 0.0f; FPitch = 0.3243f; FDist = 11.605f;
	}

	DYNAMIC void __fastcall MouseDown(TMouseButton Button, TShiftState Shift, int X, int Y)
	{
		TSDLPanel::MouseDown(Button, Shift, X, Y);
		if (CanFocus() && !Focused()) SetFocus();
		if (Button == mbLeft && FMouseMode == 0) { FOrbit = true; FOx = X; FOy = Y; }
	}

	DYNAMIC void __fastcall MouseMove(TShiftState Shift, int X, int Y)
	{
		TSDLPanel::MouseMove(Shift, X, Y);
		bool look = (FMouseMode == 1) || (FMouseMode == 0 && FOrbit);
		if (look && (FHasLast || FOrbit))
		{
			FYaw   -= (X - FOx) * 0.01f;
			FPitch += (Y - FOy) * 0.01f;
			FPitch  = std::min(1.5f, std::max(FCamOn ? -1.5f : 0.02f, FPitch));
		}
		FOx = X; FOy = Y; FHasLast = true;
	}

	DYNAMIC void __fastcall MouseUp(TMouseButton Button, TShiftState Shift, int X, int Y)
	{
		TSDLPanel::MouseUp(Button, Shift, X, Y);
		if (Button == mbLeft) FOrbit = false;
	}

	DYNAMIC bool __fastcall DoMouseWheel(TShiftState Shift, int WheelDelta, const TPoint &MousePos)
	{
		if (FMouseMode < 0 || FCamOn) return TSDLPanel::DoMouseWheel(Shift, WheelDelta, MousePos);		// wheel zoom only orbits
		FDist = std::min(40.0f, std::max(3.0f, FDist * powf(0.9f, WheelDelta / 120.0f)));
		return true;
	}

	void Restart()
	{
		FStart = FLast = ::GetTickCount64();
		FFpsLastTime = FStart;
		FFpsFrameCount = 0;
		FFps = 0;
		if (FGraph) FGraph->RunReset();
	}

	void __fastcall FormClose(TObject *Sender, TCloseAction &Action)
	{
		Action = caHide;		// keep the window and reuse it next time
	}
};
//---------------------------------------------------------------------------
// The window is created once and reused (closing it only hides it). These helpers let the Player menu of the main form
// look at it / restart it / close it without owning it.
struct StartWindowState { TForm *F; TRunView2 *V; };
inline StartWindowState &StartWindowStateRef()
{
	static StartWindowState S = { NULL, NULL };
	return S;
}
inline bool StartWindowIsOpen()
{
	StartWindowState &S = StartWindowStateRef();
	return S.F && S.F->Visible;
}
// the animation of an open window starts again from the beginning (false = the window is not open)
inline bool RestartStartWindow()
{
	StartWindowState &S = StartWindowStateRef();
	if (!S.F || !S.V || !S.F->Visible) return false;
	S.V->Restart();
	return true;
}
inline bool CloseStartWindow()
{
	StartWindowState &S = StartWindowStateRef();
	if (!S.F || !S.F->Visible) return false;
	S.F->Close();		// OnClose = caHide: the window is kept for the next time
	return true;
}
inline void RefreshStartWindow()
{
	StartWindowState &S = StartWindowStateRef();
	if (S.V && S.F && S.F->Visible) S.V->Invalidate();
}
// Opens (or brings to front) the render window for the given node graph.
//   ARestart = true  -> the animation starts again from the beginning ("Run" button)
//   ARestart = false -> keeps running if the window is already open ("Start" button)
// A window that was closed / hidden always starts from the beginning.
inline void ShowStartWindow(TComponent *Owner, TViewport2D *Graph, bool ARestart = true)
{
	StartWindowState &S = StartWindowStateRef();
	TForm *&F = S.F;
	TRunView2 *&V = S.V;
	if (!F)
	{
		F = new TForm(Owner, 1);				// CreateNew: this form has no .dfm resource
		F->Caption      = L"\u5F00\u59CB";					// 开始
		F->ClientWidth  = 960;
		F->ClientHeight = 600;
		F->Position     = poScreenCenter;
		V = new TRunView2(F, Graph);
		V->Parent = F;
		V->Align  = alClient;
		F->OnClose = V->FormClose;
	}
	if (ARestart || !F->Visible)
		V->Restart();
	F->Show();
	F->BringToFront();
}
//---------------------------------------------------------------------------
#endif
