
[Setup]

; インストール先の指定 xxxxxxx のインストール先を指定してください。
AppName=FireAlpaca SE Thumbnail arm64 (MDZ)

; アプリケーションを一意に識別するための内部的なID
AppId=FireAlpacaMdzThumb_arm64

; タイトルバー表示
AppVerName=FireAlpacaMdzThumb arm64 1.0.1

; 生成バイナリ名
OutputBaseFilename=FireAlpacaMdzThumb_arm64_setup

DefaultDirName={commonpf}\FireAlpaca\FireAlpacaMdzThumb_arm64
DefaultGroupName=FireAlpacaMdzThumb_arm64

Compression=lzma
SolidCompression=yes
OutputDir=Output
ChangesAssociations=yes
VersionInfoVersion=1.0.1.0

ArchitecturesInstallIn64BitMode=arm64
ArchitecturesAllowed=arm64

AppPublisher=PGN Inc.
AppPublisherURL=http://firealpaca.com/
AppSupportURL=http://firealpaca.com/se/
AppVersion=1.0.1

WizardImageFile=wizard_mdz.bmp
WizardImageStretch=no

[Files]

Source: "firealpaca_mdz.ico"; DestDir: "{app}"

; MDP/MDZ サムネイル登録 (64bit/Vista以降)
Source: "FAThumbDll_arm64.dll"; DestDir: "{sys}"; Flags: regserver sharedfile 64bit; Check: IsWin64; MinVersion: 6.0

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
