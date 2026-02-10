// Box Package Manager - Version Header
#ifndef BOX_VERSION_H
#define BOX_VERSION_H

#include <string>

namespace box {

class Version {
public:
    static constexpr int MAJOR = 2;
    static constexpr int MINOR = 0;
    static constexpr int PATCH = 0;
    static constexpr const char* STAGE = "stable";

    static std::string getVersion() {
        return std::to_string(MAJOR) + "." + std::to_string(MINOR) + "." + std::to_string(PATCH);
    }

    static std::string getFullVersion() {
        return "Box " + getVersion() + "-" + STAGE;
    }

    static std::string getBuildDate() {
        return std::string(__DATE__) + " " + __TIME__;
    }
};

} // namespace box

#endif // BOX_VERSION_H
