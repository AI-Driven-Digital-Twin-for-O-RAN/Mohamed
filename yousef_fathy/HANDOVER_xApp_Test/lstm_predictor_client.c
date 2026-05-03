#include "lstm_predictor_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <unistd.h>

static CURL* curl_handle = NULL;
static bool initialized = false;

/* Service base URL: from env LSTM_SERVICE_URL, else LSTM_SERVICE_URL_DEFAULT */
static const char* get_lstm_service_url(void) {
    const char* env = getenv("LSTM_SERVICE_URL");
    if (env && env[0] != '\0') {
        return env;
    }
    return LSTM_SERVICE_URL_DEFAULT;
}

// Response buffer for curl
struct ResponseBuffer {
    char* data;
    size_t size;
};

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, struct ResponseBuffer* buffer) {
    size_t total_size = size * nmemb;
    buffer->data = realloc(buffer->data, buffer->size + total_size + 1);
    if (buffer->data) {
        memcpy(&(buffer->data[buffer->size]), contents, total_size);
        buffer->size += total_size;
        buffer->data[buffer->size] = 0;
    }
    return total_size;
}

int lstm_predictor_init(void) {
    if (initialized) {
        return 0;
    }
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_handle = curl_easy_init();
    
    if (!curl_handle) {
        fprintf(stderr, "[LSTM Client] Failed to initialize CURL\n");
        return -1;
    }
    
    initialized = true;
    printf("[LSTM Client] Initialized\n");
    return 0;
}

void lstm_predictor_cleanup(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
    curl_global_cleanup();
    initialized = false;
    printf("[LSTM Client] Cleaned up\n");
}

bool lstm_service_available(void) {
    if (!initialized) {
        lstm_predictor_init();
    }
    
    if (!curl_handle) {
        return false;
    }
    
    struct ResponseBuffer buffer = {NULL, 0};
    char url[256];
    snprintf(url, sizeof(url), "%s/health", get_lstm_service_url());
    
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, LSTM_SERVICE_TIMEOUT_SEC);
    
    CURLcode res = curl_easy_perform(curl_handle);
    
    bool available = (res == CURLE_OK && buffer.data != NULL);
    
    if (buffer.data) {
        free(buffer.data);
    }
    
    return available;
}

int lstm_predict_handover(const lstm_ue_measurements_t* measurements, 
                          lstm_prediction_result_t* result) {
    if (!initialized) {
        if (lstm_predictor_init() != 0) {
            return -1;
        }
    }
    
    if (!measurements || !result) {
        return -1;
    }
    
    // Initialize result
    memset(result, 0, sizeof(lstm_prediction_result_t));
    result->prediction_success = false;
    
    if (!curl_handle) {
        fprintf(stderr, "[LSTM Client] CURL handle not initialized\n");
        return -1;
    }
    
    // Build JSON request
    json_object* json_req = json_object_new_object();
    json_object_object_add(json_req, "serving_sinr", 
                          json_object_new_double(measurements->serving_sinr));
    json_object_object_add(json_req, "serving_rsrp", 
                          json_object_new_double(measurements->serving_rsrp));
    json_object_object_add(json_req, "serving_qual", 
                          json_object_new_double(measurements->serving_qual));
    json_object_object_add(json_req, "neighbor_sinr", 
                          json_object_new_double(measurements->neighbor_sinr));
    json_object_object_add(json_req, "neighbor_rsrp", 
                          json_object_new_double(measurements->neighbor_rsrp));
    json_object_object_add(json_req, "neighbor_cell_id", 
                          json_object_new_int(measurements->neighbor_cell_id));
    json_object_object_add(json_req, "cqi", 
                          json_object_new_double(measurements->cqi));
    json_object_object_add(json_req, "speed", 
                          json_object_new_double(measurements->speed));
    json_object_object_add(json_req, "dl_bitrate", 
                          json_object_new_double(measurements->dl_bitrate));
    json_object_object_add(json_req, "ul_bitrate", 
                          json_object_new_double(measurements->ul_bitrate));
    json_object_object_add(json_req, "bandwidth", 
                          json_object_new_int(measurements->bandwidth));
    
    const char* json_string = json_object_to_json_string(json_req);
    
    // Setup curl for POST request
    struct ResponseBuffer buffer = {NULL, 0};
    char url[256];
    snprintf(url, sizeof(url), "%s/predict", get_lstm_service_url());
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_string);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, LSTM_SERVICE_TIMEOUT_SEC);
    
    CURLcode res = curl_easy_perform(curl_handle);
    
    curl_slist_free_all(headers);
    json_object_put(json_req);
    
    if (res != CURLE_OK) {
        fprintf(stderr, "[LSTM Client] CURL error: %s\n", curl_easy_strerror(res));
        if (buffer.data) {
            free(buffer.data);
        }
        return -1;
    }
    
    // Parse JSON response
    if (!buffer.data) {
        fprintf(stderr, "[LSTM Client] No response data\n");
        return -1;
    }
    
    json_object* json_resp = json_tokener_parse(buffer.data);
    if (!json_resp) {
        fprintf(stderr, "[LSTM Client] Failed to parse JSON response\n");
        free(buffer.data);
        return -1;
    }
    
    // Extract prediction results
    json_object* should_ho_obj = NULL;
    json_object* target_cell_obj = NULL;
    json_object* confidence_obj = NULL;
    json_object* time_to_ho_obj = NULL;
    
    if (json_object_object_get_ex(json_resp, "should_handover", &should_ho_obj)) {
        result->should_handover = json_object_get_boolean(should_ho_obj);
    }
    
    if (json_object_object_get_ex(json_resp, "target_cell", &target_cell_obj)) {
        result->target_cell = (uint16_t)json_object_get_int(target_cell_obj);
    }
    
    if (json_object_object_get_ex(json_resp, "confidence", &confidence_obj)) {
        result->confidence = json_object_get_double(confidence_obj);
    }
    
    if (json_object_object_get_ex(json_resp, "time_to_handover", &time_to_ho_obj)) {
        result->time_to_handover = json_object_get_double(time_to_ho_obj);
    }
    
    result->prediction_success = true;
    
    json_object_put(json_resp);
    free(buffer.data);
    
    return 0;
}
