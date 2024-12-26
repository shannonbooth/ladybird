/*
 * Copyright (c) 2022, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2023, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2024, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/String.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/StoragePrototype.h>
#include <LibWeb/HTML/Storage.h>

namespace Web::HTML {

GC_DEFINE_ALLOCATOR(Storage);

GC::Ref<Storage> Storage::create(JS::Realm& realm, Type type, NonnullRefPtr<StorageAPI::StorageBottle> storage_bottle)
{
    return realm.create<Storage>(realm, type, move(storage_bottle));
}

Storage::Storage(JS::Realm& realm, Type type, NonnullRefPtr<StorageAPI::StorageBottle> storage_bottle)
    : Bindings::PlatformObject(realm)
    , m_type(type)
    , m_storage_bottle(move(storage_bottle))
{
    m_legacy_platform_object_flags = LegacyPlatformObjectFlags {
        .supports_indexed_properties = true,
        .supports_named_properties = true,
        .has_indexed_property_setter = true,
        .has_named_property_setter = true,
        .has_named_property_deleter = true,
        .indexed_property_setter_has_identifier = true,
        .named_property_setter_has_identifier = true,
        .named_property_deleter_has_identifier = true,
    };
}

Storage::~Storage() = default;

void Storage::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    WEB_SET_PROTOTYPE_FOR_INTERFACE(Storage);
}

// https://html.spec.whatwg.org/multipage/webstorage.html#dom-storage-length
size_t Storage::length() const
{
    // The length getter steps are to return this's map's size.
    return m_storage_bottle->map.size();
}

// https://html.spec.whatwg.org/multipage/webstorage.html#dom-storage-key
Optional<String> Storage::key(size_t index)
{
    // 1. If index is greater than or equal to this's map's size, then return null.
    if (index >= m_storage_bottle->map.size())
        return {};

    // 2. Let keys be the result of running get the keys on this's map.
    auto keys = m_storage_bottle->map.keys();

    // 3. Return keys[index].
    return keys[index];
}

// https://html.spec.whatwg.org/multipage/webstorage.html#dom-storage-getitem
Optional<String> Storage::get_item(StringView key) const
{
    // 1. If this's map[key] does not exist, then return null.
    auto it = m_storage_bottle->map.find(key);
    if (it == m_storage_bottle->map.end())
        return {};

    // 2. Return this's map[key].
    return it->value;
}

// https://html.spec.whatwg.org/multipage/webstorage.html#dom-storage-setitem
WebIDL::ExceptionOr<void> Storage::set_item(String const& key, String const& value)
{
    auto& realm = this->realm();

    // 1. Let oldValue be null.
    String old_value;

    // 2. Let reorder be true.
    bool reorder = true;

    // 3. If this's map[key] exists:
    auto new_size = m_stored_bytes;
    if (auto it = m_storage_bottle->map.find(key); it != m_storage_bottle->map.end()) {
        // 1. Set oldValue to this's map[key].
        old_value = it->value;

        // 2. If oldValue is value, then return.
        if (old_value == value)
            return {};

        // 3. Set reorder to false.
        reorder = false;
    } else {
        new_size += key.bytes().size();
    }

    // 4. If value cannot be stored, then throw a "QuotaExceededError" DOMException exception.
    new_size += value.bytes().size() - old_value.bytes().size();
    if (new_size > *m_storage_bottle->quota)
        return WebIDL::QuotaExceededError::create(realm, MUST(String::formatted("Unable to store more than {} bytes in storage"sv, *m_storage_bottle->quota)));

    // 5. Set this's map[key] to value.
    m_storage_bottle->map.set(key, value);
    m_stored_bytes = new_size;

    // 6. If reorder is true, then reorder this.
    if (reorder)
        this->reorder();

    // 7. Broadcast this with key, oldValue, and value.
    broadcast(key, old_value, value);

    return {};
}

// https://html.spec.whatwg.org/multipage/webstorage.html#dom-storage-removeitem
void Storage::remove_item(StringView key)
{
    // 1. If this's map[key] does not exist, then return null.
    // FIXME: Return null?
    auto it = m_storage_bottle->map.find(key);
    if (it == m_storage_bottle->map.end())
        return;

    // 2. Set oldValue to this's map[key].
    auto old_value = it->value;

    // 3. Remove this's map[key].
    m_storage_bottle->map.remove(it);
    m_stored_bytes = m_stored_bytes - key.bytes().size() - old_value.bytes().size();

    // 4. Reorder this.
    reorder();

    // 5. Broadcast this with key, oldValue, and null.
    broadcast(key, old_value, {});
}

// https://html.spec.whatwg.org/multipage/webstorage.html#dom-storage-clear
void Storage::clear()
{
    // 1. Clear this's map.
    m_storage_bottle->map.clear();

    // 2. Broadcast this with null, null, and null.
    broadcast({}, {}, {});
}

// https://html.spec.whatwg.org/multipage/webstorage.html#concept-storage-reorder
void Storage::reorder()
{
    // To reorder a Storage object storage, reorder storage's map's entries in an implementation-defined manner.
    // NOTE: This basically means that we're not required to maintain any particular iteration order.
}

// https://html.spec.whatwg.org/multipage/webstorage.html#concept-storage-broadcast
void Storage::broadcast(StringView key, StringView old_value, StringView new_value)
{
    (void)key;
    (void)old_value;
    (void)new_value;
    // FIXME: Implement.
}

Vector<FlyString> Storage::supported_property_names() const
{
    // The supported property names on a Storage object storage are the result of running get the keys on storage's map.
    Vector<FlyString> names;
    names.ensure_capacity(m_storage_bottle->map.size());
    for (auto const& key : m_storage_bottle->map.keys())
        names.unchecked_append(key);
    return names;
}

Optional<JS::Value> Storage::item_value(size_t index) const
{
    // Handle index as a string since that's our key type
    auto key = String::number(index);
    auto value = get_item(key);
    if (!value.has_value())
        return {};
    return JS::PrimitiveString::create(vm(), value.release_value());
}

JS::Value Storage::named_item_value(FlyString const& name) const
{
    auto value = get_item(name);
    if (!value.has_value())
        // AD-HOC: Spec leaves open to a description at: https://html.spec.whatwg.org/multipage/webstorage.html#the-storage-interface
        // However correct behavior expected here: https://github.com/whatwg/html/issues/8684
        return JS::js_undefined();
    return JS::PrimitiveString::create(vm(), value.release_value());
}

WebIDL::ExceptionOr<Bindings::PlatformObject::DidDeletionFail> Storage::delete_value(String const& name)
{
    remove_item(name);
    return DidDeletionFail::NotRelevant;
}

WebIDL::ExceptionOr<void> Storage::set_value_of_indexed_property(u32 index, JS::Value unconverted_value)
{
    // Handle index as a string since that's our key type
    auto key = String::number(index);
    return set_value_of_named_property(key, unconverted_value);
}

WebIDL::ExceptionOr<void> Storage::set_value_of_named_property(String const& key, JS::Value unconverted_value)
{
    // NOTE: Since PlatformObject does not know the type of value, we must convert it ourselves.
    //       The type of `value` is `DOMString`.
    auto value = TRY(unconverted_value.to_string(vm()));
    return set_item(key, value);
}

void Storage::dump() const
{
    dbgln("Storage ({} key(s))", m_storage_bottle->map.size());
    size_t i = 0;
    for (auto const& it : m_storage_bottle->map) {
        dbgln("[{}] \"{}\": \"{}\"", i, it.key, it.value);
        ++i;
    }
}

}
