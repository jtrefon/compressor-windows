#include "pch.h"
#include "UpdateService.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Web.Http.h>

#include <vector>

namespace updates {

bool IsNewerVersion(const std::wstring &current, const std::wstring &candidate) {
  auto parse = [](const std::wstring &s) {
    std::vector<int> parts;
    std::size_t start = 0;
    for (int i = 0; i < 3; ++i) {
      while (start < s.size() && !isdigit(static_cast<unsigned char>(s[start]))) {
        ++start;
      }
      const auto end = s.find(L'.', start);
      const auto token = (end == std::wstring::npos) ? s.substr(start) : s.substr(start, end - start);
      parts.push_back(token.empty() ? 0 : _wtoi(token.c_str()));
      if (end == std::wstring::npos) {
        break;
      }
      start = end + 1;
    }
    while (parts.size() < 3) {
      parts.push_back(0);
    }
    return parts;
  };
  const auto a = parse(current);
  const auto b = parse(candidate);
  for (int i = 0; i < 3; ++i) {
    if (a[i] != b[i]) {
      return b[i] > a[i];
    }
  }
  return false;
}

winrt::Windows::Foundation::IAsyncOperation<UpdateInfo> CheckForUpdate() {
  UpdateInfo info;
  try {
    winrt::Windows::Web::Http::HttpClient client;
    client.DefaultRequestHeaders().UserAgent().ParseAdd(L"CompressorWindows");
    const winrt::Windows::Foundation::Uri uri(
        L"https://api.github.com/repos/jtrefon/compressor-windows/releases/latest");
    const auto response = co_await client.GetAsync(uri);
    if (!response.IsSuccessStatusCode()) {
      co_return info;
    }
    const auto body = co_await response.Content().ReadAsStringAsync();
    const auto root = winrt::Windows::Data::Json::JsonObject::Parse(body);
    const std::wstring tag = root.GetNamedString(L"tag_name", L"");
    const std::wstring page = root.GetNamedString(L"html_url", L"");
    const std::wstring notes = root.GetNamedString(L"body", L"");
    if (tag.empty() || !IsNewerVersion(kAppVersion, tag)) {
      co_return info;
    }
    std::wstring downloadUrl;
    if (root.HasKey(L"assets")) {
      const auto assets = root.GetNamedArray(L"assets");
      for (const auto &asset : assets) {
        const auto obj = asset.GetObject();
        const std::wstring name = obj.GetNamedString(L"name", L"");
        if (name.find(L"-setup.exe") != std::wstring::npos) {
          downloadUrl = obj.GetNamedString(L"browser_download_url", L"");
          break;
        }
      }
    }
    info.available = true;
    info.version = tag;
    info.notes = notes;
    info.downloadUrl = downloadUrl;
    info.pageUrl = page;
  } catch (...) {
    // Network/parse failures are non-fatal: report no update.
  }
  co_return info;
}

} // namespace updates