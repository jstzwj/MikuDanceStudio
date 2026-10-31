#pragma once
#include "mikudancestudio/model_reader.hpp"
#include "mikudancestudio/cpu_model_state.hpp"
#include <string>
namespace mikudancestudio::runtime {
bool NormalizePmd(const format::PmdDocument&, format::PmxDocument&, std::string& reason);
bool Cp932Identifier(const char*, std::size_t, std::string& utf8);
bool BuildSkeleton(const format::PmxDocument&, format::ModelKind,
                   const std::vector<format::PmdIkData>&, const std::vector<std::size_t>&,
                   const std::vector<std::string>&, std::vector<cpu_model::Bone>&,
                   std::vector<cpu_model::IkChain>&, std::string& reason);
}
