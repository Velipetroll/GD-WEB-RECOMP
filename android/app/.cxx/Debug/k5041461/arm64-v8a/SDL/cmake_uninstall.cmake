if (NOT EXISTS "/home/varzonelp/PROYECTOS/GDSRC/gd-web-decompiled/android/app/.cxx/Debug/k5041461/arm64-v8a/install_manifest.txt")
    message(FATAL_ERROR "Cannot find install manifest: \"/home/varzonelp/PROYECTOS/GDSRC/gd-web-decompiled/android/app/.cxx/Debug/k5041461/arm64-v8a/install_manifest.txt\"")
endif(NOT EXISTS "/home/varzonelp/PROYECTOS/GDSRC/gd-web-decompiled/android/app/.cxx/Debug/k5041461/arm64-v8a/install_manifest.txt")

file(READ "/home/varzonelp/PROYECTOS/GDSRC/gd-web-decompiled/android/app/.cxx/Debug/k5041461/arm64-v8a/install_manifest.txt" files)
string(REGEX REPLACE "\n" ";" files "${files}")
foreach (file ${files})
    message(STATUS "Uninstalling \"$ENV{DESTDIR}${file}\"")
    execute_process(
        COMMAND /home/varzonelp/Android/Sdk/cmake/3.22.1/bin/cmake -E remove "$ENV{DESTDIR}${file}"
        OUTPUT_VARIABLE rm_out
        RESULT_VARIABLE rm_retval
    )
    if(NOT ${rm_retval} EQUAL 0)
        message(FATAL_ERROR "Problem when removing \"$ENV{DESTDIR}${file}\"")
    endif (NOT ${rm_retval} EQUAL 0)
endforeach(file)

