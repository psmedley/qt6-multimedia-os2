# FindFFmpeg
# ----------
#
# Try to locate the FFmpeg libraries used by the OS/2 media player backend:
# libavformat, libavcodec, libavutil, libswresample and libswscale.
# pkg-config is used when available, with a plain header/library search as
# fallback. If found, this will define the following variables:
#
# ``FFmpeg_FOUND``
#     True if all required FFmpeg libraries are available
# ``FFmpeg_INCLUDE_DIRS``
#     The FFmpeg include directories
# ``FFmpeg_LIBRARIES``
#     The FFmpeg libraries
#
# If ``FFmpeg_FOUND`` is TRUE, it will also define the following imported target:
#
# ``FFmpeg::FFmpeg``
#     The FFmpeg libraries to link to

set(_ffmpeg_components avformat avcodec swresample swscale avutil)
set(_ffmpeg_headers
    avformat libavformat/avformat.h
    avcodec libavcodec/avcodec.h
    swresample libswresample/swresample.h
    swscale libswscale/swscale.h
    avutil libavutil/avutil.h)

find_package(PkgConfig QUIET)

set(FFmpeg_INCLUDE_DIRS "")
set(FFmpeg_LIBRARIES "")
set(_ffmpeg_required_vars "")

foreach(_comp IN LISTS _ffmpeg_components)
    list(FIND _ffmpeg_headers ${_comp} _idx)
    math(EXPR _idx "${_idx} + 1")
    list(GET _ffmpeg_headers ${_idx} _header)

    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_FFmpeg_${_comp} QUIET lib${_comp})
    endif()

    find_path(FFmpeg_${_comp}_INCLUDE_DIR ${_header}
              HINTS ${PC_FFmpeg_${_comp}_INCLUDE_DIRS})
    find_library(FFmpeg_${_comp}_LIBRARY NAMES ${_comp} lib${_comp} ${_comp}_dll
                 HINTS ${PC_FFmpeg_${_comp}_LIBRARY_DIRS})

    list(APPEND _ffmpeg_required_vars FFmpeg_${_comp}_INCLUDE_DIR FFmpeg_${_comp}_LIBRARY)
    if(FFmpeg_${_comp}_INCLUDE_DIR)
        list(APPEND FFmpeg_INCLUDE_DIRS ${FFmpeg_${_comp}_INCLUDE_DIR})
    endif()
    if(FFmpeg_${_comp}_LIBRARY)
        list(APPEND FFmpeg_LIBRARIES ${FFmpeg_${_comp}_LIBRARY})
    endif()
    mark_as_advanced(FFmpeg_${_comp}_INCLUDE_DIR FFmpeg_${_comp}_LIBRARY)
endforeach()

list(REMOVE_DUPLICATES FFmpeg_INCLUDE_DIRS)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg REQUIRED_VARS ${_ffmpeg_required_vars})

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${FFmpeg_LIBRARIES}")
endif()

unset(_ffmpeg_components)
unset(_ffmpeg_headers)
unset(_ffmpeg_required_vars)
