FIND_PATH(OpusFile_INCLUDE_DIR opusfile.h
  HINTS
  $ENV{OPUSDIR}
  PATH_SUFFIXES include/opus include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include/SDL2
  /usr/include/SDL2
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
)

FIND_LIBRARY(OpusFile_LIBRARY
  NAMES opusfile
  HINTS
  $ENV{OPUSDIR}
  PATH_SUFFIXES lib64 lib
  PATHS
  /sw
  /opt/local
  /opt/csw
  /opt
)

set(OpusFile_FOUND "NO")
if(OpusFile_INCLUDE_DIR AND OpusFile_LIBRARY AND OpusFileFile_LIBRARY)
  set(OpusFile_FOUND "YES")
endif()

INCLUDE(FindPackageHandleStandardArgs)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(OpusFile
                                  REQUIRED_VARS OpusFile_LIBRARY OpusFile_INCLUDE_DIR)

