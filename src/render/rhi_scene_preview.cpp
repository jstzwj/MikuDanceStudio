#include "mikudancestudio/rhi_scene_preview.hpp"
#include <algorithm>
namespace mikudancestudio::rhi {
ScenePreviewController::~ScenePreviewController() {
    Close();
}
bool ScenePreviewController::Fail(const char* why) {
    state = PreviewState::Failed;
    reason = why;
    return false;
}
bool ScenePreviewController::Open(const SwapchainDesc& desc, const BackendOptions& selected) {
    if (!Close())
        return false;
    options = selected;
    description = desc;
    backend = CreateBackendDevice(desc, options);
    if (!backend)
        return Fail(backend.reason.c_str());
    renderer = std::make_unique<SceneRenderer>(*backend.device);
    state = PreviewState::Ready;
    reason.clear();
    return true;
}
bool ScenePreviewController::Resize(std::uint32_t width, std::uint32_t height) {
    if (!backend || state == PreviewState::Failed || state == PreviewState::Blocked)
        return false;
    if (!width || !height || backend.swapchain->Health() == SurfaceHealth::Suspended) {
        state = PreviewState::Suspended;
        reason = "window_suspended";
        return true;
    }
    auto next = description;
    next.width = width;
    next.height = height;
    if (width != description.width || height != description.height) {
        const auto uploads = renderer->Stats().uploads;
        const auto result = backend.swapchain->Rebuild(next, RebuildReason::Resize);
        if (result == RebuildResult::RetryLater) {
            state = PreviewState::Suspended;
            reason = "resize_retry_later";
            return true;
        }
        if (result != RebuildResult::Success)
            return Fail("swapchain_resize_failed");
        CompletionToken token;
        if ((hasPrepared && renderer->Submit(prepared, token)) ||
            renderer->Stats().uploads != uploads)
            return Fail("resize_invalidated_resource_contract");
        description = next;
        hasPrepared = false;
    }
    state = PreviewState::Ready;
    reason.clear();
    return true;
}
bool ScenePreviewController::ReplaceDevice() {
    if (!backend || !renderer) {
        const auto desc = description;
        const auto selected = options;
        return Open(desc, selected);
    }
    const auto old = backend.device->Status();
    state = PreviewState::Failed; // stop submissions throughout the transaction
    if (!renderer->Detach() || !backend.swapchain->Retire())
        return Fail("replacement_release_failed");
    backend.swapchain.reset();
    backend.device.reset();
    backend = CreateBackendDevice(description, options);
    if (!backend)
        return Fail(backend.reason.c_str());
    if (!renderer->Rebind(*backend.device))
        return Fail("replacement_rebind_failed");
    CompletionToken token;
    if (backend.device->Status().identity == old.identity ||
        (hasPrepared && renderer->Submit(prepared, token)))
        return Fail("replacement_old_submission_accepted");
    hasPrepared = false;
    state = PreviewState::Ready;
    reason.clear();
    return true;
}
bool ScenePreviewController::SuspendUnavailableSurface() {
    const auto health = backend.swapchain->Health();
    if (health != SurfaceHealth::Suspended && health != SurfaceHealth::Resettable &&
        health != SurfaceHealth::TemporarilyLost)
        return false;
    state = PreviewState::Suspended;
    reason = health == SurfaceHealth::Suspended ? "window_suspended" : "surface_retry_later";
    return true;
}
bool ScenePreviewController::ReadyToDraw() {
    if (!backend || !renderer || state == PreviewState::Failed || state == PreviewState::Closed ||
        state == PreviewState::Blocked)
        return false;
    const auto health = backend.swapchain->Health();
    if (health == SurfaceHealth::Suspended) {
        SuspendUnavailableSurface();
        return false;
    }
    if (health == SurfaceHealth::Resettable || health == SurfaceHealth::TemporarilyLost) {
        const auto rebuilt = backend.swapchain->Rebuild(description, RebuildReason::TemporaryLoss);
        if (rebuilt == RebuildResult::RetryLater) {
            state = PreviewState::Suspended;
            reason = "surface_retry_later";
            return false;
        }
        if (rebuilt != RebuildResult::Success)
            return Fail("surface_rebuild_failed");
        hasPrepared = false;
        state = PreviewState::Ready;
    }
    return state == PreviewState::Ready;
}
bool ScenePreviewController::Draw(const ModelReplayStream& source, RgbaImage* acceptanceImage) {
    if (!ReadyToDraw())
        return state == PreviewState::Suspended;
    // The controller changes presentation framing only. Camera and immutable
    // CPU resources retain their scene values, including on window resize.
    auto stream = source;
    const auto width = description.width, height = description.height;
    stream.target.width = width;
    stream.target.height = height;
    stream.initializedRegion.reset();
    if (!source.target.width || !source.target.height)
        return Fail("invalid_scene_extent");
    const double scale =
        std::min(double(width) / source.target.width, double(height) / source.target.height);
    const auto fullW = std::min(width, std::max(1u, unsigned(source.target.width * scale)));
    const auto fullH = std::min(height, std::max(1u, unsigned(source.target.height * scale)));
    for (auto& event : stream.events)
        if (event.op == ModelReplayOp::Draw) {
            if (!ApplyMaterialSampling(event.draw.pipeline, options.materialSampling))
                return Fail("invalid_sampling_policy");
            auto v = event.draw.pipeline.viewport.value_or(
                ModelViewport{0, 0, source.target.width, source.target.height, 0, 1});
            const auto x = std::min(width - 1, (width - fullW) / 2 + unsigned(v.x * scale));
            const auto y = std::min(height - 1, (height - fullH) / 2 + unsigned(v.y * scale));
            event.draw.pipeline.viewport =
                ModelViewport{x,
                              y,
                              std::min(width - x, std::max(1u, unsigned(v.width * scale))),
                              std::min(height - y, std::max(1u, unsigned(v.height * scale))),
                              v.minDepth,
                              v.maxDepth};
        }
    auto result = renderer->Prepare(1, stream, prepared, FrameOutput::Presentation);
    CompletionToken token;
    if (result)
        hasScene = true;
    if (result)
        result = renderer->Submit(prepared, token);
    hasPrepared = bool(result);
    if (!result) {
        // A minimize can occur after Resize/Health and before BeginFrame. Its
        // temporary failure must not poison an otherwise healthy controller.
        if (SuspendUnavailableSurface()) {
            return true;
        }
        return Fail(result.reason.c_str());
    }
    return FinishDraw(acceptanceImage);
}
bool ScenePreviewController::FinishDraw(RgbaImage* acceptanceImage) {
    if (acceptanceImage &&
        !ReadBackendPresentation(options.backend, *backend.device, *acceptanceImage))
        return Fail("acceptance_readback_failed");
    if (!backend.device->Present() || !renderer->Collect()) {
        if (SuspendUnavailableSurface()) {
            return true;
        }
        return Fail("present_or_collect_failed");
    }
    state = PreviewState::Ready;
    reason.clear();
    return true;
}
bool ScenePreviewController::Unload() {
    hasPrepared = false;
    prepared = PreparedModelFrame{};
    if (renderer && hasScene && !renderer->Unload(1))
        return Fail("scene_unload_failed");
    hasScene = false;
    if (state != PreviewState::Closed && state != PreviewState::Failed)
        Block("waiting_for_scene");
    return true;
}
bool ScenePreviewController::Draw(const RenderFrame& source, RgbaImage* acceptanceImage) {
    if (!ReadyToDraw())
        return state == PreviewState::Suspended;
    auto frame = source;
    if (!ApplyMaterialSampling(frame, options.materialSampling))
        return Fail("invalid_sampling_policy");
    const auto final = std::find_if(frame.targets.begin(), frame.targets.end(),
                                    [&](const auto& t) { return t.id == frame.finalColor; });
    if (final == frame.targets.end() || final->description.width != description.width ||
        final->description.height != description.height)
        return Fail("final_color_surface_extent_mismatch");
    auto result = renderer->Prepare(1, frame, prepared, FrameOutput::Presentation);
    CompletionToken token;
    if (result)
        hasScene = true;
    if (result)
        result = renderer->Submit(prepared, token);
    hasPrepared = bool(result);
    if (!result) {
        if (SuspendUnavailableSurface())
            return true;
        return Fail(result.reason.c_str());
    }
    return FinishDraw(acceptanceImage);
}
bool ScenePreviewController::Close() {
    bool okay = Unload();
    if (renderer && !renderer->Detach())
        okay = false;
    renderer.reset();
    if (backend.swapchain && !backend.swapchain->Retire())
        okay = false;
    backend.swapchain.reset();
    backend.device.reset();
    hasPrepared = hasScene = false;
    state = PreviewState::Closed;
    reason = okay ? "" : "close_release_failed";
    return okay;
}
void ScenePreviewController::Block(const char* why) {
    if (state != PreviewState::Failed) {
        state = PreviewState::Blocked;
        reason = why;
    }
}
void ScenePreviewController::Resume() {
    if (state == PreviewState::Blocked) {
        state = PreviewState::Ready;
        reason.clear();
    }
}
}
