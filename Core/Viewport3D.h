//---------------------------------------------------------------------------
// TViewport3D - 3ds Max style 3D viewport (SDL3 rendering, VCL input)
//
// "+" menu in the top-left corner of the viewport (click the [+] button):
//   Perspective / Orthographic / Statistics > Show FPS / Camera
// Every command also has a keyboard shortcut. The shortcuts are edited in the settings window
// (keyboard picture, see ViewHotkeys.h). Defaults: P Perspective, O Orthographic, W Move, E Rotate, R Scale; wireframe is available from the [+] menu.
// C Camera, 7 Show FPS, T / F / L Top / Front / Left, Z Focus selected, G Toggle grid.
//
// Controls (Z axis up, like 3ds Max):
//   Middle drag            pan                (not in the Camera view)
//   Alt + Middle drag      orbit              (not in the Camera view)
//   Ctrl+Alt + Middle drag zoom               (not in the Camera view)
//   Right drag             orbit (for mice without a middle button)
//   Mouse wheel            zoom
//   Top / Front / Left     orthographic views     (configurable keys, default T / F / L)
//   Focus selected object  (configurable key, default Z; no selection = fit all objects)
//   Reset view             (configurable key, not bound by default)
//   Toggle grid            (configurable key, default G)
//   Snaps (3ds Max style): S 3D snap, A angle snap, Shift+P percent snap; options in [+] > Snaps.
//   Move tool: drag a gizmo axis, or drag the object itself (ground plane / view plane).
//   Shaded mode is rendered with a software z-buffer (objects occlude / intersect per pixel).
//
// Input is polled every frame (GetAsyncKeyState / GetCursorPos) and the wheel is
// caught with Application->OnMessage, because SDL subclasses the HWND and VCL's
// MouseDown / KeyDown / DoMouseWheel are not reliably called.
// The mouse must be over the viewport (no click-to-focus needed).
//
// The 2D grid view lives in Viewport2D.h (TViewport2D).
//---------------------------------------------------------------------------
#ifndef Viewport3DH
#define Viewport3DH
//---------------------------------------------------------------------------
#include "SDLPanel.h"
#include "ViewHotkeys.h"
#include <Vcl.Forms.hpp>
#include <Vcl.StdCtrls.hpp>
#include <Vcl.Menus.hpp>
#include <System.SysUtils.hpp>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>		// std::fill

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
// Euler rotation order: X, then Y, then Z. Scene rotations are stored in radians.
static inline Vec3 RotateVector(Vec3 V, Vec3 R)
{
	float cx = cosf(R.x), sx = sinf(R.x);
	float cy = cosf(R.y), sy = sinf(R.y);
	float cz = cosf(R.z), sz = sinf(R.z);
	Vec3 Q = V3(V.x, V.y * cx - V.z * sx, V.y * sx + V.z * cx);
	Q = V3(Q.x * cy + Q.z * sy, Q.y, -Q.x * sy + Q.z * cy);
	return V3(Q.x * cz - Q.y * sz, Q.x * sz + Q.y * cz, Q.z);
}
static inline Vec3 InverseRotateVector(Vec3 V, Vec3 R)
{
	float cz = cosf(-R.z), sz = sinf(-R.z);
	float cy = cosf(-R.y), sy = sinf(-R.y);
	float cx = cosf(-R.x), sx = sinf(-R.x);
	Vec3 Q = V3(V.x * cz - V.y * sz, V.x * sz + V.y * cz, V.z);
	Q = V3(Q.x * cy + Q.z * sy, Q.y, -Q.x * sy + Q.z * cy);
	return V3(Q.x, Q.y * cx - Q.z * sx, Q.y * sx + Q.z * cx);
}
static inline Vec3 RotateAroundAxis(Vec3 V, int Axis, float A)
{
	float C = cosf(A), S = sinf(A);
	if (Axis == 0) return V3(V.x, V.y * C - V.z * S, V.y * S + V.z * C);
	if (Axis == 1) return V3(V.x * C + V.z * S, V.y, -V.x * S + V.z * C);
	return V3(V.x * C - V.y * S, V.x * S + V.y * C, V.z);
}
//---------------------------------------------------------------------------
// A scene object (box primitive with position, size and Euler rotation)
struct TSceneObj
{
	String Name;
	String Kind;			// Box / Plane (Plane is represented by a thin box for now)
	Vec3   Pos;			// center
	Vec3   Half;		// half size
	Vec3   Rot;			// Euler rotation in radians (X, Y, Z)
	int    R, G, B;		// object color
};
//---------------------------------------------------------------------------
// The scene camera used by the "Camera" view (fixed for now; later it can come from a camera object)
struct TSceneCam
{
	Vec3  Pos;
	Vec3  Target;
	float Fov;			// vertical field of view (radians)
};
//---------------------------------------------------------------------------
enum TViewKind { vwPerspective, vwOrtho, vwStand, vwWireframe, vwCamera };
enum TDisplayMode { dmStand, dmWireframe };

// Posted to the viewport window when the [+] button is clicked. The menu is opened from the message
// loop (Application->OnMessage), not from inside the render timer.
static const UINT WM_VIEWPORT_MENU = WM_APP + 0x31;
//---------------------------------------------------------------------------
class TViewport3D : public TSDLPanel
{
private:
	enum { BTN_X = 6, BTN_Y = 4, BTN_S = 16 };		// the [+] button (client coordinates)

	// ---- orbit camera (Z up) ----
	Vec3  FTarget;			// look-at point
	float FYaw, FPitch;		// radians
	float FDist;			// camera distance to target
	float FFov;				// vertical field of view (radians)
	int   FLastX, FLastY;
	bool  FShowGrid;
	bool  FWasDown, FDragging;
	int   FWheel;					// accumulated wheel delta (from OnMessage)
	bool  FCmdPrev[vcCount];		// configurable view hotkeys
	TMessageEvent FOldOnMessage;
	// ---- scene + selection ----
	std::vector<TSceneObj> FObjs;
	std::vector<int> FSelectedObjs;	// all selected objects; FSelected remains the active/primary one
	std::vector<TSceneObj> FClipboardObjs;
	int   FPasteCount;
	int   FSelected;				// -1 = nothing selected
	TNotifyEvent FOnSelect;
	enum TTransformMode { tmSelect, tmMove, tmRotate, tmScale };
	TTransformMode FTransformMode;
	bool FPrevDelete, FPrevCopy, FPrevPaste, FPrevQ, FPrevW, FPrevE, FPrevR;
	bool FTransformDragging;
	int  FTransformAxis;
	int  FTransformStartX, FTransformStartY;
	float FTransformPixelsPerWorld;
	float FTransformScreenDirX, FTransformScreenDirY;
	float FTransformStartAngle;
	float FTransformPivotScreenX, FTransformPivotScreenY;
	float FTransformScaleReference;
	Vec3 FTransformPivot;
	std::vector<int> FTransformIndices;
	std::vector<TSceneObj> FTransformStartObjects;
	bool  FLDown, FLDownInside;
	bool  FLDownOnPlus;				// don't start a selection marquee from the [+] button
	bool  FMarqueeActive;			// left-to-right window / right-to-left crossing selection
	int   FLDownX, FLDownY;
	int   FMarqueeEndX, FMarqueeEndY;
	// ---- view kind (perspective / orthographic / camera) ----
	int         FViewKind;			// TViewKind: perspective / orthographic / camera
	TDisplayMode FDisplayMode;		// TDisplayMode: shaded default lighting / wireframe
	bool        FStdView;			// true after Top / Front / Left (ortho label = FStdName)
	const char *FStdName;
	char        FViewLabelText[80];
	TSceneCam   FSceneCam;
	// ---- "+" menu, statistics ----
	TPopupMenu *FMenu;
	TMenuItem  *FmiPersp, *FmiOrtho, *FmiWire, *FmiStand, *FmiStats, *FmiFps, *FmiCam;
	bool        FMenuOpen;			// the popup menu is showing: no viewport input meanwhile
	HBRUSH      FMenuBrush;			// background of the popup menu (owner-drawn, see MenuDrawItem)
	bool        FPlusHot;			// mouse over the [+] button
	bool        FShowFps;
	Uint64      FFpsT0;
	int         FFpsFrames;
	float       FFps;

	// camera values computed every frame
	Vec3  FPos, FRight, FUp, FFwd;
	float FFocal, FCx, FCy;
	bool  FOrtho;					// this frame is drawn orthographic
	float FScale;					// ortho: pixels per world unit

	void __fastcall ResetView()
	{
		FTarget = V3(0, 0, 1);
		FYaw    = -0.9f;
		FPitch  = 0.5f;
		FDist   = 14.0f;
		FStdView = false;
	}

	// Top / Front / Left: orthographic views looking along an axis
	void __fastcall SetStdView(float Yaw, float Pitch, const char *Name)
	{
		FYaw = Yaw; FPitch = Pitch;
		FStdView = true;
		FStdName = Name;
		FViewKind = vwOrtho;
	}

	void __fastcall SetViewKind(int K)
	{
		// Compatibility: old callers may still use vwStand / vwWireframe as commands.
		if (K == vwStand)     { FDisplayMode = dmStand;     return; }
		if (K == vwWireframe) { FDisplayMode = dmWireframe; return; }
		if (K != vwPerspective && K != vwOrtho && K != vwCamera)
			return;
		if (K == vwPerspective && FStdView)		// leaving Top / Front / Left: back to the default angles
		{
			FYaw = -0.9f;
			FPitch = 0.5f;
			FStdView = false;
		}
		FViewKind = K;
	}

	const char* __fastcall ViewLabel()
	{
		const char *View = "Perspective";
		if (FViewKind == vwCamera) View = "Camera";
		else if (FViewKind == vwOrtho) View = FStdView ? FStdName : "Orthographic";
		const char *Mode = (FDisplayMode == dmWireframe) ? "Wireframe" : "Stand";
		snprintf(FViewLabelText, sizeof(FViewLabelText), "%s - %s", View, Mode);
		return FViewLabelText;
	}

	void __fastcall ClampDist()
	{
		if (FDist < 0.2f)   FDist = 0.2f;
		if (FDist > 1000.f) FDist = 1000.f;
	}

	void __fastcall UpdateCamera(float W, float H)
	{
		float fov = FFov;
		FOrtho = (FViewKind == vwOrtho);
		if (FViewKind == vwCamera)
		{
			FPos = FSceneCam.Pos;
			FFwd = Norm(FSceneCam.Target - FSceneCam.Pos);
			fov  = FSceneCam.Fov;
		}
		else
		{
			float cp = cosf(FPitch), sp = sinf(FPitch);
			FPos = FTarget + V3(cp * cosf(FYaw), cp * sinf(FYaw), sp) * FDist;
			FFwd = Norm(FTarget - FPos);
		}
		FRight = Norm(Cross(FFwd, V3(0, 0, 1)));
		FUp    = Cross(FRight, FFwd);
		FFocal = (H * 0.5f) / tanf(fov * 0.5f);
		FScale = (H * 0.5f) / (FDist * tanf(FFov * 0.5f));	// ortho: same size as the perspective view at the target
		FCx = W * 0.5f;
		FCy = H * 0.5f;
	}

	// ======================  SOFTWARE Z-BUFFER (shaded mode)  ======================
	// Every shaded face of every object is rasterised into one colour + depth buffer, so objects hide
	// each other per pixel and boxes that intersect show a correct intersection line (no painter's
	// ordering). The result is uploaded as a texture and drawn over the grid.
	std::vector<Uint32> FPix;		// ARGB colour (alpha 0 = nothing drawn there)
	std::vector<float>  FZBuf;		// depth key per pixel, larger = closer (1/z in perspective, -z in ortho)
	SDL_Texture  *FTex;
	SDL_Renderer *FTexR;
	int FBufW, FBufH;

	void __fastcall BeginRaster(SDL_Renderer *R, int W, int H)
	{
		size_t N = (size_t)W * (size_t)H;
		if (R != FTexR || W != FBufW || H != FBufH || !FTex)
		{
			// the texture of an older renderer died together with that renderer
			if (FTex && R == FTexR)
				SDL_DestroyTexture(FTex);
			FTex = NULL;
			FTexR = R; FBufW = W; FBufH = H;
			FPix.assign(N, 0);
			FZBuf.assign(N, -1.0e30f);
			FTex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
			if (FTex)
			{
				SDL_SetTextureBlendMode(FTex, SDL_BLENDMODE_BLEND);
				SDL_SetTextureScaleMode(FTex, SDL_SCALEMODE_NEAREST);
			}
		}
		else
		{
			std::fill(FPix.begin(), FPix.end(), (Uint32)0);
			std::fill(FZBuf.begin(), FZBuf.end(), -1.0e30f);
		}
	}

	void __fastcall EndRaster(SDL_Renderer *R)
	{
		if (!FTex || FPix.empty()) return;
		SDL_UpdateTexture(FTex, NULL, &FPix[0], FBufW * 4);
		SDL_RenderTexture(R, FTex, NULL, NULL);
	}

	// one triangle, screen coordinates + depth key per corner
	void __fastcall RasterTri(float x0, float y0, float k0, float x1, float y1, float k1,
							  float x2, float y2, float k2, Uint32 Col)
	{
		float Area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
		if (fabsf(Area) < 1.0e-6f) return;
		float dkdx = ((k1 - k0) * (y2 - y0) - (k2 - k0) * (y1 - y0)) / Area;
		float dkdy = ((k2 - k0) * (x1 - x0) - (k1 - k0) * (x2 - x0)) / Area;
		float MinY = MinF(y0, MinF(y1, y2)), MaxY = MaxF(y0, MaxF(y1, y2));
		int Ya = (int)ceilf(MinY - 0.5f), Yb = (int)ceilf(MaxY - 0.5f) - 1;
		if (Ya < 0) Ya = 0;
		if (Yb > FBufH - 1) Yb = FBufH - 1;
		const float X[3] = { x0, x1, x2 }, Y[3] = { y0, y1, y2 };
		for (int y = Ya; y <= Yb; ++y)
		{
			float Sy = (float)y + 0.5f;
			float Hit[3];
			int N = 0;
			for (int i = 0; i < 3; ++i)
			{
				int j = (i + 1) % 3;
				if ((Y[i] <= Sy && Y[j] > Sy) || (Y[j] <= Sy && Y[i] > Sy))
					Hit[N++] = X[i] + (Sy - Y[i]) * (X[j] - X[i]) / (Y[j] - Y[i]);
			}
			if (N < 2) continue;
			float Xl = MinF(Hit[0], Hit[1]), Xr = MaxF(Hit[0], Hit[1]);
			int Xa = (int)ceilf(Xl - 0.5f), Xb = (int)ceilf(Xr - 0.5f) - 1;
			if (Xa < 0) Xa = 0;
			if (Xb > FBufW - 1) Xb = FBufW - 1;
			size_t Row = (size_t)y * (size_t)FBufW;
			float k = k0 + dkdx * ((float)Xa + 0.5f - x0) + dkdy * (Sy - y0);
			for (int x = Xa; x <= Xb; ++x, k += dkdx)
			{
				if (k > FZBuf[Row + x])
				{
					FZBuf[Row + x] = k;
					FPix[Row + x] = Col;
				}
			}
		}
	}

	static float MinF(float a, float b) { return a < b ? a : b; }
	static float MaxF(float a, float b) { return a > b ? a : b; }

	struct TCamPt { float x, y, z; };

	// camera space (x right, y up, z forward)
	TCamPt __fastcall ToCam(Vec3 P) const
	{
		Vec3 q = P - FPos;
		TCamPt c = { Dot(q, FRight), Dot(q, FUp), Dot(q, FFwd) };
		return c;
	}

	// camera space point -> screen x, y and depth key
	void __fastcall CamToScreen(const TCamPt &c, float &sx, float &sy, float &k) const
	{
		if (FOrtho)
		{
			sx = FCx + c.x * FScale;
			sy = FCy - c.y * FScale;
			k  = -c.z;
		}
		else
		{
			sx = FCx + c.x / c.z * FFocal;
			sy = FCy - c.y / c.z * FFocal;
			k  = 1.0f / c.z;
		}
	}

	// convex polygon in world space, clipped against the near plane, depth tested
	void __fastcall RasterPoly(const Vec3 *P, int N, Uint32 Col)
	{
		if (N < 3 || N > 4) return;
		TCamPt In[8], Out[8];
		int Cnt = N;
		for (int i = 0; i < N; ++i) In[i] = ToCam(P[i]);
		if (!FOrtho)
		{
			const float Nz = 0.05f;
			int M = 0;
			for (int i = 0; i < Cnt; ++i)
			{
				const TCamPt &A = In[i];
				const TCamPt &B = In[(i + 1) % Cnt];
				bool Ai = A.z >= Nz, Bi = B.z >= Nz;
				if (Ai) Out[M++] = A;
				if (Ai != Bi)
				{
					float T = (Nz - A.z) / (B.z - A.z);
					TCamPt C = { A.x + (B.x - A.x) * T, A.y + (B.y - A.y) * T, Nz };
					Out[M++] = C;
				}
			}
			Cnt = M;
			for (int i = 0; i < Cnt; ++i) In[i] = Out[i];
		}
		if (Cnt < 3) return;
		float Sx[8], Sy[8], K[8];
		for (int i = 0; i < Cnt; ++i) CamToScreen(In[i], Sx[i], Sy[i], K[i]);
		for (int i = 1; i + 1 < Cnt; ++i)
			RasterTri(Sx[0], Sy[0], K[0], Sx[i], Sy[i], K[i], Sx[i + 1], Sy[i + 1], K[i + 1], Col);
	}

	// 1 px line that is hidden by nearer surfaces (bias keeps edges of a surface visible on it)
	void __fastcall RasterLineDepth(Vec3 A, Vec3 B, Uint32 Col)
	{
		TCamPt a = ToCam(A), b = ToCam(B);
		if (!FOrtho)
		{
			const float Nz = 0.05f;
			if (a.z < Nz && b.z < Nz) return;
			if (a.z < Nz)
			{
				float T = (Nz - a.z) / (b.z - a.z);
				a.x += (b.x - a.x) * T; a.y += (b.y - a.y) * T; a.z = Nz;
			}
			else if (b.z < Nz)
			{
				float T = (Nz - b.z) / (a.z - b.z);
				b.x += (a.x - b.x) * T; b.y += (a.y - b.y) * T; b.z = Nz;
			}
		}
		float x0, y0, k0, x1, y1, k1;
		CamToScreen(a, x0, y0, k0);
		CamToScreen(b, x1, y1, k1);
		float dx = x1 - x0, dy = y1 - y0;
		int Steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
		if (Steps > 6000) return;
		for (int i = 0; i <= Steps; ++i)
		{
			float T = (float)i / (float)Steps;
			int x = (int)floorf(x0 + dx * T), y = (int)floorf(y0 + dy * T);
			if (x < 0 || y < 0 || x >= FBufW || y >= FBufH) continue;
			float k = k0 + (k1 - k0) * T;
			float Kb = FOrtho ? (k + 0.02f) : (k * 1.004f);
			if (Kb >= FZBuf[(size_t)y * FBufW + x])
				FPix[(size_t)y * FBufW + x] = Col;
		}
	}

	// 3D line -> screen (perspective with near plane clipping, or orthographic)
	void __fastcall Line3(SDL_Renderer *R, Vec3 A, Vec3 B)
	{
		if (FOrtho)
		{
			Vec3 pa = A - FPos, pb = B - FPos;
			SDL_RenderLine(R,
				FCx + Dot(pa, FRight) * FScale, FCy - Dot(pa, FUp) * FScale,
				FCx + Dot(pb, FRight) * FScale, FCy - Dot(pb, FUp) * FScale);
			return;
		}
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

	// Project a 3D point to viewport coordinates. Returns false when it is behind
	// the perspective near plane; orthographic projection has no near-plane divide.
	bool __fastcall ProjectPoint(Vec3 P, float &SX, float &SY, float &Depth)
	{
		Vec3 q = P - FPos;
		Depth = Dot(q, FFwd);
		if (FOrtho)
		{
			SX = FCx + Dot(q, FRight) * FScale;
			SY = FCy - Dot(q, FUp) * FScale;
			return true;
		}
		if (Depth < 0.05f)
			return false;
		SX = FCx + Dot(q, FRight) / Depth * FFocal;
		SY = FCy - Dot(q, FUp) / Depth * FFocal;
		return true;
	}

	struct TScreenVertex
	{
		float X, Y, Depth;
	};

	// True when the object is currently part of the (possibly multi-object) selection.
	bool __fastcall IsObjectSelected(int Index) const
	{
		for (size_t i = 0; i < FSelectedObjs.size(); ++i)
			if (FSelectedObjs[i] == Index)
				return true;
		return false;
	}

	// Fill a convex projected face using horizontal SDL lines. This deliberately
	// uses SDL_RenderLine rather than a GPU-specific pipeline, so it also works
	// with the SDL renderer used by TSDLPanel.
	void __fastcall FillFace(SDL_Renderer *R, const TScreenVertex *V, int Count)
	{
		if (Count < 3) return;
		float MinY = V[0].Y, MaxY = V[0].Y;
		for (int i = 1; i < Count; ++i)
		{
			if (V[i].Y < MinY) MinY = V[i].Y;
			if (V[i].Y > MaxY) MaxY = V[i].Y;
		}
		int Y0 = (int)ceilf(MinY - 0.5f);
		int Y1 = (int)floorf(MaxY - 0.5f);
		for (int y = Y0; y <= Y1; ++y)
		{
			float ScanY = (float)y + 0.5f;
			float Hits[8];
			int HitCount = 0;
			for (int i = 0; i < Count; ++i)
			{
				const TScreenVertex &A = V[i];
				const TScreenVertex &B = V[(i + 1) % Count];
				if ((A.Y <= ScanY && B.Y > ScanY) || (B.Y <= ScanY && A.Y > ScanY))
				{
					if (HitCount < 8)
						Hits[HitCount++] = A.X + (ScanY - A.Y) * (B.X - A.X) / (B.Y - A.Y);
				}
			}
			// A convex quad normally gives two intersections. Sort for safety.
			for (int i = 0; i < HitCount - 1; ++i)
				for (int j = i + 1; j < HitCount; ++j)
					if (Hits[j] < Hits[i]) { float T = Hits[i]; Hits[i] = Hits[j]; Hits[j] = T; }
			for (int i = 0; i + 1 < HitCount; i += 2)
				SDL_RenderLine(R, Hits[i], (float)y, Hits[i + 1], (float)y);
		}
	}

	void __fastcall DrawBox(SDL_Renderer *R, Vec3 C, Vec3 S, Vec3 Rot, int BaseR, int BaseG, int BaseB, bool Selected)
	{
		Vec3 p[8];
		for (int i = 0; i < 8; i++)
		{
			Vec3 Local = V3((i & 1) ? S.x : -S.x,
							(i & 2) ? S.y : -S.y,
							(i & 4) ? S.z : -S.z);
			p[i] = C + RotateVector(Local, Rot);
		}

		static const int Edges[12][2] =
		{ {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };

		// Wireframe mode intentionally draws no filled surfaces.
		if (FDisplayMode == dmWireframe)
		{
			Color(R, Selected ? 255 : BaseR, Selected ? 255 : BaseG, Selected ? 255 : BaseB);
			for (int i = 0; i < 12; ++i)
				Line3(R, p[Edges[i][0]], p[Edges[i][1]]);
			return;
		}

		// Face corner order and outward normal for each of the six box faces.
		static const int FaceCorners[6][4] =
		{
			{0,2,6,4}, // -X
			{1,5,7,3}, // +X
			{0,4,5,1}, // -Y
			{2,3,7,6}, // +Y
			{0,1,3,2}, // -Z
			{4,6,7,5}  // +Z
		};
		static const Vec3 FaceNormals[6] =
		{
			{-1,0,0}, {1,0,0}, {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}
		};

		// Shaded: every visible face goes to the z-buffer (see RasterPoly), so the result does not
		// depend on the order of the objects and intersecting objects are drawn correctly.
		// Fixed soft light coming from the upper-left/front side, plus ambient fill.
		const Vec3 LightDir = Norm(V3(-0.45f, -0.55f, 0.70f));
		for (int f = 0; f < 6; ++f)
		{
			Vec3 FaceOffset = V3(0, 0, 0);
			if (f == 0) FaceOffset.x = -S.x;
			if (f == 1) FaceOffset.x =  S.x;
			if (f == 2) FaceOffset.y = -S.y;
			if (f == 3) FaceOffset.y =  S.y;
			if (f == 4) FaceOffset.z = -S.z;
			if (f == 5) FaceOffset.z =  S.z;
			Vec3 FaceCenter = C + RotateVector(FaceOffset, Rot);
			Vec3 FaceNormal = RotateVector(FaceNormals[f], Rot);
			Vec3 ToCamera = FOrtho ? (FFwd * -1.0f) : Norm(FPos - FaceCenter);
			if (Dot(FaceNormal, ToCamera) <= 0.0f)
				continue; // back face

			float Diffuse = Dot(FaceNormal, LightDir);
			if (Diffuse < 0.0f) Diffuse = 0.0f;
			float Brightness = 0.28f + 0.72f * Diffuse;
			int rr = (int)(BaseR * Brightness);
			int gg = (int)(BaseG * Brightness);
			int bb = (int)(BaseB * Brightness);
			if (Selected) // selection stays clearly visible but keeps face shading
			{
				rr = (int)(rr * 0.55f + 255.0f * 0.45f);
				gg = (int)(gg * 0.55f + 255.0f * 0.45f);
				bb = (int)(bb * 0.55f + 255.0f * 0.45f);
			}
			if (rr > 255) rr = 255;
			if (gg > 255) gg = 255;
			if (bb > 255) bb = 255;
			Vec3 Q[4];
			for (int k = 0; k < 4; ++k) Q[k] = p[FaceCorners[f][k]];
			RasterPoly(Q, 4, 0xFF000000u | ((Uint32)rr << 16) | ((Uint32)gg << 8) | (Uint32)bb);
		}

		// Selected box: white outline, only the parts that are not hidden by other objects
		if (Selected)
			for (int i = 0; i < 12; ++i)
				RasterLineDepth(p[Edges[i][0]], p[Edges[i][1]], 0xFFFFFFFFu);
	}

	// The scene camera as a small wire frustum (drawn in the perspective / orthographic views)
	void __fastcall DrawSceneCamera(SDL_Renderer *R)
	{
		Vec3  f  = Norm(FSceneCam.Target - FSceneCam.Pos);
		Vec3  r  = Norm(Cross(f, V3(0, 0, 1)));
		Vec3  u  = Cross(r, f);
		float d  = 1.2f;
		float hh = d * tanf(FSceneCam.Fov * 0.5f);
		float hw = hh * 1.5f;
		Vec3  c  = FSceneCam.Pos + f * d;
		Vec3  q[4] = { c - r * hw - u * hh, c + r * hw - u * hh, c + r * hw + u * hh, c - r * hw + u * hh };
		Color(R, 255, 220, 80);
		for (int i = 0; i < 4; i++)
		{
			Line3(R, FSceneCam.Pos, q[i]);
			Line3(R, q[i], q[(i + 1) % 4]);
		}
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

	Vec3 __fastcall TransformAxisVector(int Axis) const
	{
		if (Axis == 0) return V3(1, 0, 0);
		if (Axis == 1) return V3(0, 1, 0);
		return V3(0, 0, 1);
	}

	Vec3 __fastcall TransformCenter() const
	{
		if (FSelectedObjs.empty()) return FTarget;
		Vec3 C = V3(0, 0, 0);
		int N = 0;
		for (size_t I = 0; I < FSelectedObjs.size(); ++I)
		{
			int Idx = FSelectedObjs[I];
			if (Idx < 0 || Idx >= (int)FObjs.size()) continue;
			C = C + FObjs[Idx].Pos;
			++N;
		}
		return N ? C * (1.0f / (float)N) : FTarget;
	}

	float __fastcall TransformGizmoWorldLength(Vec3 Center) const
	{
		if (FOrtho)
			return 78.0f / (FScale > 0.001f ? FScale : 0.001f);
		float Depth = Dot(Center - FPos, FFwd);
		if (Depth < 0.1f) Depth = 0.1f;
		return 78.0f * Depth / (FFocal > 0.001f ? FFocal : 0.001f);
	}

	void __fastcall DrawTransformGizmo(SDL_Renderer *R)
	{
		if (FTransformMode == tmSelect || FSelectedObjs.empty()) return;
		Vec3 C = TransformCenter();
		float CX, CY, CD;
		if (!ProjectPoint(C, CX, CY, CD)) return;
		static const int AxisColors[3][3] = { {235, 75, 75}, {80, 220, 100}, {80, 145, 255} };
		if (FTransformMode == tmRotate)
		{
			// Concentric screen-space rings make the three rotation handles easy to grab
			// while retaining the correct axis color convention (X red, Y green, Z blue).
			for (int A = 0; A < 3; ++A)
			{
				Color(R, AxisColors[A][0], AxisColors[A][1], AxisColors[A][2]);
				float Radius = 34.0f + A * 12.0f;
				const int Segments = 64;
				for (int I = 0; I < Segments; ++I)
				{
					float A0 = (float)I * 6.28318530718f / Segments;
					float A1 = (float)(I + 1) * 6.28318530718f / Segments;
					SDL_RenderLine(R, CX + cosf(A0) * Radius, CY + sinf(A0) * Radius,
						CX + cosf(A1) * Radius, CY + sinf(A1) * Radius);
				}
			}
			return;
		}

		float WorldLen = TransformGizmoWorldLength(C);
		for (int A = 0; A < 3; ++A)
		{
			float EX, EY, ED;
			Vec3 End = C + TransformAxisVector(A) * WorldLen;
			if (!ProjectPoint(End, EX, EY, ED)) continue;
			Color(R, AxisColors[A][0], AxisColors[A][1], AxisColors[A][2]);
			SDL_RenderLine(R, CX, CY, EX, EY);
			if (FTransformMode == tmMove)
			{
				float DX = EX - CX, DY = EY - CY;
				float L = sqrtf(DX * DX + DY * DY);
				if (L > 1.0f)
				{
					DX /= L; DY /= L;
					const float Back = 9.0f, Side = 4.0f;
					SDL_RenderLine(R, EX, EY, EX - DX * Back - DY * Side, EY - DY * Back + DX * Side);
					SDL_RenderLine(R, EX, EY, EX - DX * Back + DY * Side, EY - DY * Back - DX * Side);
				}
			}
			else // scale: a small square at each axis tip
			{
				SDL_FRect Box = { EX - 4.0f, EY - 4.0f, 8.0f, 8.0f };
				SDL_RenderFillRect(R, &Box);
			}
		}
		SDL_SetRenderDrawColor(R, 245, 245, 245, 255);
		SDL_FRect CenterMark = { CX - 3.0f, CY - 3.0f, 6.0f, 6.0f };
		SDL_RenderFillRect(R, &CenterMark);
	}

	// the [+] button: opens the view menu
	void __fastcall DrawPlusButton(SDL_Renderer *R)
	{
		SDL_FRect b = { (float)BTN_X, (float)BTN_Y, (float)BTN_S, (float)BTN_S };
		if (FPlusHot)
		{
			SDL_SetRenderDrawColor(R, 70, 70, 70, 255);
			SDL_RenderFillRect(R, &b);
		}
		SDL_SetRenderDrawColor(R, FPlusHot ? 200 : 140, FPlusHot ? 200 : 140, FPlusHot ? 200 : 140, 255);
		SDL_RenderRect(R, &b);
		SDL_SetRenderDrawColor(R, 230, 230, 230, 255);
		SDL_RenderLine(R, (float)(BTN_X + 4), (float)(BTN_Y + 8), (float)(BTN_X + 12), (float)(BTN_Y + 8));
		SDL_RenderLine(R, (float)(BTN_X + 8), (float)(BTN_Y + 4), (float)(BTN_X + 8), (float)(BTN_Y + 12));
	}

	bool __fastcall PlusHit(int X, int Y)
	{
		return X >= BTN_X && X < BTN_X + BTN_S && Y >= BTN_Y && Y < BTN_Y + BTN_S;
	}

	// ======================  SNAPS (like 3ds Max)  ======================
	// S  : 3D snap (grid points / vertices / edge midpoints / face centres / pivots - see the [+] menu)
	// A  : angle snap for Rotate      Shift+P : percent snap for Scale
	// While moving, the corners / centre of the moving objects are pulled to the snap points of the
	// other objects when they come within a few pixels (alignment); otherwise the moved box is
	// pulled onto the grid. Move along an axis (gizmo) or free (drag the object itself).
	enum { stGrid, stVertex, stEdge, stFace, stPivot, stBBox };
	bool  FSnapOn, FAngleSnapOn, FPercentSnapOn;
	bool  FSnapType[6];
	float FGridStep, FAngleStepDeg, FPercentStep, FSnapRadius;	// set from the toolbar settings window
	int   FMajorEvery;
	bool  FShowSnapMarker;
	TNotifyEvent FOnSnapToggled;		// the S key switched snap on / off (the toolbar button follows)
	bool  FSnapMarkerOn;				// a snap target is being used right now (yellow marker)
	Vec3  FSnapMarker;
	bool  FTransformFree;				// Move: dragging the object itself on a plane
	bool  FTransformMoved;
	Vec3  FFreeNormal, FFreeStartHit;
	std::vector<Vec3> FSnapTargets;		// snap points of the objects that are not moving

	static void __fastcall BoxCorners(const TSceneObj &O, Vec3 *P8)
	{
		for (int i = 0; i < 8; i++)
		{
			Vec3 L = V3((i & 1) ? O.Half.x : -O.Half.x, (i & 2) ? O.Half.y : -O.Half.y, (i & 4) ? O.Half.z : -O.Half.z);
			P8[i] = O.Pos + RotateVector(L, O.Rot);
		}
	}

	bool __fastcall IsMovingIndex(int I) const
	{
		for (size_t k = 0; k < FTransformIndices.size(); ++k)
			if (FTransformIndices[k] == I) return true;
		return false;
	}

	// snap points of every object that is not being moved
	void __fastcall CollectSnapTargets()
	{
		static const int Edges[12][2] =
		{ {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
		FSnapTargets.clear();
		for (int i = 0; i < (int)FObjs.size(); ++i)
		{
			if (IsMovingIndex(i)) continue;
			const TSceneObj &O = FObjs[i];
			Vec3 P[8];
			BoxCorners(O, P);
			if (FSnapType[stVertex])
				for (int k = 0; k < 8; ++k) FSnapTargets.push_back(P[k]);
			if (FSnapType[stEdge])
				for (int k = 0; k < 12; ++k) FSnapTargets.push_back((P[Edges[k][0]] + P[Edges[k][1]]) * 0.5f);
			if (FSnapType[stFace])
			{
				for (int a = 0; a < 3; ++a)
					for (int sgn = -1; sgn <= 1; sgn += 2)
					{
						Vec3 L = V3(0, 0, 0);
						if (a == 0) L.x = O.Half.x * sgn; else if (a == 1) L.y = O.Half.y * sgn; else L.z = O.Half.z * sgn;
						FSnapTargets.push_back(O.Pos + RotateVector(L, O.Rot));
					}
			}
			if (FSnapType[stPivot])
				FSnapTargets.push_back(O.Pos);
			if (FSnapType[stBBox])			// corners of the world-aligned bounding box
			{
				Vec3 Lo = P[0], Hi = P[0];
				for (int k = 1; k < 8; ++k)
				{
					if (P[k].x < Lo.x) Lo.x = P[k].x;
					if (P[k].x > Hi.x) Hi.x = P[k].x;
					if (P[k].y < Lo.y) Lo.y = P[k].y;
					if (P[k].y > Hi.y) Hi.y = P[k].y;
					if (P[k].z < Lo.z) Lo.z = P[k].z;
					if (P[k].z > Hi.z) Hi.z = P[k].z;
				}
				for (int k = 0; k < 8; ++k)
					FSnapTargets.push_back(V3((k & 1) ? Hi.x : Lo.x, (k & 2) ? Hi.y : Lo.y, (k & 4) ? Hi.z : Lo.z));
			}
		}
	}

	// Moves are snapped here. D = tentative translation (already limited to the axis / plane);
	// Axis = 0..2 when constrained to a world axis, -1 when free (then FFreeNormal is the plane normal).
	Vec3 __fastcall SnapMoveDelta(Vec3 D, int Axis)
	{
		FSnapMarkerOn = false;
		if (!FSnapOn || FTransformStartObjects.empty())
			return D;

		// source points: the 8 corners and the centre of every moving object (at the start of the drag)
		std::vector<Vec3> Src;
		for (size_t i = 0; i < FTransformStartObjects.size(); ++i)
		{
			Vec3 P[8];
			BoxCorners(FTransformStartObjects[i], P);
			for (int k = 0; k < 8; ++k) Src.push_back(P[k]);
			Src.push_back(FTransformStartObjects[i].Pos);
		}

		// 1) object snaps: nearest (in pixels) source / target pair
		bool ObjSnap = FSnapType[stVertex] || FSnapType[stEdge] || FSnapType[stFace] || FSnapType[stPivot] || FSnapType[stBBox];
		if (ObjSnap)
		{
			CollectSnapTargets();
			std::vector<float> Tx(FSnapTargets.size()), Ty(FSnapTargets.size());
			std::vector<char>  Tok(FSnapTargets.size());
			for (size_t t = 0; t < FSnapTargets.size(); ++t)
			{
				float dep;
				Tok[t] = ProjectPoint(FSnapTargets[t], Tx[t], Ty[t], dep) ? 1 : 0;
			}
			const float Radius = FSnapRadius;
			float Best = Radius * Radius;
			int BestS = -1, BestT = -1;
			for (size_t s = 0; s < Src.size(); ++s)
			{
				float qx, qy, dep;
				if (!ProjectPoint(Src[s] + D, qx, qy, dep)) continue;
				for (size_t t = 0; t < FSnapTargets.size(); ++t)
				{
					if (!Tok[t]) continue;
					float dx = qx - Tx[t], dy = qy - Ty[t];
					float d2 = dx * dx + dy * dy;
					if (d2 < Best) { Best = d2; BestS = (int)s; BestT = (int)t; }
				}
			}
			if (BestS >= 0)
			{
				Vec3 Full = FSnapTargets[BestT] - Src[BestS];			// moves the source exactly onto the target
				if (Axis >= 0)
				{
					Vec3 Ax = TransformAxisVector(Axis);
					Full = Ax * Dot(Full, Ax);						// stays on the gizmo axis
				}
				FSnapMarkerOn = true;
				FSnapMarker = FSnapTargets[BestT];
				return Full;
			}
		}

		// 2) grid: pull the corner / centre that is closest to a grid line onto it (per moving axis)
		if (FSnapType[stGrid])
		{
			float Step = FGridStep;
			for (int a = 0; a < 3; ++a)
			{
				bool Moves = (Axis >= 0) ? (a == Axis)
					: (fabsf(a == 0 ? FFreeNormal.x : (a == 1 ? FFreeNormal.y : FFreeNormal.z)) < 0.5f);
				if (!Moves) continue;
				float BestAdj = 1.0e30f;
				for (size_t s = 0; s < Src.size(); ++s)
				{
					float v = (a == 0 ? Src[s].x : (a == 1 ? Src[s].y : Src[s].z)) +
							  (a == 0 ? D.x : (a == 1 ? D.y : D.z));
					float adj = floorf(v / Step + 0.5f) * Step - v;
					if (fabsf(adj) < fabsf(BestAdj)) BestAdj = adj;
				}
				if (BestAdj < 1.0e29f)
				{
					if (a == 0) D.x += BestAdj; else if (a == 1) D.y += BestAdj; else D.z += BestAdj;
				}
			}
		}
		return D;
	}

	// plane for free moves: ground parallel in perspective, view plane in orthographic views
	bool __fastcall RayFromScreen(int X, int Y, Vec3 &Org, Vec3 &Dir)
	{
		if (FOrtho)
		{
			Dir = FFwd;
			Org = FPos + FRight * ((X - FCx) / FScale) + FUp * (-(Y - FCy) / FScale);
			Org = Org - FFwd * 1000.0f;
		}
		else
		{
			Dir = Norm(FFwd + FRight * ((X - FCx) / FFocal) + FUp * (-(Y - FCy) / FFocal));
			Org = FPos;
		}
		return true;
	}

	bool __fastcall HitFreePlane(int X, int Y, Vec3 &Hit)
	{
		Vec3 Org, Dir;
		RayFromScreen(X, Y, Org, Dir);
		float Den = Dot(Dir, FFreeNormal);
		if (fabsf(Den) < 1.0e-4f) return false;
		float T = Dot(FTransformPivot - Org, FFreeNormal) / Den;
		if (!FOrtho && T < 0.0f) return false;
		Hit = Org + Dir * T;
		return true;
	}

	// Move tool: press on an object (not on a gizmo axis) and drag it on a plane
	bool __fastcall BeginFreeMove(int X, int Y)
	{
		int Idx = PickAt(X, Y);
		if (Idx < 0) return false;
		if (!IsObjectSelected(Idx))
		{
			std::vector<int> One(1, Idx);
			SetSelection(One);
		}
		Vec3 C = TransformCenter();
		FTransformPivot = C;
		if (FOrtho)
			FFreeNormal = FFwd * -1.0f;
		else
			FFreeNormal = (fabsf(FFwd.z) > 0.2f) ? V3(0, 0, 1) : FFwd * -1.0f;
		if (!HitFreePlane(X, Y, FFreeStartHit)) return false;
		FTransformIndices.clear();
		FTransformStartObjects.clear();
		for (size_t I = 0; I < FSelectedObjs.size(); ++I)
		{
			int Id = FSelectedObjs[I];
			if (Id < 0 || Id >= (int)FObjs.size()) continue;
			FTransformIndices.push_back(Id);
			FTransformStartObjects.push_back(FObjs[Id]);
		}
		FTransformAxis = 3;
		FTransformStartX = X;
		FTransformStartY = Y;
		FTransformFree = true;
		FTransformMoved = false;
		FTransformDragging = !FTransformIndices.empty();
		return FTransformDragging;
	}

	bool __fastcall BeginTransformDrag(int X, int Y)
	{
		FTransformFree = false;
		FTransformMoved = true;
		FSnapMarkerOn = false;
		if (FTransformMode == tmSelect) return false;
		if (FSelectedObjs.empty()) return FTransformMode == tmMove && BeginFreeMove(X, Y);
		Vec3 C = TransformCenter();
		float CX, CY, CD;
		if (!ProjectPoint(C, CX, CY, CD)) return FTransformMode == tmMove && BeginFreeMove(X, Y);
		int BestAxis = -1;
		float BestDistance = 1.0e30f;
		float BestPixelsPerWorld = 0.0f, BestDirX = 0.0f, BestDirY = 0.0f;
		float WorldLen = TransformGizmoWorldLength(C);

		if (FTransformMode == tmRotate)
		{
			float DX = (float)X - CX, DY = (float)Y - CY;
			float Radius = sqrtf(DX * DX + DY * DY);
			for (int A = 0; A < 3; ++A)
			{
				float Ring = 34.0f + A * 12.0f;
				float Dist = fabsf(Radius - Ring);
				if (Dist < 8.0f && Dist < BestDistance)
				{
					BestDistance = Dist;
					BestAxis = A;
				}
			}
			if (BestAxis < 0 || Radius < 1.0f) return false;
			FTransformStartAngle = atan2f((float)Y - CY, (float)X - CX);
		}
		else
		{
			for (int A = 0; A < 3; ++A)
			{
				float EX, EY, ED;
				if (!ProjectPoint(C + TransformAxisVector(A) * WorldLen, EX, EY, ED)) continue;
				float DX = EX - CX, DY = EY - CY;
				float Len2 = DX * DX + DY * DY;
				if (Len2 < 16.0f) continue;
				float T = (((float)X - CX) * DX + ((float)Y - CY) * DY) / Len2;
				if (T < 0.18f || T > 1.15f) continue;
				float PX = CX + T * DX, PY = CY + T * DY;
				float QX = X - PX, QY = Y - PY;
				float Dist = sqrtf(QX * QX + QY * QY);
				if (Dist < 9.0f && Dist < BestDistance)
				{
					BestDistance = Dist;
					BestAxis = A;
					float Len = sqrtf(Len2);
					BestPixelsPerWorld = Len / WorldLen;
					BestDirX = DX / Len;
					BestDirY = DY / Len;
				}
			}
			if (BestAxis < 0 || BestPixelsPerWorld < 0.01f) return FTransformMode == tmMove && BeginFreeMove(X, Y);
		}

		FTransformAxis = BestAxis;
		FTransformStartX = X;
		FTransformStartY = Y;
		FTransformPivot = C;
		FTransformPivotScreenX = CX;
		FTransformPivotScreenY = CY;
		FTransformPixelsPerWorld = BestPixelsPerWorld;
		FTransformScreenDirX = BestDirX;
		FTransformScreenDirY = BestDirY;
		FTransformIndices.clear();
		FTransformStartObjects.clear();
		float MinAxis = 1.0e30f, MaxAxis = -1.0e30f;
		for (size_t I = 0; I < FSelectedObjs.size(); ++I)
		{
			int Idx = FSelectedObjs[I];
			if (Idx < 0 || Idx >= (int)FObjs.size()) continue;
			FTransformIndices.push_back(Idx);
			FTransformStartObjects.push_back(FObjs[Idx]);
			Vec3 P = FObjs[Idx].Pos;
			float PosA = BestAxis == 0 ? P.x : (BestAxis == 1 ? P.y : P.z);
			float HalfA = BestAxis == 0 ? FObjs[Idx].Half.x : (BestAxis == 1 ? FObjs[Idx].Half.y : FObjs[Idx].Half.z);
			if (PosA - HalfA < MinAxis) MinAxis = PosA - HalfA;
			if (PosA + HalfA > MaxAxis) MaxAxis = PosA + HalfA;
		}
		FTransformScaleReference = MaxAxis - MinAxis;
		if (FTransformScaleReference < 0.1f || FTransformScaleReference > 1.0e20f)
			FTransformScaleReference = 1.0f;
		FTransformDragging = !FTransformIndices.empty();
		return FTransformDragging;
	}

	void __fastcall UpdateTransformDrag(int X, int Y)
	{
		if (!FTransformDragging || FTransformIndices.empty()) return;

		// Move, dragging the object itself on a plane
		if (FTransformFree)
		{
			if (!FTransformMoved)
			{
				if (abs(X - FTransformStartX) < 4 && abs(Y - FTransformStartY) < 4) return;	// still a click
				FTransformMoved = true;
			}
			Vec3 Hit;
			if (!HitFreePlane(X, Y, Hit)) return;
			Vec3 D = SnapMoveDelta(Hit - FFreeStartHit, -1);
			for (size_t I = 0; I < FTransformIndices.size(); ++I)
			{
				int Idx = FTransformIndices[I];
				if (Idx < 0 || Idx >= (int)FObjs.size()) continue;
				FObjs[Idx] = FTransformStartObjects[I];
				FObjs[Idx].Pos = FTransformStartObjects[I].Pos + D;
			}
			return;
		}

		float Amount = 0.0f;
		float Angle = 0.0f;
		if (FTransformMode == tmRotate)
		{
			Angle = atan2f((float)Y - FTransformPivotScreenY,
						   (float)X - FTransformPivotScreenX) - FTransformStartAngle;
			while (Angle > 3.14159265f) Angle -= 6.28318531f;
			while (Angle < -3.14159265f) Angle += 6.28318531f;
			if (FAngleSnapOn)				// angle snap: whole multiples of the step
			{
				float St = FAngleStepDeg * 0.01745329252f;
				Angle = floorf(Angle / St + 0.5f) * St;
			}
		}
		else
		{
			float ScreenAmount = ((float)X - FTransformStartX) * FTransformScreenDirX +
				((float)Y - FTransformStartY) * FTransformScreenDirY;
			Amount = ScreenAmount / FTransformPixelsPerWorld;
		}

		Vec3 MoveD = V3(0, 0, 0);
		if (FTransformMode == tmMove)
			MoveD = SnapMoveDelta(TransformAxisVector(FTransformAxis) * Amount, FTransformAxis);

		float ScaleFactor = 1.0f + Amount / FTransformScaleReference;
		if (ScaleFactor < 0.01f) ScaleFactor = 0.01f;
		if (FTransformMode == tmScale && FPercentSnapOn)	// percent snap
		{
			float St = FPercentStep * 0.01f;
			ScaleFactor = floorf(ScaleFactor / St + 0.5f) * St;
			if (ScaleFactor < St) ScaleFactor = St;
		}
		for (size_t I = 0; I < FTransformIndices.size(); ++I)
		{
			int Idx = FTransformIndices[I];
			if (Idx < 0 || Idx >= (int)FObjs.size()) continue;
			TSceneObj O = FTransformStartObjects[I];
			if (FTransformMode == tmMove)
			{
				O.Pos = O.Pos + MoveD;
			}
			else if (FTransformMode == tmScale)
			{
				O.Pos = FTransformPivot + (O.Pos - FTransformPivot) * ScaleFactor;
				if (FTransformAxis == 0) O.Half.x *= ScaleFactor;
				else if (FTransformAxis == 1) O.Half.y *= ScaleFactor;
				else O.Half.z *= ScaleFactor;
			}
			else if (FTransformMode == tmRotate)
			{
				O.Pos = FTransformPivot + RotateAroundAxis(O.Pos - FTransformPivot, FTransformAxis, Angle);
				if (FTransformAxis == 0) O.Rot.x += Angle;
				else if (FTransformAxis == 1) O.Rot.y += Angle;
				else O.Rot.z += Angle;
			}
			FObjs[Idx] = O;
		}
	}

	void __fastcall EndTransformDrag()
	{
		if (FTransformDragging && FTransformMoved && FOnSelect)
			FOnSelect(this);
		FTransformDragging = false;
		FTransformFree = false;
		FSnapMarkerOn = false;
		FTransformIndices.clear();
		FTransformStartObjects.clear();
	}

	// Draw one dashed horizontal/vertical edge for the crossing-selection marquee.
	void __fastcall DrawDashedLine(SDL_Renderer *R, int X1, int Y1, int X2, int Y2)
	{
		int DX = X2 - X1;
		int DY = Y2 - Y1;
		int Len = abs(DX) > abs(DY) ? abs(DX) : abs(DY);
		if (Len <= 0)
			return;

		for (int Start = 0; Start < Len; Start += 8)
		{
			int End = Start + 5;
			if (End > Len) End = Len;
			float T1 = (float)Start / (float)Len;
			float T2 = (float)End / (float)Len;
			SDL_RenderLine(R,
				(float)X1 + DX * T1, (float)Y1 + DY * T1,
				(float)X1 + DX * T2, (float)Y1 + DY * T2);
		}
	}

	void __fastcall DrawSelectionMarquee(SDL_Renderer *R)
	{
		if (!FMarqueeActive)
			return;

		int Left   = FLDownX < FMarqueeEndX ? FLDownX : FMarqueeEndX;
		int Right  = FLDownX > FMarqueeEndX ? FLDownX : FMarqueeEndX;
		int Top    = FLDownY < FMarqueeEndY ? FLDownY : FMarqueeEndY;
		int Bottom = FLDownY > FMarqueeEndY ? FLDownY : FMarqueeEndY;

		if (FMarqueeEndX < FLDownX)
		{
			// Right -> left: green dashed crossing-selection rectangle.
			SDL_SetRenderDrawColor(R, 70, 220, 120, 255);
			DrawDashedLine(R, Left, Top, Right, Top);
			DrawDashedLine(R, Right, Top, Right, Bottom);
			DrawDashedLine(R, Right, Bottom, Left, Bottom);
			DrawDashedLine(R, Left, Bottom, Left, Top);
		}
		else
		{
			// Left -> right: blue solid window-selection rectangle.
			SDL_SetRenderDrawColor(R, 70, 160, 255, 255);
			SDL_FRect Box = {
				(float)Left, (float)Top,
				(float)(Right - Left), (float)(Bottom - Top)
			};
			SDL_RenderRect(R, &Box);
		}
	}

	// ray through a screen point (perspective: from the camera, ortho: parallel to the view direction),
	// nearest box hit (-1 = none)
	int __fastcall PickAt(int X, int Y)
	{
		Vec3 org, d;
		if (FOrtho)
		{
			d   = FFwd;
			org = FPos + FRight * ((X - FCx) / FScale) + FUp * (-(Y - FCy) / FScale);
			org = org - FFwd * 1000.0f;		// start far behind so nothing in front of the camera plane is missed
		}
		else
		{
			d   = Norm(FFwd + FRight * ((X - FCx) / FFocal) + FUp * (-(Y - FCy) / FFocal));
			org = FPos;
		}
		int best = -1;
		float bestT = 1e30f;
		for (int i = 0; i < (int)FObjs.size(); i++)
		{
			const TSceneObj &ob = FObjs[i];
			Vec3 LocalOrg = InverseRotateVector(org - ob.Pos, ob.Rot);
			Vec3 LocalDir = InverseRotateVector(d, ob.Rot);
			float o[3]  = { LocalOrg.x, LocalOrg.y, LocalOrg.z };
			float dd[3] = { LocalDir.x, LocalDir.y, LocalDir.z };
			float a[3] = { -ob.Half.x, -ob.Half.y, -ob.Half.z };
			float b[3] = {  ob.Half.x,  ob.Half.y,  ob.Half.z };
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

	// Replace the current selection and notify the owner only if it actually changed.
	void __fastcall SetSelection(const std::vector<int> &NewSelection)
	{
		if (FSelectedObjs == NewSelection)
			return;

		FSelectedObjs = NewSelection;
		FSelected = FSelectedObjs.empty() ? -1 : FSelectedObjs.back();
		if (FOnSelect)
			FOnSelect(this);
	}

	void __fastcall SelectAt(int X, int Y)
	{
		int idx = PickAt(X, Y);
		std::vector<int> Selection;
		if (idx >= 0)
			Selection.push_back(idx);
		SetSelection(Selection);
	}

	// Calculate the projected screen-space bounds of a box.
	// AllCornersProjected is false if one or more corners are behind the perspective near plane.
	bool __fastcall ProjectObjectBounds(int Index, float &MinX, float &MinY,
		float &MaxX, float &MaxY, bool &AllCornersProjected)
	{
		if (Index < 0 || Index >= (int)FObjs.size())
			return false;

		const TSceneObj &Ob = FObjs[Index];
		bool AnyProjected = false;
		AllCornersProjected = true;

		for (int i = 0; i < 8; ++i)
		{
			Vec3 Local = V3(
				(i & 1) ? Ob.Half.x : -Ob.Half.x,
				(i & 2) ? Ob.Half.y : -Ob.Half.y,
				(i & 4) ? Ob.Half.z : -Ob.Half.z);
			Vec3 P = Ob.Pos + RotateVector(Local, Ob.Rot);

			float SX = 0.0f, SY = 0.0f, Depth = 0.0f;
			if (!ProjectPoint(P, SX, SY, Depth))
			{
				AllCornersProjected = false;
				continue;
			}

			if (!AnyProjected)
			{
				MinX = MaxX = SX;
				MinY = MaxY = SY;
				AnyProjected = true;
			}
			else
			{
				if (SX < MinX) MinX = SX;
				if (SX > MaxX) MaxX = SX;
				if (SY < MinY) MinY = SY;
				if (SY > MaxY) MaxY = SY;
			}
		}
		return AnyProjected;
	}

	// 3ds Max-style marquee:
	// left -> right: window selection (the complete projected bounds must fit inside);
	// right -> left: crossing selection (any overlap with the projected bounds is enough).
	void __fastcall SelectByMarquee(int X1, int Y1, int X2, int Y2)
	{
		const float Left   = (float)(X1 < X2 ? X1 : X2);
		const float Right  = (float)(X1 > X2 ? X1 : X2);
		const float Top    = (float)(Y1 < Y2 ? Y1 : Y2);
		const float Bottom = (float)(Y1 > Y2 ? Y1 : Y2);
		const bool Crossing = X2 < X1;

		std::vector<int> Selection;
		for (int i = 0; i < (int)FObjs.size(); ++i)
		{
			float MinX = 0.0f, MinY = 0.0f, MaxX = 0.0f, MaxY = 0.0f;
			bool AllCornersProjected = false;
			if (!ProjectObjectBounds(i, MinX, MinY, MaxX, MaxY, AllCornersProjected))
				continue;

			bool Hit = false;
			if (Crossing)
			{
				// Crossing mode: any screen-space overlap selects the object.
				Hit = MaxX >= Left && MinX <= Right &&
					  MaxY >= Top  && MinY <= Bottom;
			}
			else
			{
				// Window mode: the entire projected bounding box must be enclosed.
				Hit = AllCornersProjected &&
					  MinX >= Left && MaxX <= Right &&
					  MinY >= Top  && MaxY <= Bottom;
			}

			if (Hit)
				Selection.push_back(i);
		}
		SetSelection(Selection);
	}

	// ---- "+" menu ----
	TMenuItem* __fastcall AddMenuItem(TMenuItem *Parent, const String &Cap, int Cmd, bool Radio)
	{
		TMenuItem *M = new TMenuItem(FMenu);
		M->Caption = Cap;
		M->Tag     = Cmd;				// TViewCmd
		if (Radio)
		{
			M->RadioItem  = true;
			M->GroupIndex = 1;
		}
		M->OnClick = MenuItemClick;
		M->OnAdvancedDrawItem = MenuDrawItem;
		M->OnMeasureItem      = MenuMeasureItem;
		Parent->Add(M);
		return M;
	}

	// ---- owner-drawn menu: white text on a dark background, the highlighted item is inverted
	//      (dark text on a white bar) ----
	void __fastcall MenuMeasureItem(TObject *Sender, TCanvas *ACanvas, int &Width, int &Height)
	{
		TMenuItem *Item = static_cast<TMenuItem*>(Sender);
		//String Cap = StringReplace(Item->Caption, "&", "", TReplaceFlags() << rfReplaceAll);
		String Cap = Item->Caption;
		int p = Cap.Pos(L"(&");
		while (p > 0)
		{
			// 从 (& 开始查找右括号 )
			String Sub = Cap.SubString(p, Cap.Length() - p + 1);
			int e = Sub.Pos(L")");

			if (e > 0)
			{
				// 删除整个 (&U)，包括右括号
				Cap.Delete(p, e);
			}
			else
			{
				break;
			}
			 // 继续查找后续快捷键提示
			p = Cap.Pos(L"(&");
		}
		// 删除剩余的 & 符号
		Cap = StringReplace(
        Cap,
        L"&",
        L"",
        TReplaceFlags() << rfReplaceAll
		);

		int W = 28 + ACanvas->TextWidth(Cap) + 16;		// left gutter + text + right padding
		if (Item->Count > 0)
			W += 16;									// room for the sub-menu arrow
		Width  = W;
		Height = 24;
	}

	void __fastcall MenuDrawItem(TObject *Sender, TCanvas *ACanvas, const TRect &ARect, TOwnerDrawState State)
	{
		TMenuItem *Item = static_cast<TMenuItem*>(Sender);
		TRect R = ARect;
		const bool Hot = State.Contains(odSelected);
		const COLORREF Bg = Hot ? RGB(255, 255, 255) : RGB(32, 32, 32);		// highlighted = inverted
		const COLORREF Fg = Hot ? RGB(32, 32, 32)    : RGB(255, 255, 255);

		ACanvas->Brush->Style = bsSolid;
		ACanvas->Brush->Color = static_cast<TColor>(Bg);
		ACanvas->FillRect(R);

		int MidY = (R.Top + R.Bottom) / 2;

		// check mark / radio dot
		if (Item->Checked)
		{
			ACanvas->Pen->Color = static_cast<TColor>(Fg);
			if (Item->RadioItem)
			{
				ACanvas->Brush->Color = static_cast<TColor>(Fg);
				ACanvas->Ellipse(R.Left + 9, MidY - 3, R.Left + 16, MidY + 4);
			}
			else
			{
				ACanvas->Pen->Width = 2;
				ACanvas->MoveTo(R.Left + 8, MidY);
				ACanvas->LineTo(R.Left + 11, MidY + 3);
				ACanvas->LineTo(R.Left + 17, MidY - 4);
				ACanvas->Pen->Width = 1;
			}
		}

		// sub-menu arrow
		int RightEdge = R.Right - 12;
		if (Item->Count > 0)
		{
			TPoint Arrow[3] = { TPoint(R.Right - 14, MidY - 4),
								TPoint(R.Right - 14, MidY + 4),
								TPoint(R.Right - 10, MidY) };
			ACanvas->Brush->Color = static_cast<TColor>(Fg);
			ACanvas->Pen->Color   = static_cast<TColor>(Fg);
			ACanvas->Polygon(Arrow, 2);
			RightEdge = R.Right - 24;
		}

		// caption
		TRect TR = R;
		TR.Left  += 28;
		TR.Right  = RightEdge;
		ACanvas->Brush->Style = bsClear;
		ACanvas->Font->Color  = static_cast<TColor>(Fg);
		String Cap = Item->Caption;

		// 删除类似 (&U)、(&F) 的快捷键提示
		int p = Cap.Pos(L"(&");
		while (p > 0)
		{
			String Sub = Cap.SubString(p, Cap.Length() - p + 1);
			int e = Sub.Pos(L")");

			if (e > 0)
			{
				// 删除整个 (&U)，包括右括号
				Cap.Delete(p, e);
			}
			else
			{
				break;
			}
			p = Cap.Pos(L"(&");
		}
		// 删除剩余的 & 符号
		Cap = StringReplace(
		Cap,
		L"&",
		L"",
		TReplaceFlags() << rfReplaceAll
		);
		DrawText(ACanvas->Handle, Cap.c_str(), -1, &TR, DT_SINGLELINE | DT_LEFT | DT_VCENTER | DT_NOPREFIX);
		ACanvas->Brush->Style = bsSolid;
	}

	void __fastcall BuildMenu()
	{
		FMenu = new TPopupMenu(this);
		FMenu->OwnerDraw = true;
		FMenuBrush = CreateSolidBrush(RGB(32, 32, 32));
		FMenu->OnPopup = MenuPopup;
		FmiPersp = AddMenuItem(FMenu->Items, L"透视图", vcPerspective, true);	// 透视图
		FmiOrtho = AddMenuItem(FMenu->Items, L"正交图", vcOrtho,       true);	// 正交图
		FmiStand = AddMenuItem(FMenu->Items, L"默认照明", vcStand, true);	// 显示样式：实体照明
		FmiWire  = AddMenuItem(FMenu->Items, L"线框图", vcWireframe, true);	// 显示样式：线框
		FmiWire->GroupIndex  = 2;
		FmiStand->GroupIndex = 2;
		FmiStats = new TMenuItem(FMenu);													// 统计 (sub menu)
		FmiStats->Caption = L"统计";
		FmiStats->OnAdvancedDrawItem = MenuDrawItem;
		FmiStats->OnMeasureItem      = MenuMeasureItem;
		FMenu->Items->Add(FmiStats);
		FmiFps   = AddMenuItem(FmiStats,       L"显示 FPS",    vcShowFps,     false);	// 显示 FPS
		FmiCam   = AddMenuItem(FMenu->Items, L"相机",         vcCamera,      true);	// 相机
	}

	void __fastcall MenuPopup(TObject *Sender)
	{
		FmiPersp->Checked = (FViewKind == vwPerspective);
		FmiOrtho->Checked = (FViewKind == vwOrtho);
		FmiStand->Checked = (FDisplayMode == dmStand);
		FmiWire->Checked  = (FDisplayMode == dmWireframe);
		FmiCam->Checked   = (FViewKind == vwCamera);
		FmiFps->Checked   = FShowFps;

		// dark background around / behind the items (no white margin)
		MENUINFO mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize  = sizeof(mi);
		mi.fMask   = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
		mi.hbrBack = FMenuBrush;
		SetMenuInfo(FMenu->Handle, &mi);
	}

	void __fastcall MenuItemClick(TObject *Sender)
	{
		TMenuItem *M = dynamic_cast<TMenuItem*>(Sender);
		if (M)
			ApplyCommand(static_cast<int>(M->Tag));
	}

	// Z: centre the view on the selected object and zoom so that it fills the viewport.
	// Nothing selected: fit all objects. The view direction is kept (works in Perspective and Orthographic).
	void __fastcall FocusSelected()
	{
		if (FViewKind == vwCamera || FObjs.empty())		// the scene camera is fixed
			return;
		Vec3 lo = V3(0, 0, 0), hi = V3(0, 0, 0);
		bool first = true;
		for (int i = 0; i < (int)FObjs.size(); i++)
		{
			if (!FSelectedObjs.empty() && !IsObjectSelected(i))
				continue;
			const TSceneObj &o = FObjs[i];
			for (int Corner = 0; Corner < 8; ++Corner)
			{
				Vec3 Local = V3((Corner & 1) ? o.Half.x : -o.Half.x,
								(Corner & 2) ? o.Half.y : -o.Half.y,
								(Corner & 4) ? o.Half.z : -o.Half.z);
				Vec3 P = o.Pos + RotateVector(Local, o.Rot);
				if (first)
				{
					lo = hi = P;
					first = false;
				}
				else
				{
					if (P.x < lo.x) lo.x = P.x; if (P.y < lo.y) lo.y = P.y; if (P.z < lo.z) lo.z = P.z;
					if (P.x > hi.x) hi.x = P.x; if (P.y > hi.y) hi.y = P.y; if (P.z > hi.z) hi.z = P.z;
				}
			}
		}
		Vec3 half = (hi - lo) * 0.5f;
		float r = sqrtf(Dot(half, half));				// radius of the bounding sphere
		if (r < 0.1f) r = 0.1f;
		FTarget = (lo + hi) * 0.5f;
		FDist   = r / tanf(FFov * 0.5f) * 1.25f;		// 25 % margin around the object
		ClampDist();
	}

	// menu item and keyboard shortcut do the same
	void __fastcall ApplyCommand(int Cmd)
	{
		switch (Cmd)
		{
			case vcPerspective: SetViewKind(vwPerspective); break;
			case vcOrtho:       SetViewKind(vwOrtho);       break;
			case vcWireframe:   FDisplayMode = dmWireframe;   break;
			case vcStand:       FDisplayMode = dmStand;       break;
			case vcCamera:      SetViewKind(vwCamera);      break;
			case vcShowFps:     FShowFps = !FShowFps;       break;
			case vcToolSelect:  FTransformMode = tmSelect;  break;
			case vcToolMove:    FTransformMode = tmMove;    break;
			case vcToolRotate:  FTransformMode = tmRotate;  break;
			case vcToolScale:   FTransformMode = tmScale;   break;
			case vcTop:         SetStdView(0.0f,     1.5533f, "Top");   break;
			case vcFront:       SetStdView(-1.5708f, 0.0f,    "Front"); break;
			case vcLeft:        SetStdView(3.1416f,  0.0f,    "Left");  break;
			case vcResetView:   ResetView();                break;
			case vcGrid:        FShowGrid = !FShowGrid;     break;
			case vcFocus:       FocusSelected();            break;
			case vcSnapToggle:  FSnapOn = !FSnapOn; if (FOnSnapToggled) FOnSnapToggled(this); break;
			case vcAngleSnap:   FAngleSnapOn = !FAngleSnapOn;   break;
			case vcPercentSnap: FPercentSnapOn = !FPercentSnapOn; break;
		}
	}

	void __fastcall ShowViewMenu()
	{
		if (FMenuOpen || !FMenu || !HandleAllocated())
			return;
		TPoint P = ClientToScreen(Point(BTN_X, BTN_Y + BTN_S + 2));
		FMenuOpen = true;
		try
		{
			FMenu->Popup(P.x, P.y);
		}
		__finally
		{
			FMenuOpen = false;
		}
	}

	// Application-level message hook: catches the mouse wheel and the "open the + menu" request
	void __fastcall AppMessage(tagMSG &Msg, bool &Handled)
	{
		if (FOldOnMessage)
			FOldOnMessage(Msg, Handled);
		if (Handled || !HandleAllocated())
			return;
		if (Msg.message == WM_VIEWPORT_MENU && Msg.hwnd == Handle)
		{
			Handled = true;
			ShowViewMenu();
		}
		else if (Msg.message == WM_MOUSEWHEEL && Showing)
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

	// called once per frame, at the start of Render3D
	void __fastcall PollInput()
	{
		// No input while the window is hidden / inactive, while a dialog (e.g. the settings window
		// with its hotkey boxes) is the active form, or while the "+" menu is open.
		if (!Showing || !Application->Active || FMenuOpen || GetParentForm(this) != Screen->ActiveForm)
		{
			FDragging = false; FWasDown = false; FWheel = 0; FLDown = false;
			FPlusHot = false; FMarqueeActive = false; FLDownOnPlus = false;
			FTransformDragging = false;
			FPrevDelete = FPrevCopy = FPrevPaste = FPrevQ = FPrevW = FPrevE = FPrevR = false;
			return;
		}
		UpdateCamera((float)ClientWidth, (float)ClientHeight);
		TPoint sp;
		GetCursorPos(&sp);
		TPoint cp = ScreenToClient(sp);
		bool inside = cp.x >= 0 && cp.y >= 0 && cp.x < ClientWidth && cp.y < ClientHeight;
		FPlusHot = inside && PlusHit(cp.x, cp.y);

		bool mmb = KeyIsDown(VK_MBUTTON);
		bool rmb = KeyIsDown(VK_RBUTTON);
		bool alt = KeyIsDown(VK_MENU);
		bool ctl = KeyIsDown(VK_CONTROL);
		bool shf = KeyIsDown(VK_SHIFT);
		bool camView = (FViewKind == vwCamera);		// the scene camera is fixed: no orbit / pan / zoom

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
			if (camView)
			{
				// nothing to move
			}
			else if (mmb && alt && ctl)				// Ctrl+Alt+MMB: zoom
			{
				FDist *= expf(dy * 0.01f);
			}
			else if ((mmb && alt) || rmb)			// Alt+MMB / RMB: orbit
			{
				FYaw   -= dx * 0.01f;
				FPitch += dy * 0.01f;
				if (FPitch >  1.553f) FPitch =  1.553f;
				if (FPitch < -1.553f) FPitch = -1.553f;
				FStdView = false;					// no longer exactly Top / Front / Left
			}
			else if (mmb)							// MMB: pan
			{
				float s = 2.0f * FDist * tanf(FFov * 0.5f) / (float)(ClientHeight > 0 ? ClientHeight : 1);
				FTarget = FTarget - FRight * (dx * s) + FUp * (dy * s);
			}
		}

		// ---- left mouse: click-select or 3ds Max-style window/crossing marquee ----
		bool lmb = KeyIsDown(VK_LBUTTON);
		if (lmb && !FLDown)
		{
			FLDown = true;
			FLDownInside = inside;
			FLDownX = cp.x;
			FLDownY = cp.y;
			FLDownOnPlus = inside && PlusHit(cp.x, cp.y);
			FMarqueeActive = false;
			FMarqueeEndX = cp.x;
			FMarqueeEndY = cp.y;
			FTransformDragging = inside && !FLDownOnPlus && BeginTransformDrag(cp.x, cp.y);
		}

		// Start the marquee only after the pointer has moved far enough to be a drag.
		if (lmb && FLDown && FLDownInside && !FLDownOnPlus)
		{
			if (FTransformDragging)
			{
				UpdateTransformDrag(cp.x, cp.y);
			}
			else
			{
				if (abs(cp.x - FLDownX) >= 5 || abs(cp.y - FLDownY) >= 5)
					FMarqueeActive = true;

				if (FMarqueeActive)
			{
				// Keep the marquee inside the viewport even if the cursor is dragged outside it.
				FMarqueeEndX = cp.x;
				FMarqueeEndY = cp.y;
				if (FMarqueeEndX < 0) FMarqueeEndX = 0;
				if (FMarqueeEndY < 0) FMarqueeEndY = 0;
				if (FMarqueeEndX >= ClientWidth)  FMarqueeEndX = ClientWidth - 1;
				if (FMarqueeEndY >= ClientHeight) FMarqueeEndY = ClientHeight - 1;
				}
			}
		}

		if (!lmb && FLDown)
		{
			FLDown = false;

			if (FTransformDragging)
			{
				bool WasClick = FTransformFree && !FTransformMoved;
				EndTransformDrag();
				if (WasClick && inside)
					SelectAt(cp.x, cp.y);
			}
			else if (FLDownInside && FLDownOnPlus && inside &&
				abs(cp.x - FLDownX) < 5 && abs(cp.y - FLDownY) < 5 &&
				PlusHit(FLDownX, FLDownY) && PlusHit(cp.x, cp.y))
			{
				::PostMessageW(Handle, WM_VIEWPORT_MENU, 0, 0);		// open menu from the message loop
			}
			else if (FLDownInside && !FLDownOnPlus)
			{
				if (FMarqueeActive)
				{
					int EndX = cp.x, EndY = cp.y;
					if (EndX < 0) EndX = 0;
					if (EndY < 0) EndY = 0;
					if (EndX >= ClientWidth)  EndX = ClientWidth - 1;
					if (EndY >= ClientHeight) EndY = ClientHeight - 1;
					SelectByMarquee(FLDownX, FLDownY, EndX, EndY);
				}
				else if (inside && abs(cp.x - FLDownX) < 5 && abs(cp.y - FLDownY) < 5)
				{
					SelectAt(cp.x, cp.y);
				}
			}

			FMarqueeActive = false;
			FLDownOnPlus = false;
			FTransformDragging = false;
		}

		// ---- wheel ----
		if (FWheel != 0)
		{
			if (!camView)
				FDist *= powf(0.9f, FWheel / 120.0f);
			FWheel = 0;
		}
		ClampDist();

		// ---- 3ds Max style transform / edit shortcuts ----
		bool typing = dynamic_cast<TCustomEdit*>(Screen->ActiveControl) != NULL;
		bool noMods = !ctl && !alt && !shf;
		bool canShortcut = !typing && (inside || Focused());
		bool keyDelete = canShortcut && noMods && KeyIsDown(VK_DELETE);
		bool keyCopy = canShortcut && ctl && !alt && !shf && KeyIsDown('C');
		bool keyPaste = canShortcut && ctl && !alt && !shf && KeyIsDown('V');
		if (keyDelete && !FPrevDelete) DeleteSelectedObjects();
		if (keyCopy && !FPrevCopy) CopySelectionToClipboard();
		if (keyPaste && !FPrevPaste) PasteClipboardObjects();
		FPrevDelete = keyDelete; FPrevCopy = keyCopy; FPrevPaste = keyPaste;

		// configurable keys of all commands (settings window -> ViewHotkeys).
		// Q/W/E/R (transform tools) are ordinary configurable commands too.
		for (int c = 0; c < vcCount; c++)
		{
			TShortCut Sc = ViewHotkeys().Key[c];
			bool d = false;
			bool isTool = (c >= vcToolSelect);
			if (Sc != 0 && !typing && (isTool ? (inside || Focused()) : inside))
			{
				Word vk;
				TShiftState ss;
				ShortCutToKey(Sc, vk, ss);
				d = KeyIsDown(vk) &&
					(ss.Contains(ssCtrl)  == ctl) &&
					(ss.Contains(ssAlt)   == alt) &&
					(ss.Contains(ssShift) == shf);
			}
			if (d && !FCmdPrev[c])
				ApplyCommand(c);
			FCmdPrev[c] = d;
		}
	}

	void __fastcall Render3D(TObject *Sender, SDL_Renderer *R)
	{
		if (!Showing)						// hidden (2D is active): no input polling, no drawing
			return;
		int w = ClientWidth, h = ClientHeight;
		if (w < 2 || h < 2)
			return;

		// frames per second, refreshed twice a second
		FFpsFrames++;
		Uint64 now = SDL_GetTicks();
		if (FFpsT0 == 0)
			FFpsT0 = now;
		if (now - FFpsT0 >= 500)
		{
			FFps = FFpsFrames * 1000.0f / (float)(now - FFpsT0);
			FFpsFrames = 0;
			FFpsT0 = now;
		}

		PollInput();
		UpdateCamera((float)w, (float)h);

		// grid
		if (FShowGrid)
		{
			// spacing / major lines come from the snap settings window (10 units each way)
			float Gs = FGridStep > 0.001f ? FGridStep : 1.0f;
			int N = (int)(10.0f / Gs + 0.5f);
			if (N > 200) N = 200;
			if (N < 1) N = 1;
			int Major = FMajorEvery > 0 ? FMajorEvery : 5;
			float Ext = N * Gs;
			for (int i = -N; i <= N; i++)
			{
				if (i == 0) continue;
				if (i % Major == 0) Color(R, 95, 95, 95); else Color(R, 62, 62, 62);
				Line3(R, V3(i * Gs, -Ext, 0), V3(i * Gs, Ext, 0));
				Line3(R, V3(-Ext, i * Gs, 0), V3(Ext, i * Gs, 0));
			}
			// the X / Y centre lines of the grid: plain dark grey (no coloured world axes, no Z axis line)
			Color(R, 38, 38, 38);
			Line3(R, V3(-Ext, 0, 0), V3(Ext, 0, 0));
			Line3(R, V3(0, -Ext, 0), V3(0, Ext, 0));
		}

		// scene objects: shaded solid faces in Stand, edges only in Wireframe
		bool Shaded = (FDisplayMode != dmWireframe);
		if (Shaded)
			BeginRaster(R, w, h);
		for (int i = 0; i < (int)FObjs.size(); i++)
		{
			const TSceneObj &ob = FObjs[i];
			DrawBox(R, ob.Pos, ob.Half, ob.Rot, ob.R, ob.G, ob.B, IsObjectSelected(i));
		}
		if (Shaded)
			EndRaster(R);

		if (FViewKind != vwCamera)			// seen from the camera itself there is nothing to draw
			DrawSceneCamera(R);

		DrawTransformGizmo(R);
		if (FSnapMarkerOn && FShowSnapMarker)	// the snap target in use: yellow square with a cross
		{
			float mx, my, md;
			if (ProjectPoint(FSnapMarker, mx, my, md))
			{
				SDL_SetRenderDrawColor(R, 255, 220, 0, 255);
				SDL_FRect Mk = { mx - 6.0f, my - 6.0f, 12.0f, 12.0f };
				SDL_RenderRect(R, &Mk);
				SDL_RenderLine(R, mx - 9.0f, my, mx + 9.0f, my);
				SDL_RenderLine(R, mx, my - 9.0f, mx, my + 9.0f);
			}
		}
		DrawGizmo(R, (float)h);
		const char *ToolName = FTransformMode == tmMove ? "Move" :
			(FTransformMode == tmRotate ? "Rotate" :
			(FTransformMode == tmScale ? "Scale" : "Select"));
		SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
		SDL_RenderDebugText(R, (float)(BTN_X + BTN_S + 6), (float)(BTN_Y + 18), ToolName);
		if (FSnapOn || FAngleSnapOn || FPercentSnapOn)
		{
			char sb[96];
			int n = 0;
			sb[0] = '\0';
			if (FSnapOn)         n += snprintf(sb + n, sizeof(sb) - n, "Snap %.2g  ", FGridStep);
			if (FAngleSnapOn)    n += snprintf(sb + n, sizeof(sb) - n, "Angle %g  ", FAngleStepDeg);
			if (FPercentSnapOn)  n += snprintf(sb + n, sizeof(sb) - n, "Percent %g%%", FPercentStep);
			SDL_SetRenderDrawColor(R, 255, 220, 0, 255);
			SDL_RenderDebugText(R, (float)(BTN_X + BTN_S + 6), (float)(BTN_Y + 32), sb);
			SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
		}

		// [+] button and view name, top left
		DrawPlusButton(R);
		SDL_SetRenderDrawColor(R, 220, 220, 220, 255);
		SDL_RenderDebugText(R, (float)(BTN_X + BTN_S + 6), (float)(BTN_Y + 4), ViewLabel());

		// statistics: FPS, top right
		if (FShowFps)
		{
			char buf[32];
			snprintf(buf, sizeof(buf), "FPS: %.1f", FFps);
			SDL_RenderDebugText(R, (float)(w - 8 - 8 * (int)strlen(buf)), (float)(BTN_Y + 4), buf);
		}

		// Draw the marquee last so it remains visible over the scene and HUD.
		DrawSelectionMarquee(R);
	}

	// Put new objects in the current view's visible area rather than at a hard-coded
	// world X coordinate. This matters in Top/Front/Left and Camera views, where a
	// world-X offset can send the new object off screen or directly behind another one.
	Vec3 __fastcall NewObjectPosition()
	{
		int W = ClientWidth;
		int H = ClientHeight;
		if (W < 100) W = 800;
		if (H < 100) H = 600;
		UpdateCamera((float)W, (float)H);

		Vec3 Center = (FViewKind == vwCamera) ? FSceneCam.Target : FTarget;
		Vec3 Right = Norm(FRight);
		Vec3 Up = Norm(FUp);
		if (Dot(Right, Right) < 0.5f) Right = V3(1.0f, 0.0f, 0.0f);
		if (Dot(Up, Up) < 0.5f) Up = V3(0.0f, 0.0f, 1.0f);

		// Approximate a useful screen-space spacing, then clamp it so objects remain
		// close to the viewport center even when the user is zoomed far in or out.
		float Step = 2.0f;
		if (FOrtho)
		{
			if (FScale > 0.001f) Step = (float)W * 0.16f / FScale;
		}
		else
		{
			float Depth = (FViewKind == vwCamera) ? Dot(Center - FPos, FFwd) : FDist;
			if (Depth < 1.0f) Depth = 1.0f;
			if (FFocal > 0.001f) Step = (float)W * 0.16f * Depth / FFocal;
		}
		if (Step < 0.8f) Step = 0.8f;
		if (Step > 4.0f) Step = 4.0f;

		// Arrange successive objects in a small 3x3 screen-facing pattern.
		int Ordinal = (int)FObjs.size() - 3;
		if (Ordinal < 0) Ordinal = 0;
		int Col = Ordinal % 3;
		int Row = (Ordinal / 3) % 3;
		return Center + Right * (Step * (0.65f + 0.75f * Col))
			+ Up * (Step * 0.75f * Row);
	}

public:
	__fastcall TViewport3D(TComponent *Owner) : TSDLPanel(Owner)
	{
		FFov = 0.9f;				// about 51 degrees
		FShowGrid = true;
		FLastX = FLastY = 0;
		FWasDown = FDragging = false;
		FWheel = 0;
		for (int i = 0; i < vcCount; i++) FCmdPrev[i] = false;
		FSelected = -1;
		FClipboardObjs.clear();
		FPasteCount = 0;
		FTransformMode = tmSelect;
		FPrevDelete = FPrevCopy = FPrevPaste = FPrevQ = FPrevW = FPrevE = FPrevR = false;
		FTransformDragging = false;
		FTransformFree = false; FTransformMoved = true;
		FFreeNormal = V3(0, 0, 1); FFreeStartHit = V3(0, 0, 0);
		FSnapOn = false; FAngleSnapOn = false; FPercentSnapOn = false;
		FSnapType[stGrid] = true; FSnapType[stVertex] = true; FSnapType[stEdge] = false;
		FSnapType[stFace] = false; FSnapType[stPivot] = false; FSnapType[stBBox] = false;
		FGridStep = 1.0f; FAngleStepDeg = 5.0f; FPercentStep = 10.0f; FSnapRadius = 10.0f;
		FMajorEvery = 10; FShowSnapMarker = true; FOnSnapToggled = NULL;
		FSnapMarkerOn = false; FSnapMarker = V3(0, 0, 0);
		FTex = NULL; FTexR = NULL; FBufW = FBufH = 0;
		FTransformAxis = -1;
		FTransformStartX = FTransformStartY = 0;
		FTransformPixelsPerWorld = 0.0f;
		FTransformScreenDirX = FTransformScreenDirY = 0.0f;
		FTransformStartAngle = 0.0f;
		FTransformPivotScreenX = FTransformPivotScreenY = 0.0f;
		FTransformScaleReference = 1.0f;
		FTransformPivot = V3(0, 0, 0);
		FOnSelect = NULL;
		FLDown = FLDownInside = false;
		FLDownOnPlus = false;
		FMarqueeActive = false;
		FLDownX = FLDownY = 0;
		FMarqueeEndX = FMarqueeEndY = 0;
		FViewKind = vwPerspective;
		FDisplayMode = dmStand;		// start with default lighting / shaded solid mode
		FStdView = false;
		FStdName = "";
		FViewLabelText[0] = '\0';
		FOrtho = false;
		FScale = 1.0f;
		FSceneCam.Pos    = V3(8, -8, 6);
		FSceneCam.Target = V3(0, 0, 1);
		FSceneCam.Fov    = 0.7f;
		FMenu = NULL;
		FmiPersp = FmiOrtho = FmiStand = FmiWire = FmiStats = FmiFps = FmiCam = NULL;
		FMenuOpen = false;
		FPlusHot = false;
		FShowFps = false;
		FFpsT0 = 0;
		FFpsFrames = 0;
		FFps = 0.0f;
		AddBox("Box001", V3(0, 0, 1),        V3(1, 1, 1),          240, 200, 80);
		AddBox("Box002", V3(4, 2, 0.5f),     V3(0.5f, 0.5f, 0.5f), 120, 200, 240);
		AddBox("Box003", V3(-3, -3, 1.5f),   V3(0.7f, 0.7f, 1.5f), 240, 120, 160);
		ResetView();
		ViewHotkeysLoad();					// shortcuts from the ini file (read once)
		BuildMenu();
		OnRender = Render3D;
		FOldOnMessage = Application->OnMessage;		// chain to any existing hook
		Application->OnMessage = AppMessage;
	}

	String __fastcall NextObjectName(const String &Prefix) const
	{
		for (int N = 1; N < 100000; ++N)
		{
			String Candidate = Prefix + FormatFloat(L"000", N);
			bool Exists = false;
			for (size_t I = 0; I < FObjs.size(); ++I)
			{
				if (SameText(FObjs[I].Name, Candidate))
				{
					Exists = true;
					break;
				}
			}
			if (!Exists)
				return Candidate;
		}
		return Prefix + IntToStr((int)FObjs.size() + 1);
	}

	void __fastcall AddBox(const String &Name, Vec3 Pos, Vec3 Half, int R, int G, int B)
	{
		TSceneObj o;
		o.Name = Name; o.Kind = L"Box"; o.Pos = Pos; o.Half = Half; o.Rot = V3(0, 0, 0);
		o.R = R; o.G = G; o.B = B;
		FObjs.push_back(o);
	}

	// Create a new box and make it the active selection. Size is the full XYZ size,
	// while the renderer stores half extents in TSceneObj::Half.
	int __fastcall CreateBoxObject()
	{
		String Name = NextObjectName(L"Box");
		Vec3 Pos = NewObjectPosition();
		Pos.z += 1.0f;
		AddBox(Name, Pos, V3(1.0f, 1.0f, 1.0f), 210, 170, 80);
		int Index = (int)FObjs.size() - 1;
		std::vector<int> NewSelection(1, Index);
		SetSelection(NewSelection);
		return Index;
	}

	// The current renderer is box-based, so a plane is represented by a very thin box.
	int __fastcall CreatePlaneObject()
	{
		String Name = NextObjectName(L"Plane");
		Vec3 Pos = NewObjectPosition();
		Pos.z += 0.05f;
		AddBox(Name, Pos, V3(2.0f, 2.0f, 0.05f), 100, 160, 210);
		int Index = (int)FObjs.size() - 1;
		FObjs[Index].Kind = L"Plane";
		std::vector<int> NewSelection(1, Index);
		SetSelection(NewSelection);
		return Index;
	}

	void __fastcall CopySelectionToClipboard()
	{
		FClipboardObjs.clear();
		for (size_t I = 0; I < FSelectedObjs.size(); ++I)
		{
			int Idx = FSelectedObjs[I];
			if (Idx >= 0 && Idx < (int)FObjs.size())
				FClipboardObjs.push_back(FObjs[Idx]);
		}
		FPasteCount = 0;
	}

	bool __fastcall PasteClipboardObjects()
	{
		if (FClipboardObjs.empty()) return false;
		std::vector<int> NewSelection;
		++FPasteCount;
		Vec3 Offset = FRight * (2.5f * (float)FPasteCount);
		for (size_t I = 0; I < FClipboardObjs.size(); ++I)
		{
			TSceneObj Copy = FClipboardObjs[I];
			Copy.Name = NextObjectName(Copy.Kind.IsEmpty() ? L"Box" : Copy.Kind);
			Copy.Pos = Copy.Pos + Offset;
			FObjs.push_back(Copy);
			NewSelection.push_back((int)FObjs.size() - 1);
		}
		SetSelection(NewSelection);
		return true;
	}

	bool __fastcall DuplicateSelectedObject()
	{
		if (FSelected < 0 || FSelected >= (int)FObjs.size()) return false;
		CopySelectionToClipboard();
		return PasteClipboardObjects();
	}

	void __fastcall DeleteSelectedObjects()
	{
		if (FSelectedObjs.empty())
			return;
		// Erase from back to front so the stored selection indices remain valid while removing.
		for (int I = (int)FObjs.size() - 1; I >= 0; --I)
			if (IsObjectSelected(I))
				FObjs.erase(FObjs.begin() + I);
		SetSelection(std::vector<int>());
	}

	TSceneObj* __fastcall MutableObj(int I)
	{
		return (I >= 0 && I < (int)FObjs.size()) ? &FObjs[I] : NULL;
	}

	// ---- snap settings (the toolbar button / "Grid and Snap Settings" window of Form1 own them) ----
	struct TViewSnap
	{
		bool   Enabled, Grid, Vertex, Edge, Midpoint, Center, BBox, ShowMarker;
		int    Radius, MajorEvery;
		double AngleDeg, Percent, GridSpacing;
	};
	void __fastcall SetSnapSettings(const TViewSnap &S)
	{
		FSnapOn = S.Enabled;
		FSnapType[stGrid]   = S.Grid;
		FSnapType[stVertex] = S.Vertex;
		FSnapType[stEdge]   = S.Edge || S.Midpoint;		// edge midpoints
		FSnapType[stPivot]  = S.Center;
		FSnapType[stBBox]   = S.BBox;
		FShowSnapMarker = S.ShowMarker;
		if (S.Radius > 0)       FSnapRadius = (float)S.Radius;
		if (S.AngleDeg > 0)     FAngleStepDeg = (float)S.AngleDeg;
		if (S.Percent > 0)      FPercentStep = (float)S.Percent;
		if (S.GridSpacing > 0)  FGridStep = (float)S.GridSpacing;
		if (S.MajorEvery > 0)   FMajorEvery = S.MajorEvery;
	}
	bool __fastcall GetSnapEnabled() { return FSnapOn; }
	__property bool SnapEnabled = { read = GetSnapEnabled };
	__property TNotifyEvent OnSnapToggled = { read = FOnSnapToggled, write = FOnSnapToggled };

	int __fastcall ObjectCount() { return (int)FObjs.size(); }
	const TSceneObj& __fastcall Obj(int I) { return FObjs[I]; }

	__property int Selected = { read = FSelected };
	__property TNotifyEvent OnSelect = { read = FOnSelect, write = FOnSelect };
	__property int  ViewKind = { read = FViewKind, write = SetViewKind };	// TViewKind
	__property bool ShowFps  = { read = FShowFps,  write = FShowFps };
	__property bool ShowGrid = { read = FShowGrid, write = FShowGrid };
	__property TPopupMenu *ViewMenu = { read = FMenu };						// the [+] menu (e.g. to owner-draw it)

	__fastcall ~TViewport3D()
	{
		Application->OnMessage = FOldOnMessage;
	}
};
//---------------------------------------------------------------------------
#endif
