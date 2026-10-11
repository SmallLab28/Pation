!include x64.nsh
Name "libjpeg-turbo SDK for GCC 64-bit"
OutFile "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}libjpeg-turbo-3.2.1-gcc-x64.exe"
InstallDir "c:\libjpeg-turbo-gcc64"

SetCompressor bzip2

Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

Section "libjpeg-turbo SDK for GCC 64-bit (required)"
!ifdef WIN64
	${If} ${RunningX64}
	${DisableX64FSRedirection}
	${Endif}
!endif
	SectionIn RO
!ifdef GCC
	IfFileExists $SYSDIR/libturbojpeg.dll exists 0
!else
	IfFileExists $SYSDIR/turbojpeg.dll exists 0
!endif
	goto notexists
	exists:
!ifdef GCC
	MessageBox MB_OK "An existing version of the libjpeg-turbo SDK for GCC 64-bit is already installed.  Please uninstall it first."
!else
	MessageBox MB_OK "An existing version of the libjpeg-turbo SDK for GCC 64-bit or the TurboJPEG SDK is already installed.  Please uninstall it first."
!endif
	quit

	notexists:
	SetOutPath $SYSDIR
!ifdef GCC
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\libturbojpeg.dll"
!else
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}turbojpeg.dll"
!endif
	SetOutPath $INSTDIR\bin
!ifdef GCC
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\libturbojpeg.dll"
!else
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}turbojpeg.dll"
!endif
!ifdef GCC
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\libjpeg-62.dll"
!else
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}jpeg62.dll"
!endif
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}cjpeg.exe"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}djpeg.exe"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}jpegtran.exe"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}tjbench.exe"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}rdjpgcom.exe"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}wrjpgcom.exe"
	SetOutPath $INSTDIR\lib
!ifdef GCC
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\libturbojpeg.dll.a"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\libturbojpeg.a"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\libjpeg.dll.a"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\libjpeg.a"
!else
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}turbojpeg.lib"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}turbojpeg-static.lib"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}jpeg.lib"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\${BUILDDIR}jpeg-static.lib"
!endif
	SetOutPath $INSTDIR\lib\pkgconfig
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\pkgscripts\libjpeg.pc"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\pkgscripts\libturbojpeg.pc"
	SetOutPath $INSTDIR\lib\cmake\libjpeg-turbo
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\pkgscripts\libjpeg-turboConfig.cmake"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\pkgscripts\libjpeg-turboConfigVersion.cmake"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\win\libjpeg-turboTargets.cmake"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\win\libjpeg-turboTargets-release.cmake"
!ifdef JNA
	SetOutPath $INSTDIR\classes
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\jna\turbojpeg-jna.jar"
!endif
	SetOutPath $INSTDIR\include
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_ucrt\jconfig.h"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\src\jerror.h"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\src\jmorecfg.h"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\src\jpeglib.h"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\src\turbojpeg.h"
	SetOutPath $INSTDIR\doc
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\README.ijg"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\README.md"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\LICENSE.md"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\src\example.c"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\libjpeg.txt"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\structure.txt"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\usage.txt"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\wizard.txt"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\src\tjcomp.c"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\src\tjdecomp.c"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\src\tjtran.c"
	SetOutPath $INSTDIR\doc\jna
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\jna\TJ.java"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\jna\TJComp.java"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\jna\TJDecomp.java"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\jna\TJTran.java"
!ifdef GCC
	SetOutPath $INSTDIR\man\man1
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\cjpeg.1"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\djpeg.1"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\jpegtran.1"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\rdjpgcom.1"
	File "C:/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo\doc\wrjpgcom.1"
!endif

	WriteRegStr HKLM "SOFTWARE\libjpeg-turbo-gcc64 3.2.1" "Install_Dir" "$INSTDIR"

	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo-gcc64 3.2.1" "DisplayName" "libjpeg-turbo SDK v3.2.1 for GCC 64-bit"
	WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo-gcc64 3.2.1" "UninstallString" '"$INSTDIR\uninstall_3.2.1.exe"'
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo-gcc64 3.2.1" "NoModify" 1
	WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo-gcc64 3.2.1" "NoRepair" 1
	WriteUninstaller "uninstall_3.2.1.exe"
SectionEnd

Section "Uninstall"
!ifdef WIN64
	${If} ${RunningX64}
	${DisableX64FSRedirection}
	${Endif}
!endif

	SetShellVarContext all

	DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\libjpeg-turbo-gcc64 3.2.1"
	DeleteRegKey HKLM "SOFTWARE\libjpeg-turbo-gcc64 3.2.1"

!ifdef GCC
	Delete $INSTDIR\bin\libjpeg-62.dll
	Delete $INSTDIR\bin\libturbojpeg.dll
	Delete $SYSDIR\libturbojpeg.dll
	Delete $INSTDIR\lib\libturbojpeg.dll.a
	Delete $INSTDIR\lib\libturbojpeg.a
	Delete $INSTDIR\lib\libjpeg.dll.a
	Delete $INSTDIR\lib\libjpeg.a
!else
	Delete $INSTDIR\bin\jpeg62.dll
	Delete $INSTDIR\bin\turbojpeg.dll
	Delete $SYSDIR\turbojpeg.dll
	Delete $INSTDIR\lib\jpeg.lib
	Delete $INSTDIR\lib\jpeg-static.lib
	Delete $INSTDIR\lib\turbojpeg.lib
	Delete $INSTDIR\lib\turbojpeg-static.lib
!endif
	Delete $INSTDIR\lib\pkgconfig\libjpeg.pc
	Delete $INSTDIR\lib\pkgconfig\libturbojpeg.pc
	Delete $INSTDIR\lib\cmake\libjpeg-turbo\libjpeg-turboConfig.cmake
	Delete $INSTDIR\lib\cmake\libjpeg-turbo\libjpeg-turboConfigVersion.cmake
	Delete $INSTDIR\lib\cmake\libjpeg-turbo\libjpeg-turboTargets.cmake
	Delete $INSTDIR\lib\cmake\libjpeg-turbo\libjpeg-turboTargets-release.cmake
!ifdef JNA
	Delete $INSTDIR\classes\turbojpeg-jna.jar
!endif
	Delete $INSTDIR\bin\cjpeg.exe
	Delete $INSTDIR\bin\djpeg.exe
	Delete $INSTDIR\bin\jpegtran.exe
	Delete $INSTDIR\bin\tjbench.exe
	Delete $INSTDIR\bin\rdjpgcom.exe
	Delete $INSTDIR\bin\wrjpgcom.exe
	Delete $INSTDIR\include\jconfig.h
	Delete $INSTDIR\include\jerror.h
	Delete $INSTDIR\include\jmorecfg.h
	Delete $INSTDIR\include\jpeglib.h
	Delete $INSTDIR\include\turbojpeg.h
	Delete $INSTDIR\uninstall_3.2.1.exe
	Delete $INSTDIR\doc\README.ijg
	Delete $INSTDIR\doc\README.md
	Delete $INSTDIR\doc\LICENSE.md
	Delete $INSTDIR\doc\example.c
	Delete $INSTDIR\doc\libjpeg.txt
	Delete $INSTDIR\doc\structure.txt
	Delete $INSTDIR\doc\usage.txt
	Delete $INSTDIR\doc\wizard.txt
	Delete $INSTDIR\doc\tjcomp.c
	Delete $INSTDIR\doc\tjdecomp.c
	Delete $INSTDIR\doc\tjtran.c
	Delete $INSTDIR\doc\jna\TJ.java
	Delete $INSTDIR\doc\jna\TJComp.java
	Delete $INSTDIR\doc\jna\TJDecomp.java
	Delete $INSTDIR\doc\jna\TJTran.java
!ifdef GCC
	Delete $INSTDIR\man\man1\cjpeg.1
	Delete $INSTDIR\man\man1\djpeg.1
	Delete $INSTDIR\man\man1\jpegtran.1
	Delete $INSTDIR\man\man1\rdjpgcom.1
	Delete $INSTDIR\man\man1\wrjpgcom.1
!endif

	RMDir "$INSTDIR\include"
	RMDir "$INSTDIR\lib\pkgconfig"
	RMDir "$INSTDIR\lib\cmake\libjpeg-turbo"
	RMDir "$INSTDIR\lib\cmake"
	RMDir "$INSTDIR\lib"
	RMDir "$INSTDIR\doc\jna"
	RMDir "$INSTDIR\doc"
!ifdef GCC
	RMDir "$INSTDIR\man\man1"
	RMDir "$INSTDIR\man"
!endif
!ifdef JNA
	RMDir "$INSTDIR\classes"
!endif
	RMDir "$INSTDIR\bin"
	RMDir "$INSTDIR"

SectionEnd
