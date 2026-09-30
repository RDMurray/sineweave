# Integration-only portability changes: static core, no GNU flags/Unix m on MSVC,
# no Python/utilities/procedural wrappers/experimental fast synthesis.
set(_loris_names AiffData AiffFile Analyzer AssociateBandwidth BigEndian Breakpoint
  BreakpointUtils Channelizer Collator Dilator Distiller Envelope F0Estimate
  LorisExceptions Filter FourierTransform FrequencyReference Fundamental Harmonifier
  ImportLemur KaiserWindow LinearEnvelope Marker Morpher NoiseGenerator Notifier
  Oscillator Partial PartialBuilder PartialList PartialUtils phasefix ReassignedSpectrum
  Resampler SdifFile Sieve SpcFile SpectralPeakSelector SpectralSurface Synthesizer)
set(_loris_sources "${loris_SOURCE_DIR}/src/fftsg.c")
foreach(_name IN LISTS _loris_names)
  list(APPEND _loris_sources "${loris_SOURCE_DIR}/src/${_name}.cpp")
endforeach()
add_library(sineweave_loris STATIC ${_loris_sources})
target_include_directories(sineweave_loris PUBLIC "${loris_SOURCE_DIR}/src")
set_target_properties(sineweave_loris PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_definitions(sineweave_loris PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)
else()
  target_link_libraries(sineweave_loris PRIVATE m)
endif()

if(SINEWEAVE_BUILD_LORIS_TOOLS)
  # Build the upstream drivers against the portable static library. Do not use
  # upstream's Unix-only -Wno-comment/libm options or change the plugin library.
  set(_loris_util_targets)
  foreach(_util IN ITEMS analyze dilate mark spewmarkers synthesize unmark)
    add_executable(loris-${_util} "${loris_SOURCE_DIR}/utils/loris_${_util}.cpp")
    target_link_libraries(loris-${_util} PRIVATE sineweave_loris)
    set_target_properties(loris-${_util} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(MSVC)
      target_compile_definitions(loris-${_util} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)
    endif()
    list(APPEND _loris_util_targets loris-${_util})
  endforeach()

  # Upstream builds this experimental driver too. Its extra engine is separate
  # from sineweave_loris, so enabling CLI tools does not add it to the VST3.
  set(_loris_fast_dir "${loris_SOURCE_DIR}/src/fast-synth-src")
  add_library(sineweave_loris_fast STATIC
    "${_loris_fast_dir}/BlockOscillator.cpp"
    "${_loris_fast_dir}/BlockSynthBwe.cpp"
    "${_loris_fast_dir}/BlockSynthReader.cpp"
    "${_loris_fast_dir}/r250.c"
    "${_loris_fast_dir}/randlcg.c")
  target_include_directories(sineweave_loris_fast PUBLIC "${_loris_fast_dir}")
  target_compile_definitions(sineweave_loris_fast PUBLIC FASTSYNTH_FLOAT_TYPE=double)
  target_link_libraries(sineweave_loris_fast PUBLIC sineweave_loris)
  if(MSVC)
    target_compile_definitions(sineweave_loris_fast PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)
  endif()
  add_executable(loris-fastsynth "${loris_SOURCE_DIR}/utils/loris_fastsynth_main.cpp")
  target_link_libraries(loris-fastsynth PRIVATE sineweave_loris_fast)
  if(MSVC)
    target_compile_definitions(loris-fastsynth PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX _USE_MATH_DEFINES)
  endif()
  set_target_properties(loris-fastsynth PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
  add_custom_target(loris_cli DEPENDS ${_loris_util_targets} loris-fastsynth)
endif()
