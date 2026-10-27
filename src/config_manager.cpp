#include "balance/config_manager.hpp"

namespace balance {

bool ConfigManager::load(RobotConfig& config) {
    RobotConfig loaded;
    if (!storage_.load(loaded) || !validateConfig(loaded)) {
        return false;
    }
    config = loaded;
    return true;
}

bool ConfigManager::save(const RobotConfig& config) {
    return validateConfig(config) && storage_.save(config);
}

}
