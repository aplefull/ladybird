/*
 * Copyright (c) 2022, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/ProgressEventPrototype.h>
#include <LibWeb/XHR/ProgressEvent.h>

namespace Web::XHR {

GC_DEFINE_ALLOCATOR(ProgressEvent);

GC::Ref<ProgressEvent> ProgressEvent::create(JS::Realm& realm, FlyString const& event_name, ProgressEventInit const& event_init)
{
    return realm.create<ProgressEvent>(realm, event_name, event_init);
}

WebIDL::ExceptionOr<GC::Ref<ProgressEvent>> ProgressEvent::construct_impl(JS::Realm& realm, FlyString const& event_name, ProgressEventInit const& event_init)
{
    return create(realm, event_name, event_init);
}

ProgressEvent::ProgressEvent(JS::Realm& realm, FlyString const& event_name, ProgressEventInit const& event_init)
    : Event(realm, event_name, event_init)
    , m_length_computable(event_init.length_computable)
    , m_loaded(event_init.loaded)
    , m_total(event_init.total)
{
}

ProgressEvent::~ProgressEvent() = default;

void ProgressEvent::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(ProgressEvent);
    Base::initialize(realm);
}

}
