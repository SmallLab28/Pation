# Install script for directory: /mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/opt/libjpeg-turbo")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "TRUE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/sbin/objdump")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/src/spng/cmake_install.cmake")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/src/md5/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "lib" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib32" TYPE STATIC_LIBRARY FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/libturbojpeg.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "bin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM RENAME "tjbench.js" FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/tjbench-static.js")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "include" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/src/turbojpeg.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "lib" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib32" TYPE STATIC_LIBRARY FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/libjpeg.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "bin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM RENAME "cjpeg.js" FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/cjpeg-static.js")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "bin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM RENAME "djpeg.js" FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/djpeg-static.js")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "bin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM RENAME "jpegtran.js" FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/jpegtran-static.js")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "bin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/rdjpgcom.js")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/rdjpgcom.js" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/rdjpgcom.js")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/sbin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/rdjpgcom.js")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "bin" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/wrjpgcom.js")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/wrjpgcom.js" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/wrjpgcom.js")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/sbin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/wrjpgcom.js")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "doc" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/doc" TYPE FILE FILES
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/README.ijg"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/README.md"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/src/example.c"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/libjpeg.txt"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/structure.txt"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/usage.txt"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/wizard.txt"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/LICENSE.md"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "doc" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/doc" TYPE FILE FILES
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/src/tjcomp.c"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/src/tjdecomp.c"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/src/tjtran.c"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "man" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/man/man1" TYPE FILE FILES
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/cjpeg.1"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/djpeg.1"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/jpegtran.1"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/rdjpgcom.1"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/doc/wrjpgcom.1"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "lib" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib32/pkgconfig" TYPE FILE FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/pkgscripts/libjpeg.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "lib" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib32/pkgconfig" TYPE FILE FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/pkgscripts/libturbojpeg.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "lib" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib32/cmake/libjpeg-turbo" TYPE FILE FILES
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/pkgscripts/libjpeg-turboConfig.cmake"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/pkgscripts/libjpeg-turboConfigVersion.cmake"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "lib" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib32/cmake/libjpeg-turbo/libjpeg-turboTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib32/cmake/libjpeg-turbo/libjpeg-turboTargets.cmake"
         "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/CMakeFiles/Export/a49e8630ad8d1c31c1c0cd3196981171/libjpeg-turboTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib32/cmake/libjpeg-turbo/libjpeg-turboTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib32/cmake/libjpeg-turbo/libjpeg-turboTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib32/cmake/libjpeg-turbo" TYPE FILE FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/CMakeFiles/Export/a49e8630ad8d1c31c1c0cd3196981171/libjpeg-turboTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib32/cmake/libjpeg-turbo" TYPE FILE FILES "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/CMakeFiles/Export/a49e8630ad8d1c31c1c0cd3196981171/libjpeg-turboTargets-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "include" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/jconfig.h"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/src/jerror.h"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/src/jmorecfg.h"
    "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/src/jpeglib.h"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
if(CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_COMPONENT MATCHES "^[a-zA-Z0-9_.+-]+$")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
  else()
    string(MD5 CMAKE_INST_COMP_HASH "${CMAKE_INSTALL_COMPONENT}")
    set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INST_COMP_HASH}.txt")
    unset(CMAKE_INST_COMP_HASH)
  endif()
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/mnt/c/Users/ACER/Downloads/Pation/third-party/libjpeg-turbo/build_wasm/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
