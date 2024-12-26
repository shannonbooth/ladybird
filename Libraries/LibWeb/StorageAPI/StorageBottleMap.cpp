/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/StorageAPI/StorageBottleMap.h>
#include <LibWeb/StorageAPI/StorageShed.h>

namespace Web::StorageAPI {

// https://storage.spec.whatwg.org/#registered-storage-endpoints
struct RegisteredStorageEndpoint {
    String identifier;
    StorageType type;
    Optional<u64> quota;
};

static Array<RegisteredStorageEndpoint, 5> const registered_storage_endpoints {
    RegisteredStorageEndpoint { "caches"_string, StorageType::Local, {} },
    RegisteredStorageEndpoint { "indexedDB"_string, StorageType::Local, {} },
    RegisteredStorageEndpoint { "localStorage"_string, StorageType::Local, 5 * MiB },
    RegisteredStorageEndpoint { "serviceWorkerRegistrations"_string, StorageType::Local, {} },
    RegisteredStorageEndpoint { "sessionStorage"_string, StorageType::Session, 5 * MiB },
};

StorageBucket::StorageBucket(StorageType type)
{
    // 1. Let bucket be null.
    // 2. If type is "local", then set bucket to a new local storage bucket.
    // 3. Otherwise:
    //     1. Assert: type is "session".
    //     2. Set bucket to a new session storage bucket.

    // 4. For each endpoint of registered storage endpoints whose types contain type, set bucket’s bottle map[endpoint’s identifier] to a new storage bottle whose quota is endpoint’s quota.
    for (auto const& endpoint : registered_storage_endpoints) {
        if (endpoint.type != type)
            continue;

        auto new_bottle = StorageBottle::create();
        new_bottle->quota = endpoint.quota;
        bottle_map.set(endpoint.identifier, move(new_bottle));
    }

    // 5. Return bucket.
}

static StorageShed& user_agent_storage_shed()
{
    // FIXME: Storing this statically in memory is not the correct place or way of doing this!
    static StorageShed storage_shed;
    return storage_shed;
}

// https://storage.spec.whatwg.org/#obtain-a-storage-bottle-map
RefPtr<StorageBottle> obtain_a_storage_bottle_map(StorageType type, HTML::EnvironmentSettingsObject& environment, StringView identifier)
{
    // 1. Let shed be null.
    StorageShed* shed = nullptr;

    // 2. If type is "local", then set shed to the user agent’s storage shed.
    if (type == StorageType::Local) {
        shed = &user_agent_storage_shed();
    }
    // 3. Otherwise:
    else {
        // 1. Assert: type is "session".
        VERIFY(type == StorageType::Session);

        // 2. Set shed to environment’s global object’s associated Document’s node navigable’s traversable navigable’s storage shed.
        shed = &verify_cast<HTML::Window>(environment.global_object()).associated_document().navigable()->traversable_navigable()->storage_shed();
    }

    // 4. Let shelf be the result of running obtain a storage shelf, with shed, environment, and type.
    VERIFY(shed);
    auto shelf = shed->obtain_a_storage_shelf(environment, type);

    // 5. If shelf is failure, then return failure.
    if (!shelf.has_value())
        return {};

    // 6. Let bucket be shelf’s bucket map["default"].
    auto& bucket = shelf->bucket_map.get("default"sv).value();

    // 7. Let bottle be bucket’s bottle map[identifier].
    auto bottle = bucket.bottle_map.get(identifier).value();

    // 8. Let proxyMap be a new storage proxy map whose backing map is bottle’s map.
    // 9. Append proxyMap to bottle’s proxy map reference set.
    // 10. Return proxyMap.
    return bottle->proxy();
}

// https://storage.spec.whatwg.org/#obtain-a-session-storage-bottle-map
RefPtr<StorageBottle> obtain_a_session_storage_bottle_map(HTML::EnvironmentSettingsObject& environment, StringView identifier)
{
    // To obtain a session storage bottle map, given an environment settings object environment and storage identifier identifier,
    // return the result of running obtain a storage bottle map with "session", environment, and identifier.
    return obtain_a_storage_bottle_map(StorageType::Session, environment, identifier);
}

// https://storage.spec.whatwg.org/#obtain-a-local-storage-bottle-map
RefPtr<StorageBottle> obtain_a_local_storage_bottle_map(HTML::EnvironmentSettingsObject& environment, StringView identifier)
{
    // To obtain a local storage bottle map, given an environment settings object environment and storage identifier identifier,
    // return the result of running obtain a storage bottle map with "local", environment, and identifier.
    return obtain_a_storage_bottle_map(StorageType::Local, environment, identifier);
}

}
