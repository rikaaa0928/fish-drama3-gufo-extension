# Fish Audio Drama-3 external model extension for Gufo.

gufo_extension_target(
  NAME gufo_fish_drama3
  SOURCES
    config.cpp
    tensor.cpp
    loader.cpp
    tokenizer.cpp
    prompt.cpp
    host_memory.cpp
    extension.cpp
  LINK_LIBRARIES
    gufo_ext_runtime
    ICU::uc
    ICU::i18n
    Threads::Threads
)

if(TARGET gufo_fish_drama3)
  target_include_directories(gufo_fish_drama3 PUBLIC
    "$<BUILD_INTERFACE:${GUFO_EXTENSION_BUILD_ROOT}>"
  )
endif()

gufo_extension_target(
  NAME gufo_fish_drama3_hip
  SOURCES
    hip/codec_runtime.cpp
    hip/backbone_runtime.cpp
    hip/tower_runtime.cpp
    hip/synthesis_runtime.cpp
    provider.cpp
  LINK_LIBRARIES
    gufo_fish_drama3
    gufo_ext_runtime
    Threads::Threads
    hip::host
    roc::rocblas
)

if(TARGET gufo_fish_drama3_hip)
  gufo_extension_hip_sources(gufo_fish_drama3_hip
    SOURCES
      hip/codec_ops.hip
      hip/backbone_ops.hip
      hip/tower_ops.hip
  )
  target_compile_definitions(gufo_fish_drama3_hip PUBLIC ENGINE_ENABLE_HIP=1)
  target_compile_options(gufo_fish_drama3_hip PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-O3>
    $<$<COMPILE_LANGUAGE:HIP>:-O3>
    $<$<COMPILE_LANGUAGE:HIP>:-Wno-unused-command-line-argument>
  )
  target_include_directories(gufo_fish_drama3_hip PUBLIC
    "$<BUILD_INTERFACE:${GUFO_EXTENSION_BUILD_ROOT}>"
    "$<BUILD_INTERFACE:${GUFO_EXTENSION_BUILD_ROOT}/hip>"
  )
endif()

gufo_extension_target(
  NAME fish_drama3_config_test
  SOURCES
    tests/config_test.cpp
  LINK_LIBRARIES
    gufo_fish_drama3
    gufo_ext_runtime
)

if(TARGET fish_drama3_config_test)
  add_test(NAME fish_drama3.config COMMAND fish_drama3_config_test)
  set_tests_properties(fish_drama3.config PROPERTIES LABELS "fish-drama3;cpu")
endif()

gufo_extension_target(
  NAME fish_drama3_test
  SOURCES
    tests/fish_drama3_test.cpp
  LINK_LIBRARIES
    gufo_fish_drama3_hip
    gufo_fish_drama3
    gufo_ext_runtime
    hip::host
)

if(TARGET fish_drama3_test)
  gufo_extension_hip_sources(fish_drama3_test
    SOURCES
      tests/fish_drama3_test.cpp
  )
  add_test(NAME fish_drama3.gpu COMMAND fish_drama3_test)
  set_tests_properties(fish_drama3.gpu PROPERTIES LABELS "fish-drama3;hip;gpu;gfx1151")
endif()
