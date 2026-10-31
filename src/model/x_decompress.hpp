#pragma once
#include <cstddef>
#include <string>
#include <vector>
namespace mikudancestudio::runtime {
bool DecompressX(const unsigned char*, std::size_t, std::vector<unsigned char>&, std::string&);
}
