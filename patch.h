/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef ROMHACKPLAZA_PATCH_H
#define ROMHACKPLAZA_PATCH_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace romhackplaza {
using Bytes = std::vector<uint8_t>;
using PatchProgress = std::function<bool(size_t, size_t)>;
#ifndef ROMHACKPLAZA_MAX_ROM_SIZE
#define ROMHACKPLAZA_MAX_ROM_SIZE (64 * 1024 * 1024)
#endif
constexpr size_t MaxRomSize = ROMHACKPLAZA_MAX_ROM_SIZE;
// Returning false from progress cancels the operation. IPS requires an externally verified base.
bool ApplyPatch(const Bytes& base, const Bytes& patch, Bytes& output, std::string& error,
                bool baseVerified = false, PatchProgress progress = PatchProgress());
// Raw patches or a ZIP containing exactly one IPS, BPS or UPS patch are accepted.
bool ExtractPatch(const Bytes& download, Bytes& patch, std::string& error,
                  PatchProgress progress = PatchProgress());
// Original is opened read-only. Existing destination files are never replaced.
bool InstallPatch(const std::string& original, const Bytes& download,
                  const std::string& directory, const std::string& title, uint64_t entryId,
                  uint32_t expectedCRC32, std::string& installed, std::string& error,
                  PatchProgress progress = PatchProgress());
}
#endif
