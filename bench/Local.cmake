# Maul Audio's further benchmarks: the binaural renderer and the
# ambisonic bed on the shipped HRTF set.
if(NOT MAUL_AUDIO_SPATIAL)
    return()
endif()
add_executable(${PROJECT_NAME}_bench_binaural bench_binaural.c)
target_link_libraries(${PROJECT_NAME}_bench_binaural PRIVATE ${PROJECT_NAME})
target_compile_definitions(${PROJECT_NAME}_bench_binaural PRIVATE
    MAUD_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
maul_apply_flags(${PROJECT_NAME}_bench_binaural)
