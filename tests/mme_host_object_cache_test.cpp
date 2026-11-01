#include "mmeffect/host_object_cache.h"

#include <cstdio>
#include <list>
#include <map>
#include <unordered_map>

int main() {
    struct Geometry {
        int references = 1; // The host owns the initial reference.
        int destroyed = 0;
        int invalidCalls = 0;
        int AddRef() {
            if (references == 0) ++invalidCalls;
            return ++references;
        }
        int Release() {
            if (references == 0) ++invalidCalls;
            if (--references == 0) ++destroyed;
            return references;
        }
    };
    struct Object { unsigned long long id; Geometry* geometry; };
    std::list<Object*> objects;
    std::unordered_map<unsigned long long, Object*> byId;
    std::map<unsigned long long, Geometry*> liveObjects;
    int failures = 0;
    int registered = 0;
    int released = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    const auto registerObject = [&](unsigned long long id, Geometry* geometry = nullptr) {
        if (byId.find(id) != byId.end())
            return false;
        auto* object = new Object{id, geometry};
        if (geometry) geometry->AddRef();
        objects.push_back(object);
        byId[id] = object;
        ++registered;
        return true;
    };
    const auto prune = [&] {
        mme::RemoveStaleHostObjects(objects, byId, liveObjects,
            [](Object* object, Geometry* geometry) { return object->geometry == geometry; },
            [&](Object* object) {
                check(byId.find(object->id) == byId.end(),
                      "Identity must be removed before the object is freed");
                ++released;
                if (object->geometry) object->geometry->Release();
                delete object;
            });
    };

    registerObject(1);
    registerObject(2);
    Object* retained = byId.at(2);
    liveObjects[2] = nullptr;
    prune();
    check(released == 1 && objects.size() == 1 && byId.size() == 1,
          "An unloaded object must leave both cache containers");
    check(byId.at(2) == retained && objects.front() == retained,
          "The still-live object must retain its cache record");
    prune();
    check(released == 1, "Repeated frames must not release twice");

    // The host allocator may reuse the unloaded object's address/identity.
    liveObjects[1] = nullptr;
    check(registerObject(1), "Reused host identity must register a new object");
    check(registered == 3 && byId.size() == 2,
          "Replacement must trigger registration, not use a dangling entry");
    check(!registerObject(2), "A live identity must not register twice");
    prune();
    check(released == 1, "Both current objects must survive the next frame");

    liveObjects.clear();
    prune();
    check(released == 3 && objects.empty() && byId.empty(),
          "Removing all host objects must completely empty the cache");

    // Atomic scene replacement: no frame ever observes an absent ID. The old
    // geometry, rather than ID membership, distinguishes the two lifetimes.
    Geometry oldGeometry, replacementGeometry;
    liveObjects[7] = &oldGeometry;
    registerObject(7, &oldGeometry);
    prune();
    prune();
    check(oldGeometry.references == 2 && oldGeometry.destroyed == 0,
          "A live geometry must retain exactly one cache reference across frames");
    oldGeometry.AddRef(); // A previous D3D stream binding still holds it.
    oldGeometry.Release(); // Host unloads, then reuses the same object address.
    liveObjects[7] = &replacementGeometry;
    prune();
    check(byId.empty() && objects.empty() && released == 4,
          "Same-address replacement must delete the old MME object");
    check(oldGeometry.references == 1 && oldGeometry.invalidCalls == 0,
          "An old D3D binding must not keep a replaced host object in the cache");
    oldGeometry.Release();
    check(oldGeometry.destroyed == 1, "Old geometry must be destroyed exactly once");
    check(registerObject(7, &replacementGeometry),
          "Replacement at a still-present identity must trigger registration");
    prune();
    check(replacementGeometry.references == 2 && registered == 5,
          "The replacement must own a fresh geometry reference");
    liveObjects.clear();
    prune();
    check(replacementGeometry.references == 1 && released == 5,
          "Removing an ID must release only the cache's geometry reference");
    replacementGeometry.Release();
    check(replacementGeometry.destroyed == 1 && replacementGeometry.invalidCalls == 0,
          "Host and cache geometry references must balance");
    if (failures == 0)
        std::puts("MME host object cache regression passed");
    return failures == 0 ? 0 : 1;
}
