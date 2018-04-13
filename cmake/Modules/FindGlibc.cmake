# Try to find glibc-devel
# Once done, this will define
#
# GLIBC_FOUND      - system has glibc-devel
# GLIBC_LIBRARIES  - libraries needed to use glibc
# GLIBC_DL_LIBRARY - libraries needed to use dl
# GLIBC_RT_LIBRARY - libraries needed to use rt

include(FindPackageHandleStandardArgs)

if(GLIBC_LIBRARIES)
  set(GLIBC_FIND_QUIETLY TRUE)
else()
  find_library(
    GLIBC_DL_LIBRARY
    NAMES dl
    HINTS ${GLIBC_ROOT_DIR}
    PATH_SUFFIXES ${LIBRARY_PATH_PREFIX})

  find_library(
    GLIBC_RT_LIBRARY
    NAMES rt
    HINTS ${GLIBC_ROOT_DIR}
    PATH_SUFFIXES ${LIBRARY_PATH_PREFIX})

  # Math library
  find_library(
    GLIBC_M_LIBRARY
    NAMES m
    HINTS ${GLIBC_ROOT_DIR}
    PATH_SUFFIXES ${LIBRARY_PATH_PREFIX})

  find_package_handle_standard_args(Glibc-dl DEFAULT_MSG
    GLIBC_DL_LIBRARY)
  find_package_handle_standard_args(Glibc-rt DEFAULT_MSG
    GLIBC_RT_LIBRARY)
  find_package_handle_standard_args(Glibc-m DEFAULT_MSG
    GLIBC_M_LIBRARY)

  find_package_handle_standard_args(Glibc DEFAULT_MSG
    GLIBC_DL_LIBRARY
    GLIBC_RT_LIBRARY
    GLIBC_M_LIBRARY)

  if (Glibc_FOUND)
    set(GLIBC_LIBRARIES
      ${GLIBC_M_LIBRARY}
      ${GLIBC_DL_LIBRARY}
      ${GLIBC_RT_LIBRARY})

    mark_as_advanced(GLIBC_DL_LIBRARY GLIBC_RT_LIBRARY)

    if(NOT TARGET Glibc::m)
      add_library(Glibc::m UNKNOWN IMPORTED)
      set_target_properties(Glibc::m PROPERTIES
        IMPORTED_LOCATION ${GLIBC_M_LIBRARY})
    endif()

    if(NOT TARGET Glibc::rt)
      add_library(Glibc::rt UNKNOWN IMPORTED)
      set_target_properties(Glibc::rt PROPERTIES
        IMPORTED_LOCATION ${GLIBC_RT_LIBRARY})
    endif()

    if(NOT TARGET Glibc::dl)
      add_library(Glibc::dl UNKNOWN IMPORTED)
      set_target_properties(Glibc::dl PROPERTIES
        IMPORTED_LOCATION ${GLIBC_DL_LIBRARY})
    endif()

    if(NOT TARGET Glibc::Glibc)
      add_library(Glibc::Glibc UNKNOWN IMPORTED)
      set_target_properties(Glibc::Glibc PROPERTIES
        #INTERFACE_LINK_LIBRARIES Glibc::m Glibc::rt Glibc::dl
        IMPORTED_LOCATION ${GLIBC_M_LIBRARY}
        IMPORTED_LOCATION ${GLIBC_DL_LIBRARY}
        IMPORTED_LOCATION ${GLIBC_RT_LIBRARY})
    endif()
  endif()
endif()
