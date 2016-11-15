cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
set (CMAKE_CXX_STANDARD 11)

add_library (autil audio_driver.cpp signal_buffer.cpp signal_processor.cpp test.cpp file_io.cpp)


find_path (JACK_INC jack/jack.h)
find_library(JACK_LIB NAMES jack  )

find_library(SNDFILE_LIB NAMES sndfile )

target_link_libraries (autil ${JACK_LIB} ${SNDFILE_LIB})
target_include_directories (autil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${JACK_INC} ../rtt)
