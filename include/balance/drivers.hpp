#pragma once

#include "balance/types.hpp"

namespace balance {

class ImuDriver {
public:
    virtual ~ImuDriver() = default;
    virtual bool begin() = 0;
    virtual bool read(ImuReading& reading) = 0;
};

class EncoderDriver {
public:
    virtual ~EncoderDriver() = default;
    virtual int32_t count() const = 0;
    virtual void reset() = 0;
};

}
