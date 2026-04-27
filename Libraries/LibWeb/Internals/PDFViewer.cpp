#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/PDFViewer.h>
#include <LibWeb/Internals/PDFViewer.h>

namespace Web::Internals {

GC_DEFINE_ALLOCATOR(PDFViewer);

PDFViewer::PDFViewer(JS::Realm& realm, GC::Ref<JS::ArrayBuffer> data, String url)
    : InternalsBase(realm)
    , m_data(data)
    , m_url(move(url))
{
}

PDFViewer::~PDFViewer() = default;

void PDFViewer::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(PDFViewer);
    Base::initialize(realm);
}

void PDFViewer::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_data);
}

GC::Ref<JS::ArrayBuffer> PDFViewer::get_data() const
{
    return m_data;
}

String PDFViewer::get_url() const
{
    return m_url;
}

}
