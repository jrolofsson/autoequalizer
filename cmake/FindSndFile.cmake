# AutoEqualizer uses libsndfile for deterministic offline decoding/encoding of WAV,
# FLAC, and AIFF/AIF without introducing a custom codec stack.

find_path(
  SNDFILE_INCLUDE_DIR
  NAMES sndfile.h
  HINTS ENV SNDFILE_ROOT
  PATH_SUFFIXES include
  PATHS /opt/homebrew /usr/local /usr)

find_library(
  SNDFILE_LIBRARY
  NAMES sndfile libsndfile
  HINTS ENV SNDFILE_ROOT
  PATH_SUFFIXES lib
  PATHS /opt/homebrew /usr/local /usr)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  SndFile
  REQUIRED_VARS SNDFILE_INCLUDE_DIR SNDFILE_LIBRARY)

if(SndFile_FOUND AND NOT TARGET SndFile::sndfile)
  add_library(SndFile::sndfile UNKNOWN IMPORTED)
  set_target_properties(
    SndFile::sndfile
    PROPERTIES
      IMPORTED_LOCATION "${SNDFILE_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${SNDFILE_INCLUDE_DIR}")
endif()

mark_as_advanced(SNDFILE_INCLUDE_DIR SNDFILE_LIBRARY)

