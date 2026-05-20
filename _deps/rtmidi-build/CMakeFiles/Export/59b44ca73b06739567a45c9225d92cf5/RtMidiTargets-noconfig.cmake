#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "RtMidi::rtmidi" for configuration ""
set_property(TARGET RtMidi::rtmidi APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(RtMidi::rtmidi PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/librtmidi.so.7.0.0"
  IMPORTED_SONAME_NOCONFIG "librtmidi.so.7"
  )

list(APPEND _cmake_import_check_targets RtMidi::rtmidi )
list(APPEND _cmake_import_check_files_for_RtMidi::rtmidi "${_IMPORT_PREFIX}/lib/librtmidi.so.7.0.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
