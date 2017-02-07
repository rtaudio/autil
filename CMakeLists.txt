cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
set (CMAKE_CXX_STANDARD 11)

find_path (ALSA_INC alsa/asoundlib.h)
find_path (JACK_INC jack/jack.h)


find_library(ALSA_LIB NAMES asound  )
find_library(JACK_LIB NAMES jack  )

option(WITH_ALSA "with ALSA driver" OFF)
option(WITH_JACK "with JACK driver" OFF)


SET (DRIVER_SRCS audio_driver_base.cpp )
SET (DRIVER_LIBS )
SET (DRIVER_INCS )

if( WITH_ALSA AND EXISTS ${ALSA_LIB} )
    list(APPEND DRIVER_SRCS audio_driver_alsa.cpp)
    list(APPEND DRIVER_LIBS ${ALSA_LIB})
    list(APPEND DRIVER_INCS ${ALSA_INC})
endif()

if( WITH_JACK AND EXISTS ${JACK_LIB} )
    list(APPEND DRIVER_SOURCES, audio_driver_jack.cpp)
    list(APPEND DRIVER_LIBS, ${JACK_LIB})
    list(APPEND DRIVER_INCS, ${JACK_INC})
endif()

add_library (autil  ${DRIVER_SRCS} signal_buffer.cpp signal_processor.cpp test.cpp file_io.cpp net.cpp)

#
find_library(SNDFILE_LIB NAMES sndfile sndfile-1 libsndfile libsndfile-1 PATHS $ENV{PROGRAMFILES}/Mega-Nerd/libsndfile/lib )

target_link_libraries (autil ${DRIVER_LIBS} ${SNDFILE_LIB})
target_include_directories (autil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${DRIVER_INCS} ../)

#debug
add_definitions("-g -ggdb")
