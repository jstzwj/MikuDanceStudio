#include "../third_party/mmeffect/include/mme_legacy_api.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
int main() try {
    std::ifstream file(std::string(MDS_TEST_SOURCE_DIR) +
                       "/assets/effects/portable/LegacyMaterial.fx");
    const std::string source{std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>()};
    CHECK(!source.empty());
    MmeLegacyProgram program;
    std::string reason, hlsl;
    CHECK(MmeLegacyImport(source, program, reason));
    CHECK(program.variables.size() == 11 && program.samplers.size() == 1 &&
          program.passes.size() == 2);
    CHECK(program.samplers[0].name == "ColorSampler" && program.samplers[0].texture == "Albedo" &&
          program.samplers[0].semantic == "MATERIALTEXTURE");
    CHECK(program.hlsl.find("tex2D (") == std::string::npos);
    CHECK(program.hlsl.find("uint TintEnabled") != std::string::npos);
    std::vector<MmeLegacyInput> inputs;
    CHECK(MmeLegacyShader(program, program.passes[0], hlsl, inputs, reason));
    CHECK(inputs.size() == 3 && inputs[1].semantic == "NORMAL");
    CHECK(hlsl.find("SV_POSITION") != std::string::npos &&
          hlsl.find("SV_Target0") != std::string::npos);
    const auto rejects = [&](std::string text, const std::string& expected) {
        MmeLegacyProgram invalid;
        CHECK(!MmeLegacyImport(text, invalid, reason));
        if (reason != expected)
            std::fprintf(stderr, "expected %s, got %s\n", expected.c_str(), reason.c_str());
        CHECK(reason == expected && invalid.hlsl.empty());
    };
    const auto replace = [&](const char* from, const char* to) {
        auto text = source;
        const auto at = text.find(from);
        CHECK(at != std::string::npos);
        text.replace(at, std::string(from).size(), to);
        return text;
    };
    rejects(replace("AddressV=WRAP", "AddressV=BORDER"), "MME_legacy_sampler_address");
    rejects(replace("MipFilter=LINEAR", "MipFilter=NONE"), "MME_legacy_sampler_filter");
    rejects(replace("sampler2D ColorSampler", "samplerCUBE ColorSampler"),
            "MME_legacy_parameter_type");
    rejects(replace("tex2D(ColorSampler,", "tex2D(OtherSampler,"), "MME_legacy_texture_intrinsic");
    rejects(replace("float4 UserTint", "float4x3 UserTint"), "MME_legacy_parameter_type");
    rejects(source + "\n/*", "MME_legacy_unclosed_comment");
    rejects(source + "\nfloat Other,Also;", "MME_legacy_syntax");
    rejects("#include \"missing.fxsub\"\n" + source, "MME_legacy_preprocessor_directive");
    CHECK(MmeLegacyImport(source, program, reason));
    program.functions["PixelMain"].body += " f . p ;";
    CHECK(!MmeLegacyShader(program, program.passes[0], hlsl, inputs, reason));
    CHECK(reason == "MME_legacy_pixel_position_read");
    CHECK(MmeLegacyImport(source, program, reason));
    program.functions["PixelMain"].body += " Helper ( f ) ;";
    CHECK(!MmeLegacyShader(program, program.passes[0], hlsl, inputs, reason));
    CHECK(reason == "MME_legacy_pixel_input_escape");
    CHECK(MmeLegacyImport(source, program, reason));
    program.structures["Fragment"][1].semantic = "COLOR0";
    CHECK(!MmeLegacyShader(program, program.passes[0], hlsl, inputs, reason));
    CHECK(reason == "MME_legacy_varying_semantic");
    CHECK(MmeLegacyImport(source, program, reason));
    program.functions["PixelMain"].parameters[0].direction = "inout";
    CHECK(!MmeLegacyShader(program, program.passes[0], hlsl, inputs, reason));
    CHECK(reason == "MME_legacy_pixel_inout");
    CHECK(MmeLegacyImport(source, program, reason));
    program.structures["Fragment"][2].semantic = program.structures["Fragment"][1].semantic;
    CHECK(!MmeLegacyShader(program, program.passes[0], hlsl, inputs, reason));
    CHECK(reason == "MME_legacy_varying_layout");
    CHECK(MmeLegacyImport(source, program, reason));
    program.functions["PixelMain"].semantic = "COLOR1";
    CHECK(!MmeLegacyShader(program, program.passes[0], hlsl, inputs, reason));
    CHECK(reason == "MME_legacy_stage_signature");
    std::puts(
        "PASS FX9 source import: entries, declarations, samplers, stage wrappers and unsupported syntax");
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
