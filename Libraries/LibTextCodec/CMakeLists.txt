include(libtextcodec_generators)

set(SOURCES
    Decoder.cpp
    Encoder.cpp
    RustIntegration.cpp
)

generate_encoding_indexes()

set(GENERATED_SOURCES
    LookupTables.cpp
)

ladybird_lib(LibTextCodec textcodec EXPLICIT_SYMBOL_EXPORT)

import_rust_crate(MANIFEST_PATH Rust/Cargo.toml CRATE_NAME libtextcodec_rust FFI_HEADER RustFFI.h)
target_link_libraries(LibTextCodec PRIVATE libtextcodec_rust)
