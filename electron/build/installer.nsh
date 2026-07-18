; Custom NSIS include merged into electron-builder's generated installer.
;
; Adds Explorer right-click context-menu verbs that shell out to the bundled
; standalone CLI (resources/cli/mscompress.exe) with NO GUI:
;   .msz  -> "Decompress here"  (writes <name>.mzML next to the file)
;   .mszx -> "Extract here"     (writes a <name>/ directory next to the file)
;
; The verbs hang off SystemFileAssociations\<ext> (extension-scoped) rather than
; the app's ProgID, so the action is always present regardless of which app owns
; the default "open" for the type. SHCTX is set by electron-builder to HKCU for a
; per-user install (perMachine:false) or HKLM for all-users — the same script
; works for both with no elevation assumptions.
;
; The CLI infers the operation from the extension and writes output next to the
; input when no output path is given, so a single quoted "%1" is all we pass.
; `cmd /s /c "... & echo. & pause"` keeps the console open so the user sees the
; CLI's progress output and any error before the window closes.

!macro customInstall
  DetailPrint "Registering MScompress context-menu actions…"

  ; ---- .msz : Decompress here ----
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress" "" "Decompress here"
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress" "Icon" "$INSTDIR\resources\cli\mscompress.exe,0"
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress\command" "" 'cmd /s /c ""$INSTDIR\resources\cli\mscompress.exe" "%1" & echo. & pause"'

  ; ---- .mszx : Extract here ----
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.mszx\shell\MScompressExtract" "" "Extract here"
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.mszx\shell\MScompressExtract" "Icon" "$INSTDIR\resources\cli\mscompress.exe,0"
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.mszx\shell\MScompressExtract\command" "" 'cmd /s /c ""$INSTDIR\resources\cli\mscompress.exe" "%1" & echo. & pause"'

  ; Tell Explorer to reload associations/icons so the verbs appear immediately.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend

!macro customUnInstall
  DetailPrint "Removing MScompress context-menu actions…"
  DeleteRegKey SHCTX "Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress"
  DeleteRegKey SHCTX "Software\Classes\SystemFileAssociations\.mszx\shell\MScompressExtract"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend
