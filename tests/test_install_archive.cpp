#include "install_archive.h"
#include "install_requirements.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

namespace {

void u16(std::vector<unsigned char> &bytes, unsigned value)
{
    bytes.push_back(static_cast<unsigned char>(value));
    bytes.push_back(static_cast<unsigned char>(value >> 8));
}

void u32(std::vector<unsigned char> &bytes, unsigned value)
{
    u16(bytes, value);
    u16(bytes, value >> 16);
}

void entry(std::vector<unsigned char> &archive,
           std::vector<unsigned char> &central, std::string_view name,
           std::string_view content)
{
    const unsigned crc = crc32(
        0, reinterpret_cast<const Bytef *>(content.data()), content.size());
    const unsigned local_offset = archive.size();
    u32(archive, 0x04034b50);
    u16(archive, 20);
    u16(archive, 0);
    u16(archive, 0);
    u16(archive, 0);
    u16(archive, 0);
    u32(archive, crc);
    u32(archive, content.size());
    u32(archive, content.size());
    u16(archive, name.size());
    u16(archive, 0);
    archive.insert(archive.end(), name.begin(), name.end());
    archive.insert(archive.end(), content.begin(), content.end());

    u32(central, 0x02014b50);
    u16(central, 20);
    u16(central, 20);
    u16(central, 0);
    u16(central, 0);
    u16(central, 0);
    u16(central, 0);
    u32(central, crc);
    u32(central, content.size());
    u32(central, content.size());
    u16(central, name.size());
    u16(central, 0);
    u16(central, 0);
    u16(central, 0);
    u16(central, 0);
    u32(central, 0);
    u32(central, local_offset);
    central.insert(central.end(), name.begin(), name.end());
}

void write_archive(const std::filesystem::path &path,
                   const std::vector<std::pair<std::string, std::string>> &files)
{
    std::vector<unsigned char> archive;
    std::vector<unsigned char> central;
    for (const auto &[name, content] : files)
        entry(archive, central, name, content);
    const unsigned central_offset = archive.size();
    archive.insert(archive.end(), central.begin(), central.end());
    u32(archive, 0x06054b50);
    u16(archive, 0);
    u16(archive, 0);
    u16(archive, files.size());
    u16(archive, files.size());
    u32(archive, central.size());
    u32(archive, central_offset);
    u16(archive, 0);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(archive.data()), archive.size());
}

std::string read_file(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::vector<std::pair<std::string, std::string>> complete_install(
    std::string_view directory, std::string_view executable_contents)
{
    std::vector<std::pair<std::string, std::string>> files;
    for (unsigned i = 0; i < X2_INSTALL_REQUIRED_IMAGE_COUNT; ++i) {
        const std::string name = x2_install_required_images[i];
        files.emplace_back(std::string(directory) + "/" + name,
                           name == "XMen2.exe" ? std::string(executable_contents)
                                                : "required");
    }
    for (unsigned i = 0; i < X2_INSTALL_REQUIRED_CONTENT_COUNT; ++i) {
        files.emplace_back(std::string(directory) + "/" +
                               x2_install_required_content[i],
                           "required content");
    }
    return files;
}

} // namespace

int main()
{
    const std::filesystem::path root =
        std::filesystem::current_path() / "install-archive-test";
    const std::filesystem::path config = root / "config";
    const std::filesystem::path archive = root / "selected.zip";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    setenv("XDG_CONFIG_HOME", config.string().c_str(), 1);

    char executable[4096];
    char reason[512];
    auto old_install = complete_install("Old/Sub", "old");
    old_install.emplace_back("Old/stale.txt", "stale");
    write_archive(archive, old_install);
    if (!x2_install_archive_prepare(archive.string().c_str(), executable,
                                    sizeof executable, reason, sizeof reason) ||
        read_file(executable) != "old") {
        std::cerr << "initial archive preparation failed: " << reason << "\n";
        return 1;
    }
    const std::filesystem::path accepted_root =
        std::filesystem::path(executable).parent_path().parent_path().parent_path();
    if (!std::filesystem::is_regular_file(accepted_root / "Old/stale.txt")) {
        std::cerr << "initial archive was not extracted completely\n";
        return 1;
    }

    write_archive(archive, complete_install("New/Deep", "new"));
    if (!x2_install_archive_prepare(archive.string().c_str(), executable,
                                    sizeof executable, reason, sizeof reason) ||
        read_file(executable) != "new" ||
        std::filesystem::exists(accepted_root / "Old/stale.txt")) {
        std::cerr << "replacement did not atomically discard stale files: "
                  << reason << "\n";
        return 1;
    }
    const std::filesystem::path replacement = executable;

    const std::filesystem::path staged_destination = root / "staging/.x2-prepared";
    std::filesystem::create_directories(staged_destination.parent_path());
    if (!x2_install_archive_prepare_to(archive.string().c_str(),
                                       staged_destination.string().c_str(),
                                       executable, sizeof executable,
                                       reason, sizeof reason) ||
        !std::filesystem::path(executable).string().starts_with(
            staged_destination.string()) || read_file(executable) != "new") {
        std::cerr << "private staged extraction failed: " << reason << "\n";
        return 1;
    }

    write_archive(archive, {{"Broken/XMen2.exe", "invalid"}});
    if (x2_install_archive_prepare(archive.string().c_str(), executable,
                                   sizeof executable, reason, sizeof reason) ||
        read_file(replacement) != "new" ||
        std::filesystem::exists(accepted_root.string() + ".preparing")) {
        std::cerr << "invalid replacement damaged the accepted install: "
                  << reason << "\n";
        return 1;
    }

    std::filesystem::remove_all(root);
    std::cout << "install_archive: replacement is atomic and invalid ZIP "
                 "preserves the accepted install\n";
    return 0;
}
