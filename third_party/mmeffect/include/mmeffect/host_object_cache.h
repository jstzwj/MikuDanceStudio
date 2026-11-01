#pragma once

namespace mme {

// Keep the identity index and its owning list in sync. Membership alone is
// insufficient when an entire scene is replaced without an intervening frame.
template<class ObjectList, class IdentityIndex, class LiveObjects, class Matches, class Release>
void RemoveStaleHostObjects(ObjectList& objects, IdentityIndex& byId,
                           const LiveObjects& liveObjects, Matches matches, Release release) {
    auto it = objects.begin();
    while (it != objects.end()) {
        auto* object = *it;
        const auto current = liveObjects.find(object->id);
        if (current != liveObjects.end() && matches(object, current->second)) {
            ++it;
            continue;
        }
        byId.erase(object->id);
        release(object);
        it = objects.erase(it);
    }
}

} // namespace mme
