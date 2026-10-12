//---------------------------------------------------------------------------
// TSDLPanel - a VCL control that renders with SDL3.
//
// Header-only: just #include "SDLPanel.h" (in Unit1.h), add the SDL3 include path to the project,
// add SDL3.lib to the project and copy SDL3.dll next to the .exe.
//
// Usage:
//   FSdl = new TSDLPanel(this);
//   FSdl->Parent   = Document;          // any parent: TPanel, TLMDDockPanel, the form ...
//   FSdl->Align    = alClient;
//   FSdl->OnRender = SdlRender;         // void __fastcall TForm1::SdlRender(TObject *Sender, SDL_Renderer *R)
//---------------------------------------------------------------------------
#ifndef SDLPanelH
#define SDLPanelH
//---------------------------------------------------------------------------
#include <System.Classes.hpp>
#include <Vcl.Controls.hpp>
#include <Vcl.ExtCtrls.hpp>

// A VCL program has its own WinMain: SDL must not replace it with SDL_main
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
//---------------------------------------------------------------------------
typedef void __fastcall (__closure *TSDLRenderEvent)(System::TObject *Sender, SDL_Renderer *Renderer);
//---------------------------------------------------------------------------
class TSDLPanel : public TCustomControl
{
private:
	SDL_Window      *FWindow;
	SDL_Renderer    *FRenderer;
	TTimer          *FTimer;
	bool             FSdlInited;
	TSDLRenderEvent  FOnRender;

	void __fastcall TimerTick(TObject *Sender)
	{
		RenderFrame();
	}

protected:
	// The SDL window is wrapped around the Windows handle of this control. VCL can destroy and
	// re-create that handle (e.g. when a docking panel is moved), so SDL is created / destroyed
	// together with it.
	virtual void __fastcall CreateWnd()
	{
		TCustomControl::CreateWnd();
		if (ComponentState.Contains(csDesigning))
			return;

		SDL_SetMainReady();
		FSdlInited = SDL_Init(SDL_INIT_VIDEO);
		if (!FSdlInited)
			return;

		SDL_PropertiesID Props = SDL_CreateProperties();
		SDL_SetPointerProperty(Props, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, Handle);
		FWindow = SDL_CreateWindowWithProperties(Props);
		SDL_DestroyProperties(Props);
		if (FWindow)
			FRenderer = SDL_CreateRenderer(FWindow, NULL);
		FTimer->Enabled = (FRenderer != NULL);
	}

	virtual void __fastcall DestroyWnd()
	{
		FTimer->Enabled = false;
		if (FRenderer)
		{
			SDL_DestroyRenderer(FRenderer);
			FRenderer = NULL;
		}
		if (FWindow)
		{
			SDL_DestroyWindow(FWindow);
			FWindow = NULL;
		}
		if (FSdlInited)
		{
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			FSdlInited = false;
		}
		TCustomControl::DestroyWnd();
	}

	// SDL wraps an external HWND: tell it when the control size changes
	virtual void __fastcall Resize()
	{
		TCustomControl::Resize();
		if (FWindow && ClientWidth > 0 && ClientHeight > 0)
			SDL_SetWindowSize(FWindow, ClientWidth, ClientHeight);
	}

	// SDL draws everything, VCL must not paint over it
	virtual void __fastcall Paint()
	{
	}

public:
	__fastcall TSDLPanel(TComponent *Owner)
		: TCustomControl(Owner), FWindow(NULL), FRenderer(NULL), FSdlInited(false), FOnRender(NULL)
	{
		ControlStyle = ControlStyle << csOpaque;	// no background erase -> no flicker
		FTimer = new TTimer(this);
		FTimer->Enabled  = false;
		FTimer->Interval = 16;						// about 60 frames per second
		FTimer->OnTimer  = TimerTick;
	}

	// Draw one frame (called by the timer; can also be called manually)
	void __fastcall RenderFrame()
	{
		if (!FRenderer)
			return;

		SDL_Event E;
		while (SDL_PollEvent(&E))
		{
			// events of the SDL window (mouse, keyboard ...) arrive here
		}

		SDL_SetRenderDrawColor(FRenderer, 32, 32, 32, 255);
		SDL_RenderClear(FRenderer);
		if (FOnRender)
			FOnRender(this, FRenderer);
		SDL_RenderPresent(FRenderer);
	}

	__property SDL_Renderer *Renderer = { read = FRenderer };
	__property TSDLRenderEvent OnRender = { read = FOnRender, write = FOnRender };
};
//---------------------------------------------------------------------------
#endif
