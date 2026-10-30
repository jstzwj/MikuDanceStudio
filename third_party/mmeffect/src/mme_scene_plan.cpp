#include "../include/mme_portable_api.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
bool MmePortableExtent(const MmePortableTarget& target, unsigned viewportWidth,
                       unsigned viewportHeight, unsigned& width, unsigned& height,
                       std::string& reason) {
    if (!viewportWidth || !viewportHeight || (bool(target.width) != bool(target.height)) ||
        (target.relative && (target.width || target.height))) {
        reason = "MME_target_extent_layout";
        return false;
    }
    const float w = target.relative ? float(viewportWidth) * target.viewportRatio[0]
                                    : float(target.width ? target.width : viewportWidth);
    const float h = target.relative ? float(viewportHeight) * target.viewportRatio[1]
                                    : float(target.height ? target.height : viewportHeight);
    // Native SAS truncates positive products. Zero/default-size requests have
    // creation/refresh-dependent legacy rules and retain whole-frame fallback.
    if (!std::isfinite(w) || !std::isfinite(h) || w < 1 || h < 1 || w >= 4097 || h >= 4097) {
        reason = "MME_target_extent_not_supported";
        return false;
    }
    width = unsigned(w);
    height = unsigned(h);
    reason.clear();
    return true;
}
bool MmePortableMaterialLayout(MmePortableEffect& effect, std::string& reason) {
    const auto fail = [&](const char* text) {
        reason = text;
        return false;
    };
    if (effect.post || effect.script.empty() || effect.passes.empty())
        return fail("MME_material_script_missing");
    std::set<std::string> targets;
    for (const auto& target : effect.targets)
        if (target.depth || !target.identity || target.name == "DrawColor" ||
            !targets.insert(target.name).second)
            return fail("MME_material_script_target");
    std::array<std::string, 4> colors{"DrawColor", "", "", ""};
    std::vector<MmePortableStep> steps;
    for (const auto& command : effect.script) {
        if (command.kind == MmePortableCommand::Kind::ColorTarget) {
            if (command.value < 0 || command.value > 3 ||
                (!command.parameter.empty() && !targets.count(command.parameter)))
                return fail("MME_material_script_target");
            colors[command.value] =
                command.parameter.empty() && command.value == 0 ? "DrawColor" : command.parameter;
        } else if (command.kind == MmePortableCommand::Kind::Pass) {
            if (command.value < 0 || unsigned(command.value) >= effect.passes.size() ||
                steps.size() >= 32)
                return fail("MME_material_script_pass");
            MmePortableStep step;
            step.pass = unsigned(command.value);
            bool gap = false;
            std::set<std::string> unique;
            for (const auto& name : colors) {
                if (name.empty()) {
                    gap = true;
                    continue;
                }
                if (gap || !unique.insert(name).second)
                    return fail("MME_material_script_output_layout");
                step.colors.push_back(name);
            }
            for (const auto& sampled : effect.passes[step.pass].sampled)
                if (unique.count(sampled))
                    return fail("MME_material_script_feedback");
            steps.push_back(std::move(step));
        } else
            return fail("MME_material_script_command");
    }
    if (steps.empty())
        return fail("MME_material_script_missing");
    effect.steps = std::move(steps);
    reason.clear();
    return true;
}
bool MmePortableSceneLayout(MmePortableEffect& effect, std::string& reason) {
    const auto fail = [&](const char* r) {
        reason = r;
        return false;
    };
    if (!effect.post || effect.script.empty())
        return fail("MME_scene_script_missing");
    std::map<std::string, MmePortableTarget> targets;
    targets.emplace("FinalColor", MmePortableTarget{"FinalColor"});
    targets.emplace("FinalDepth", MmePortableTarget{"FinalDepth", MmePortableFormat::Rgba8, true});
    for (const auto& t : effect.targets)
        if (t.name.empty() || !targets.emplace(t.name, t).second)
            return fail("MME_scene_duplicate_target");
    auto parameters = effect.parameters;
    std::set<std::string> written;
    const auto parameter = [&](const std::string& name) -> MmePortableParameter* {
        const auto p = std::find_if(parameters.begin(), parameters.end(),
                                    [&](const auto& p) { return p.name == name; });
        return p == parameters.end() ? nullptr : &*p;
    };
    const auto numeric = [&](const std::string& name, unsigned count, std::vector<float>& values) {
        const auto* p = parameter(name);
        if (!p || p->value.size() < count * 4)
            return false;
        values.resize(count);
        for (unsigned i = 0; i < count; ++i) {
            if (p->type == MmePortableParameter::Type::Float)
                std::memcpy(&values[i], p->value.data() + i * 4, 4);
            else {
                int value;
                std::memcpy(&value, p->value.data() + i * 4, 4);
                values[i] = float(value);
            }
            if (!std::isfinite(values[i]))
                return false;
        }
        return true;
    };
    std::array<std::string, 4> colors{"FinalColor", "", "", ""};
    std::string depth = "FinalDepth";
    auto clear = effect.scriptClearColor;
    float clearDepth = effect.scriptClearDepth;
    std::set<std::string> initialized;
    std::vector<MmePortableStep> steps;
    unsigned captures = 0, work = 0;
    struct Loop {
        std::size_t begin;
        int index, count;
    };
    std::vector<Loop> loops;
    const auto record = [&](MmePortableStep step) {
        bool gap = false;
        for (const auto& name : colors) {
            if (name.empty()) {
                gap = true;
                continue;
            }
            const auto t = targets.find(name);
            if (gap || t == targets.end() || t->second.depth)
                return fail("MME_scene_color_layout");
            step.colors.push_back(name);
        }
        step.depth = depth;
        step.color = clear;
        step.depthValue = clearDepth;
        if (step.colors.empty())
            return fail("MME_scene_color_layout");
        std::set<std::string> unique(step.colors.begin(), step.colors.end());
        if (unique.size() != step.colors.size())
            return fail("MME_scene_duplicate_attachment");
        if (step.kind == MmePortableStep::Kind::Draw) {
            if (step.pass >= effect.passes.size())
                return fail("MME_scene_pass_index");
            const auto& pass = effect.passes[step.pass];
            for (const auto& input : pass.sampled) {
                if (unique.count(input))
                    return fail("MME_scene_feedback");
                if (!initialized.count(input) &&
                    (!targets.count(input) || !targets.at(input).identity))
                    return fail("MME_scene_read_before_write");
            }
            for (const auto& name : step.colors)
                if (!initialized.count(name))
                    return fail("MME_scene_write_requires_clear");
            step.parameters = parameters;
        }
        if (step.kind == MmePortableStep::Kind::Capture) {
            if (++captures != 1 || unique.count("FinalColor"))
                return fail("MME_scene_capture_layout");
            for (const auto& name : step.colors)
                if (!initialized.count(name))
                    return fail("MME_scene_capture_requires_clear");
            if (!depth.empty() && !initialized.count(depth))
                return fail("MME_scene_capture_requires_clear");
        }
        if (step.clearDepth && !step.depth.empty())
            initialized.insert(step.depth);
        if (step.kind != MmePortableStep::Kind::Clear || step.clearColor)
            initialized.insert(step.colors.begin(), step.colors.end());
        steps.push_back(std::move(step));
        return steps.size() <= 64 || fail("MME_scene_step_budget");
    };
    using K = MmePortableCommand::Kind;
    for (std::size_t pc = 0; pc < effect.script.size(); ++pc) {
        if (++work > 4096)
            return fail("MME_scene_script_budget");
        const auto& cmd = effect.script[pc];
        std::vector<float> value;
        switch (cmd.kind) {
        case K::ColorTarget:
            if (cmd.value < 0 || cmd.value >= 4)
                return fail("MME_scene_target_slot");
            colors[cmd.value] =
                cmd.parameter.empty() ? (cmd.value == 0 ? "FinalColor" : "") : cmd.parameter;
            break;
        case K::DepthTarget:
            if (!cmd.parameter.empty() &&
                (!targets.count(cmd.parameter) || !targets.at(cmd.parameter).depth))
                return fail("MME_scene_depth_target");
            depth = cmd.parameter.empty() ? "FinalDepth" : cmd.parameter;
            break;
        case K::ClearColorValue:
            if (!numeric(cmd.parameter, 4, value))
                return fail("MME_scene_clear_parameter");
            for (unsigned c = 0; c < 4; ++c)
                clear[c] = float(int(std::clamp(value[c], 0.f, 1.f) * 255)) / 255;
            break;
        case K::ClearDepthValue:
            if (!numeric(cmd.parameter, 1, value) || value[0] < 0 || value[0] > 1)
                return fail("MME_scene_clear_parameter");
            clearDepth = value[0];
            break;
        case K::Clear: {
            MmePortableStep step;
            step.kind = MmePortableStep::Kind::Clear;
            step.clearColor = (cmd.value & 1) != 0;
            step.clearDepth = (cmd.value & 6) != 0;
            if (step.clearDepth && depth.empty())
                return fail("MME_scene_clear_without_depth");
            if (!record(std::move(step)))
                return false;
            break;
        }
        case K::Capture: {
            MmePortableStep step;
            step.kind = MmePortableStep::Kind::Capture;
            if (!record(std::move(step)))
                return false;
            break;
        }
        case K::Pass: {
            if (cmd.value < 0 || unsigned(cmd.value) >= effect.passes.size())
                return fail("MME_scene_pass_index");
            MmePortableStep step;
            step.kind = MmePortableStep::Kind::Draw;
            step.pass = unsigned(cmd.value);
            if (!record(std::move(step)))
                return false;
            break;
        }
        case K::Loop:
            if (!numeric(cmd.parameter, 1, value) || value[0] > 4096 || value[0] < -4096)
                return fail("MME_scene_loop_parameter");
            if (int(value[0]) <= 0) {
                unsigned nesting = 1;
                while (++pc < effect.script.size() && nesting) {
                    if (effect.script[pc].kind == K::Loop)
                        ++nesting;
                    else if (effect.script[pc].kind == K::LoopEnd)
                        --nesting;
                }
                if (nesting)
                    return fail("MME_scene_unbalanced_loop");
                --pc;
            } else
                loops.push_back({pc, 0, int(value[0])});
            break;
        case K::LoopIndex: {
            auto* p = parameter(cmd.parameter);
            if (!p || p->shared || p->type != MmePortableParameter::Type::Int ||
                p->value.size() != 4)
                return fail("MME_scene_loop_index_parameter");
            const int index = loops.empty() ? 0 : loops.back().index;
            std::memcpy(p->value.data(), &index, 4);
            written.insert(p->name);
            break;
        }
        case K::LoopEnd:
            if (loops.empty())
                return fail("MME_scene_unbalanced_loop");
            if (++loops.back().index < loops.back().count)
                pc = loops.back().begin;
            else
                loops.pop_back();
            break;
        default:
            return fail("MME_scene_command_not_supported");
        }
    }
    if (!loops.empty())
        return fail("MME_scene_unbalanced_loop");
    if (captures != 1 || !initialized.count("FinalColor"))
        return fail("MME_scene_incomplete_script");
    std::map<unsigned, unsigned> outputs;
    for (const auto& step : steps)
        if (step.kind == MmePortableStep::Kind::Draw) {
            if (outputs.count(step.pass) && outputs.at(step.pass) != step.colors.size())
                return fail("MME_scene_pass_output_mismatch");
            outputs[step.pass] = unsigned(step.colors.size());
            effect.passes[step.pass].colors = step.colors;
        }
    effect.steps = std::move(steps);
    effect.writes.clear();
    for (const auto& p : parameters)
        if (written.count(p.name))
            effect.writes.push_back(p);
    return true;
}
