# AWF Load Balancing xApp
# Implements Gures et al. ICT Express 2023: "Load balancing in 5G HetNets
# based on automatic weight function (AWF)"
# Equations: Eq.(1-14) + Algorithm 1
# Metrics CSV: ~/alyaadone.csv (HO log)  ~/sinr_xapp.csv (SINR snapshots)

add_executable(xapp_lb_awf
  ${CMAKE_CURRENT_SOURCE_DIR}/xapp_lb_awf.c
  ../../../../src/util/alg_ds/alg/defer.c
  ../../../../src/util/time_now_us.c
)

target_include_directories(xapp_lb_awf PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(xapp_lb_awf
  PUBLIC
  e42_xapp
  -pthread
  -lsctp
  -ldl
  -lm
)

message(STATUS "xapp_lb_awf: AWF Load Balancing xApp configured")
message(STATUS "  Paper  : Gures et al. ICT Express 2023 — AWF Load Balancing")
message(STATUS "  Source : ${CMAKE_CURRENT_SOURCE_DIR}/xapp_lb_awf.c")
