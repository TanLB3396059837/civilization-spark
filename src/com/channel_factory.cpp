#include "civ/com/channel_factory.hpp"
#include "civ/channel/tcp_channel.hpp"
#include "civ/channel/shm_channel.hpp"
#include "civ/channel/multi_channel.hpp"

#ifdef CIV_HAS_VERBS
#include "civ/channel/rocev2_channel.hpp"
#endif

#include <stdexcept>

namespace civ {

ChannelFactory& ChannelFactory::instance() {
    static ChannelFactory f;
    return f;
}

void ChannelFactory::register_channel(const std::string& t, Creator c) {
    creators_[t] = std::move(c);
}

std::unique_ptr<IChannel> ChannelFactory::create(
    const std::string& type,
    const std::unordered_map<std::string, std::string>& params) {
    auto it = creators_.find(type);
    if (it == creators_.end())
        throw std::runtime_error("Unknown channel: " + type);
    return it->second(params);
}

std::vector<std::string> ChannelFactory::list_types() const {
    std::vector<std::string> out;
    for (const auto& [k, _] : creators_) out.push_back(k);
    return out;
}

namespace {
struct Registrar {
    Registrar() {
        auto& f = ChannelFactory::instance();

        f.register_channel("tcp",
            [](const auto& p) -> std::unique_ptr<IChannel> {
                TcpChannel::Config cfg{};
                cfg.port      = static_cast<std::uint16_t>(std::stoi(p.at("port")));
                cfg.is_server = (p.at("role") == "server");
                cfg.host      = p.count("host") ? p.at("host") : "127.0.0.1";
                return std::make_unique<TcpChannel>(cfg);
            });

        f.register_channel("shm",
            [](const auto& p) -> std::unique_ptr<IChannel> {
                std::string name = p.at("name");
                bool create = (p.at("role") == "server");
                std::size_t size = p.count("size")
                    ? std::stoull(p.at("size")) : (1u << 20);
                return std::make_unique<ShmChannel>(name, create, size);
            });

#ifdef CIV_HAS_VERBS
        f.register_channel("rocev2",
            [](const auto& p) -> std::unique_ptr<IChannel> {
                RoCEv2Channel::Config cfg{};
                cfg.port      = static_cast<std::uint16_t>(std::stoi(p.at("port")));
                cfg.is_server = (p.at("role") == "server");
                cfg.host      = p.count("host") ? p.at("host") : "127.0.0.1";
                return std::make_unique<RoCEv2Channel>(cfg);
            });
#endif
    }
};
static Registrar g_reg;
} // namespace

} // namespace civ