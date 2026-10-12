//---------------------------------------------------------------------------

#ifndef Unit1H
#define Unit1H
//---------------------------------------------------------------------------
#include "Viewport3D.h"
#include <System.Classes.hpp>
#include <Vcl.Controls.hpp>
#include <Vcl.StdCtrls.hpp>
#include <Vcl.Forms.hpp>
#include "LMDDckSite.hpp"
#include <Vcl.ComCtrls.hpp>
#include <Vcl.Menus.hpp>
#include <Vcl.ToolWin.hpp>
#include <System.ImageList.hpp>
#include <Vcl.ImgList.hpp>
#include <Vcl.TitleBarCtrls.hpp>
#include <Vcl.WinXCtrls.hpp>
#include <Vcl.ExtCtrls.hpp>
#include <System.Actions.hpp>
#include <Vcl.ActnCtrls.hpp>
#include <Vcl.ActnList.hpp>
#include <Vcl.ActnMan.hpp>
#include <Vcl.ActnMenus.hpp>
#include <Vcl.ValEdit.hpp>
#include <Vcl.PlatformDefaultStyleActnCtrls.hpp>
//---------------------------------------------------------------------------
// Snap / grid settings (edited in the "Grid and Snap Settings" window, read by the viewport)
struct TSnapSettings
{
    bool   Enabled;          // snap toggle (toolbar button)
    bool   Grid, Vertex, Edge, Midpoint, Center, BBox;   // snap types
    int    Radius;           // snap radius in pixels
    double AngleDeg;         // angle snap (degrees)
    double Percent;          // percent snap
    bool   AxisConstraint;
    bool   ShowMarker;
    double GridSpacing;      // home grid: spacing
    int    MajorEvery;       // home grid: major line every N lines
    TSnapSettings() : Enabled(false), Grid(true), Vertex(true), Edge(false), Midpoint(false),
        Center(false), BBox(false), Radius(10), AngleDeg(5.0), Percent(10.0),
        AxisConstraint(true), ShowMarker(true), GridSpacing(1.0), MajorEvery(10) {}
};
//---------------------------------------------------------------------------
class TForm1 : public TForm
{
__published:    // IDE-managed Components
    TTitleBarPanel *TitleBarPanel1;
    TMainMenu *MainMenu1;
    TMainMenu *MainMenu2;
    TMenuItem *mnuFile;
    TMenuItem *mnuEdit;
    TMenuItem *mnuHelp;
    TImageList *ImageList;
    TMenuItem *mnuView;
    TMenuItem *mnuNew;
    TMenuItem *About;
    TMenuItem *OnLine;
    TLMDDockManager *LMDDockManager1;
    TLMDDockSite *LMDDockSite1;
    TLMDDockPanel *Output;
    TLMDDockPanel *LMDDockPanel2;
    TLMDDockPanel *Explore;
    TPanel *pnlMenuBar;
    TLMDDockPanel *Document;
    TMenuItem *N1;
    TMenuItem *mnuOpen;
    TMenuItem *E1;
    TMenuItem *S1;
    TMenuItem *N2;
    TMenuItem *N11;
    TMenuItem *N4;
    TMenuItem *N5;
    TMenuItem *N6;
    TMenuItem *N7;
    TMenuItem *N8;
    TMenuItem *N9;
    TMenuItem *N3;
    TToolBar *tbrMenu;
    TToolBar *tbrHelp;
    TTreeView *TreeView1;
    TControlBar *cbrTheme;
    TToolBar *tbrTheme;
    TToolBar *ToolBar1;
    TToolButton *ToolButton1;
    TToolButton *ToolButton3;
    TToolBar *ToolBar2;
    TToolButton *ToolButton7;
	TToolButton *ToolButton5;
	TToolBar *ToolBar3;
	TToolBar *ToolBar4;
	TToolButton *ToolButton9;     // ToolBar3: 2D
	TToolButton *ToolButton11;    // ToolBar3: 3D
	TToolButton *ToolButton13;    // ToolBar4: snap toggle
	void __fastcall TitleBarPanel1CustomButtons0Paint(TObject *Sender);
    void __fastcall TitleBarPanel1CustomButtons1Paint(TObject *Sender);
    void __fastcall FormCreate(TObject *Sender);
    void __fastcall FormResize(TObject *Sender);
    void __fastcall FormShow(TObject *Sender);
    void __fastcall TitleBarPanel1Paint(TObject *Sender, TCanvas *Canvas, TRect &ARect);
    void __fastcall N9Click(TObject *Sender);
    void __fastcall AboutClick(TObject *Sender);
private:
    bool FDark;        // true = dark theme active
    void __fastcall AlignHelpToolBar();
    // ---- file tree (Explore panel) ----
    TPopupMenu *FTreeMenu;
    TMenuItem *FmiNewFile, *FmiCut, *FmiCopy, *FmiPaste, *FmiRename, *FmiDelete, *FmiRefresh;
    bool FAllowRename;        // true only while "Rename" is running (blocks the slow-click rename)

    // ---- toolbar visibility menu (right-click a ControlBar drag handle) ----
    TPopupMenu *FBarMenu;
    TMenuItem *FmiShowThemeBar, *FmiShowToolBar1, *FmiShowToolBar2, *FmiShowToolBar3, *FmiShowToolBar4;
    void __fastcall InitToolBarVisibilityMenu();
    void __fastcall ThemeBarContextPopup(TObject *Sender, const TPoint &MousePos, bool &Handled);
    void __fastcall ToolBarVisibilityMenuPopup(TObject *Sender);
    void __fastcall ToolBarVisibilityMenuClick(TObject *Sender);

    void __fastcall InitFileTree();
    TMenuItem* __fastcall AddTreeMenuItem(const String &ACaption, TNotifyEvent AHandler);
    void __fastcall TreeView1Expanding(TObject *Sender, TTreeNode *Node, bool &AllowExpansion);
    void __fastcall TreeView1Deletion(TObject *Sender, TTreeNode *Node);
    void __fastcall TreeView1MouseDown(TObject *Sender, TMouseButton Button, TShiftState Shift, int X, int Y);
    void __fastcall TreeView1KeyDown(TObject *Sender, Word &Key, TShiftState Shift);
    void __fastcall TreeView1Editing(TObject *Sender, TTreeNode *Node, bool &AllowEdit);
    void __fastcall TreeView1Edited(TObject *Sender, TTreeNode *Node, UnicodeString &S);
    void __fastcall TreeMenuPopup(TObject *Sender);
    void __fastcall TreeMenuCut(TObject *Sender);
    void __fastcall TreeMenuCopy(TObject *Sender);
    void __fastcall TreeMenuPaste(TObject *Sender);
    void __fastcall TreeMenuRename(TObject *Sender);
    void __fastcall TreeMenuNewFile(TObject *Sender);
    void __fastcall TreeMenuDelete(TObject *Sender);
    void __fastcall TreeMenuRefresh(TObject *Sender);
    void __fastcall ApplyTheme(bool ADark);
    void __fastcall TitleBarPanel1CustomButtons0Click(TObject *Sender);
    void __fastcall HookMenuItems(TMenuItem *Parent);
    void __fastcall ApplyMenuColors();
    void __fastcall MenuItemAdvancedDrawItem(TObject *Sender, TCanvas *ACanvas,
		const TRect &ARect, TOwnerDrawState State);
    void __fastcall MenuItemMeasureItem(TObject *Sender, TCanvas *ACanvas, int &Width, int &Height);
    void __fastcall ToolBarAdvancedCustomDraw(TToolBar *Sender, const TRect &ARect,
        TCustomDrawStage Stage, bool &DefaultDraw);
    void __fastcall ToolBarAdvancedCustomDrawButton(TToolBar *Sender, TToolButton *Button,
        TCustomDrawState State, TCustomDrawStage Stage, TTBCustomDrawFlags &Flags, bool &DefaultDraw);
    // ---- theme colour bar (row of colour swatches below the menu bar) ----
    void __fastcall InitThemeBar();
    void __fastcall LayoutThemeBar();
    void __fastcall ThemeSwatchClick(TObject *Sender);
 	void __fastcall ThemeBarGetSiteInfo(TObject *Sender, TControl *DockClient,
	TRect &InfluenceRect, const TPoint &MousePos, bool &CanDock);
    void __fastcall ThemeBarBandInfo(TObject *Sender, TControl *Control, TRect &Insets,
        int &PreferredSize, int &RowCount);
    void __fastcall ThemeBarDockOver(TObject *Sender, TDragDockObject *Source,
        int X, int Y, TDragState State, bool &Accept);
    void __fastcall ThemeBarBandPaint(TObject *Sender, TControl *Control, TCanvas *Canvas,
        TRect &ARect, TBandPaintOptions &Options);
    void __fastcall ThemeBarAdvancedCustomDraw(TToolBar *Sender, const TRect &ARect,
        TCustomDrawStage Stage, bool &DefaultDraw);
    void __fastcall ThemeBarAdvancedCustomDrawButton(TToolBar *Sender, TToolButton *Button,
        TCustomDrawState State, TCustomDrawStage Stage, TTBCustomDrawFlags &Flags, bool &DefaultDraw);
    TViewport3D *FSdl;
    TImageList *FThemeImages;        // icons of the theme buttons (built in code)
    TImageList *FToolBarImages;     // icons for the movable toolbar buttons
    TValueListEditor *FProps;        // property panel (LMDDockPanel2)
    // ---- ToolBar3 (2D / 3D) and ToolBar4 (snap toggle, long press = settings window) ----
    TTimer *FSnapHoldTimer;          // long-press detection of the snap button
    bool FSnapLongPress;             // true when the last press was a long press (swallows the click)
    int FViewMode;                   // 0 = 2D, 1 = 3D
    TSnapSettings FSnap;
    TForm *FSnapForm;                // settings window (created on first use)
    void __fastcall SetViewMode(int AMode);
    void __fastcall ModeButtonClick(TObject *Sender);
    void __fastcall SnapButtonMouseDown(TObject *Sender, TMouseButton Button, TShiftState Shift, int X, int Y);
    void __fastcall SnapButtonClick(TObject *Sender);
    void __fastcall SnapHoldTimer(TObject *Sender);
    void __fastcall SnapSettingsChanged(TObject *Sender);
    void __fastcall ShowSnapSettings();
    void __fastcall ModeBarAdvancedCustomDrawButton(TToolBar *Sender, TToolButton *Button,
        TCustomDrawState State, TCustomDrawStage Stage, TTBCustomDrawFlags &Flags, bool &DefaultDraw);
    void __fastcall InitPropertyPanel();
    void __fastcall ShowProperties();
    void __fastcall ApplyPropsTheme();
    void __fastcall PropsDrawCell(TObject *Sender, int ACol, int ARow, const TRect &Rect, TGridDrawState State);
    void __fastcall ViewportSelect(TObject *Sender);
public:
    __fastcall TForm1(TComponent* Owner);
    __fastcall ~TForm1();
};
//---------------------------------------------------------------------------
extern PACKAGE TForm1 *Form1;
//---------------------------------------------------------------------------
#endif
