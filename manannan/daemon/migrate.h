//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// The migration from a 0.3 installation (manannan.yaml and an environment
// file of KEY=VALUE lines) to 0.4's config.yaml and secrets.yaml.
//
// manannan.log_level moves to log.level. The Route 53 base credentials move
// out of the configuration into secrets.yaml, and AWS_ACCESS_KEY_ID,
// AWS_SECRET_ACCESS_KEY and AWS_SESSION_TOKEN from the environment file fill
// whatever that block left empty, as the plugin's fallback did. LOG_LEVEL in
// the environment file becomes log.level. Any other variable cannot be
// placed, so the migration is refused, naming it, and nothing is written. A
// missing environment file is an empty one.
#pragma once
#include <filesystem>

namespace manannan::daemon {

// Writes config (mode 0644) and secrets (mode 0600), each replaced whole.
void migrate(const std::filesystem::path& old_config, const std::filesystem::path& environment, const std::filesystem::path& config,
             const std::filesystem::path& secrets);

}  // namespace manannan::daemon
