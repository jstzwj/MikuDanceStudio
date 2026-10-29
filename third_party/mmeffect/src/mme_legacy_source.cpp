#include "../include/mme_legacy_api.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace {
std::string Upper(std::string s) {
    for (auto& c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
bool Identifier(const std::string& s) {
    return !s.empty() && (std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_') &&
           std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isalnum(c) || c == '_'; });
}
std::vector<std::string> Lex(const std::string& source) {
    std::vector<std::string> tokens;
    for (std::size_t p = 0; p < source.size();) {
        const auto start = p;
        const unsigned char c = source[p];
        if (std::isspace(c) || !c) {
            ++p;
            continue;
        }
        if (c == '#') {
            const auto end = source.find('\n', p);
            if (source.compare(p, 5, "#line") != 0)
                throw std::runtime_error("MME_legacy_preprocessor_directive");
            p = end == std::string::npos ? source.size() : end + 1;
            continue;
        }
        if (source.compare(p, 2, "//") == 0) {
            const auto end = source.find('\n', p);
            p = end == std::string::npos ? source.size() : end + 1;
            continue;
        }
        if (source.compare(p, 2, "/*") == 0) {
            const auto end = source.find("*/", p + 2);
            if (end == std::string::npos)
                throw std::runtime_error("MME_legacy_unclosed_comment");
            p = end + 2;
            continue;
        }
        if (c == '"') {
            ++p;
            bool closed = false;
            while (p < source.size()) {
                if (source[p] == '\\')
                    p += 2;
                else if (source[p++] == '"') {
                    closed = true;
                    break;
                }
            }
            if (!closed)
                throw std::runtime_error("MME_legacy_unclosed_string");
        } else if (std::isalpha(c) || c == '_') {
            while (++p < source.size() &&
                   (std::isalnum(static_cast<unsigned char>(source[p])) || source[p] == '_')) {}
        } else if (std::isdigit(c) || (c == '.' && p + 1 < source.size() &&
                                       std::isdigit(static_cast<unsigned char>(source[p + 1])))) {
            ++p;
            while (p < source.size()) {
                const unsigned char n = source[p];
                if (std::isalnum(n) || n == '.' ||
                    ((n == '+' || n == '-') && (source[p - 1] == 'e' || source[p - 1] == 'E')))
                    ++p;
                else
                    break;
            }
        } else {
            ++p;
            static const std::set<std::string> pairs = {
                "++", "--", "<=", ">=", "==", "!=", "&&", "||",
                "+=", "-=", "*=", "/=", "<<", ">>", "&=", "|="};
            if (p < source.size() && pairs.count(source.substr(start, 2)))
                ++p;
        }
        tokens.push_back(source.substr(start, p - start));
        if (tokens.size() > 1024 * 1024)
            throw std::runtime_error("MME_legacy_source_budget");
    }
    return tokens;
}
std::string Join(const std::vector<std::string>& tokens, std::size_t begin, std::size_t end) {
    std::string out;
    for (auto p = begin; p < end; ++p)
        out += tokens[p] + " ";
    return out;
}
class Parser {
public:
    std::vector<std::string> t;
    std::size_t p = 0;
    MmeLegacyProgram result;
    std::map<std::string, std::string> textures;
    std::set<std::string> names;
    std::string constants, code;
    explicit Parser(const std::string& source) : t(Lex(source)) {}
    std::string Peek() const { return p < t.size() ? t[p] : ""; }
    bool Eat(const char* s) {
        if (Peek() != s)
            return false;
        ++p;
        return true;
    }
    std::string Take() {
        if (p == t.size())
            Fail("unexpected_end");
        return t[p++];
    }
    std::string Name() {
        auto s = Take();
        if (!Identifier(s) || s.rfind("mdsLegacy", 0) == 0)
            Fail("identifier");
        return s;
    }
    void Need(const char* s) {
        if (!Eat(s))
            Fail("syntax");
    }
    [[noreturn]] void Fail(const char* s) const {
        throw std::runtime_error(std::string("MME_legacy_") + s);
    }
    void Unique(const std::string& name) {
        if (!names.insert(name).second)
            Fail("duplicate_declaration");
    }
    void Annotations(std::map<std::string, std::string>& values) {
        if (!Eat("<"))
            return;
        while (!Eat(">")) {
            const auto type = Take(), name = Upper(Name());
            Need("=");
            auto value = Take();
            if (type == "string") {
                if (value.size() < 2 || value.front() != '"' || value.back() != '"')
                    Fail("annotation");
                value = value.substr(1, value.size() - 2);
            } else if (type != "bool" && type != "int" && type != "float")
                Fail("annotation");
            Need(";");
            if (!values.emplace(name, value).second)
                Fail("duplicate_annotation");
        }
    }
    std::string Semantic() { return Eat(":") ? Upper(Name()) : ""; }
    void Initializer() {
        if (Eat("=")) {
            unsigned paren = 0, brace = 0, bracket = 0;
            while (Peek() != ";" || paren || brace || bracket) {
                const auto v = Take();
                if (v == "(")
                    ++paren;
                else if (v == ")") {
                    if (!paren)
                        Fail("initializer");
                    --paren;
                } else if (v == "{")
                    ++brace;
                else if (v == "}") {
                    if (!brace)
                        Fail("initializer");
                    --brace;
                } else if (v == "[")
                    ++bracket;
                else if (v == "]") {
                    if (!bracket)
                        Fail("initializer");
                    --bracket;
                } else if (v == "," && !paren && !brace && !bracket)
                    Fail("multiple_declarators");
            }
        }
        Need(";");
    }
    std::string Body() {
        const auto start = p;
        Need("{");
        unsigned depth = 1;
        while (depth) {
            const auto v = Take();
            if (v == "{")
                ++depth;
            if (v == "}")
                --depth;
        }
        return Join(t, start, p);
    }
    void Structure() {
        const auto begin = p;
        Need("struct");
        const auto name = Name();
        Unique(name);
        Need("{");
        std::vector<MmeLegacyProgram::Function::Field> fields;
        while (!Eat("}")) {
            const auto type = Name(), member = Name(), semantic = Semantic();
            Need(";");
            if (semantic.empty())
                Fail("struct_semantic");
            fields.push_back({type, member, semantic});
        }
        Need(";");
        result.structures.emplace(name, fields);
        code += Join(t, begin, p);
    }
    void Technique() {
        Take();
        const auto technique = Name();
        Unique(technique);
        std::map<std::string, std::string> annotations;
        Annotations(annotations);
        if (annotations.count("SCRIPT"))
            Fail("material_technique_script");
        Need("{");
        while (!Eat("}")) {
            Need("pass");
            MmeLegacyPass pass;
            pass.technique = technique;
            pass.name = Name();
            annotations.clear();
            Annotations(annotations);
            if (!annotations.empty())
                Fail("pass_annotations");
            Need("{");
            while (!Eat("}")) {
                const auto state = Upper(Name());
                Need("=");
                if (state == "VERTEXSHADER" || state == "PIXELSHADER") {
                    Need("compile");
                    const auto profile = Take();
                    if (profile != (state == "VERTEXSHADER" ? "vs_3_0" : "ps_3_0"))
                        Fail("shader_model");
                    auto& entry = state == "VERTEXSHADER" ? pass.vertex : pass.pixel;
                    if (!entry.empty())
                        Fail("duplicate_state");
                    entry = Name();
                    Need("(");
                    Need(")");
                    Need(";");
                } else {
                    auto value = Upper(Take());
                    if (value == "-" || value == "+")
                        value += Take();
                    Need(";");
                    if (!pass.states.emplace(state, value).second)
                        Fail("duplicate_state");
                }
            }
            if (pass.vertex.empty() || pass.pixel.empty())
                Fail("missing_shaders");
            result.passes.push_back(std::move(pass));
            if (result.passes.size() > 64)
                Fail("pass_budget");
        }
        Eat(";");
    }
    void Declaration() {
        const auto begin = p;
        bool isStatic = false, isConst = false;
        while (Peek() == "shared" || Peek() == "uniform" || Peek() == "extern" ||
               Peek() == "row_major" || Peek() == "column_major" || Peek() == "static" ||
               Peek() == "const") {
            const auto q = Take();
            isStatic |= q == "static";
            isConst |= q == "const";
        }
        const auto type = Name(), name = Name();
        if (Eat("(")) {
            MmeLegacyProgram::Function f;
            f.result = type;
            f.name = name;
            if (!Eat(")")) {
                do {
                    if (Peek() == "in")
                        Take();
                    const auto pt = Name(), pn = Name(), semantic = Semantic();
                    f.parameters.push_back({pt, pn, semantic});
                } while (Eat(","));
                Need(")");
            }
            f.semantic = Semantic();
            f.body = Body();
            if (!result.functions.emplace(name, f).second)
                Fail("overloaded_function");
            code += Join(t, begin, p);
            return;
        }
        Unique(name);
        if (isStatic || isConst) {
            if (!isConst)
                Fail("static_initializer");
            Initializer();
            code += Join(t, begin, p);
            return;
        }
        unsigned elements = 1;
        if (Eat("[")) {
            const auto n = Take();
            if (n.empty() || n.size() > 3 ||
                !std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isdigit(c); }))
                Fail("array_extent");
            elements = unsigned(std::stoul(n));
            if (!elements || elements > 256)
                Fail("array_extent");
            Need("]");
        }
        const auto semantic = Semantic();
        std::map<std::string, std::string> annotations;
        Annotations(annotations);
        if (type == "texture" || type == "texture2D") {
            if (elements != 1 || !annotations.empty() ||
                (semantic != "MATERIALTEXTURE" && semantic != "MATERIALSPHEREMAP" &&
                 semantic != "MATERIALTOONTEXTURE"))
                Fail("texture_semantic");
            textures.emplace(name, semantic);
            Need(";");
            return;
        }
        if (type == "sampler" || type == "sampler2D") {
            if (elements != 1 || !semantic.empty() || !annotations.empty())
                Fail("sampler_layout");
            MmeLegacySampler s;
            s.name = name;
            Need("=");
            Need("sampler_state");
            Need("{");
            std::map<std::string, std::string> states;
            while (!Eat("}")) {
                const auto state = Upper(Name());
                Need("=");
                auto v = Take();
                if (state == "TEXTURE") {
                    if (v != "<")
                        Fail("sampler_texture");
                    v = Name();
                    Need(">");
                } else
                    v = Upper(v);
                Need(";");
                if (!states.emplace(state, v).second)
                    Fail("duplicate_state");
            }
            Need(";");
            s.texture = states["TEXTURE"];
            states.erase("TEXTURE");
            for (auto field : {"MINFILTER", "MAGFILTER", "MIPFILTER"}) {
                auto v = states[field];
                if (v != "LINEAR" && v != "POINT")
                    Fail("sampler_filter");
                (std::string(field) == "MINFILTER"   ? s.linearMin
                 : std::string(field) == "MAGFILTER" ? s.linearMag
                                                     : s.linearMip) = v == "LINEAR";
                states.erase(field);
            }
            for (auto field : {"ADDRESSU", "ADDRESSV"}) {
                auto v = states[field];
                if (v != "WRAP" && v != "CLAMP")
                    Fail("sampler_address");
                (std::string(field) == "ADDRESSU" ? s.clampU : s.clampV) = v == "CLAMP";
                states.erase(field);
            }
            if (states.count("SRGBTEXTURE")) {
                if (states["SRGBTEXTURE"] != "FALSE" && states["SRGBTEXTURE"] != "0")
                    Fail("sampler_srgb");
                states.erase("SRGBTEXTURE");
            }
            if (!states.empty() || s.texture.empty())
                Fail("sampler_state");
            result.samplers.push_back(s);
            if (result.samplers.size() > 4)
                Fail("sampler_budget");
            return;
        }
        MmeLegacyVariable v;
        v.name = name;
        v.type = type;
        v.semantic = semantic;
        v.elements = elements;
        if (type == "float4x4") {
            v.rows = v.columns = 4;
        } else if (type == "float" || type == "int" || type == "bool") {
        } else if (type.size() == 6 && type.substr(0, 5) == "float" && type[5] >= '2' &&
                   type[5] <= '4')
            v.columns = unsigned(type[5] - '0');
        else
            Fail("parameter_type");
        if (!annotations.empty()) {
            if (semantic == "CONTROLOBJECT") {
                for (const auto& a : annotations)
                    if (a.first != "NAME" && a.first != "ITEM")
                        Fail("control_annotation");
                if (!annotations.count("NAME"))
                    Fail("control_annotation");
            } else {
                if (annotations.size() != 1 || !annotations.count("OBJECT") ||
                    Upper(annotations["OBJECT"]) != "GEOMETRY")
                    Fail("parameter_annotation");
                v.object = "GEOMETRY";
            }
        }
        Initializer();
        constants +=
            (v.rows == 4 ? "row_major " : "") + (type == "bool" ? "uint" : type) + " " + name;
        if (elements > 1)
            constants += "[" + std::to_string(elements) + "]";
        constants += ";\n";
        result.variables.push_back(v);
        if (result.variables.size() > 256)
            Fail("parameter_budget");
    }
    void Finish() {
        for (auto& s : result.samplers) {
            if (!textures.count(s.texture))
                Fail("sampler_texture");
            s.semantic = textures.at(s.texture);
        }
        const auto tokens = Lex(code);
        code.clear();
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            if (token.rfind("tex", 0) == 0 && token != "texture" && i + 1 < tokens.size() &&
                tokens[i + 1] == "(") {
                if (i + 3 >= tokens.size() || tokens[i + 3] != ",")
                    Fail("sampler_expression");
                const auto s = std::find_if(result.samplers.begin(), result.samplers.end(),
                                            [&](const auto& s) { return s.name == tokens[i + 2]; });
                if (s == result.samplers.end() ||
                    (token != "tex2D" && token != "tex2Dlod" && token != "tex2Dbias" &&
                     token != "tex2Dgrad" && token != "tex2Dproj"))
                    Fail("texture_intrinsic");
                code += "mdsLegacySample" + std::to_string(s - result.samplers.begin()) +
                        token.substr(5) + " ( ";
                i += 3;
            } else
                code += token + " ";
        }
        std::string resources;
        for (unsigned i = 0; i < result.samplers.size(); ++i) {
            const auto n = std::to_string(i), image = "mdsLegacyTexture" + n,
                       sampler = "mdsLegacySampler" + n, sample = "mdsLegacySample" + n;
            resources += "Texture2D " + image + ":register(t" + n + ");SamplerState " + sampler +
                         ":register(s" + n + ");\n";
            resources += "float4 " + sample + "(float2 uv){return " + image + ".Sample(" + sampler +
                         ",uv);}\n";
            resources += "float4 " + sample + "lod(float4 uv){return " + image + ".SampleLevel(" +
                         sampler + ",uv.xy,uv.w);}\n";
            resources += "float4 " + sample + "bias(float4 uv){return " + image + ".SampleBias(" +
                         sampler + ",uv.xy,uv.w);}\n";
            resources += "float4 " + sample + "grad(float2 uv,float2 dx,float2 dy){return " +
                         image + ".SampleGrad(" + sampler + ",uv,dx,dy);}\n";
            resources += "float4 " + sample + "proj(float4 uv){return " + image + ".Sample(" +
                         sampler + ",uv.xy/uv.w);}\n";
        }
        result.hlsl = "cbuffer mdsLegacyViewportBlock:register(b0){float4 mdsLegacyViewport;};\n";
        if (!constants.empty())
            result.hlsl += "cbuffer mdsLegacyValues:register(b1){\n" + constants + "};\n";
        result.hlsl += resources + code;
        if (result.passes.empty())
            Fail("empty_effect");
    }
};
}
bool MmeLegacyImport(const std::string& source, MmeLegacyProgram& out, std::string& reason) {
    try {
        if (source.size() > 16 * 1024 * 1024)
            throw std::runtime_error("MME_legacy_source_budget");
        Parser parser(source);
        while (!parser.Peek().empty()) {
            if (parser.Peek() == "struct")
                parser.Structure();
            else if (parser.Peek() == "technique")
                parser.Technique();
            else
                parser.Declaration();
        }
        parser.Finish();
        out = std::move(parser.result);
        reason.clear();
        return true;
    } catch (const std::exception& e) {
        reason = e.what();
        return false;
    }
}
bool MmeLegacyShader(const MmeLegacyProgram& program, const MmeLegacyPass& pass, std::string& hlsl,
                     std::vector<MmeLegacyInput>& inputs, std::string& reason) {
    const auto fail = [&](const char* s) {
        reason = std::string("MME_legacy_") + s;
        return false;
    };
    const auto vs = program.functions.find(pass.vertex), ps = program.functions.find(pass.pixel);
    if (vs == program.functions.end() || ps == program.functions.end())
        return fail("entry_not_found");
    const auto output = program.structures.find(vs->second.result);
    if (output == program.structures.end() || ps->second.result != "float4" ||
        (ps->second.semantic != "COLOR" && ps->second.semantic != "COLOR0") ||
        ps->second.parameters.size() != 1)
        return fail("stage_signature");
    const auto pixelInput = program.structures.find(ps->second.parameters[0].type);
    if (pixelInput == program.structures.end())
        return fail("stage_signature");
    std::string call, signature, varyings = "struct mdsLegacyVaryings {", position;
    std::vector<MmeLegacyInput> nextInputs;
    std::set<std::string> semantics;
    const auto input = [&](const MmeLegacyProgram::Function::Field& f) {
        unsigned columns = f.semantic == "POSITION" || f.semantic == "POSITION0" ||
                                   f.semantic == "NORMAL" || f.semantic == "NORMAL0"
                               ? 3
                           : f.semantic == "TEXCOORD0" ? 2
                                                       : 0;
        if (!columns || f.type != "float" + std::to_string(columns) ||
            !semantics.insert(f.semantic).second)
            return false;
        nextInputs.push_back({f.semantic, columns});
        return true;
    };
    for (const auto& f : vs->second.parameters) {
        if (!call.empty()) {
            call += ",";
            signature += ",";
        }
        call += f.name;
        signature += f.type + " " + f.name;
        if (!f.semantic.empty()) {
            signature += ":" + f.semantic;
            if (!input(f))
                return fail("vertex_input");
        } else {
            const auto fields = program.structures.find(f.type);
            if (fields == program.structures.end())
                return fail("vertex_input");
            for (const auto& member : fields->second)
                if (!input(member))
                    return fail("vertex_input");
        }
    }
    std::set<std::string> outputs;
    for (const auto& f : output->second) {
        if (!outputs.insert(f.semantic).second)
            return fail("varying_layout");
        const bool pos = f.semantic == "POSITION" || f.semantic == "POSITION0";
        if (pos) {
            if (!position.empty() || f.type != "float4")
                return fail("vertex_position");
            position = f.name;
        } else if (f.semantic.rfind("TEXCOORD", 0) != 0)
            return fail("varying_semantic");
        varyings += f.type + " " + f.name + ":" + (pos ? "SV_POSITION" : f.semantic) + ";";
    }
    if (position.empty() || nextInputs.empty())
        return fail("vertex_position");
    varyings += "};\nmdsLegacyVaryings mdsLegacyVS(" + signature + "){" + vs->second.result +
                " original=" + pass.vertex + "(" + call + ");mdsLegacyVaryings output;";
    for (const auto& f : output->second)
        varyings += "output." + f.name + "=original." + f.name + ";";
    varyings += "output." + position +
                ".xy+=float2(mdsLegacyViewport.x,-mdsLegacyViewport.y)*output." + position +
                ".w;return output;}\n";
    varyings += "float4 mdsLegacyPS(mdsLegacyVaryings input):SV_Target0{" +
                ps->second.parameters[0].type + " original;";
    const auto pixelTokens = Lex(ps->second.body);
    // Passing the entire input to a helper or alias could hide a POSITION
    // read. Only member access is imported until that data flow is modeled.
    for (std::size_t i = 0; i < pixelTokens.size(); ++i)
        if (pixelTokens[i] == ps->second.parameters[0].name &&
            (i + 1 == pixelTokens.size() || pixelTokens[i + 1] != "."))
            return fail("pixel_input_escape");
    for (const auto& f : pixelInput->second) {
        const auto field =
            std::find_if(output->second.begin(), output->second.end(), [&](const auto& v) {
                return v.semantic == f.semantic && v.type == f.type;
            });
        if (field == output->second.end())
            return fail("stage_interface");
        if (f.semantic == "POSITION" || f.semantic == "POSITION0") {
            for (std::size_t i = 0; i + 2 < pixelTokens.size(); ++i)
                if (pixelTokens[i] == ps->second.parameters[0].name && pixelTokens[i + 1] == "." &&
                    pixelTokens[i + 2] == f.name)
                    return fail("pixel_position_read");
        }
        varyings += "original." + f.name + "=input." + field->name + ";";
    }
    varyings += "return " + pass.pixel + "(original);}\n";
    hlsl = program.hlsl + varyings;
    inputs = std::move(nextInputs);
    reason.clear();
    return true;
}
