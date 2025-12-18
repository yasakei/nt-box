#include "builder.h"
#include "platform.h"
#include <iostream>
#include <cstdlib>
#include <fstream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace box {

Builder::Builder() {
}

std::string Builder::getCompiler() {
    if (Platform::isWindows()) {
        // Use MSVC on Windows
        return "cl";
    } else {
        // Check for clang++ or g++
        if (system("which clang++ > /dev/null 2>&1") == 0) {
            return "clang++";
        } else {
            return "g++";
        }
    }
}

std::vector<std::string> Builder::getLinkerFlags() {
    std::vector<std::string> flags;
    
    if (Platform::isWindows()) {
        // MSVC flags
        flags.push_back("/LD");  // Create DLL
        flags.push_back("/MD");  // Multithreaded DLL runtime
    } else if (Platform::isMacOS()) {
        flags.push_back("-shared");
        flags.push_back("-fPIC");
        flags.push_back("-dynamiclib");
    } else {  // Linux
        flags.push_back("-shared");
        flags.push_back("-fPIC");
    }
    
    return flags;
}

std::vector<std::string> Builder::getIncludePaths() {
    std::vector<std::string> paths;
    
    // Try to find Neutron installation
    std::string neutronDir = findNeutronDir();
    
    if (!neutronDir.empty()) {
        paths.push_back(neutronDir + "/include/core");
    }
    
    // Add standard locations
    paths.push_back("../include");
    paths.push_back("../../include");
    
    return paths;
}

std::string Builder::findNeutronDir() {
    // Check environment variable
    char* neutronHome = getenv("NEUTRON_HOME");
    if (neutronHome) {
        return std::string(neutronHome);
    }
    
    // Check standard installation locations
    std::vector<std::string> candidates;
    
#ifdef _WIN32
    candidates.push_back("C:\\Program Files\\Neutron");
    candidates.push_back("C:\\Neutron");
    
    // MSYS2/MINGW64 paths
    const char* msystem = getenv("MSYSTEM");
    if (msystem) {
        candidates.push_back("/mingw64/neutron");
        candidates.push_back("/usr/local/neutron");
        candidates.push_back("/opt/neutron");
    }
#else
    candidates.push_back("/usr/local/neutron");
    candidates.push_back("/opt/neutron");
    char* home = getenv("HOME");
    if (home) {
        candidates.push_back(std::string(home) + "/.neutron");
    }
#endif
    
    // Check current directory and parent
    candidates.push_back(".");
    candidates.push_back("..");
    
    for (const auto& dir : candidates) {
        std::string testPath = dir + "/include/core/neutron.h";
        std::ifstream test(testPath);
        if (test.good()) {
            return dir;
        }
    }
    
    return "";
}

std::string Builder::findNativeShim() {
    // Check standard locations for native_shim.cpp
    std::vector<std::string> candidates;
    
    // First check relative to current directory (for development)
    candidates.push_back("nt-box/src/native_shim.cpp");
    candidates.push_back("../nt-box/src/native_shim.cpp");
    
    // Check in Neutron installation directory
    std::string neutronDir = findNeutronDir();
    if (!neutronDir.empty()) {
        candidates.push_back(neutronDir + "/nt-box/src/native_shim.cpp");
    }
    
    // Check standard installation locations
#ifdef _WIN32
    candidates.push_back("C:\\Program Files\\Neutron\\nt-box\\src\\native_shim.cpp");
    candidates.push_back("C:\\Neutron\\nt-box\\src\\native_shim.cpp");
#else
    candidates.push_back("/usr/local/neutron/nt-box/src/native_shim.cpp");
    candidates.push_back("/opt/neutron/nt-box/src/native_shim.cpp");
#endif
    
    for (const auto& path : candidates) {
        std::ifstream test(path);
        if (test.good()) {
            return path;
        }
    }
    
    return "";
}

std::string Builder::generateBuildCommand(const std::string& moduleName,
                                         const std::string& sourcePath,
                                         const std::string& outputPath) {
    std::string compiler = getCompiler();
    std::vector<std::string> linkerFlags = getLinkerFlags();
    std::vector<std::string> includePaths = getIncludePaths();

    // Find the actual source file path for buildNative
    std::string nativeCppPath = sourcePath + "/native.cpp";
    std::ifstream sourceFile(nativeCppPath);
    if (!sourceFile.good()) {
        // Try alternative source file locations
        std::vector<std::string> potentialSources = {
            sourcePath + "/src/native.cpp",
            sourcePath + "/src/main.cpp",
            sourcePath + "/source/native.cpp",
            sourcePath + "/lib/native.cpp"
        };

        for (const auto& potentialSource : potentialSources) {
            std::ifstream altSource(potentialSource);
            if (altSource.good()) {
                nativeCppPath = potentialSource;
                break;
            }
        }
    }

    std::string command;

    // Check if we're using MSVC or MINGW64
    bool isMSVC = (Platform::isWindows() && compiler == "cl");

    if (isMSVC) {
        // MSVC command
        command = compiler + " ";
        command += "/nologo /std:c++17 /EHsc ";

        // Add include paths
        for (const auto& path : includePaths) {
            command += "/I\"" + path + "\" ";
        }

        // Source file
        command += "\"" + nativeCppPath + "\" ";

        // Include shim that dynamically resolves Neutron API at runtime so native
        // modules don't have to link against an import library on every platform
        std::string shimPath = findNativeShim();
        if (shimPath.empty()) {
            std::cerr << "Error: Could not find native_shim.cpp" << std::endl;
            return "";
        }
        command += "\"" + shimPath + "\" ";

        // Linker flags
        command += "/LD /MD ";

        // Output
        command += "/Fe:\"" + outputPath + "\" ";

        // Check for module definition file
        std::string defFile = sourcePath + "/" + moduleName + ".def";
        std::ifstream defCheck(defFile);
        if (defCheck.good()) {
            command += "/link /DEF:\"" + defFile + "\" ";
        } else {
            command += "/link ";
        }

        // Native modules use the shim to dynamically resolve Neutron API at runtime
        // No need to link against any import library
    } else {
        // GCC/Clang command (Linux, macOS, MINGW64)
        command = compiler + " ";
        command += "-std=c++17 ";
        command += "-fPIC -shared ";

        // Add include paths
        for (const auto& path : includePaths) {
            command += "-I\"" + path + "\" ";
        }

        // Source file
        command += "\"" + nativeCppPath + "\" ";
        // Include shim to resolve Neutron API at runtime on POSIX
        std::string shimPath = findNativeShim();
        if (shimPath.empty()) {
            std::cerr << "Error: Could not find native_shim.cpp" << std::endl;
            return "";
        }
        command += "\"" + shimPath + "\" ";

        // Output
        command += "-o \"" + outputPath + "\" ";

        // Link against neutron runtime if found
        std::string neutronDir = findNeutronDir();
        if (!neutronDir.empty()) {
            command += "-L\"" + neutronDir + "/build\" ";
            // Don't use -rpath on Windows/MINGW64
            if (!Platform::isWindows()) {
                command += "-Wl,-rpath,\"" + neutronDir + "/build\" ";
            }
            // Link against the neutron runtime library
            command += "-lneutron_runtime ";
        }
    }

    return command;
}

bool Builder::executeCommand(const std::string& command) {
    #ifdef _WIN32
    if (command.find("cl ") == 0 || command.find("cl.exe") != std::string::npos) {
        // Check if cl.exe is in PATH
        int checkResult = system("where cl >nul 2>&1");
        if (checkResult != 0) {
            // cl.exe not in PATH, try to find and use vcvarsall.bat
            
            // Common Visual Studio installation paths
            std::vector<std::string> vsPaths = {
                "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\18\\Professional\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\18\\Enterprise\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\18\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\18\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2025\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2025\\Professional\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2025\\Enterprise\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2025\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\2025\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\2025\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2019\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat",
                "C:\\Program Files\\Microsoft Visual Studio\\2019\\BuildTools\\VC\\Auxiliary\\Build\\vcvarsall.bat"
            };
            
            std::string vcvarsPath;
            for (const auto& path : vsPaths) {
                std::ifstream test(path);
                if (test.good()) {
                    vcvarsPath = path;
                    break;
                }
            }
            
            if (!vcvarsPath.empty()) {
                // Found vcvarsall.bat, wrap command with it (suppress setup output)
                std::string wrappedCmd = "cmd /c \"\"" + vcvarsPath + "\" x64 >nul 2>&1 && " + command + "\"";
                int result = system(wrappedCmd.c_str());
                return result == 0;
            } else {
                // Could not find MSVC installation
                std::cerr << "\n✗ Error: MSVC compiler not found\n" << std::endl;
                std::cerr << "Please install Microsoft C++ Build Tools." << std::endl;
                std::cerr << "Download: https://aka.ms/vs/17/release/vs_BuildTools.exe" << std::endl;
                return false;
            }
        }
    }
    #endif
    
    int result = system(command.c_str());
    return result == 0;
}

bool Builder::buildNative(const std::string& moduleName,
                         const std::string& sourcePath,
                         const std::string& outputDir,
                         const std::string& version) {
    std::cout << "Building native module: " << moduleName << std::endl;
    std::cout << "Version: " << version << std::endl;
    std::cout << "Platform: " << Platform::getOSString() << std::endl;
    
    // Check if source exists
    std::string nativeCpp = sourcePath + "/native.cpp";
    std::ifstream sourceFile(nativeCpp);
    if (!sourceFile.good()) {
        std::cerr << "Error: Source file not found: " << nativeCpp << std::endl;
        return false;
    }
    
    // Extract just the module name (remove path if present)
    std::string baseModuleName = moduleName;
    size_t lastSlash = moduleName.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        baseModuleName = moduleName.substr(lastSlash + 1);
    }
    
    // Create output directory: box/{module_name}/
    std::string moduleOutputDir = outputDir + "/" + baseModuleName;
    std::string mkdirCmd;
#ifdef _WIN32
    mkdirCmd = "mkdir \"" + moduleOutputDir + "\" 2>nul";
#else
    mkdirCmd = "mkdir -p \"" + moduleOutputDir + "\"";
#endif
    system(mkdirCmd.c_str());
    
    // Generate output path: box/{module_name}/{module_name}.so
    std::string outputPath = moduleOutputDir + "/" + baseModuleName + Platform::getLibraryExtension();
    
    // Generate and execute build command
    std::string buildCommand = generateBuildCommand(baseModuleName, sourcePath, outputPath);
    
    std::cerr << "Command: " << buildCommand << std::endl;
    
    if (executeCommand(buildCommand)) {
        std::cout << "✓ Built: " << outputPath << std::endl;
        
        // Create metadata.json
        if (createMetadata(baseModuleName, version, moduleOutputDir)) {
            std::cout << "✓ Created: " << moduleOutputDir << "/metadata.json" << std::endl;
        }
        
        return true;
    } else {
        std::cerr << "✗ Build failed" << std::endl;
        return false;
    }
}

bool Builder::createMetadata(const std::string& moduleName,
                            const std::string& version,
                            const std::string& outputDir) {
    std::string metadataPath = outputDir + "/metadata.json";
    std::ofstream metadataFile(metadataPath);
    
    if (!metadataFile.is_open()) {
        std::cerr << "Warning: Could not create metadata.json" << std::endl;
        return false;
    }
    
    // Write JSON metadata
    metadataFile << "{\n";
    metadataFile << "  \"name\": \"" << moduleName << "\",\n";
    metadataFile << "  \"version\": \"" << version << "\",\n";
    metadataFile << "  \"description\": \"" << moduleName << " native module for Neutron\",\n";
    metadataFile << "  \"platform\": \"" << Platform::getOSString() << "\",\n";
    metadataFile << "  \"library\": \"" << moduleName << Platform::getLibraryExtension() << "\"\n";
    metadataFile << "}\n";
    
    metadataFile.close();
    return true;
}

bool Builder::buildFromSource(const std::string& moduleName,
                             const std::string& sourcePath,
                             const std::string& installDir,
                             const std::string& version) {
    std::cout << "Building module " << moduleName << " from source..." << std::endl;
    std::cout << "Version: " << version << std::endl;
    std::cout << "Platform: " << Platform::getOSString() << std::endl;

    // Check if source exists and has required files
    std::string nativeCpp = sourcePath + "/native.cpp";
    std::ifstream sourceFile(nativeCpp);
    if (!sourceFile.good()) {
        std::cerr << "Error: Source file not found: " << nativeCpp << std::endl;

        // Try alternatives
        std::vector<std::string> potentialSources = {
            sourcePath + "/src/main.cpp",
            sourcePath + "/source/native.cpp",
            sourcePath + "/lib/native.cpp"
        };

        bool foundSource = false;
        for (const auto& potentialSource : potentialSources) {
            std::ifstream altSource(potentialSource);
            if (altSource.good()) {
                nativeCpp = potentialSource;
                foundSource = true;
                std::cout << "Found source at: " << nativeCpp << std::endl;
                break;
            }
        }

        if (!foundSource) {
            std::cerr << "Error: No native source file found in the repository" << std::endl;
            return false;
        }
    }

    // Generate output path: install_dir/{module_name}.{ext}
    std::string outputPath = installDir + "/" + moduleName + Platform::getLibraryExtension();

    // Generate and execute build command
    std::string buildCommand = generateBuildCommand(moduleName, sourcePath, outputPath);

    if (executeCommand(buildCommand)) {
        std::cout << "✓ Built: " << outputPath << std::endl;

        // Create metadata.json
        if (createMetadata(moduleName, version, installDir)) {
            std::cout << "✓ Created: " << installDir << "/metadata.json" << std::endl;
        }

        return true;
    } else {
        std::cerr << "✗ Build failed" << std::endl;
        return false;
    }
}

bool Builder::buildNeutron(const std::string& moduleName,
                          const std::string& sourcePath,
                          const std::string& outputDir) {
    std::cerr << "Neutron source module builds not yet implemented" << std::endl;
    return false;
}

} // namespace box
