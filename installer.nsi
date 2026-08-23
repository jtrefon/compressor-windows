; CompressorWindows NSIS installer script
; Build: makensis /DVERSION=vX.Y.Z installer.nsi
; Expects the staged portable tree in dist/CompressorWindows.

Unicode true
Name "Compressor for Windows"
OutFile "CompressorWindows-${VERSION}-setup.exe"
InstallDir "$PROGRAMFILES64\CompressorWindows"
RequestExecutionLevel admin

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "Install"
  SetOutPath "$INSTDIR"
  File /r "dist\CompressorWindows\*.*"
  CreateDirectory "$SMPROGRAMS\CompressorWindows"
  CreateShortcut "$SMPROGRAMS\CompressorWindows\Compressor for Windows.lnk" "$INSTDIR\CompressorWindows.exe"
  CreateShortcut "$DESKTOP\Compressor for Windows.lnk" "$INSTDIR\CompressorWindows.exe"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompressorWindows" "DisplayName" "Compressor for Windows"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompressorWindows" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompressorWindows" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompressorWindows" "Publisher" "jtrefon"
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\*.*"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\CompressorWindows\Compressor for Windows.lnk"
  RMDir "$SMPROGRAMS\CompressorWindows"
  Delete "$DESKTOP\Compressor for Windows.lnk"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\CompressorWindows"
SectionEnd