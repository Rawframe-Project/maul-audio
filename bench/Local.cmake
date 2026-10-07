# Maul Audio's further benchmarks: voice processing (the Device part);
# the binaural renderer and the ambisonic bed on the shipped HRTF set,
# and acoustic scenes (the Spatial part).
if(MAUL_AUDIO_DEVICE)
    add_executable(${PROJECT_NAME}_bench_voice bench_voice.c)
    target_link_libraries(${PROJECT_NAME}_bench_voice PRIVATE ${PROJECT_NAME})
    target_compile_definitions(${PROJECT_NAME}_bench_voice PRIVATE
        MAUD_BENCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    maul_apply_flags(${PROJECT_NAME}_bench_voice)
endif()

# A feature's download on the web, each linked alone (size_web.c).
if(EMSCRIPTEN)
    set(sizes 1 stream 2 binaural 3 reverb 4 voice)
    while(sizes)
        list(POP_FRONT sizes number feature)
        if((feature STREQUAL "stream" OR feature STREQUAL "voice") AND NOT MAUL_AUDIO_DEVICE)
            continue()
        endif()
        if((feature STREQUAL "binaural" OR feature STREQUAL "reverb") AND NOT MAUL_AUDIO_SPATIAL)
            continue()
        endif()
        add_executable(${PROJECT_NAME}_size_${feature} size_web.c)
        target_link_libraries(${PROJECT_NAME}_size_${feature} PRIVATE ${PROJECT_NAME})
        target_compile_definitions(${PROJECT_NAME}_size_${feature} PRIVATE
            MAUD_SIZE_FEATURE=${number})
        maul_apply_flags(${PROJECT_NAME}_size_${feature})
    endwhile()
endif()

if(NOT MAUL_AUDIO_SPATIAL)
    return()
endif()
add_executable(${PROJECT_NAME}_bench_binaural bench_binaural.c)
target_link_libraries(${PROJECT_NAME}_bench_binaural PRIVATE ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}_bench_binaural PRIVATE
    MAUD_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    MAUD_BENCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
maul_apply_flags(${PROJECT_NAME}_bench_binaural)

add_executable(${PROJECT_NAME}_bench_scene bench_scene.c)
target_link_libraries(${PROJECT_NAME}_bench_scene PRIVATE ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}_bench_scene PRIVATE
    MAUD_BENCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
maul_apply_flags(${PROJECT_NAME}_bench_scene)
