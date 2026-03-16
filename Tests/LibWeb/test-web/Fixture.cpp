/*
 * Copyright (c) 2024, Andrew Kaster <andrew@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Fixture.h"
#include "Application.h"

#include <AK/ByteBuffer.h>
#include <AK/LexicalPath.h>
#include <AK/StringView.h>
#include <LibCore/Process.h>
#include <LibCore/StandardPaths.h>
#include <LibCore/System.h>

namespace TestWeb {

static ByteString s_fixtures_path;

// Key function for Fixture
Fixture::~Fixture() = default;

Optional<Fixture&> Fixture::lookup(StringView name)
{
    for (auto const& fixture : all()) {
        if (fixture->name() == name)
            return *fixture;
    }
    return {};
}

Vector<NonnullOwnPtr<Fixture>>& Fixture::all()
{
    static Vector<NonnullOwnPtr<Fixture>> fixtures;
    return fixtures;
}

class HttpEchoServerFixture final : public Fixture {
public:
    virtual ErrorOr<void> setup(WebView::WebContentOptions&) override;
    virtual void teardown_impl() override;
    virtual StringView name() const override { return "HttpEchoServer"sv; }
    virtual bool is_running() const override { return !m_processes.is_empty(); }

private:
    ErrorOr<u16> launch_server(ByteString const& log_path, Vector<ByteString> const& arguments);

    ByteString m_script_path { "http-test-server.py" };
    Vector<Core::Process> m_processes;
};

#if defined(AK_OS_WINDOWS)

ErrorOr<void> HttpEchoServerFixture::setup(WebView::WebContentOptions&)
{
    VERIFY(0 && "HttpEchoServerFixture::setup is not implemented");
}

void HttpEchoServerFixture::teardown_impl()
{
    VERIFY(0 && "HttpEchoServerFixture::teardown_impl is not implemented");
}

#else

ErrorOr<void> HttpEchoServerFixture::setup(WebView::WebContentOptions& web_content_options)
{
    auto const script_path = LexicalPath::join(s_fixtures_path, m_script_path);
    auto const wpt_path = LexicalPath::join(Application::the().test_root_path, "WPT"sv, "wpt"sv).string();
    auto const arguments = Vector { script_path.string(), "--directory", Application::the().test_root_path, "--wpt-directory", wpt_path };

    // FIXME: Pick a more reasonable log path that is more observable
    auto const log_path = LexicalPath::join(Core::StandardPaths::tempfile_directory(), "http-test-server.log"sv).string();
    auto const secondary_log_path = LexicalPath::join(Core::StandardPaths::tempfile_directory(), "http-test-server-secondary.log"sv).string();

    web_content_options.echo_server_port = TRY(launch_server(log_path, arguments));
    web_content_options.secondary_echo_server_port = TRY(launch_server(secondary_log_path, arguments));

    return {};
}

ErrorOr<u16> HttpEchoServerFixture::launch_server(ByteString const& log_path, Vector<ByteString> const& arguments)
{
    auto stdout_fds = TRY(Core::System::pipe2(0));

    auto const process_options = Core::ProcessSpawnOptions {
        .executable = Application::the().python_executable_path,
        .search_for_executable_in_path = true,
        .arguments = arguments,
        .file_actions = {
            Core::FileAction::OpenFile { ByteString::formatted("{}.stderr", log_path), Core::File::OpenMode::Write, STDERR_FILENO },
            Core::FileAction::DupFd { stdout_fds[1], STDOUT_FILENO } }
    };

    auto process = TRY(Core::Process::spawn(process_options));

    TRY(Core::System::close(stdout_fds[1]));

    auto const stdout_file = MUST(Core::File::adopt_fd(stdout_fds[0], Core::File::OpenMode::Read));

    auto buffer = MUST(ByteBuffer::create_uninitialized(16));
    TRY(stdout_file->read_some(buffer));

    auto const raw_output = ByteString { buffer, AK::ShouldChomp::Chomp };

    auto const trimmed_output = raw_output.trim_whitespace();
    if (auto const maybe_port = trimmed_output.to_number<u16>(); maybe_port.has_value()) {
        m_processes.append(move(process));
        return maybe_port.value();
    }

    return Error::from_string_literal("Failed to read echo server port from fixture output");
}

void HttpEchoServerFixture::teardown_impl()
{
    for (auto& process : m_processes) {
        if (auto kill_or_error = Core::System::kill(process.pid(), SIGINT); kill_or_error.is_error()) {
            if (kill_or_error.error().code() != ESRCH) {
                warnln("Failed to kill HTTP echo server, error: {}", kill_or_error.error());
            } else if (auto termination_or_error = process.wait_for_termination(); termination_or_error.is_error()) {
                warnln("Failed to terminate HTTP echo server, error: {}", termination_or_error.error());
            }
        }
    }

    m_processes.clear();
}

#endif

void Fixture::initialize_fixtures()
{
    s_fixtures_path = LexicalPath::join(Application::the().test_root_path, "Fixtures"sv).string();

    auto& registry = all();
    registry.append(make<HttpEchoServerFixture>());
}

}
