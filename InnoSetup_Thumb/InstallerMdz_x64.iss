
[Setup]

; インストール先の指定 xxxxxxx のインストール先を指定してください。
AppName=FireAlpaca SE Thumbnail x64 (MDZ)

; アプリケーションを一意に識別するための内部的なID
AppId=FireAlpacaMdzThumb_x64

; タイトルバー表示
AppVerName=FireAlpacaMdzThumb x64 1.0.1

; 生成バイナリ名
OutputBaseFilename=FireAlpacaMdzThumb_x64_setup

DefaultDirName={commonpf}\FireAlpaca\FireAlpacaMdzThumb_x64
DefaultGroupName=FireAlpacaMdzThumb_x64

Compression=lzma
SolidCompression=yes
OutputDir=Output
ChangesAssociations=yes
VersionInfoVersion=1.0.1.0

ArchitecturesInstallIn64BitMode=x64os
ArchitecturesAllowed=x64os

AppPublisher=PGN Inc.
AppPublisherURL=http://firealpaca.com/
AppSupportURL=http://firealpaca.com/se/
AppVersion=1.0.1

WizardImageFile=wizard_mdz.bmp
WizardImageStretch=no

[Files]

Source: "firealpaca_mdz.ico"; DestDir: "{app}"

; MDP/MDZ サムネイル登録 (64bit/Vista以降)
Source: "FAThumbDll_x64.dll"; DestDir: "{sys}"; Flags: regserver sharedfile 64bit; Check: IsWin64; MinVersion: 6.0

[Languages]

Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "jp"; MessagesFile: "compiler:Languages\Japanese.isl"

[Registry]

Root: HKCR; Subkey: ".mdz"; ValueType: string; ValueName: ""; ValueData: "FireAlpacaSeFile"; Flags: uninsdeletevalue

Root: HKCR; Subkey: "FireAlpacaSeFile"; ValueType: string; ValueName: ""; ValueData: "FireAlpaca SE Document"; Flags: uninsdeletekey

;実行ファイルには結びつけられない
;Root: HKCR; Subkey: "FireAlpacaFile\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\FireAlpacaSE.exe"" ""%1"""

;関連付けたファイルのアイコン＝指定アイコン (MdpThumb でサムネイルが表示されない時のアイコン)
Root: HKCR; Subkey: "FireAlpacaSeFile\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\firealpaca_mdz.ico"
