#pragma once
#include "../core/types.hpp"
#include <string>

namespace civ {

    class IChannel {
    public:
        virtual ~IChannel() = default;
        virtual void send(ByteView data) = 0;
        virtual Bytes recv() = 0;
        virtual void close() noexcept = 0;
        virtual std::string name() const = 0;
    };

} // namespace civ