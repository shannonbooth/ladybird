/*
 * Copyright (c) 2020-2021, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2021-2023, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 * Copyright (c) 2022, Ali Mohammad Pur <mpfard@serenityos.org>
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "IDLGenerators.h"
#include "Namespaces.h"
#include <AK/Assertions.h>
#include <AK/Debug.h>
#include <AK/HashTable.h>
#include <AK/LexicalPath.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibCore/MappedFile.h>
#include <LibIDL/IDLParser.h>
#include <LibIDL/Types.h>

static ErrorOr<void> generate_depfile(StringView depfile_path, StringView depfile_prefix, ReadonlySpan<ByteString> dependency_paths, ReadonlySpan<ByteString> output_files)
{
    auto depfile = TRY(Core::File::open_file_or_standard_stream(depfile_path, Core::File::OpenMode::Write));

    StringBuilder depfile_builder;
    bool first_output = true;
    for (auto const& s : output_files) {
        if (s.is_empty())
            continue;

        if (!first_output)
            depfile_builder.append(' ');

        if (!depfile_prefix.is_empty())
            depfile_builder.append(LexicalPath::join(depfile_prefix, s).string());
        else
            depfile_builder.append(s);
        first_output = false;
    }
    depfile_builder.append(':');
    for (auto const& path : dependency_paths) {
        depfile_builder.append(" \\\n "sv);
        depfile_builder.append(path);
    }
    depfile_builder.append('\n');
    return depfile->write_until_depleted(depfile_builder.string_view().bytes());
}

template<typename GeneratorFunction>
static ErrorOr<void> write_if_changed(GeneratorFunction generator_function, IDL::Interface const& interface, StringView file_path)
{
    StringBuilder output_builder;
    generator_function(interface, output_builder);

    auto current_file_or_error = Core::File::open(file_path, Core::File::OpenMode::Read);
    if (current_file_or_error.is_error() && current_file_or_error.error().code() != ENOENT)
        return current_file_or_error.release_error();

    ByteBuffer current_contents;
    if (!current_file_or_error.is_error())
        current_contents = TRY(current_file_or_error.value()->read_until_eof());
    // Only write to disk if contents have changed
    if (current_contents != output_builder.string_view().bytes()) {
        auto output_file = TRY(Core::File::open(file_path, Core::File::OpenMode::Write | Core::File::OpenMode::Truncate));
        TRY(output_file->write_until_depleted(output_builder.string_view().bytes()));
    }

    return {};
}

struct GeneratedFilePaths {
    ByteString namespace_header;
    ByteString namespace_implementation;
    ByteString constructor_header;
    ByteString constructor_implementation;
    ByteString prototype_header;
    ByteString prototype_implementation;
    ByteString iterator_prototype_header;
    ByteString iterator_prototype_implementation;
    ByteString async_iterator_prototype_header;
    ByteString async_iterator_prototype_implementation;
    ByteString global_mixin_header;
    ByteString global_mixin_implementation;

    auto files_view() const
    {
        return to_array<StringView>({
            constructor_header,
            constructor_implementation,
            prototype_header,
            prototype_implementation,
            namespace_header,
            namespace_implementation,
            iterator_prototype_header,
            iterator_prototype_implementation,
            async_iterator_prototype_header,
            async_iterator_prototype_implementation,
            global_mixin_header,
            global_mixin_implementation,
        });
    }
};

static ErrorOr<GeneratedFilePaths> generate_binding_for_interface(IDL::Interface const& interface, LexicalPath const& lexical_path, StringView output_path)
{
    GeneratedFilePaths generated_file_paths;

    auto path_prefix = LexicalPath::join(output_path, lexical_path.basename(LexicalPath::StripExtension::Yes));

    if (interface.is_namespace) {
        generated_file_paths.namespace_header = ByteString::formatted("{}Namespace.h", path_prefix);
        generated_file_paths.namespace_implementation = ByteString::formatted("{}Namespace.cpp", path_prefix);

        TRY(write_if_changed(&IDL::generate_namespace_header, interface, generated_file_paths.namespace_header));
        TRY(write_if_changed(&IDL::generate_namespace_implementation, interface, generated_file_paths.namespace_implementation));
    } else {
        generated_file_paths.constructor_header = ByteString::formatted("{}Constructor.h", path_prefix);
        generated_file_paths.constructor_implementation = ByteString::formatted("{}Constructor.cpp", path_prefix);
        generated_file_paths.prototype_header = ByteString::formatted("{}Prototype.h", path_prefix);
        generated_file_paths.prototype_implementation = ByteString::formatted("{}Prototype.cpp", path_prefix);

        TRY(write_if_changed(&IDL::generate_constructor_header, interface, generated_file_paths.constructor_header));
        TRY(write_if_changed(&IDL::generate_constructor_implementation, interface, generated_file_paths.constructor_implementation));
        TRY(write_if_changed(&IDL::generate_prototype_header, interface, generated_file_paths.prototype_header));
        TRY(write_if_changed(&IDL::generate_prototype_implementation, interface, generated_file_paths.prototype_implementation));
    }

    if (interface.pair_iterator_types.has_value()) {
        generated_file_paths.iterator_prototype_header = ByteString::formatted("{}IteratorPrototype.h", path_prefix);
        generated_file_paths.iterator_prototype_implementation = ByteString::formatted("{}IteratorPrototype.cpp", path_prefix);

        TRY(write_if_changed(&IDL::generate_iterator_prototype_header, interface, generated_file_paths.iterator_prototype_header));
        TRY(write_if_changed(&IDL::generate_iterator_prototype_implementation, interface, generated_file_paths.iterator_prototype_implementation));
    }

    if (interface.async_value_iterator_type.has_value()) {
        generated_file_paths.async_iterator_prototype_header = ByteString::formatted("{}AsyncIteratorPrototype.h", path_prefix);
        generated_file_paths.async_iterator_prototype_implementation = ByteString::formatted("{}AsyncIteratorPrototype.cpp", path_prefix);

        TRY(write_if_changed(&IDL::generate_async_iterator_prototype_header, interface, generated_file_paths.async_iterator_prototype_header));
        TRY(write_if_changed(&IDL::generate_async_iterator_prototype_implementation, interface, generated_file_paths.async_iterator_prototype_implementation));
    }

    if (interface.extended_attributes.contains("Global")) {
        generated_file_paths.global_mixin_header = ByteString::formatted("{}GlobalMixin.h", path_prefix);
        generated_file_paths.global_mixin_implementation = ByteString::formatted("{}GlobalMixin.cpp", path_prefix);

        TRY(write_if_changed(&IDL::generate_global_mixin_header, interface, generated_file_paths.global_mixin_header));
        TRY(write_if_changed(&IDL::generate_global_mixin_implementation, interface, generated_file_paths.global_mixin_implementation));
    }

    return generated_file_paths;
}

ErrorOr<int> ladybird_main(Main::Arguments arguments)
{
    Core::ArgsParser args_parser;
    Vector<ByteString> paths;
    Vector<ByteString> base_paths;
    StringView output_path = "-"sv;
    StringView depfile_path;
    StringView depfile_prefix;

    args_parser.add_option(Core::ArgsParser::Option {
        .argument_mode = Core::ArgsParser::OptionArgumentMode::Required,
        .help_string = "Add a header search path passed to the compiler",
        .long_name = "header-include-path",
        .short_name = 'i',
        .value_name = "path",
        .accept_value = [&](StringView s) {
            IDL::g_header_search_paths.append(s);
            return true;
        },
    });
    args_parser.add_option(Core::ArgsParser::Option {
        .argument_mode = Core::ArgsParser::OptionArgumentMode::Required,
        .help_string = "Path to root of IDL file tree(s)",
        .long_name = "base-path",
        .short_name = 'b',
        .value_name = "base-path",
        .accept_value = [&](StringView s) {
            base_paths.append(s);
            return true;
        },
    });
    args_parser.add_option(output_path, "Path to output generated files into", "output-path", 'o', "output-path");
    args_parser.add_option(depfile_path, "Path to write dependency file to", "depfile", 'd', "depfile-path");
    args_parser.add_option(depfile_prefix, "Prefix to prepend to relative paths in dependency file", "depfile-prefix", 'p', "depfile-prefix");
    args_parser.add_positional_argument(paths, "IDL file(s)", "idl-files");
    args_parser.parse(arguments);

    if (paths.first().starts_with("@"sv)) {
        auto file_or_error = Core::File::open(paths.first().substring_view(1), Core::File::OpenMode::Read);
        paths.remove(0);
        VERIFY(paths.is_empty());

        auto file = TRY(file_or_error);
        auto string = TRY(file->read_until_eof());
        for (auto const& path : StringView(string).split_view('\n')) {
            if (path.is_empty())
                continue;
            paths.append(path);
        }
    }

    Vector<ByteString> dependency_paths;
    HashTable<ByteString> seen_dependency_paths;
    Vector<ByteString> output_files;
    Vector<NonnullOwnPtr<Core::MappedFile>> files;
    Vector<LexicalPath> lexical_paths;
    Vector<IDL::Parser> parsers;
    Vector<IDL::Interface*> interfaces;

    auto append_dependency_path = [&](ByteString const& dependency_path) {
        if (seen_dependency_paths.set(dependency_path) != AK::HashSetResult::InsertedNewEntry)
            return;
        dependency_paths.append(dependency_path);
    };

    for (auto const& path : paths) {
        auto file = TRY(Core::MappedFile::map(path, Core::MappedFile::Mode::ReadOnly));
        files.append(move(file));
        lexical_paths.empend(path);
    }

    for (size_t i = 0; i < paths.size(); ++i) {
        auto import_base_paths = base_paths;
        if (import_base_paths.is_empty())
            import_base_paths.append(lexical_paths[i].dirname());

        IDL::Parser parser(paths[i], files[i]->bytes(), move(import_base_paths));
        auto& interface = parser.parse();
        interfaces.append(&interface);
        parsers.append(move(parser));

        append_dependency_path(paths[i]);
        for (auto const& imported_file : parser.imported_files())
            append_dependency_path(imported_file);
    }

    for (size_t i = 0; i < paths.size(); ++i) {
        auto const& lexical_path = lexical_paths[i];
        auto& namespace_ = lexical_path.parts_view().at(lexical_path.parts_view().size() - 2);
        auto& interface = *interfaces[i];

        // If the interface name is the same as its namespace, qualify the name in the generated code.
        // e.g. Selection::Selection
        if (IDL::libweb_interface_namespaces.span().contains_slow(namespace_)) {
            StringBuilder builder;
            builder.append(namespace_);
            builder.append("::"sv);
            builder.append(interface.implemented_name);
            interface.fully_qualified_name = builder.to_byte_string();
        } else {
            interface.fully_qualified_name = interface.implemented_name;
        }

        if constexpr (BINDINGS_GENERATOR_DEBUG)
            interface.dump();

        auto generated_file_paths = TRY(generate_binding_for_interface(interface, lexical_path, output_path));
        for (auto const& output_file : generated_file_paths.files_view())
            output_files.append(output_file);
    }

    if (!depfile_path.is_empty()) {
        TRY(generate_depfile(depfile_path, depfile_prefix, dependency_paths, output_files));
    }
    return 0;
}
