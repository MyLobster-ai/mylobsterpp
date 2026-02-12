#pragma once

#include <memory>
#include <string>

#include <CLI/CLI.hpp>

#include "openclaw/core/config.hpp"

namespace openclaw::cli {

/// Top-level CLI application.
///
/// Parses command-line arguments using CLI11, dispatches to registered
/// subcommands (gateway, config, version, status), and manages the
/// configuration lifecycle.
class App {
public:
    App();
    ~App();

    // Non-copyable, non-movable.
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// Parse arguments and execute the selected subcommand.
    /// @returns Process exit code (0 on success).
    auto run(int argc, char** argv) -> int;

    /// Access the underlying CLI11 app (for testing or extension).
    [[nodiscard]] auto cli() -> CLI::App&;

    /// Access the loaded configuration.
    [[nodiscard]] auto config() -> Config&;
    [[nodiscard]] auto config() const -> const Config&;

private:
    /// Register all subcommands on the CLI11 app.
    void setup_commands();

    CLI::App cli_;
    Config config_;
    std::string config_path_;
};

} // namespace openclaw::cli
