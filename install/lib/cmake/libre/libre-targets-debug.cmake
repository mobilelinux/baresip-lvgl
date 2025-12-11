#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "libre::re-shared" for configuration "Debug"
set_property(TARGET libre::re-shared APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(libre::re-shared PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libre.39.3.0.dylib"
  IMPORTED_SONAME_DEBUG "@rpath/libre.39.dylib"
  )

list(APPEND _cmake_import_check_targets libre::re-shared )
list(APPEND _cmake_import_check_files_for_libre::re-shared "${_IMPORT_PREFIX}/lib/libre.39.3.0.dylib" )

# Import target "libre::re" for configuration "Debug"
set_property(TARGET libre::re APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(libre::re PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "C"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/libre.a"
  )

list(APPEND _cmake_import_check_targets libre::re )
list(APPEND _cmake_import_check_files_for_libre::re "${_IMPORT_PREFIX}/lib/libre.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
