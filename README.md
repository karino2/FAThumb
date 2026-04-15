# ダイナミック リンク ライブラリ : FAThumb レポジトリの概要

このレポジトリは、FireAlpaca SE 3.0のWindows用Explorer サムネイルハンドラのうち、mdzの部分をオープンソース化したものです。

この FAThumbDll DLL は、AppWizard によって作成されたものを、手で修正して使っています。
もともとmdpとmdzの両方をサポートしていたのを公開用に分離したりしているので、
mdzのみなら不要なコードも含まれています。

また、歴史的事情でコード内でmdzをpxvと呼ぶこともあります。

## 動作確認

このファイルのあるディレクトリから見てFAThumb/x64/DebugにFAThumbDll.dllが生成されます。
これに対し「管理者権限」で以下を実行します。

```
> regsvr32.exe .\FAThumb\x64\Debug\FAThumbDll.dll
```

unregisterは以下。

```
> regsvr32.exe /u .\FAThumb\x64\Debug\FAThumbDll.dll
```

## ライセンス

- ClassFactory.h, Reg.cpp, Reg.hはCppShellExtThumbnailHandlerを元にしているため MS-LPL ([msdn-code-gallery-microsoft/OneCodeTeam/C++ Windows Shell thumbnail handler (CppShellExtThumbnailHandler)/README.md at master · microsoftarchive/msdn-code-gallery-microsoft](https://github.com/microsoftarchive/msdn-code-gallery-microsoft/blob/master/OneCodeTeam/C%2B%2B%20Windows%20Shell%20thumbnail%20handler%20(CppShellExtThumbnailHandler)/README.md))
- Zstdは BSD 3.0 https://github.com/facebook/zstd/blob/dev/LICENSE
- FlatBuffers及びその他のファイルは Apache 2.0ライセンス ([LICENSE](LICENSE)ファイルを参照)

## 参考リンク

- [サムネイル ハンドラーの構築 - Win32 apps - Microsoft Learn](https://learn.microsoft.com/ja-jp/windows/win32/shell/building-thumbnail-providers)
- [レシピ サムネイル プロバイダーのサンプル - Win32 apps - Microsoft Learn](https://learn.microsoft.com/ja-jp/windows/win32/shell/samples-recipethumbnailprovider)

