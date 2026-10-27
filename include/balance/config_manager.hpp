#pragma once

#include "balance/config.hpp"

namespace balance {

class ConfigManager {
public:
    explicit ConfigManager(ConfigStorage& storage) : storage_(storage) {}
    bool load(RobotConfig& config);
    bool save(const RobotConfig& config);

private:
    ConfigStorage& storage_;
};

}
