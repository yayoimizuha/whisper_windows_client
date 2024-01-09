cmake_minimum_required(VERSION 3.27)
project(whisper_windows_client)

set(CMAKE_CXX_STANDARD 20)


include(FetchContent)


add_executable(whisper_windows_client main.cpp)
FetchContent_Declare(
        whisper.cpp
        GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
)
FetchContent_MakeAvailable(whisper.cpp)

find_package(FFMPEG REQUIRED)

target_include_directories(whisper_windows_client PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(whisper_windows_client PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(whisper_windows_client PRIVATE ${FFMPEG_LIBRARIES})


find_package(nameof REQUIRED)
target_link_libraries(whisper_windows_client PRIVATE nameof::nameof)
