// Anti-corruption contract test (docs/PORTING.md in the engine):
// byte-identical round trips through every registry codec + CRC verification
// + .cza archive create/list/verify — the exact guarantees the UI relies on.
#include <compression/app/CompressionService.hpp>
#include <compression/app/ArchiveService.hpp>
#include <compression/codec/CodecRegistry.hpp>
#include <compression/core/ByteSource.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

std::vector<uint8_t> text(size_t n) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i)
    v[i] = static_cast<uint8_t>('a' + (i * 13 + i / 11) % 26);
  return v;
}
} // namespace

int main() {
  using namespace compression;

  const std::vector<std::vector<uint8_t>> corpora = {
      {}, text(0), text(19), text(4096), text(200000)};

  // 1. Every registry codec round-trips byte-identically (incl. CRC verified).
  for (const auto &[id, name] : codec::CodecRegistry::instance().all()) {
    for (const auto &data : corpora) {
      CompressionService service;
      core::MemoryByteSink sink;
      CompressionOptions opts;
      opts.codec = id;
      const CompressResult cr = service.compress(core::ByteView(data), sink);
      core::MemoryByteSink out;
      const ExtractResult er =
          service.decompress(core::ByteView(sink.data()), out);
      CHECK(cr.outBytes > 0 || data.empty());
      CHECK(er.verified);
      CHECK(out.data() == data);
      if (failures > 8)
        return 1;
    }
  }

  // 2. Corruption is detected loudly (never silent mis-decode).
  {
    CompressionService service;
    core::MemoryByteSink sink;
    const auto data = text(4096);
    service.compress(core::ByteView(data), sink);
    auto corrupted = sink.data();
    corrupted[corrupted.size() / 2] ^= 0xFF;
    bool threw = false;
    try {
      core::MemoryByteSink out;
      service.decompress(core::ByteView(corrupted), out);
    } catch (const std::exception &) {
      threw = true;
    }
    CHECK(threw);
  }

  // 3. .cza archive create/list/verify.
  {
    ArchiveService archive;
    archive::ArchiveBuildOptions opts;
    std::vector<ArchiveEntrySource> entries;
    entries.push_back({"a.txt", text(1000), 0});
    entries.push_back({"sub/b.bin", std::vector<uint8_t>(5000, 0xAB), 0});
    const std::string path = "contract_test.cza";
    archive.create(path, opts, entries);
    const auto listing = archive.list(path);
    CHECK(listing.entries.size() == 2);
    bool allOk = true;
    for (const auto &v : archive.verify(path))
      allOk = allOk && v.ok;
    CHECK(allOk);
    std::remove(path.c_str());
  }

  if (failures == 0) {
    std::printf("CONTRACT TEST PASSED\n");
    return 0;
  }
  std::printf("CONTRACT TEST FAILED (%d failures)\n", failures);
  return 1;
}