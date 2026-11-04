# Renders the scenario with the JS engine and the C++ port and compares them.
execute_process(COMMAND ${NODE} ${DIR}/render.js ${OUT}/js.f32 RESULT_VARIABLE r1)
execute_process(COMMAND ${RENDER} ${OUT}/cpp.f32 RESULT_VARIABLE r2)
if(r1 OR r2)
  message(FATAL_ERROR "render failed")
endif()
# bit-exact where the maths library matches V8's; elsewhere allow tiny rounding drift
execute_process(COMMAND ${NODE} ${DIR}/compare.js ${OUT}/js.f32 ${OUT}/cpp.f32 -60 RESULT_VARIABLE r3)
if(r3)
  message(FATAL_ERROR "C++ engine differs from the JS engine")
endif()
