// Classify the foreground app into focus-relevant categories.
// Rust: engine/app_context.rs. Plain string matching + user AppRules override.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "types.hpp"

namespace snapback {

struct AppContext {
    bool is_browser{};
    bool is_ide{};
    bool is_communication{};
    bool is_entertainment{};
    bool is_productivity{};
    bool is_terminal{};
    bool title_is_distracting{};
    bool personal_allow{};
    bool personal_block{};

    std::string productivity_category() const;
};

AppContext classify_app_context(const std::string& app_name,
                                const std::string& window_title,
                                const std::vector<AppRuleRecord>& rules = {});

std::vector<GoalCategory> default_goal_categories();
double goal_alignment_score(const std::optional<std::string>& goal,
                            const AppContext& ctx,
                            const std::string& title,
                            const std::vector<GoalCategory>& categories = {});

bool is_clearly_off_task(const AppContext& ctx);
bool snapback_on_task(const AppContext& ctx,
                      const std::string& window_title,
                      const std::optional<std::string>& focus_state = std::nullopt,
                      const std::optional<std::string>& session_goal = std::nullopt,
                      const std::vector<GoalCategory>& categories = {});

}  // namespace snapback
