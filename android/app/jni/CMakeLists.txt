cmake_minimum_required(VERSION 3.6)

project(GAME)

# armeabi-v7a requires cpufeatures library
# include(AndroidNdkModules)
# android_ndk_import_module_cpufeatures()


add_definitions(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)

# SDL sources are in a subfolder named "SDL"
add_subdirectory(SDL)

target_sources(SDL2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/bionic_compat.c)

# Compilation of companion libraries
#add_subdirectory(SDL_image)
#add_subdirectory(SDL_mixer)
#add_subdirectory(SDL_ttf)

# Your game and its CMakeLists.txt are in a subfolder named "src"
add_subdirectory(src)

