
#include "config.h"

#include "Core.h"
#include "Debug.h"
#include "Error.h"

#include <fstream>

namespace DFHack {
namespace DFCH {
namespace Config {

    std::filesystem::path getDataPath() {
        static const auto path = Core::getInstance().getHackPath() / "data" / "dfch";
        return path;
    }

    // Returns DFHack installation directory (parent of hack/)
    std::filesystem::path getDFHackPath() {
        static const auto path = Core::getInstance().getHackPath().parent_path();
        return path;
    }

    std::unordered_map<std::string, std::string> loadConfigFile(const std::filesystem::path& configPath) {
        std::unordered_map<std::string, std::string> config;
        std::ifstream configFile(configPath);
        if (!configFile.is_open()) {
            return config;
        }

        std::string line;
        config.reserve(10);
        line.reserve(128);
        while (std::getline(configFile, line)) {
            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }
            // Parse lines in format [KEY:VALUE]
            int startBracket = line.find('[');
            int endBracket = line.find(']');
            int colonPos = line.find(':');
            if (startBracket == 0 && endBracket != std::string::npos && colonPos != std::string::npos && colonPos > startBracket && colonPos < endBracket) {
                // Directly use emplace to construct key-value pairs, avoiding extra string copies
                config.emplace(
                    std::string(line, startBracket + 1, colonPos - startBracket - 1),
                    std::string(line, colonPos + 1, endBracket - colonPos - 1)
                );
            }
        }
        configFile.close();
        return config;
    }

}
}
}