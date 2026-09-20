#include "mmeffect/host_object_cache.h"

#include <cstdio>
#include <list>
#include <map>
#include <unordered_map>

int main() {
    struct Object { unsigned long long id; };
    std::list<Object*> objects;
    std::unordered_map<unsigned long long, Object*> byId;
    std::map<unsigned long long, bool> liveIds;
    int failures = 0;
    int registered = 0;
    int released = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    const auto registerObject = [&](unsigned long long id) {
        if (byId.find(id) != byId.end())
            return false;
        auto* object = new Object{id};
        objects.push_back(object);
        byId[id] = object;
        ++registered;
        return true;
    };
    const auto prune = [&] {
        mme::RemoveMissingHostObjects(objects, byId, liveIds,
            [&](Object* object) {
                check(byId.find(object->id) == byId.end(),
                      "Identity must be removed before the object is freed");
                ++released;
                delete object;
            });
    };

    registerObject(1);
    registerObject(2);
    Object* retained = byId.at(2);
    liveIds[2] = true;
    prune();
    check(released == 1 && objects.size() == 1 && byId.size() == 1,
          "An unloaded object must leave both cache containers");
    check(byId.at(2) == retained && objects.front() == retained,
          "The still-live object must retain its cache record");
    prune();
    check(released == 1, "Repeated frames must not release twice");

    // The host allocator may reuse the unloaded object's address/identity.
    liveIds[1] = true;
    check(registerObject(1), "Reused host identity must register a new object");
    check(registered == 3 && byId.size() == 2,
          "Replacement must trigger registration, not use a dangling entry");
    check(!registerObject(2), "A live identity must not register twice");
    prune();
    check(released == 1, "Both current objects must survive the next frame");

    liveIds.clear();
    prune();
    check(released == 3 && objects.empty() && byId.empty(),
          "Removing all host objects must completely empty the cache");
    if (failures == 0)
        std::puts("MME host object cache regression passed");
    return failures == 0 ? 0 : 1;
}
