#include "../src/app/frame_state_dump.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>

namespace mikudancestudio {
MMDApp::MMDApp() : state{} {
}
}

namespace {
std::string Address(const void* pointer) {
    std::ostringstream text;
    text << std::uppercase << std::hex << std::setfill('0') << std::setw(sizeof(void*) * 2)
         << reinterpret_cast<std::uintptr_t>(pointer);
    return text.str();
}
}

int main() {
    namespace mds = mikudancestudio;
    char temporary[MAX_PATH]{}, directory[MAX_PATH]{};
    if (!GetTempPathA(MAX_PATH, temporary) || !GetTempFileNameA(temporary, "mds", 0, directory))
        return 1;
    DeleteFileA(directory);
    if (!CreateDirectoryA(directory, nullptr))
        return 1;
    char previous[MAX_PATH]{};
    GetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", previous, sizeof(previous));
    SetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", directory);
    auto app = std::make_unique<mds::MMDApp>();
    std::array<mds::mdl::DisplayClipboardRecord, 2> records{};
    std::array<mds::mdl::IkClipboardState, 3> ik{};
    std::array<mds::mdl::SelectorClipboardState, 5> selectors{};
    records[0].frame = 101;
    records[0].visible = 1;
    records[0].ikCount = 1;
    records[0].ikStates = &ik[0];
    records[0].selectorCount = 2;
    records[0].selectorStates = &selectors[0];
    records[1].frame = 203;
    records[1].visible = 0;
    records[1].ikCount = 2;
    records[1].ikStates = &ik[1];
    records[1].selectorCount = 3;
    records[1].selectorStates = &selectors[2];
    app->DisplayClipboard() = records.data();
    app->ClipboardCounts().displays = static_cast<std::uint32_t>(records.size());
    const std::string path = std::string(directory) + "\\layout.json";
    const auto readDump = [&] {
        mds::DumpFrameEntryState(app.get(), "layout.json", false);
        std::ifstream file(path);
        return std::string(std::istreambuf_iterator<char>(file), {});
    };
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::fprintf(stderr, "%s\n", message);
            ++failures;
        }
    };
    const auto text = readDump();
    check(text.find("\"clipboard.model_display.ptr\": \"" + Address(records.data()) + "\"") !=
              std::string::npos,
          "Clipboard pointer keeps its native width");
    // Independent field values and two adjacent records detect both the old
    // 24-byte stride and the old +12/+20 pointer offsets on x64.
    std::ostringstream expected;
    expected << "\"clipboard.model_display.records\": ["
             << "{\"frame\":101,\"view\":1,\"ik_count\":1,\"ik_ptr\":\"" << Address(&ik[0])
             << "\",\"relation_count\":2,\"relation_ptr\":\"" << Address(&selectors[0]) << "\"}, "
             << "{\"frame\":203,\"view\":0,\"ik_count\":2,\"ik_ptr\":\"" << Address(&ik[1])
             << "\",\"relation_count\":3,\"relation_ptr\":\"" << Address(&selectors[2]) << "\"}]";
    check(text.find(expected.str()) != std::string::npos,
          "Dump reports both complete native clipboard records");
    app->DisplayClipboard() = nullptr;
    check(readDump().find("\"clipboard.model_display.records\": []") != std::string::npos,
          "Null clipboard does not read records");
    app->DisplayClipboard() = records.data();
    app->ClipboardCounts().displays = 10001;
    check(readDump().find("\"clipboard.model_display.records\": []") != std::string::npos,
          "Diagnostic count guard avoids reading past the buffer");
    app->ClipboardCounts().displays = 0;
    check(readDump().find("\"clipboard.model_display.records\": []") != std::string::npos,
          "Zero clipboard count produces no records");
    app->DisplayClipboard() = nullptr;
    SetEnvironmentVariableA("MIKUDANCESTUDIO_STATE_DUMP_DIR", previous[0] ? previous : nullptr);
    DeleteFileA(path.c_str());
    RemoveDirectoryA(directory);
    return failures == 0 ? 0 : 1;
}
