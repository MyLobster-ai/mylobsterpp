#pragma once

#include <CLI/CLI.hpp>

#include "openclaw/core/config.hpp"

namespace openclaw::cli {

/// Register the `gateway` subcommand.
/// Starts the OpenClaw gateway server on the configured port.
void register_gateway_command(CLI::App& app, Config& config);

/// Register the `config` subcommand.
/// Shows or validates the current configuration.
void register_config_command(CLI::App& app, Config& config);

/// Register the `version` subcommand.
/// Prints the build version and exits.
void register_version_command(CLI::App& app);

/// Register the `status` subcommand.
/// Connects to a running gateway and prints its status.
void register_status_command(CLI::App& app, Config& config);

} // namespace openclaw::cli
