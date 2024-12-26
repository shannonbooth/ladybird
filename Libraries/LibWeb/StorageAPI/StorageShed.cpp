/*
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/StorageAPI/StorageShed.h>

namespace Web::StorageAPI {

// https://storage.spec.whatwg.org/#create-a-storage-shelf
StorageShelf::StorageShelf(StorageType type)
{
    // 1. Let shelf be a new storage shelf.
    // 2. Set shelf’s bucket map["default"] to the result of running create a storage bucket with type.
    bucket_map.set("default"_string, StorageBucket { type });
    // 3. Return shelf.
}

// https://storage.spec.whatwg.org/#obtain-a-storage-shelf
Optional<StorageShelf&> StorageShed::obtain_a_storage_shelf(HTML::EnvironmentSettingsObject const& environment, StorageType type)
{
    // 1. Let key be the result of running obtain a storage key with environment.
    auto key = obtain_a_storage_key(environment);

    // 2. If key is failure, then return failure.
    if (!key.has_value())
        return {};

    // 3. If shed[key] does not exist, then set shed[key] to the result of running create a storage shelf with type.
    // 4. Return shed[key].
    return m_data.ensure(key.value(), [type] {
        return StorageShelf { type };
    });
}

}
