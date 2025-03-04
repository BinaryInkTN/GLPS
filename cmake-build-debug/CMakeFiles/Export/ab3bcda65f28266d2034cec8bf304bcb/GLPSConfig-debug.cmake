#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "GLPS::GLPS" for configuration "Debug"
set_property(TARGET GLPS::GLPS APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(GLPS::GLPS PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libGLPS.so"
  IMPORTED_SONAME_DEBUG "libGLPS.so"
  )

list(APPEND _cmake_import_check_targets GLPS::GLPS )
list(APPEND _cmake_import_check_files_for_GLPS::GLPS "${_IMPORT_PREFIX}/lib/libGLPS.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
