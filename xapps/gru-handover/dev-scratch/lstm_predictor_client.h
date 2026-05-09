#ifndef LSTM_PREDICTOR_CLIENT_H
#define LSTM_PREDICTOR_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

// LSTM Prediction Service Client for C xApp
// Provides interface to call Python LSTM model service

/* Default when LSTM_SERVICE_URL env is not set (e.g. Docker container on host: -p 5000:5000) */
#ifndef LSTM_SERVICE_URL_DEFAULT
#define LSTM_SERVICE_URL_DEFAULT "http://localhost:5000"
#endif
#define LSTM_SERVICE_TIMEOUT_SEC 2

// Structure for handover prediction result
typedef struct {
    bool should_handover;
    uint16_t target_cell;
    double confidence;
    double time_to_handover;
    bool prediction_success;
} lstm_prediction_result_t;

// Structure for UE measurements (input to LSTM)
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
} lstm_ue_measurements_t;

/**
 * Initialize LSTM prediction service client
 * @return 0 on success, -1 on failure
 */
int lstm_predictor_init(void);

/**
 * Cleanup LSTM prediction service client
 */
void lstm_predictor_cleanup(void);

/**
 * Predict handover decision using LSTM model
 * 
 * @param measurements: UE measurement data
 * @param result: Output prediction result
 * @return 0 on success, -1 on failure
 */
int lstm_predict_handover(const lstm_ue_measurements_t* measurements, 
                          lstm_prediction_result_t* result);

/**
 * Check if LSTM service is available
 * @return true if service is reachable, false otherwise
 */
bool lstm_service_available(void);

#endif // LSTM_PREDICTOR_CLIENT_H
