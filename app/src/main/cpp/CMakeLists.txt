cmake_minimum_required(VERSION 3.10.2)
project("dinoapp")

# Add JNI headers path for both host and Android builds
include_directories($ENV{JAVA_HOME}/include $ENV{JAVA_HOME}/include/win32)

# Create the shared library (libnative-lib.so)
add_library( native-lib
        SHARED
        native-lib.cpp )

# Locate the log library included in the NDK.
find_library( log-lib
        log )

# Link the log library to native-lib.
target_link_libraries( native-lib
        ${log-lib} )
