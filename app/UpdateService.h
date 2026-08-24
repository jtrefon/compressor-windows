#pragma once

// In-app auto-update via the GitHub Releases API - the repository itself is
// the update server (no dedicated infrastructure needed):
//   https://api.github.com/repos/jtrefon/compressor-windows/releases/latest
//
// The release pipeline already publishes versioned installers; this service
// discovers the newest one, compares it against the running build, and hands
// the user the download link.

#include <winrt/Windows.Foundation.h>

#include <functional>
#include <string>

namespace updates {

// Bump with every release tag (kept in sync with docs/RELEASING.md).
inline constexpr wchar_t kAppVersion[] = L"0.1.10";

struct UpdateInfo {
  bool available = false;
  std::wstring version;      // e.g. "0.1.6"
  std::wstring notes;        // release body
  std::wstring downloadUrl;  // direct installer asset URL
  std::wstring pageUrl;      // release page URL
};

// Compares semantic versions "vX.Y.Z"/"X.Y.Z". Returns true when candidate is
// strictly newer than current.
bool IsNewerVersion(const std::wstring &current, const std::wstring &candidate);

// Queries the GitHub Releases API for the latest release; fills @p out when a
// newer release exists. Never throws on network/parse failures - returns
// false instead.
winrt::Windows::Foundation::IAsyncOperation<bool> CheckForUpdate(UpdateInfo &out);

// Downloads the installer asset to @p destPath, reporting progress as
// (bytesDone, bytesTotal). Returns false on HTTP or IO failures.
winrt::Windows::Foundation::IAsyncOperation<bool> DownloadInstaller(
    const std::wstring &url, const std::wstring &destPath,
    std::function<void(uint64_t, uint64_t)> progress);

} // namespace updates