#include "openclaw/core/types.hpp"
#include "openclaw/core/utils.hpp"

namespace openclaw {

void to_json(json& j, const ContentBlock& c) {
    j = json{{"type", c.type}, {"text", c.text}};
    if (c.tool_use_id) j["tool_use_id"] = *c.tool_use_id;
    if (c.tool_name) j["tool_name"] = *c.tool_name;
    if (c.tool_input) j["tool_input"] = *c.tool_input;
    if (c.tool_result) j["tool_result"] = *c.tool_result;
}

void from_json(const json& j, ContentBlock& c) {
    j.at("type").get_to(c.type);
    if (j.contains("text")) j.at("text").get_to(c.text);
    if (j.contains("tool_use_id")) c.tool_use_id = j.at("tool_use_id").get<std::string>();
    if (j.contains("tool_name")) c.tool_name = j.at("tool_name").get<std::string>();
    if (j.contains("tool_input")) c.tool_input = j.at("tool_input");
    if (j.contains("tool_result")) c.tool_result = j.at("tool_result");
}

void to_json(json& j, const Message& m) {
    j = json{
        {"id", m.id},
        {"role", m.role},
        {"content", m.content},
        {"created_at", utils::timestamp_ms()},
    };
    if (m.model) j["model"] = *m.model;
}

void from_json(const json& j, Message& m) {
    j.at("id").get_to(m.id);
    j.at("role").get_to(m.role);
    j.at("content").get_to(m.content);
    if (j.contains("model")) m.model = j.at("model").get<std::string>();
}

void to_json(json& j, const DeviceIdentity& d) {
    j = json{
        {"device_id", d.device_id},
        {"hostname", d.hostname},
        {"os", d.os},
        {"arch", d.arch},
    };
}

void from_json(const json& j, DeviceIdentity& d) {
    j.at("device_id").get_to(d.device_id);
    j.at("hostname").get_to(d.hostname);
    j.at("os").get_to(d.os);
    j.at("arch").get_to(d.arch);
}

} // namespace openclaw
