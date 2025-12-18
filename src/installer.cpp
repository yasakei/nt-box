#include "installer.h"
#include "platform.h"
#include "builder.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <cstdlib>
#include <vector>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define mkdir(path, mode) _mkdir(path)
#else
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
#endif

namespace box {

Installer::Installer() {
    std::string homeDir;
#ifdef _WIN32
    char* userProfile = getenv("USERPROFILE");
    if (userProfile) homeDir = userProfile;
#else
    char* home = getenv("HOME");
    if (home) {
        homeDir = home;
    } else {
        struct passwd* pw = getpwuid(getuid());
        if (pw) homeDir = pw->pw_dir;
    }
#endif
    
    globalModulesDir = homeDir + "/.box/modules";
    localModulesDir = "./.box/modules";
}

std::string Installer::getInstallDir(bool global) {
    return global ? globalModulesDir : localModulesDir;
}

bool Installer::ensureDirectory(const std::string& path) {
    try {
        std::filesystem::create_directories(path);
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error creating directory " << path << ": " << e.what() << std::endl;
        return false;
    }
}

bool Installer::install(const std::string& moduleSpec, bool global) {
    std::string moduleName = moduleSpec;
    std::string requestedVersion = "";
    
    size_t atPos = moduleSpec.find('@');
    if (atPos != std::string::npos) {
        moduleName = moduleSpec.substr(0, atPos);
        requestedVersion = moduleSpec.substr(atPos + 1);
    }
    
    std::cout << "Installing " << moduleName;
    if (!requestedVersion.empty()) std::cout << "@" << requestedVersion;
    std::cout << "..." << std::endl;
    
    if (!registry.fetchIndex()) {
        std::cerr << "Failed to fetch registry index" << std::endl;
        return false;
    }
    
    ModuleMetadata metadata = registry.fetchModuleMetadata(moduleName);
    if (metadata.name.empty()) {
        std::cerr << "Module not found: " << moduleName << std::endl;
        return false;
    }
    
    std::string versionToInstall = requestedVersion.empty() ? metadata.latest : requestedVersion;
    
    if (metadata.versions.find(versionToInstall) == metadata.versions.end()) {
        std::cerr << "Version not found: " << versionToInstall << std::endl;
        return false;
    }
    
    VersionMetadata versionMeta = metadata.versions[versionToInstall];
    
    // Create install directory
    std::string installDir = getInstallDir(global) + "/" + moduleName;
    if (!ensureDirectory(installDir)) {
        std::cerr << "Failed to create directory: " << installDir << std::endl;
        return false;
    }

    // Check if git repository is available for this version
    std::string repoURL = versionMeta.git.url;
    std::string repoRef = versionMeta.git.ref;

    if (repoURL.empty()) {
        std::cerr << "No git repository available for module: " << moduleName << std::endl;
        std::cerr << "Falling back to binary download..." << std::endl;

        // Fallback to previous behavior if no git repo
        std::string binaryURL;
        if (Platform::isLinux()) {
            binaryURL = versionMeta.entryLinux;
        } else if (Platform::isWindows()) {
            binaryURL = versionMeta.entryWin;
        } else if (Platform::isMacOS()) {
            binaryURL = versionMeta.entryMac;
        }

        if (binaryURL.empty()) {
            std::cerr << "No binary or git repository available for " << Platform::getOSString() << std::endl;
            return false;
        }

        std::cout << "Downloading from " << binaryURL << "..." << std::endl;
        std::string binaryData = registry.download(binaryURL);
        if (binaryData.empty()) {
            std::cerr << "Failed to download module" << std::endl;
            return false;
        }

        std::string outputFile = installDir + "/" + moduleName + Platform::getLibraryExtension();
        std::ofstream outFile(outputFile, std::ios::binary);
        if (!outFile) {
            std::cerr << "Failed to create file: " << outputFile << std::endl;
            return false;
        }
        outFile.write(binaryData.data(), binaryData.size());
        outFile.close();

    #ifndef _WIN32
        chmod(outputFile.c_str(), 0755);
    #endif
    } else {
        // Create a unique temporary directory for building
        std::string tempBaseDir = installDir + "/.tmp";
        int attempt = 0;
        std::string uniqueTempDir;

        // Find a unique temporary directory name
        while (attempt < 10) {
            uniqueTempDir = tempBaseDir + std::to_string(attempt);
            if (!std::filesystem::exists(uniqueTempDir)) {
                if (!ensureDirectory(uniqueTempDir)) {
                    std::cerr << "Failed to create temp directory: " << uniqueTempDir << std::endl;
                    return false;
                }
                break; // Found unique directory and created it
            }
            attempt++;
        }

        if (attempt >= 10) {
            std::cerr << "Failed to create unique temp directory" << std::endl;
            return false;
        }

        // Clone the repository to the unique temp directory
        std::cout << "Cloning from " << repoURL << "..." << std::endl;
        std::string cloneCmd;
#ifdef _WIN32
        cloneCmd = "cd \"" + uniqueTempDir + "\" && git clone \"" + repoURL + "\" ./repo 2>nul";
#else
        cloneCmd = "cd \"" + uniqueTempDir + "\" && git clone \"" + repoURL + "\" ./repo";
#endif
        int cloneResult = system(cloneCmd.c_str());
        if (cloneResult != 0) {
            std::cerr << "Failed to clone repository" << std::endl;
            // Clean up temp directory
            std::string cleanupCmd;
#ifdef _WIN32
            cleanupCmd = "rmdir /s /q \"" + uniqueTempDir + "\" 2>nul";
#else
            cleanupCmd = "rm -rf \"" + uniqueTempDir + "\"";
#endif
            system(cleanupCmd.c_str());
            return false;
        }

        // Navigate to the repo directory and checkout the specific version if provided
        std::string repoDir = uniqueTempDir + "/repo";
        if (!repoRef.empty()) {
            std::string checkoutCmd;
#ifdef _WIN32
            checkoutCmd = "cd \"" + repoDir + "\" && git checkout \"" + repoRef + "\" 2>nul";
#else
            checkoutCmd = "cd \"" + repoDir + "\" && git checkout \"" + repoRef + "\"";
#endif
            int checkoutResult = system(checkoutCmd.c_str());
            if (checkoutResult != 0) {
                std::cerr << "Failed to checkout specific version: " << repoRef << std::endl;
                // Clean up temp directory
                std::string cleanupCmd;
#ifdef _WIN32
                cleanupCmd = "rmdir /s /q \"" + uniqueTempDir + "\" 2>nul";
#else
                cleanupCmd = "rm -rf \"" + uniqueTempDir + "\"";
#endif
                system(cleanupCmd.c_str());
                return false;
            }
        }

        // Build the module using the builder
        Builder builder;
        bool buildSuccess = builder.buildFromSource(moduleName, repoDir, installDir, versionToInstall);

        // Clean up the temporary directory regardless of build success/failure
        std::string cleanupCmd;
#ifdef _WIN32
        cleanupCmd = "rmdir /s /q \"" + uniqueTempDir + "\" 2>nul";
#else
        cleanupCmd = "rm -rf \"" + uniqueTempDir + "\"";
#endif
        system(cleanupCmd.c_str());

        if (!buildSuccess) {
            std::cerr << "Failed to build module from source" << std::endl;
            return false;
        }
    }
    
    std::string metadataFile = installDir + "/metadata.json";
    std::ofstream metaFile(metadataFile);
    if (metaFile) {
        metaFile << "{\n";
        metaFile << "  \"name\": \"" << moduleName << "\",\n";
        metaFile << "  \"version\": \"" << versionToInstall << "\",\n";
        metaFile << "  \"description\": \"" << versionMeta.description << "\",\n";
        metaFile << "  \"platform\": \"" << Platform::getOSString() << "\",\n";
        metaFile << "  \"library\": \"" << moduleName << Platform::getLibraryExtension() << "\"\n";
        metaFile << "}\n";
        metaFile.close();
    }
    
    std::cout << "✓ Installed " << moduleName << "@" << versionToInstall << " to " << installDir << std::endl;
    
    // If we are in a Neutron project (local install), register the dependency in .quark
    if (!global) {
        struct stat st;
        if (stat(".quark", &st) == 0) {
            std::cout << "Updating .quark configuration..." << std::endl;
            
            // Read .quark file
            std::ifstream inFile(".quark");
            std::vector<std::string> lines;
            std::string line;
            bool inDependencies = false;
            bool depFound = false;
            std::string depEntry = moduleName + "=" + versionToInstall;
            
            while (std::getline(inFile, line)) {
                // Trim line
                std::string trimmed = line;
                size_t first = trimmed.find_first_not_of(" \t\r\n");
                if (first != std::string::npos) {
                    size_t last = trimmed.find_last_not_of(" \t\r\n");
                    trimmed = trimmed.substr(first, (last - first + 1));
                } else {
                    trimmed = "";
                }
                
                if (trimmed == "[dependencies]") {
                    inDependencies = true;
                    lines.push_back(line);
                    continue;
                }
                
                if (trimmed.length() > 0 && trimmed[0] == '[') {
                    inDependencies = false;
                }
                
                if (inDependencies && trimmed.find(moduleName + "=") == 0) {
                    // Update existing dependency
                    lines.push_back(depEntry);
                    depFound = true;
                    std::cout << "Updated dependency: " << moduleName << " -> " << versionToInstall << std::endl;
                } else {
                    lines.push_back(line);
                }
            }
            inFile.close();
            
            // If dependency not found, add it
            if (!depFound) {
                // Check if [dependencies] section exists
                bool hasDepSection = false;
                for (const auto& l : lines) {
                    if (l.find("[dependencies]") != std::string::npos) {
                        hasDepSection = true;
                        break;
                    }
                }
                
                if (!hasDepSection) {
                    lines.push_back("");
                    lines.push_back("[dependencies]");
                }
                
                // Find where to insert (at end of dependencies section or end of file)
                if (hasDepSection) {
                    // Find end of dependencies section
                    size_t insertPos = lines.size();
                    bool inDep = false;
                    for (size_t i = 0; i < lines.size(); i++) {
                        std::string l = lines[i];
                        if (l.find("[dependencies]") != std::string::npos) {
                            inDep = true;
                            continue;
                        }
                        if (inDep && l.length() > 0 && l[0] == '[') {
                            insertPos = i; // Insert before next section
                            break;
                        }
                    }
                    
                    // Insert before insertPos
                    if (insertPos < lines.size()) {
                        lines.insert(lines.begin() + insertPos, depEntry);
                    } else {
                        lines.push_back(depEntry);
                    }
                } else {
                    lines.push_back(depEntry);
                }
                std::cout << "Added dependency: " << moduleName << " @ " << versionToInstall << std::endl;
            }
            
            // Write back to .quark
            std::ofstream outFile(".quark");
            for (const auto& l : lines) {
                outFile << l << "\n";
            }
            outFile.close();
        }
    }
    
    return true;
}

bool Installer::uninstall(const std::string& moduleName, bool global) {
    std::string installDir = getInstallDir(global);
    std::string moduleDir = installDir + "/" + moduleName;
    
    struct stat st;
    if (stat(moduleDir.c_str(), &st) != 0) {
        std::cerr << "Module not installed: " << moduleName << std::endl;
        return false;
    }
    
    std::cout << "Uninstalling " << moduleName << "..." << std::endl;
    
    try {
        std::filesystem::remove_all(moduleDir);
        std::cout << "✓ Successfully uninstalled " << moduleName << std::endl;
        return true;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Failed to uninstall " << moduleName << ": " << e.what() << std::endl;
        return false;
    }
}

bool Installer::update(const std::string& moduleName, bool global) {
    std::cout << "Updating " << moduleName << "..." << std::endl;
    if (isInstalled(moduleName, global)) {
        uninstall(moduleName, global);
    }
    return install(moduleName, global);
}

std::vector<std::string> Installer::listInstalled(bool global) {
    std::vector<std::string> modules;
    return modules;
}

bool Installer::isInstalled(const std::string& moduleName, bool global) {
    std::string installDir = getInstallDir(global);
    std::string moduleDir = installDir + "/" + moduleName;
    struct stat st;
    return stat(moduleDir.c_str(), &st) == 0;
}

} // namespace box
