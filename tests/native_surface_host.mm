#include "native_surface_host.hpp"
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdexcept>
namespace {
struct Host final : NativeSurfaceHost {
    NSWindow* native;
    CAMetalLayer* layer;
    Host() {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        native = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,width,height)
                    styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO];
        [native setReleasedWhenClosed:NO];
        native.contentView.wantsLayer = YES;
        layer = [CAMetalLayer layer];
        native.contentView.layer = layer;
        [native orderFront:nil]; // Isolated macOS CI runner; never used on the user's desktop.
        if (!Resize(width,height)) throw std::runtime_error("cocoa_surface_creation");
        window = {mikudancestudio::rhi::WindowSystem::Cocoa, reinterpret_cast<std::uintptr_t>((__bridge void*)layer)};
    }
    ~Host() override { [native orderOut:nil]; [native close]; }
    bool Resize(unsigned w, unsigned h) override {
        const auto scale = native.backingScaleFactor;
        [native setContentSize:NSMakeSize(w/scale,h/scale)];
        layer.contentsScale = scale;
        layer.drawableSize = CGSizeMake(w,h);
        width = w; height = h;
        return Pump();
    }
    bool Pump() override {
        @autoreleasepool {
            NSEvent* event;
            while ((event=[NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast]
                    inMode:NSDefaultRunLoopMode dequeue:YES])) [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
        return native != nil;
    }
};
}
std::shared_ptr<NativeSurfaceHost> CreateTestSurfaceHost(const std::string&) { return std::make_shared<Host>(); }
