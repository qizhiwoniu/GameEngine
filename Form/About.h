//---------------------------------------------------------------------------

#ifndef AboutH
#define AboutH
//---------------------------------------------------------------------------
#include <System.Classes.hpp>
#include <Vcl.Controls.hpp>
#include <Vcl.StdCtrls.hpp>
#include <Vcl.Forms.hpp>
//---------------------------------------------------------------------------
class TForm3 : public TForm
{
__published:	// IDE-managed Components
	TLabel *lblName;
	TLabel *lblVersion;
	TLabel *lblCopyright;
	TButton *btnOk;
private:	// User declarations
	bool FDark;		// true = dark theme active
	void __fastcall ApplyTitleBar();
protected:
	virtual void __fastcall CreateWnd();	// re-applies the dark title bar whenever the handle is (re)created
public:		// User declarations
	__fastcall TForm3(TComponent* Owner);
	void __fastcall ApplyTheme(bool ADark);	// call it with the main form's current theme
};
//---------------------------------------------------------------------------
extern PACKAGE TForm3 *Form3;
//---------------------------------------------------------------------------
#endif
