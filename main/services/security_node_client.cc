#include "security_node_client.h"

#include "board.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_log.h>
#include <mbedtls/base64.h>

namespace {
constexpr char TAG[] = "SecurityNode";
constexpr char kSecurityNodeBaseUrl[] = "http://192.168.106.1";
}

void SecurityNodeClient::Initialize() {
    LoadSettings();
}

void SecurityNodeClient::LoadSettings() {
    base_url_ = kSecurityNodeBaseUrl;
    username_ = "admin";
    password_ = "admin";
    latest_snapshot_.configured = true;
}

std::string SecurityNodeClient::Base64Encode(const std::string& data) {
    size_t dlen = 0;
    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &dlen,
                          reinterpret_cast<const unsigned char*>(data.data()), data.size());
    std::string result(dlen, 0);
    mbedtls_base64_encode(reinterpret_cast<unsigned char*>(result.data()), result.size(), &olen,
                          reinterpret_cast<const unsigned char*>(data.data()), data.size());
    result.resize(olen);
    return result;
}

bool SecurityNodeClient::Poll() {
    LoadSettings();

    bool ok = FetchAlarmStatus();
    if (ok) {
        FetchRecentEvent();
    }
    return ok;
}

bool SecurityNodeClient::FetchAlarmStatus() {
    if (base_url_.empty()) {
        latest_snapshot_ = Snapshot{};
        return false;
    }

    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    if (!http) {
        latest_snapshot_.configured = true;
        latest_snapshot_.reachable = false;
        latest_snapshot_.valid = false;
        return false;
    }

    std::string auth = username_ + ":" + password_;
    http->SetHeader("Authorization", "Basic " + Base64Encode(auth));

    std::string url = base_url_ + "/api/alarm/status";
    if (!http->Open("GET", url)) {
        latest_snapshot_.configured = true;
        latest_snapshot_.reachable = false;
        latest_snapshot_.valid = false;
        ESP_LOGW(TAG, "Failed to open %s", url.c_str());
        return false;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        latest_snapshot_.configured = true;
        latest_snapshot_.reachable = false;
        latest_snapshot_.valid = false;
        ESP_LOGW(TAG, "Unexpected status code %d from %s", status_code, url.c_str());
        http->Close();
        return false;
    }

    std::string body = http->ReadAll();
    http->Close();

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        latest_snapshot_.configured = true;
        latest_snapshot_.reachable = true;
        latest_snapshot_.valid = false;
        ESP_LOGW(TAG, "Failed to parse alarm status JSON");
        return false;
    }

    Snapshot snapshot;
    snapshot.configured = true;
    snapshot.reachable = true;
    snapshot.valid = true;

    if (auto armed = cJSON_GetObjectItem(root, "armed"); cJSON_IsBool(armed)) {
        snapshot.armed = cJSON_IsTrue(armed);
    }
    if (auto state = cJSON_GetObjectItem(root, "state"); cJSON_IsString(state)) {
        snapshot.state = state->valuestring;
    }
    if (auto zone = cJSON_GetObjectItem(root, "current_zone"); cJSON_IsString(zone)) {
        snapshot.current_zone = zone->valuestring;
    }

    auto last_event = cJSON_GetObjectItem(root, "last_event");
    if (cJSON_IsObject(last_event)) {
        if (auto reason = cJSON_GetObjectItem(last_event, "reason"); cJSON_IsString(reason)) {
            snapshot.last_reason = reason->valuestring;
        }
        if (auto zone = cJSON_GetObjectItem(last_event, "zone"); cJSON_IsString(zone)) {
            snapshot.last_zone = zone->valuestring;
        }
        if (auto source = cJSON_GetObjectItem(last_event, "trigger_source"); cJSON_IsString(source)) {
            snapshot.last_trigger_source = source->valuestring;
        }
        if (auto time = cJSON_GetObjectItem(last_event, "time"); cJSON_IsString(time)) {
            snapshot.last_time = time->valuestring;
        }
        if (auto confidence = cJSON_GetObjectItem(last_event, "fusion_confidence"); cJSON_IsNumber(confidence)) {
            snapshot.last_fusion_confidence = static_cast<float>(confidence->valuedouble);
        }
    }

    cJSON_Delete(root);
    latest_snapshot_ = snapshot;
    return true;
}

void SecurityNodeClient::FetchRecentEvent() {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    if (!http) {
        return;
    }

    std::string auth = username_ + ":" + password_;
    http->SetHeader("Authorization", "Basic " + Base64Encode(auth));

    std::string url = base_url_ + "/api/events?limit=1";
    if (!http->Open("GET", url)) {
        ESP_LOGW(TAG, "Failed to open %s", url.c_str());
        return;
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGW(TAG, "Unexpected status code %d from %s", status_code, url.c_str());
        http->Close();
        return;
    }

    std::string body = http->ReadAll();
    http->Close();

    cJSON* root = cJSON_Parse(body.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Failed to parse recent event JSON");
        return;
    }

    auto events = cJSON_GetObjectItem(root, "events");
    if (cJSON_IsArray(events) && cJSON_GetArraySize(events) > 0) {
        auto evt = cJSON_GetArrayItem(events, 0);
        if (cJSON_IsObject(evt)) {
            if (auto msg = cJSON_GetObjectItem(evt, "msg"); cJSON_IsString(msg)) {
                latest_snapshot_.recent_event_message = msg->valuestring;
            }
            if (auto type = cJSON_GetObjectItem(evt, "type"); cJSON_IsNumber(type)) {
                latest_snapshot_.recent_event_type = type->valueint;
            }
            if (auto ts = cJSON_GetObjectItem(evt, "ts"); cJSON_IsNumber(ts)) {
                latest_snapshot_.recent_event_timestamp = ts->valueint;
            }
        }
    }

    cJSON_Delete(root);
}

SecurityNodeClient::Snapshot SecurityNodeClient::GetLatestSnapshot() const {
    return latest_snapshot_;
}

std::string SecurityNodeClient::GetSnapshotJson() const {
    const auto& s = latest_snapshot_;
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "configured", s.configured);
    cJSON_AddBoolToObject(root, "reachable", s.reachable);
    cJSON_AddBoolToObject(root, "valid", s.valid);
    cJSON_AddBoolToObject(root, "armed", s.armed);
    cJSON_AddStringToObject(root, "state", s.state.c_str());
    cJSON_AddStringToObject(root, "current_zone", s.current_zone.c_str());
    cJSON_AddStringToObject(root, "last_reason", s.last_reason.c_str());
    cJSON_AddStringToObject(root, "last_zone", s.last_zone.c_str());
    cJSON_AddStringToObject(root, "last_trigger_source", s.last_trigger_source.c_str());
    cJSON_AddStringToObject(root, "last_time", s.last_time.c_str());
    cJSON_AddNumberToObject(root, "last_fusion_confidence", s.last_fusion_confidence);
    cJSON_AddStringToObject(root, "recent_event_message", s.recent_event_message.c_str());
    cJSON_AddNumberToObject(root, "recent_event_type", s.recent_event_type);
    cJSON_AddNumberToObject(root, "recent_event_timestamp", s.recent_event_timestamp);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string json = json_str != nullptr ? json_str : "{}";
    if (json_str != nullptr) {
        cJSON_free(json_str);
    }
    cJSON_Delete(root);
    return json;
}
