#include "openclaw/sessions/session.hpp"

#include "openclaw/core/utils.hpp"

namespace openclaw::sessions {

void to_json(json& j, const SessionData& d) {
    j = json{
        {"id", d.session.id},
        {"user_id", d.session.user_id},
        {"device_id", d.session.device_id},
        {"state", d.state},
        {"metadata", d.metadata},
        {"created_at", utils::timestamp_ms()},
    };
    if (d.session.channel) {
        j["channel"] = *d.session.channel;
    }
}

void from_json(const json& j, SessionData& d) {
    j.at("id").get_to(d.session.id);
    j.at("user_id").get_to(d.session.user_id);
    j.at("device_id").get_to(d.session.device_id);
    j.at("state").get_to(d.state);
    if (j.contains("metadata")) {
        d.metadata = j.at("metadata");
    }
    if (j.contains("channel")) {
        d.session.channel = j.at("channel").get<std::string>();
    }
}

} // namespace openclaw::sessions
