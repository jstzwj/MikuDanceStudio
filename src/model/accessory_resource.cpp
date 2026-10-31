#include "mikudancestudio/accessory_resource.hpp"
#include "mikudancestudio/text_encoding.hpp"
#include "x_decompress.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace mikudancestudio::runtime {
namespace {
constexpr std::size_t Limit = 1000000;
void Require(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}
struct Token {
    enum Kind {
        Word,
        String,
        Number,
        Open,
        Close,
        Separator,
        End
    } kind = End;
    std::string text;
    double number = 0;
};
struct Lexer {
    const unsigned char* bytes;
    std::size_t size, at = 16;
    bool binary = false, wide = false;
    std::size_t list = 0;
    unsigned listType = 0;
    template <class T> T Get() {
        Require(at <= size && sizeof(T) <= size - at, "x-truncated-token");
        T value;
        std::memcpy(&value, bytes + at, sizeof value);
        at += sizeof value;
        return value;
    }
    Token Next() {
        while (binary) {
            if (list) {
                --list;
                return {Token::Number,
                        {},
                        listType == 6 ? double(Get<std::uint32_t>())
                        : wide        ? Get<double>()
                                      : double(Get<float>())};
            }
            if (at == size)
                return {};
            const auto token = Get<std::uint16_t>();
            if (token == 1 || token == 2) {
                const auto length = Get<std::uint32_t>();
                Require(length <= size - at && length <= Limit, "x-token-string-size");
                std::string text(reinterpret_cast<const char*>(bytes + at), length);
                at += length;
                if (!text.empty() && !text.back())
                    text.pop_back();
                Require(text.find('\0') == std::string::npos, "x-embedded-nul");
                return {token == 1 ? Token::Word : Token::String, std::move(text)};
            }
            if (token == 3)
                return {Token::Number, {}, double(Get<std::uint32_t>())};
            if (token == 6 || token == 7) {
                list = Get<std::uint32_t>();
                Require(list <= Limit, "x-token-list-limit");
                listType = token;
                continue;
            }
            if (token == 10)
                return {Token::Open};
            if (token == 11)
                return {Token::Close};
            if (token == 19 || token == 20)
                return {Token::Separator};
            if (token == 31)
                return {Token::Word, "template"};
            if (token == 5) {
                Require(size - at >= 16, "x-truncated-guid");
                at += 16;
                return {Token::Word, "guid"};
            }
            // Template declarations are skipped by the parser; their primitive tokens have no payload.
            if ((token >= 12 && token <= 18) || (token >= 40 && token <= 52))
                return {Token::Word, "schema"};
            throw std::runtime_error("x-unsupported-binary-token:" + std::to_string(token));
        }
        for (;;) {
            while (at < size && (bytes[at] == ' ' || bytes[at] == '\t' || bytes[at] == '\r' ||
                                 bytes[at] == '\n'))
                ++at;
            if (at >= size)
                return {};
            if (bytes[at] == '#' || (at + 1 < size && bytes[at] == '/' && bytes[at + 1] == '/')) {
                while (at < size && bytes[at] != '\n')
                    ++at;
                continue;
            }
            if (at + 1 < size && bytes[at] == '/' && bytes[at + 1] == '*') {
                at += 2;
                while (at + 1 < size && !(bytes[at] == '*' && bytes[at + 1] == '/'))
                    ++at;
                Require(at + 1 < size, "x-unclosed-comment");
                at += 2;
                continue;
            }
            break;
        }
        const char c = char(bytes[at++]);
        if (c == '{')
            return {Token::Open};
        if (c == '}')
            return {Token::Close};
        if (c == ';' || c == ',')
            return {Token::Separator};
        if (c == '"') {
            std::string text;
            while (at < size && bytes[at] != '"') {
                char ch = char(bytes[at++]);
                if (ch == '\\' && at < size && (bytes[at] == '\\' || bytes[at] == '"'))
                    ch = char(bytes[at++]);
                Require(ch != 0, "x-embedded-nul");
                text += ch;
            }
            Require(at < size, "x-unclosed-string");
            ++at;
            return {Token::String, std::move(text)};
        }
        const auto begin = at - 1;
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            while (at < size &&
                   ((bytes[at] >= '0' && bytes[at] <= '9') || bytes[at] == '.' ||
                    bytes[at] == 'e' || bytes[at] == 'E' || bytes[at] == '+' || bytes[at] == '-'))
                ++at;
            const auto* first = reinterpret_cast<const char*>(bytes + begin);
            if (*first == '+')
                ++first;
            double value;
            // C++17 libc++ toolchains do not all provide floating from_chars.
            // A private classic locale keeps decimal parsing independent of
            // both the user's locale and the host standard-library version.
            std::istringstream number(
                std::string(first, reinterpret_cast<const char*>(bytes + at)));
            number.imbue(std::locale::classic());
            number >> value;
            Require(number && number.peek() == std::char_traits<char>::eof() &&
                        std::isfinite(value),
                    "x-invalid-number");
            return {Token::Number, {}, value};
        }
        while (at < size && bytes[at] != '{' && bytes[at] != '}' && bytes[at] != ';' &&
               bytes[at] != ',' && bytes[at] != ' ' && bytes[at] != '\t' && bytes[at] != '\r' &&
               bytes[at] != '\n')
            ++at;
        return {Token::Word, std::string(reinterpret_cast<const char*>(bytes + begin), at - begin)};
    }
};
struct Node {
    std::string type, name;
    std::vector<Token> values;
    std::vector<Node> children;
};
struct Parser {
    Lexer lexer;
    Token current;
    std::size_t nodes = 0;
    void Next() { current = lexer.Next(); }
    Node Object(unsigned depth = 0) {
        Require(depth < 32 && ++nodes <= Limit, "x-object-limit");
        Node node;
        Require(current.kind == Token::Word, "x-object-type");
        node.type = current.text;
        Next();
        if (current.kind == Token::Word) {
            node.name = current.text;
            Next();
        }
        Require(current.kind == Token::Open, "x-object-open");
        Next();
        while (current.kind != Token::Close) {
            Require(current.kind != Token::End, "x-unclosed-object");
            if (current.kind == Token::Separator) {
                Next();
                continue;
            }
            if (current.kind == Token::Word) {
                node.children.push_back(Object(depth + 1));
                continue;
            }
            if (current.kind == Token::Open) {
                Next();
                Require(current.kind == Token::Word, "x-reference-name");
                Node ref;
                ref.type = "reference";
                ref.name = current.text;
                Next();
                Require(current.kind == Token::Close, "x-reference-close");
                Next();
                node.children.push_back(std::move(ref));
                continue;
            }
            Require(current.kind == Token::Number || current.kind == Token::String,
                    "x-object-value");
            Require(node.values.size() < Limit * 4, "x-value-limit");
            node.values.push_back(std::move(current));
            Next();
        }
        Next();
        return node;
    }
    std::vector<Node> Parse() {
        std::vector<Node> roots;
        Next();
        while (current.kind != Token::End) {
            if (current.kind == Token::Separator) {
                Next();
                continue;
            }
            if (current.kind == Token::Word && current.text == "template") {
                do {
                    Next();
                    Require(current.kind != Token::End, "x-truncated-template");
                } while (current.kind != Token::Open);
                int depth = 1;
                while (depth) {
                    Next();
                    Require(current.kind != Token::End, "x-truncated-template");
                    if (current.kind == Token::Open) {
                        ++depth;
                        Require(depth < 32, "x-template-depth");
                    } else if (current.kind == Token::Close)
                        --depth;
                }
                Next();
                continue;
            }
            roots.push_back(Object());
        }
        return roots;
    }
};
struct Data {
    const Node& node;
    std::size_t at = 0;
    double Number() {
        Require(at < node.values.size() && node.values[at].kind == Token::Number,
                "x-numeric-payload");
        return node.values[at++].number;
    }
    unsigned Count() {
        const auto v = Number();
        Require(v >= 0 && v <= Limit && v == std::floor(v), "x-count-limit");
        return unsigned(v);
    }
    float Float() {
        const auto v = float(Number());
        Require(std::isfinite(v), "x-nonfinite-payload");
        return v;
    }
    void End() { Require(at == node.values.size(), "x-payload-size"); }
};
}
bool AccessoryResource::Parse(const void* input, std::size_t size,
                              std::shared_ptr<const AccessoryResource>& output,
                              std::string& reason) {
    try {
        Require(input && size >= 16 && size <= 64 * 1024 * 1024, "x-file-size");
        const auto* bytes = static_cast<const unsigned char*>(input);
        std::vector<unsigned char> decompressed;
        if (std::memcmp(bytes + 8, "tzip", 4) == 0 || std::memcmp(bytes + 8, "bzip", 4) == 0) {
            if (!DecompressX(bytes, size, decompressed, reason))
                return false;
            bytes = decompressed.data();
            size = decompressed.size();
        }
        const std::string_view header(reinterpret_cast<const char*>(bytes), 16);
        Require(header.substr(0, 4) == "xof " &&
                    (header.substr(4, 4) == "0302" || header.substr(4, 4) == "0303"),
                "x-header-version");
        Require(header.substr(8, 4) == "txt " || header.substr(8, 4) == "bin ",
                "x-unsupported-compression");
        Require(header.substr(12, 4) == "0032" || header.substr(12, 4) == "0064", "x-float-width");
        Parser parser{
            {bytes, size, 16, header.substr(8, 4) == "bin ", header.substr(12, 4) == "0064"}};
        const auto roots = parser.Parse();
        const Node* mesh = nullptr;
        std::map<std::string, const Node*> named;
        for (const auto& node : roots) {
            if (node.type == "Header")
                continue;
            if (node.type == "Material") {
                Require(!node.name.empty() && named.emplace(node.name, &node).second,
                        "x-material-name");
                continue;
            }
            Require(node.type == "Mesh" && !mesh, "x-only-one-static-mesh-supported");
            mesh = &node;
        }
        Require(mesh, "x-mesh-missing");
        auto next = std::shared_ptr<AccessoryResource>(new AccessoryResource);
        std::vector<cpu_math::Vec3> positions, normals;
        std::vector<std::array<float, 2>> uvs;
        std::vector<std::array<unsigned, 3>> faces, normalFaces;
        std::vector<unsigned> materials;
        const auto readFaces = [](Data& data, std::size_t count,
                                  std::vector<std::array<unsigned, 3>>& out) {
            out.resize(data.Count());
            for (auto& face : out) {
                Require(data.Count() == 3, "x-nontriangle-face-not-supported");
                for (auto& v : face) {
                    v = data.Count();
                    Require(v < count, "x-vertex-index");
                }
            }
        };
        {
            Data data{*mesh};
            positions.resize(data.Count());
            Require(!positions.empty(), "x-empty-mesh");
            for (auto& p : positions)
                for (auto& v : p)
                    v = data.Float();
            readFaces(data, positions.size(), faces);
            data.End();
        }
        bool hasMaterials = false;
        for (const auto& node : mesh->children) {
            Data data{node};
            if (node.type == "MeshNormals") {
                Require(normals.empty(), "x-duplicate-normals");
                normals.resize(data.Count());
                for (auto& p : normals)
                    for (auto& v : p)
                        v = data.Float();
                readFaces(data, normals.size(), normalFaces);
                Require(normalFaces.size() == faces.size(), "x-normal-faces");
                data.End();
            } else if (node.type == "MeshTextureCoords") {
                Require(uvs.empty(), "x-duplicate-uvs");
                uvs.resize(data.Count());
                Require(uvs.size() == positions.size(), "x-uv-count");
                for (auto& uv : uvs)
                    for (auto& v : uv)
                        v = data.Float();
                data.End();
            } else if (node.type == "MeshMaterialList") {
                Require(!hasMaterials, "x-duplicate-materials");
                hasMaterials = true;
                const auto count = data.Count();
                materials.resize(data.Count());
                Require(count && materials.size() == faces.size(), "x-material-faces");
                for (auto& index : materials) {
                    index = data.Count();
                    Require(index < count, "x-material-index");
                }
                data.End();
                for (const auto& child : node.children) {
                    const Node* source = &child;
                    if (child.type == "reference") {
                        const auto found = named.find(child.name);
                        Require(found != named.end(), "x-unresolved-material-reference");
                        source = found->second;
                    }
                    Require(source->type == "Material", "x-material-child");
                    AccessoryMaterial material;
                    Data m{*source};
                    for (auto& v : material.diffuse)
                        v = m.Float();
                    material.ambient = material.diffuse;
                    material.shininess = m.Float();
                    for (auto& v : material.specular)
                        v = m.Float();
                    for (auto& v : material.emissive)
                        v = m.Float();
                    m.End();
                    for (unsigned c = 0; c < 3; ++c) {
                        material.diffuse[c] *= .1f;
                        material.specular[c] *= .1f;
                    }
                    for (const auto& texture : source->children) {
                        Require(texture.type == "TextureFilename" &&
                                    material.originalTextureBytes.empty() &&
                                    texture.values.size() == 1 &&
                                    texture.values[0].kind == Token::String,
                                "x-material-extension-not-supported");
                        material.originalTextureBytes = texture.values[0].text;
                        Require(text::DecodeCp932(material.originalTextureBytes, material.texture),
                                "x-texture-encoding");
                    }
                    next->materials_.push_back(std::move(material));
                }
                Require(next->materials_.size() == count, "x-material-count");
            } else
                throw std::runtime_error("x-mesh-extension-not-supported:" + node.type);
        }
        Require(hasMaterials && !normals.empty() && !faces.empty(),
                "x-explicit-normals-and-materials-required");
        for (auto& normal : normals) {
            const float length =
                std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            const float inverse = length > 0 ? 1.f / length : 0;
            for (auto& v : normal)
                v *= inverse;
        }
        std::map<std::pair<unsigned, unsigned>, unsigned> vertices;
        std::vector<std::vector<unsigned>> groups(next->materials_.size());
        for (std::size_t f = 0; f < faces.size(); ++f)
            for (unsigned c = 0; c < 3; ++c) {
                const auto p = faces[f][c], n = normalFaces[f][c];
                auto found = vertices.find({p, n});
                if (found == vertices.end()) {
                    Require(next->vertices_.size() < Limit, "x-split-vertex-limit");
                    AccessoryVertex v;
                    v.position = positions[p];
                    v.normal = normals[n];
                    if (!uvs.empty())
                        v.uv = uvs[p];
                    found = vertices.emplace(std::make_pair(p, n), unsigned(next->vertices_.size()))
                                .first;
                    next->vertices_.push_back(v);
                }
                groups[materials[f]].push_back(found->second);
            }
        for (unsigned m = 0; m < groups.size(); ++m)
            if (!groups[m].empty()) {
                next->subsets_.push_back(
                    {m, std::uint32_t(next->indices_.size()), std::uint32_t(groups[m].size())});
                next->indices_.insert(next->indices_.end(), groups[m].begin(), groups[m].end());
            }
        output = std::move(next);
        reason.clear();
        return true;
    } catch (const std::exception& e) {
        reason = e.what();
        return false;
    }
}
}
