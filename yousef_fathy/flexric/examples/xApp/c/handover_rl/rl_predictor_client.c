#include "rl_predictor_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <unistd.h>

static CURL* curl_handle = NULL;
static bool initialized = false;

/* Service base URL: from env RL_SERVICE_URL, else RL_SERVICE_URL_DEFAULT */
static const char* get_rl_service_url(void) {
    const char* env = getenv("RL_SERVICE_URL");
    if (env && env[0] != '\0') {
        return env;
    }
    return RL_SERVICE_URL_DEFAULT;
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

int rl_predictor_init(void) {
    if (initialized) {
        return 0;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_handle = curl_easy_init();

    if (!curl_handle) {
        fprintf(stderr, "[RL Client] Failed to initialize CURL\n");
        return -1;
    }

    initialized = true;
    printf("[RL Client] Initialized\n");
    return 0;
}

void rl_predictor_cleanup(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
    curl_global_cleanup();
    initialized = false;
    printf("[RL Client] Cleaned up\n");
}

bool rl_service_available(void) {
    if (!initialized) {
        rl_predictor_init();
    }

    if (!curl_handle) {
        return false;
    }

    struct ResponseBuffer buffer = {NULL, 0};
    char url[256];
    snprintf(url, sizeof(url), "%s/health", get_rl_service_url());

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, RL_SERVICE_TIMEOUT_SEC);

    CURLcode res = curl_easy_perform(curl_handle);

    bool available = (res == CURLE_OK && buffer.data != NULL);

    if (buffer.data) {
        free(buffer.data);
    }

    return available;
}

int rl_predict_handover(const rl_ue_measurements_t* measurements,
                        rl_prediction_result_t* result) {
    if (!initialized) {
        if (rl_predictor_init() != 0) {
            return -1;
        }
    }

    if (!measurements || !result) {
        return -1;
    }

    // Initialize result
    memset(result, 0, sizeof(rl_prediction_result_t));
    result->prediction_success = false;

    if (!curl_handle) {
        fprintf(stderr, "[RL Client] CURL handle not initialized\n");
        return -1;
    }

    // Build JSON request
    json_object* json_req = json_object_new_object();
    json_object_object_add(json_req, "ue_id",
                          json_object_new_int(measurements->ue_id));
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
    snprintf(url, sizeof(url), "%s/predict", get_rl_service_url());

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_string);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, RL_SERVICE_TIMEOUT_SEC);

    CURLcode res = curl_easy_perform(curl_handle);

    curl_slist_free_all(headers);
    json_object_put(json_req);

    if (res != CURLE_OK) {
        fprintf(stderr, "[RL Client] CURL error: %s\n", curl_easy_strerror(res));
        if (buffer.data) {
            free(buffer.data);
        }
        return -1;
    }

    // Parse JSON response
    if (!buffer.data) {
        fprintf(stderr, "[RL Client] No response data\n");
        return -1;
    }

    json_object* json_resp = json_tokener_parse(buffer.data);
    if (!json_resp) {
        fprintf(stderr, "[RL Client] Failed to parse JSON response\n");
        free(buffer.data);
        return -1;
    }

    // Extract prediction results
    // rl_xapp.py returns: {action: 0|1, should_handover: bool, q_values: [...], confidence: float}
    json_object* action_obj     = NULL;
    json_object* confidence_obj = NULL;

    if (json_object_object_get_ex(json_resp, "action", &action_obj)) {
        int action = json_object_get_int(action_obj);
        result->should_handover = (action == 1);
    }

    if (json_object_object_get_ex(json_resp, "confidence", &confidence_obj)) {
        result->confidence = json_object_get_double(confidence_obj);
    }

    // Target cell is chosen by the xApp based on SINR, not by the RL model
    result->target_cell      = 0;
    result->time_to_handover = 0.0;
    result->prediction_success = true;

    json_object_put(json_resp);
    free(buffer.data);

    return 0;
}
