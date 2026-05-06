# RL (DDQN) Handover xApp
find_package(CURL REQUIRED)
find_library(JSON_C_LIB json-c REQUIRED)

add_executable(xapp_handover_rl
  ${CMAKE_CURRENT_SOURCE_DIR}/best2_rl.c
  ${CMAKE_CURRENT_SOURCE_DIR}/rl_predictor_client.c
  ../../../../src/util/alg_ds/alg/defer.c
)

target_include_directories(xapp_handover_rl PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(xapp_handover_rl
  PUBLIC
  e42_xapp
  ${CURL_LIBRARIES}
  ${JSON_C_LIB}
  -pthread
  -lsctp
  -ldl
)

message(STATUS "xapp_handover_rl: DDQN RL handover xApp configured")
