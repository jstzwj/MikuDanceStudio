#include "mikudancestudio/accessory_resource.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
#include <cstring>
using namespace mikudancestudio::runtime;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " reason=" << reason << '\n';        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
template <class T> void Put(std::string& bytes, T value) {
    bytes.append(reinterpret_cast<const char*>(&value), sizeof value);
}
int main(int argc, char** argv) {
    CHECK(argc == 2);
    for (const char* filename :
         {"bound.x", "bound-varied.x", "bound-tzip.x", "bound-tzip-multi.x"}) {
        std::ifstream file(std::filesystem::path(argv[1]) / filename, std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(file), {}};
        std::shared_ptr<const AccessoryResource> resource;
        CHECK(AccessoryResource::Parse(bytes.data(), bytes.size(), resource, reason));
        CHECK(resource->Vertices().size() == 4 && resource->Indices().size() == 6);
        CHECK(resource->Materials().size() == 2 && resource->Subsets().size() == 2);
        CHECK(resource->Materials()[0].texture == "checker.bmp");
        CHECK(resource->Materials()[0].diffuse[3] == .8f &&
              resource->Materials()[0].ambient[0] == .7f);
        const auto saved = resource;
        CHECK(!AccessoryResource::Parse(bytes.data(), bytes.size() - 3, resource, reason) &&
              resource == saved);
        if (bytes.substr(8, 4) == "tzip") {
            for (std::size_t cut = 16; cut < bytes.size(); ++cut)
                CHECK(!AccessoryResource::Parse(bytes.data(), cut, resource, reason) &&
                      resource == saved);
            bytes[16] = 0;
            bytes[17] = 0;
            bytes[18] = 0;
            bytes[19] = 0;
            CHECK(!AccessoryResource::Parse(bytes.data(), bytes.size(), resource, reason) &&
                  resource == saved && reason == "x-decompressed-size-limit");
        }
    }
    // Standard binary tokens with integer/float lists, independent of text parsing.
    std::string binary = "xof 0303bin 0032";
    const auto word = [&](const char* s) {
        Put<std::uint16_t>(binary, 1);
        Put<std::uint32_t>(binary, std::uint32_t(std::strlen(s)));
        binary += s;
    };
    const auto open = [&](const char* s) {
        word(s);
        Put<std::uint16_t>(binary, 10);
    };
    const auto close = [&] { Put<std::uint16_t>(binary, 11); };
    const auto ints = [&](std::initializer_list<unsigned> values) {
        Put<std::uint16_t>(binary, 6);
        Put<std::uint32_t>(binary, unsigned(values.size()));
        for (auto v : values)
            Put<std::uint32_t>(binary, v);
    };
    const auto floats = [&](std::initializer_list<float> values) {
        Put<std::uint16_t>(binary, 7);
        Put<std::uint32_t>(binary, unsigned(values.size()));
        for (auto v : values)
            Put<float>(binary, v);
    };
    open("Mesh");
    ints({3});
    floats({0, 0, 0, 1, 0, 0, 0, 1, 0});
    ints({1, 3, 0, 1, 2});
    open("MeshNormals");
    ints({1});
    floats({0, 0, 1});
    ints({1, 3, 0, 0, 0});
    close();
    open("MeshMaterialList");
    ints({1, 1, 0});
    open("Material");
    floats({1, 1, 1, 1, 4, 0, 0, 0, 0, 0, 0});
    close();
    close();
    close();
    std::shared_ptr<const AccessoryResource> resource;
    CHECK(AccessoryResource::Parse(binary.data(), binary.size(), resource, reason));
    CHECK(resource->Vertices().size() == 3 && resource->Indices().size() == 3 &&
          resource->Vertices()[2].normal[2] == 1);
    std::ifstream compressed(std::filesystem::path(argv[1]) / "triangle-bzip.x", std::ios::binary);
    std::string zipped{std::istreambuf_iterator<char>(compressed), {}};
    CHECK(AccessoryResource::Parse(zipped.data(), zipped.size(), resource, reason));
    CHECK(resource->Vertices().size() == 3 && resource->Indices().size() == 3);
    zipped[24] = 'X';
    CHECK(!AccessoryResource::Parse(zipped.data(), zipped.size(), resource, reason) &&
          reason == "x-invalid-mszip-magic");
    std::cout
        << "portable static X text/binary/MSZIP import, material groups, bounds and transaction passed\n";
}
