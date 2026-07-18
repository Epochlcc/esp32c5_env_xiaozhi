#ifndef SECURITY_NODE_CLIENT_H
#define SECURITY_NODE_CLIENT_H

#include <string>

class SecurityNodeClient {
public:
    struct Snapshot {
        bool configured = false;
        bool reachable = false;
        bool valid = false;
        bool armed = false;
        std::string state;
        std::string current_zone;
        std::string last_reason;
        std::string last_zone;
        std::string last_trigger_source;
        std::string last_time;
        float last_fusion_confidence = 0.0f;
        std::string recent_event_message;
        int recent_event_type = -1;
        int recent_event_timestamp = 0;
    };

    void Initialize();
    bool Poll();
    Snapshot GetLatestSnapshot() const;
    std::string GetSnapshotJson() const;

private:
    void LoadSettings();
    static std::string Base64Encode(const std::string& data);
    bool FetchAlarmStatus();
    void FetchRecentEvent();

    std::string base_url_;
    std::string username_;
    std::string password_;
    Snapshot latest_snapshot_;
};

#endif
