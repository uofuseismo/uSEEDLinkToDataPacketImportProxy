#.rst:
# FindSEEDLink
# ------------
# 
# Finds the SEEDLink library and include.
#
# Imported targets
# ^^^^^^^^^^^^^^^^
#
# ``SEEDLink_FOUND``
#   True indicates SEEDLink was found.
# ``SEEDLink_VERSION_STRING``
#   The version found.
# ``SEEDLink::SEEDLink``
#   The SEEDLink library, if found. 

# Already in cache, be silent
if (SEEDLink_INCLUDE_DIR AND SEEDLink_LIBRARY)
    set(SEEDLink_FIND_QUIETLY TRUE)
endif()

# Find the include directory
find_path(SEEDLink_INCLUDE_DIR
          NAMES libslink.h
          PATHS /usr/local/include
                /usr/include
                "$ENV{SEEDLink_ROOT}/include"
                "$ENV{SEEDLink_ROOT}")
# Find the library components
if (BUILD_SHARED_LIBS)
   message("Looking for libslink shared library")
   find_library(SEEDLink_LIBRARY
                NAMES libslink.so
                PATHS /usr/local/lib
                      /usr/local/lib64
                      "$ENV{SEEDLink_ROOT}/lib/"
                      "$ENV{SEEDLink_ROOT}/"
                )
else()
   message("Looking for libslink static library")
   find_library(SEEDLink_LIBRARY
                NAME libslink.a
                PATHS /usr/local/lib
                      /usr/local/lib64
                      "$ENV{SEEDLink_ROOT}/lib/"
                      "$ENV{SEEDLink_ROOT}/"
               )
endif()

file(STRINGS ${SEEDLink_INCLUDE_DIR}/libslink.h LIBSLINK_HEADER_DATA)
set(SEEDLink_VERSION "")
set(SEEDLink_VERSION_MAJOR "")
set(SEEDLink_VERSION_MINOR "")
set(SEEDLink_VERSION_PATCH "")
while (LIBSLINK_HEADER_DATA)
  list(POP_FRONT LIBSLINK_HEADER_DATA LINE)
  if (LINE MATCHES "#define LIBSLINK_VERSION_MAJOR")
     #message("Found libslink major version line: " ${LINE})
     string(REPLACE "#define LIBSLINK_VERSION_MAJOR " "" LINE ${LINE})
     string(REPLACE "\"" "" LINE ${LINE})
     string(REPLACE "/**< libslink major version */" "" LINE ${LINE})
     string(STRIP ${LINE} LINE)
     set(LINE_LIST ${LINE})
     list(GET LINE_LIST 0 SEEDLink_VERSION_MAJOR)
     #message("libslink patch version ${SEEDLink_VERSION_MAJOR}")
  endif()
  if (LINE MATCHES "#define LIBSLINK_VERSION_MINOR")
     #message("Found libslink minor version line: " ${LINE})
     string(REPLACE "#define LIBSLINK_VERSION_MINOR " "" LINE ${LINE})
     string(REPLACE "\"" "" LINE ${LINE})
     string(REPLACE "/**< libslink minor version */" "" LINE ${LINE})
     string(STRIP ${LINE} LINE)
     set(LINE_LIST ${LINE})
     list(GET LINE_LIST 0 SEEDLink_VERSION_MINOR)
     #message("libslink minor version ${SEEDLink_VERSION_MINOR}")
  endif()
  if (LINE MATCHES "#define LIBSLINK_VERSION_PATCH")
     #message("Found libslink patch version line: " ${LINE})
     string(REPLACE "#define LIBSLINK_VERSION_PATCH " "" LINE ${LINE})
     string(REPLACE "/**< libslink patch version */" "" LINE ${LINE})
     string(STRIP ${LINE} LINE)
     set(LINE_LIST ${LINE})
     list(GET LINE_LIST 0 SEEDLink_VERSION_PATCH)
  endif()
  if (NOT ${SEEDLink_VERSION_MAJOR} STREQUAL "" AND
      NOT ${SEEDLink_VERSION_MINOR} STREQUAL "" AND
      NOT ${SEEDLink_VERSION_PATCH} STREQUAL "")
     set(SEEDLink_VERSION "${SEEDLink_VERSION_MAJOR}.${SEEDLink_VERSION_MINOR}.${SEEDLink_VERSION_PATCH}")
     message("Extracted SEEDLink version: ${SEEDLink_VERSION}")
     break()
  endif()
endwhile()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SEEDLink
                                  FOUND_VAR SEEDLink_FOUND
                                  REQUIRED_VARS SEEDLink_LIBRARY SEEDLink_INCLUDE_DIR
                                  VERSION_VAR ${SEEDLink_VERSION})
if (SEEDLink_FOUND AND NOT TARGET SEEDLink::SEEDLink)
   add_library(SEEDLink::SEEDLink UNKNOWN IMPORTED)
   set_target_properties(SEEDLink::SEEDLink PROPERTIES
                         IMPORTED_LOCATION "${SEEDLink_LIBRARY}"
                         INTERFACE_INCLUDE_DIRECTORIES "${SEEDLink_INCLUDE_DIR}")
endif()
mark_as_advanced(SEEDLink_INCLUDE_DIR SEEDLink_LIBRARY)
