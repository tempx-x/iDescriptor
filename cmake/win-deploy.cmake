# Windows deployment script for Qt applications with MinGW/MSYS2
# This script handles Qt deployment, runtime DLL copying, and GStreamer plugins

if(NOT EXISTS ${EXECUTABLE_PATH})
    message(FATAL_ERROR "Executable not found: ${EXECUTABLE_PATH}")
endif()

message("=== Starting Windows deployment for: ${EXECUTABLE_PATH} ===")

# Step 1: Run windeployqt6 to copy Qt-related DLLs and plugins
message("Running windeployqt6 to deploy Qt dependencies...")
execute_process(
    COMMAND ${MSYS2_BIN_PATH}/windeployqt6.exe --dir ${OUTPUT_DIR} --plugindir ${OUTPUT_DIR}/plugins ${EXECUTABLE_PATH}
    RESULT_VARIABLE WINDEPLOYQT_RESULT
    OUTPUT_VARIABLE WINDEPLOYQT_OUTPUT
    ERROR_VARIABLE WINDEPLOYQT_ERROR
)

if(NOT WINDEPLOYQT_RESULT EQUAL 0)
    message(WARNING "windeployqt6 failed: ${WINDEPLOYQT_ERROR}")
else()
    message("windeployqt6 completed successfully")
endif()

# Step 2: Find and copy runtime dependencies using GET_RUNTIME_DEPENDENCIES
message("Analyzing runtime dependencies for: ${EXECUTABLE_PATH}")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES ${EXECUTABLE_PATH}
    RESOLVED_DEPENDENCIES_VAR DLLS
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" "^AVRT" "^avrt" "^MSVCP" "^VCRUNTIME" "^ucrtbase"
    POST_EXCLUDE_REGEXES ".*system32/.*\\.dll" ".*SysWOW64/.*\\.dll" ".*Windows/.*\\.dll" ".*Microsoft.VC.*"
    DIRECTORIES ${MSYS2_BIN_PATH} $ENV{PATH}
)

foreach(DLL ${DLLS})
    get_filename_component(DLL_NAME ${DLL} NAME)
    message("Copying dependency: ${DLL_NAME}")
    file(COPY ${DLL} DESTINATION ${OUTPUT_DIR})
endforeach()

list(LENGTH DLLS DLL_COUNT)
message("Successfully copied ${DLL_COUNT} runtime DLL dependencies")

# Step 3: Copy GStreamer plugins
message("Copying GStreamer plugins...")
file(GLOB GSTREAMER_PLUGINS "${MSYS2_BIN_PATH}/../lib/gstreamer-1.0/*.dll")

if(GSTREAMER_PLUGINS)
    # Create gstreamer-1.0 directory in output
    file(MAKE_DIRECTORY ${OUTPUT_DIR}/gstreamer-1.0)
    
    foreach(PLUGIN ${GSTREAMER_PLUGINS})
        get_filename_component(PLUGIN_NAME ${PLUGIN} NAME)
        message("Copying GStreamer plugin: ${PLUGIN_NAME}")
        file(COPY ${PLUGIN} DESTINATION ${OUTPUT_DIR}/gstreamer-1.0)
    endforeach()
    
    list(LENGTH GSTREAMER_PLUGINS PLUGIN_COUNT)
    message("Successfully copied ${PLUGIN_COUNT} GStreamer plugins")
else()
    message(WARNING "No GStreamer plugins found in ${MSYS2_BIN_PATH}/../lib/gstreamer-1.0/")
endif()

# Step 4: Copy additional MinGW runtime DLLs that might be missed
set(ADDITIONAL_DLLS
    "libgcc_s_seh-1.dll"
    "libstdc++-6.dll"
    "libwinpthread-1.dll"
    "libgstreamer-1.0-0.dll"
    "libgstbase-1.0-0.dll"
    "libgobject-2.0-0.dll"
    "libglib-2.0-0.dll"
    "libintl-8.dll"
    "libiconv-2.dll"
)

message("Copying additional MinGW runtime DLLs...")
foreach(DLL_NAME ${ADDITIONAL_DLLS})
    set(DLL_PATH "${MSYS2_BIN_PATH}/${DLL_NAME}")
    if(EXISTS ${DLL_PATH})
        message("Copying additional DLL: ${DLL_NAME}")
        file(COPY ${DLL_PATH} DESTINATION ${OUTPUT_DIR})
    endif()
endforeach()

message("=== Windows deployment completed ===")
