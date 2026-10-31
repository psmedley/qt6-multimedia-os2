# FindKAI
# -------
#
# Try to locate KAI (K Audio Interface), the OS/2 audio library by
# KO Myung-Hun. If found, this will define the following variables:
#
# ``KAI_FOUND``
#     True if KAI is available
# ``KAI_INCLUDE_DIRS``
#     The KAI include directories
# ``KAI_LIBRARIES``
#     The KAI libraries
#
# If ``KAI_FOUND`` is TRUE, it will also define the following imported target:
#
# ``KAI::KAI``
#     The KAI library to link to

find_path(KAI_INCLUDE_DIR kai.h)
find_library(KAI_LIBRARY NAMES kai libkai kai_dll)
find_library(KAI_MMPM2_LIBRARY NAMES mmpm2 libmmpm2)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(KAI REQUIRED_VARS KAI_INCLUDE_DIR KAI_LIBRARY)

if(KAI_FOUND)
    set(KAI_INCLUDE_DIRS ${KAI_INCLUDE_DIR})
    set(KAI_LIBRARIES ${KAI_LIBRARY})
    if(KAI_MMPM2_LIBRARY)
        list(APPEND KAI_LIBRARIES ${KAI_MMPM2_LIBRARY})
    endif()
    if(NOT TARGET KAI::KAI)
        add_library(KAI::KAI INTERFACE IMPORTED)
        set_target_properties(KAI::KAI PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${KAI_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${KAI_LIBRARIES}")
    endif()
endif()

mark_as_advanced(KAI_INCLUDE_DIR KAI_LIBRARY KAI_MMPM2_LIBRARY)
