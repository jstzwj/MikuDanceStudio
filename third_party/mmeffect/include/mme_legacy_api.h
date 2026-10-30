#pragma once
#include <map>
#include <string>
#include <vector>

// Optional source diagnostic. The reason code remains stable for frame fallback;
// locations refer to expanded bytes and, when present, preprocessor #line maps.
struct MmeLegacyDiagnostic {
    std::size_t offset = 0;
    unsigned line = 1, column = 1;
    std::string file, token, declaration, technique, pass, stage, message;
};

// Source-only FX9 import. This model has no device, effect instance or native
// resource references. It never guesses the meaning of an unknown declaration.
struct MmeLegacyVariable {
    std::string name, type, semantic, object;
    unsigned rows = 1, columns = 1, elements = 1;
};
struct MmeLegacySampler {
    std::string name, texture, semantic;
    bool linearMin = true, linearMag = true, linearMip = true;
    bool clampU = false, clampV = false, mipNone = false;
};
struct MmeLegacyPass {
    std::string technique, name, vertex, pixel;
    std::map<std::string, std::string> states;
};
struct MmeLegacyInput {
    std::string semantic;
    unsigned columns = 0;
};
struct MmeLegacyTexture {
    std::string name, semantic;
    bool shared = false;
    std::map<std::string, std::string> annotations;
};
struct MmeLegacyProgram {
    bool scene = false;
    std::vector<MmeLegacyTexture> textures;
    std::vector<MmeLegacyVariable> variables;
    std::vector<MmeLegacySampler> samplers;
    std::vector<MmeLegacyPass> passes;
    // FX-only declarations are removed. Numeric values are read from b1;
    // textures/samplers have deterministic ordered tN/sN bindings.
    std::string hlsl;
    struct Function {
        struct Field {
            std::string type, name, semantic;
            std::string direction = "in";
        };
        std::string result, name, semantic;
        std::vector<Field> parameters;
        std::string body;
    };
    std::map<std::string, std::vector<Function::Field>> structures;
    std::map<std::string, Function> functions;
};
bool MmeLegacyImport(const std::string& expandedSource, MmeLegacyProgram&, std::string& reason,
                     MmeLegacyDiagnostic* diagnostic = nullptr);
bool MmeLegacyShader(const MmeLegacyProgram&, const MmeLegacyPass&, std::string& hlsl,
                     std::vector<MmeLegacyInput>&, std::string& reason,
                     std::vector<unsigned>* colorOutputs = nullptr,
                     MmeLegacyDiagnostic* diagnostic = nullptr);

bool MmeLegacyImportScene(const std::string&, MmeLegacyProgram&, std::string& reason,
                          MmeLegacyDiagnostic* diagnostic = nullptr);
std::vector<unsigned> MmeLegacySamplers(const MmeLegacyProgram&, const MmeLegacyPass&,
                                        bool vertex = false);
