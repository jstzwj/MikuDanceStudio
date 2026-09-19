#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace mme {

// The original name registry is rebuilt in host enumeration order each frame.
// Its inner tree uses unsigned order keys and unique insertion (first wins).
// Lookup compares signed order values and wraps to the last entry when every
// candidate follows the owner. Evidence: 18003BA30 / 180057BC0 / 18005B9E0.
template <typename Model>
class ModelNameRegistry {
    struct OrderLess {
        bool operator()(int a, int b) const {
            return static_cast<std::uint32_t>(a) < static_cast<std::uint32_t>(b);
        }
    };
    using OrderedModels = std::map<int, Model*, OrderLess>;
    std::map<std::string, OrderedModels> names_;

public:
    void Clear() { names_.clear(); }

    void Add(const std::string& name, int order, Model* model) {
        names_[name].emplace(order, model);
    }

    Model* Find(const std::string& name, int ownerOrder) const {
        const auto named = names_.find(name);
        if (named == names_.end() || named->second.empty())
            return nullptr;
        Model* selected = nullptr;
        for (const auto& entry : named->second) {
            if (ownerOrder < entry.first)
                break;
            selected = entry.second;
        }
        return selected != nullptr ? selected : named->second.rbegin()->second;
    }

    Model* First(const std::string& name) const {
        const auto named = names_.find(name);
        return named == names_.end() || named->second.empty()
            ? nullptr : named->second.begin()->second;
    }

    void Remove(Model* model) {
        for (auto named = names_.begin(); named != names_.end();) {
            auto& ordered = named->second;
            for (auto entry = ordered.begin(); entry != ordered.end();) {
                if (entry->second == model)
                    entry = ordered.erase(entry);
                else
                    ++entry;
            }
            if (ordered.empty())
                named = names_.erase(named);
            else
                ++named;
        }
    }
};

} // namespace mme
