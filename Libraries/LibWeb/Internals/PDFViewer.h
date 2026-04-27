#pragma once

#include <LibWeb/Export.h>
#include <LibWeb/Internals/InternalsBase.h>

namespace Web::Internals {

class WEB_API PDFViewer final : public InternalsBase {
    WEB_PLATFORM_OBJECT(PDFViewer, InternalsBase);
    GC_DECLARE_ALLOCATOR(PDFViewer);

public:
    virtual ~PDFViewer() override;

    GC::Ref<JS::ArrayBuffer> get_data() const;
    String get_url() const;

private:
    explicit PDFViewer(JS::Realm&, GC::Ref<JS::ArrayBuffer>, String);
    virtual void initialize(JS::Realm&) override;
    virtual void visit_edges(Visitor&) override;

    GC::Ref<JS::ArrayBuffer> m_data;
    String m_url;
};

}
