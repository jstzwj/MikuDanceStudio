#include "mikudancestudio/pmm_document.hpp"
#include <algorithm>
#include <utility>
namespace mikudancestudio::format {
namespace {
constexpr std::size_t kMaxBytes = 512u << 20;
struct Cursor {
    ReadPmmBytesResult read;
    void* context;
    std::size_t consumed = 0;
    std::vector<std::uint8_t> prefix;
    static bool exact(void* opaque, void* destination, std::size_t length) {
        auto& cursor = *static_cast<Cursor*>(opaque);
        if (length > kMaxBytes - cursor.consumed)
            return false;
        auto* bytes = static_cast<std::uint8_t*>(destination);
        std::size_t complete = 0;
        while (complete < length) {
            const int result = cursor.read(cursor.context, bytes + complete, length - complete);
            if (result <= 0 || static_cast<std::size_t>(result) > length - complete)
                return false;
            cursor.prefix.insert(cursor.prefix.end(), bytes + complete, bytes + complete + result);
            cursor.consumed += static_cast<std::size_t>(result);
            complete += static_cast<std::size_t>(result);
        }
        return true;
    }
};
}
bool ReadPmmDocument(ReadPmmBytesResult read, void* context, PmmDocument& document) {
    if (!read)
        return false;
    Cursor cursor{read, context};
    PmmDocument parsed;
    if (ReadPmmHeader(Cursor::exact, &cursor, parsed.header) != PmmHeaderStatus::Ok ||
        !ReadPmmSceneSettings(Cursor::exact, &cursor, parsed.header.version, parsed.settings) ||
        !Cursor::exact(&cursor, &parsed.selectedModelSlot, 1) ||
        !Cursor::exact(&cursor, &parsed.modelCount, 1))
        return false;
    if (parsed.header.version == PmmVersion::V1) {
        parsed.v1ModelNames.resize(parsed.modelCount);
        for (auto& name : parsed.v1ModelNames)
            if (!Cursor::exact(&cursor, name.data(), name.size()))
                return false;
    }
    parsed.prefixBytes = std::move(cursor.prefix);
    std::array<std::uint8_t, 4096> block{};
    while (cursor.consumed < kMaxBytes) {
        const auto length = std::min(block.size(), kMaxBytes - cursor.consumed);
        const int received = read(context, block.data(), length);
        if (received < 0 || received > static_cast<int>(length))
            return false;
        if (received == 0) {
            document = std::move(parsed);
            return true;
        }
        parsed.remainingBytes.insert(parsed.remainingBytes.end(), block.begin(),
                                     block.begin() + received);
        cursor.consumed += static_cast<std::size_t>(received);
    }
    if (read(context, block.data(), 1) != 0)
        return false;
    document = std::move(parsed);
    return true;
}
bool WritePmmDocument(WritePmmBytes write, void* context, const PmmDocument& document) {
    const auto prefixSize =
        30u + (document.header.version == PmmVersion::V1 ? 22u : 23u) + 2u +
        (document.header.version == PmmVersion::V1 ? 20u * document.modelCount : 0u);
    if (!write || document.prefixBytes.size() != prefixSize ||
        document.remainingBytes.size() > kMaxBytes - prefixSize)
        return false;
    return write(context, document.prefixBytes.data(), document.prefixBytes.size()) &&
           (document.remainingBytes.empty() ||
            write(context, document.remainingBytes.data(), document.remainingBytes.size()));
}
}
