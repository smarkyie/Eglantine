// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/file_sys/vfs/vfs_vector.h"
#include "core/loader/loader.h"

namespace {

using Bytes = std::vector<u8>;

void PutLe(Bytes& bytes, std::size_t offset, u64 value, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        bytes[offset + i] = static_cast<u8>(value >> (i * 8));
    }
}

void PutMagic(Bytes& bytes, std::size_t offset, const std::string& magic) {
    std::copy(magic.begin(), magic.end(), bytes.begin() + offset);
}

Bytes Npdm(bool switch2) {
    Bytes bytes(0x500);
    PutMagic(bytes, 0, "META");
    PutLe(bytes, 0x70, 0x80, 4);
    PutLe(bytes, 0x74, 0x80, 4);
    PutLe(bytes, 0x78, 0x100, 4);
    PutLe(bytes, 0x7c, switch2 ? 0x380 : 0x280, 4);
    PutMagic(bytes, 0x80, "ACI0");
    PutLe(bytes, 0xa0, 0x40, 4);
    PutLe(bytes, 0xa4, 0x1c, 4);
    const std::size_t acid_body = switch2 ? 0x400 : 0x300;
    PutMagic(bytes, acid_body, "ACID");
    PutLe(bytes, acid_body + 0x20, switch2 ? 0x340 : 0x240, 4);
    PutLe(bytes, acid_body + 0x24, 0x2c, 4);
    return bytes;
}

FileSys::VirtualFile File(Bytes bytes, std::string name,
                          FileSys::VirtualDir parent = nullptr) {
    return std::make_shared<FileSys::VectorVfsFile>(std::move(bytes), std::move(name),
                                                  std::move(parent));
}

Bytes Pfs(const std::vector<std::pair<std::string, Bytes>>& files) {
    std::size_t strings_size{};
    for (const auto& [name, data] : files) {
        strings_size += name.size() + 1;
    }
    const std::size_t strings = 0x10 + 0x18 * files.size();
    const std::size_t content = strings + strings_size;
    Bytes bytes(content);
    PutMagic(bytes, 0, "PFS0");
    PutLe(bytes, 4, files.size(), 4);
    PutLe(bytes, 8, strings_size, 4);
    std::size_t name_offset{};
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto& [name, data] = files[i];
        PutLe(bytes, 0x10 + i * 0x18, bytes.size() - content, 8);
        PutLe(bytes, 0x18 + i * 0x18, data.size(), 8);
        PutLe(bytes, 0x20 + i * 0x18, name_offset, 4);
        std::copy(name.begin(), name.end(), bytes.begin() + strings + name_offset);
        name_offset += name.size() + 1;
        bytes.insert(bytes.end(), data.begin(), data.end());
    }
    return bytes;
}

FileSys::VirtualDir Companion(bool switch2) {
    return std::make_shared<FileSys::VectorVfsDirectory>(
        std::vector<FileSys::VirtualFile>{File(Npdm(switch2), "main.npdm")});
}

} // namespace

TEST_CASE("Switch 2 detection preserves loose ExeFS", "[loader][switch2]") {
    CHECK_FALSE(Loader::IsSwitch2Program(nullptr));
    CHECK_FALSE(Loader::IsSwitch2Program(File({}, "main")));
    CHECK_FALSE(Loader::IsSwitch2Program(File({}, "main", Companion(false))));
    CHECK(Loader::IsSwitch2Program(File({}, "main", Companion(true))));
}

TEST_CASE("Switch 2 detection reads an extracted-type NSP without a companion",
          "[loader][switch2]") {
    CHECK(Loader::IsSwitch2Program(
        File(Pfs({{"main", {}}, {"main.npdm", Npdm(true)}}), "game.nsp")));
    CHECK_FALSE(Loader::IsSwitch2Program(
        File(Pfs({{"main", {}}, {"main.npdm", Npdm(false)}}), "game.nsp")));
}

TEST_CASE("Embedded NSP metadata takes precedence over a neighboring NPDM",
          "[loader][switch2]") {
    CHECK_FALSE(Loader::IsSwitch2Program(File(
        Pfs({{"main", {}}, {"main.npdm", Npdm(false)}}), "game.nsp", Companion(true))));
    CHECK(Loader::IsSwitch2Program(File(
        Pfs({{"main", {}}, {"main.npdm", Npdm(true)}}), "game.nsp", Companion(false))));
}

TEST_CASE("Unreadable metadata and non-program NSPs do not select Switch 2",
          "[loader][switch2]") {
    CHECK_FALSE(Loader::IsSwitch2Program(
        File(Pfs({{"main", {}}, {"main.npdm", Bytes(16)}}), "game.nsp")));
    CHECK_FALSE(Loader::IsSwitch2Program(File(Pfs({{"content.dat", {1, 2, 3}}}), "dlc.nsp")));
    CHECK_FALSE(Loader::IsSwitch2Program(File(Bytes{'P', 'F', 'S', '0'}, "truncated.nsp")));
}
