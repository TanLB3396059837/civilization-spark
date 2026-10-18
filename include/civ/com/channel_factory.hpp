#pragma once
#include "ichannel.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>

namespace civ {

    class ChannelFactory {
    public:
        using Creator = std::function<std::unique_ptr<IChannel>(
            const std::unordered_map<std::string, std::string>&)>;

        static ChannelFactory& instance();
        void register_channel(const std::string& type, Creator c);
        std::unique_ptr<IChannel> create(
            const std::string& type,
            const std::unordered_map<std::string, std::string>& params);
        std::vector<std::string> list_types() const;

    private:
        ChannelFactory() = default;
        std::unordered_map<std::string, Creator> creators_;
    };

} // namespace civ