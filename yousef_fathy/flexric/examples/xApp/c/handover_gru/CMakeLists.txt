# GRU-Enhanced Handover xApp
# Integrates SINR-based A3 events with Fares GRU model for ping-pong avoidance
# Requires: libcurl-dev, libjson-c-dev

# Source files are symlinked into this directory so that relative #include
# paths (../../../../src/...) resolve correctly against the flexric tree.
# Symlinks: best2.c → HANDOVER_xApp_Test/best2.c
#           lstm_predictor_client.c/h → HANDOVER_xApp_Test/

# Check dependencies
find_package(CURL REQUIRED)
find_library(JSON_C_LIB json-c REQUIRED)

add_executable(xapp_handover_gru
  ${CMAKE_CURRENT_SOURCE_DIR}/best2.c
  ${CMAKE_CURRENT_SOURCE_DIR}/lstm_predictor_client.c
  ../../../../src/util/alg_ds/alg/defer.c
)

target_include_directories(xapp_handover_gru PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(xapp_handover_gru
  PUBLIC
  e42_xapp
  ${CURL_LIBRARIES}
  ${JSON_C_LIB}
  -pthread
  -lsctp
  -ldl
)

message(STATUS "xapp_handover_gru: GRU-enhanced handover xApp configured")
message(STATUS "  Sources : ${HO_SRC}/best2.c + lstm_predictor_client.c")
message(STATUS "  Requires: libcurl (${CURL_LIBRARIES}), libjson-c (${JSON_C_LIB})")
