#include "mock_fs.hpp"
#include <iostream>
#include <filesystem>

int main() {
  std::cout << "Testing path resolution...\n";
  std::filesystem::path root = std::filesystem::absolute("./webui_data");
  std::cout << "Root: " << root << "\n";

  std::string vpath = "/";
  std::string pathStr = vpath;
  if (!pathStr.empty() && pathStr[0] == '/') {
    pathStr = pathStr.substr(1);
  }
  std::cout << "Path after strip: '" << pathStr << "'\n";

  std::filesystem::path realPath = root / pathStr;
  std::cout << "Combined: " << realPath << "\n";

  realPath = std::filesystem::weakly_canonical(realPath);
  std::cout << "Canonical: " << realPath << "\n";
  std::cout << "Exists: " << std::filesystem::exists(realPath) << "\n";
  std::cout << "Is dir: " << std::filesystem::is_directory(realPath) << "\n";

  MockFilesystem fs("./webui_data");

  std::cout << "\nTesting MockFilesystem...\n";

  // Test listing root
  std::vector<HAL::FileInfo> files;
  bool success = fs.listDirectory("/", files);

  std::cout << "listDirectory('/') returned: " << (success ? "SUCCESS" : "FAILURE") << "\n";
  std::cout << "Found " << files.size() << " files\n";

  for (const auto& file : files) {
    std::cout << "  - " << file.name << " (" << (file.isDirectory ? "dir" : "file") << ")\n";
  }

  return 0;
}
