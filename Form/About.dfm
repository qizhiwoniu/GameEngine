object Form3: TForm3
  Left = 0
  Top = 0
  BorderStyle = bsDialog
  Caption = 'About'
  ClientHeight = 180
  ClientWidth = 380
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -12
  Font.Name = 'Segoe UI'
  Font.Style = []
  Position = poOwnerFormCenter
  TextHeight = 15
  object lblName: TLabel
    Left = 28
    Top = 24
    Width = 74
    Height = 25
    Caption = 'lblName'
    Font.Charset = DEFAULT_CHARSET
    Font.Color = clWindowText
    Font.Height = -19
    Font.Name = 'Segoe UI'
    Font.Style = [fsBold]
    ParentFont = False
  end
  object lblVersion: TLabel
    Left = 28
    Top = 68
    Width = 51
    Height = 15
    Caption = 'lblVersion'
  end
  object lblCopyright: TLabel
    Left = 28
    Top = 92
    Width = 66
    Height = 15
    Caption = 'lblCopyright'
  end
  object btnOk: TButton
    Left = 276
    Top = 136
    Width = 80
    Height = 28
    Caption = 'OK'
    Default = True
    ModalResult = 1
    TabOrder = 0
  end
end
