# Try to find qpid-proton
# Once done, this will define
#
# Proton_FOUND         - system has proton
# Proton_LIBRARIES     - libraries needed to use proton
# Proton_INCLUDE_DIRS  - include files needed to use proton

if (Proton_LIBRARIES)
  set(Proton_FIND_QUIETLY True)
else()

  include(FindPackageHandleStandardArgs)

  find_path(Proton_INCLUDE_DIR "proton/cproton.i"
    PATHS ${CONAN_INCLUDE_DIRS} "/usr/include")

  find_library(Proton_LIBRARY "qpid-proton"
    PATHS ${CONAN_LIB_DIRS} "/usr/lib"
    PATH_SUFFIXES "lib" "lib64")

  find_library(Proton_Core_LIBRARY "qpid-proton-core"
    PATHS ${CONAN_LIB_DIRS} "/usr/lib"
    PATH_SUFFIXES "lib" "lib64")

  find_library(Proton_Proactor_LIBRARY "qpid-proton-proactor"
    PATHS ${CONAN_LIB_DIRS} "/usr/lib"
    PATH_SUFFIXES "lib" "lib64")

  find_library(Proton_Cpp_LIBRARY "qpid-proton-cpp"
    PATHS ${CONAN_LIB_DIRS} "/usr/lib"
    PATH_SUFFIXES "lib" "lib64")

  find_path(Proton_PYTHON_BINDING "cproton.py"
    PATHS ${CONAN_LIB_DIRS} "/usr/lib"
    PATH_SUFFIXES "lib/proton/bindings/python" "lib/proton/bindings/python")

  find_package_handle_standard_args(Proton DEFAULT_MSG
    Proton_INCLUDE_DIR
    Proton_LIBRARY)

  find_package_handle_standard_args(Proton-Core DEFAULT_MSG
    Proton_Core_LIBRARY)

  find_package_handle_standard_args(Proton-Proactor DEFAULT_MSG
    Proton_Proactor_LIBRARY)

  find_package_handle_standard_args(Proton-Cpp DEFAULT_MSG
    Proton_Cpp_LIBRARY)

  find_package_handle_standard_args(Proton-Python DEFAULT_MSG
    Proton_PYTHON_BINDING)

  if (Proton_FOUND)
    set(Proton_INCLUDE_DIRS ${Proton_INCLUDE_DIR})
    set(Proton_LIBRARIES ${Proton_LIBRARY})

    mark_as_advanced(
      Proton_INCLUDE_DIR
      Proton_LIBRARY)

    if (NOT TARGET Proton::Proton)
      add_library(Proton::Proton UNKNOWN IMPORTED)
      set_target_properties(Proton::Proton PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${Proton_INCLUDE_DIRS}")
      set_target_properties(Proton::Proton PROPERTIES
        IMPORTED_LOCATION ${Proton_LIBRARIES})
    endif()

    set(Proton_Core_INCLUDE_DIRS ${Proton_INCLUDE_DIR})
    set(Proton_Core_LIBRARIES ${Proton_Core_LIBRARY})

    if (NOT TARGET Proton::Core)
      add_library(Proton::Core UNKNOWN IMPORTED)
      set_target_properties(Proton::Core PROPERTIES
        IMPORTED_LOCATION ${Proton_Core_LIBRARIES}
        INTERFACE_LINK_LIBRARIES Proton::Proton)
    endif()

    set(Proton_Proactor_LIBRARIES ${Proton_Proactor_LIBRARY})

    if (NOT TARGET Proton::Proactor)
      add_library(Proton::Proactor UNKNOWN IMPORTED)
      set_target_properties(Proton::Proactor PROPERTIES
        IMPORTED_LOCATION ${Proton_Proactor_LIBRARIES}
        INTERFACE_LINK_LIBRARIES Proton::Core)
    endif()

    set(Proton_Cpp_LIBRARIES ${Proton_Cpp_LIBRARY})

    if (NOT TARGET Proton::Cpp)
      add_library(Proton::Cpp UNKNOWN IMPORTED)
      set_target_properties(Proton::Cpp PROPERTIES
        IMPORTED_LOCATION ${Proton_Cpp_LIBRARIES}
        INTERFACE_LINK_LIBRARIES Proton::Proactor)
    endif()
  endif()
endif()
