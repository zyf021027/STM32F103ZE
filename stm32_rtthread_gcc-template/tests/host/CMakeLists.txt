cmake_minimum_required(VERSION 3.20)

project(stm32_audio_host_tests C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(host_audio_player_tests
    test_audio_player.c
    stubs/test_board_audio_stub.c
    stubs/test_sdcard_stub.c
    stubs/test_pff_stub.c
    stubs/test_minimp3_stub.c
    ../../src/audio_player.c
)

target_include_directories(host_audio_player_tests PRIVATE
    .
    ../../src
    ../../src/pff
    ../../third_party/minimp3
)

add_test(NAME host-audio-player-tests COMMAND host_audio_player_tests)

add_executable(host_diskio_tests
    test_diskio.c
    stubs/test_board_audio_stub.c
    stubs/test_sdcard_stub.c
    stubs/test_pff_stub.c
    stubs/test_minimp3_stub.c
    ../../src/pff/diskio.c
)

target_include_directories(host_diskio_tests PRIVATE
    .
    ../../src
    ../../src/pff
    ../../third_party/minimp3
)

add_test(NAME host-diskio-tests COMMAND host_diskio_tests)

add_executable(host_record_tests
    test_board_audio_logic.c
    ../../src/audio_record.c
)

target_include_directories(host_record_tests PRIVATE
    .
    ../../src
)

add_test(NAME host-record-tests COMMAND host_record_tests)