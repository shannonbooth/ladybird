/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

//! Finds the Rust bindings of the interfaces implemented in Rust, which Meta/Generators/generate_libweb_bindings.py
//! writes to Bindings/RustBindings.rs in the build directory of LibWeb. A build of LibWeb names that file in
//! LIBWEB_RUST_BINDINGS (Libraries/LibWeb/CMakeLists.txt). Building the crate on its own, as cargo test and cargo
//! clippy do, runs the generator on the IDL files of Libraries/LibWeb/idl_files.cmake instead. rustc tracks the file
//! itself, as the crate includes it.
//!
//! Also generates what the WebGL implementation needs from the rest of the tree: the methods of its recorder, from the
//! model of the command stream that libcompositing_rust has, and the GL constants. Those do not need the bindings, so
//! every build of the crate has them.

use std::env;
use std::ffi::{OsStr, OsString};
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

#[path = "generate/webgl_constants.rs"]
mod webgl_constants;

#[path = "generate/webgl_proxy.rs"]
mod webgl_proxy;

#[allow(dead_code, reason = "the model is for the generators of the other crates as well")]
#[path = "../../../LibCompositing/Rust/generate/webgl_wire_model.rs"]
mod webgl_wire_model;

fn read_tracked(path: &Path) -> String {
    println!("cargo:rerun-if-changed={}", path.display());
    fs::read_to_string(path).unwrap_or_else(|error| panic!("read {}: {error}", path.display()))
}

fn generate_webgl_command_recording(libraries: &Path, out_dir: &Path) {
    println!(
        "cargo:rerun-if-changed={}",
        libraries.join("LibCompositing/Rust/generate").display()
    );
    let functions_path = libraries.join("LibCompositing/WebGL/GLFunctions.json");
    let generate = |json: &str| -> Result<String, String> {
        let functions = webgl_wire_model::parse_functions(json)?;
        webgl_proxy::write_proxy(&functions)
    };
    let proxy = generate(&read_tracked(&functions_path))
        .unwrap_or_else(|error| panic!("{}: {error}", functions_path.display()));
    fs::write(out_dir.join("webgl_proxy.rs"), proxy).expect("write webgl_proxy.rs");
}

fn idl_files_in(directory: &Path, idl_files: &mut Vec<PathBuf>) {
    // NB: Tracking the directory reruns the script when an IDL file is added to it or removed from it.
    println!("cargo:rerun-if-changed={}", directory.display());
    let entries = fs::read_dir(directory).unwrap_or_else(|error| panic!("read {}: {error}", directory.display()));
    for entry in entries {
        let path = entry.expect("directory entry").path();
        if path.is_dir() {
            idl_files_in(&path, idl_files);
        } else if path.extension().is_some_and(|extension| extension == "idl") {
            idl_files.push(path);
        }
    }
}

fn generate_webgl_constants(libraries: &Path, out_dir: &Path) {
    let mut idl_files = Vec::new();
    idl_files_in(&libraries.join("LibWeb/WebGL"), &mut idl_files);
    // NB: The order of a directory listing is not defined, and the order of the constants follows from it.
    idl_files.sort();
    let mut constants = Vec::new();
    for idl_file in &idl_files {
        webgl_constants::collect_constants(&read_tracked(idl_file), &mut constants)
            .unwrap_or_else(|error| panic!("{}: {error}", idl_file.display()));
    }
    fs::write(
        out_dir.join("webgl_constants.rs"),
        webgl_constants::write_constants(&constants),
    )
    .expect("write webgl_constants.rs");
}

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=generate");
    let manifest_dir = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR"));
    let libraries = manifest_dir
        .ancestors()
        .nth(3)
        .expect("the crate is in Libraries/LibWeb/Bindings/Rust");
    generate_webgl_command_recording(libraries, &out_dir);
    generate_webgl_constants(libraries, &out_dir);
    generate_css_numeric_factories(libraries, &out_dir);

    println!("cargo:rerun-if-env-changed=LIBWEB_RUST_BINDINGS");
    let bindings = match env::var_os("LIBWEB_RUST_BINDINGS") {
        Some(bindings) => {
            let bindings = PathBuf::from(bindings);
            assert!(
                bindings.is_file(),
                "LIBWEB_RUST_BINDINGS names {}, which does not exist; a build of LibWeb generates the bindings before \
                 it builds the crate",
                bindings.display()
            );
            bindings
        }
        None => generate_bindings(libraries, &out_dir),
    };
    println!("cargo:rustc-env=LIBWEB_RUST_BINDINGS={}", bindings.display());
}

/// The IDL file that a build of LibWeb generates from CSS/Units.json rather than keeping in the tree.
const GENERATED_IDL_FILE: &str = "CSS/GeneratedCSSNumericFactoryMethods";

fn run_python(python: &OsStr, script: &Path, arguments: &[&OsStr]) {
    println!("cargo:rerun-if-changed={}", script.display());
    let status = Command::new(python)
        .arg(script)
        .args(arguments)
        // NB: Bytecode cached next to the generators would rerun the script on every build.
        .env("PYTHONDONTWRITEBYTECODE", "1")
        .status()
        .unwrap_or_else(|error| panic!("run {}: {error}", script.display()));
    assert!(status.success(), "{} failed", script.display());
}

fn python_files_in(directory: &Path) {
    let entries = fs::read_dir(directory).unwrap_or_else(|error| panic!("read {}: {error}", directory.display()));
    for entry in entries {
        let path = entry.expect("directory entry").path();
        if path.extension().is_some_and(|extension| extension == "py") {
            println!("cargo:rerun-if-changed={}", path.display());
        }
    }
}

/// Generates the bindings from the IDL files that a build of LibWeb generates its bindings from.
fn generate_bindings(libraries: &Path, out_dir: &Path) -> PathBuf {
    let libweb = libraries.join("LibWeb");
    let meta = libraries
        .parent()
        .expect("Libraries is in the root of the tree")
        .join("Meta");
    println!("cargo:rerun-if-env-changed=PYTHON");
    let python = env::var_os("PYTHON").unwrap_or_else(|| if cfg!(windows) { "python" } else { "python3" }.into());

    // NB: The generator derives the Rust module of an IDL file from the directory under LibWeb that holds it.
    let generated_idl_file = out_dir.join("LibWeb").join(format!("{GENERATED_IDL_FILE}.idl"));
    fs::create_dir_all(generated_idl_file.parent().expect("the IDL file is in a directory"))
        .expect("the output directory is writable");
    run_python(
        &python,
        &meta.join("Generators/generate_libweb_css_numeric_factory_methods.py"),
        &[
            "-i".as_ref(),
            generated_idl_file.as_os_str(),
            "-j".as_ref(),
            libweb.join("CSS/Units.json").as_os_str(),
        ],
    );

    let idl_list = read_tracked(&libweb.join("idl_files.cmake"));
    let mut idl_files = String::new();
    for line in idl_list.lines() {
        let Some(name) = line
            .strip_prefix("libweb_js_bindings(")
            .and_then(|rest| rest.strip_suffix(')'))
        else {
            continue;
        };
        let path = if name == GENERATED_IDL_FILE {
            generated_idl_file.clone()
        } else {
            let path = libweb.join(format!("{name}.idl"));
            println!("cargo:rerun-if-changed={}", path.display());
            path
        };
        idl_files.push_str(path.to_str().expect("the path of an IDL file is UTF-8"));
        idl_files.push('\n');
    }
    let idl_files_path = out_dir.join("idl_files.txt");
    fs::write(&idl_files_path, idl_files).expect("the output directory is writable");

    python_files_in(&meta.join("Generators/libweb_bindings"));
    for module in ["css_dimensions.py", "lexer.py", "utils.py", "webidl_parser.py"] {
        println!("cargo:rerun-if-changed={}", meta.join("Utils").join(module).display());
    }
    let mut response_file = OsString::from("@");
    response_file.push(&idl_files_path);
    run_python(
        &python,
        &meta.join("Generators/generate_libweb_bindings.py"),
        &[
            "--rust-only".as_ref(),
            "-o".as_ref(),
            out_dir.as_os_str(),
            &response_file,
        ],
    );
    out_dir.join("RustBindings.rs")
}

/// The functions of the CSS namespace that make a CSSUnitValue of a unit, one for each unit of CSS/Units.json,
/// https://drafts.css-houdini.org/css-typed-om-1/#numeric-factory
fn generate_css_numeric_factories(libraries: &Path, out_dir: &Path) {
    let units_path = libraries.join("LibWeb/CSS/Units.json");
    println!("cargo:rerun-if-changed={}", units_path.display());
    let units = fs::read_to_string(&units_path).expect("CSS/Units.json is readable");
    let dimensions: serde_json::Value = serde_json::from_str(&units).expect("CSS/Units.json is JSON");

    let mut names = vec!["number".to_string(), "percent".to_string()];
    for units in dimensions.as_object().expect("CSS/Units.json is an object").values() {
        names.extend(units.as_object().expect("a dimension is an object").keys().cloned());
    }

    let mut output = String::from("impl CSS {\n");
    for name in names {
        let function = name.to_lowercase();
        let identifier = if function == "in" { "r#in".to_string() } else { function };
        output.push_str(&format!(
            "    pub(crate) fn {identifier}(cx: &mut CallContext, value: f64) -> WebIdlResult<Ref<CSSUnitValue>> {{\n        Self::numeric_factory(cx, value, \"{name}\")\n    }}\n\n"
        ));
    }
    output.push_str("}\n");
    fs::write(out_dir.join("css_numeric_factories.rs"), output).expect("the output directory is writable");
}
