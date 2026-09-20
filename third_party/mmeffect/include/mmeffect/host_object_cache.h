#pragma once

namespace mme {

// Keep the host identity index and its owning list in sync when a frame no
// longer contains an object. Host IDs are addresses and can be reused after
// unloading, so a stale index would skip registration of the replacement.
template<class ObjectList, class IdentityIndex, class LiveIds, class Release>
void RemoveMissingHostObjects(ObjectList& objects, IdentityIndex& byId,
                              const LiveIds& liveIds, Release release) {
    auto it = objects.begin();
    while (it != objects.end()) {
        auto* object = *it;
        if (liveIds.find(object->id) != liveIds.end()) {
            ++it;
            continue;
        }
        byId.erase(object->id);
        release(object);
        it = objects.erase(it);
    }
}

} // namespace mme
