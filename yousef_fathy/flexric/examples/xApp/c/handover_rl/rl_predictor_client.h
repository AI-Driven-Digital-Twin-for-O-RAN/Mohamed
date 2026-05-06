#ifndef RL_PREDICTOR_CLIENT_H
#define RL_PREDICTOR_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

// RL Prediction Service Client for C xApp
// Provides interface to call Python DDQN RL model service

/* Default when RL_SERVICE_URL env is not set (e.g. Docker container on host: -p 5001:5001) */
#ifndef RL_SERVICE_URL_DEFAULT
#define RL_SERVICE_URL_DEFAULT "http://localhost:5001"
#endif
#define RL_SERVICE_TIMEOUT_SEC 2

// Structure for handover prediction result
typedef struct {
    bool should_handover;
    uint16_t target_cell;
    double confidence;
    double time_to_handover;
    bool prediction_success;
} rl_prediction_result_t;

// Structure for UE measurements (input to RL)
typedef struct {
    uint16_t ue_id;
    uint16_t serving_cell_id;
    double serving_sinr;
    double serving_rsrp;
    double serving_qual;
    uint16_t neighbor_cell_id;
    double neighbor_sinr;
    double neighbor_rsrp;
    double cqi;
    double speed;
    double dl_bitrate;
    double ul_bitrate;
    uint16_t bandwidth;
} rl_ue_measurements_t;

/**
 * Initialize RL prediction service client
 * @return 0 on success, -1 on failure
 */
int rl_predictor_init(void);

/**
 * Cleanup RL prediction service client
 */
void rl_predictor_cleanup(void);

/**
 * Predict handover decision using RL model
 *
 * @param measurements: UE measurement data
 * @param result: Output prediction result
 * @return 0 on success, -1 on failure
 */
int rl_predict_handover(const rl_ue_measurements_t* measurements,
                        rl_prediction_result_t* result);

/**
 * Check if RL service is available
 * @return true if service is reachable, false otherwise
 */
bool rl_service_available(void);

#endif // RL_PREDICTOR_CLIENT_H
