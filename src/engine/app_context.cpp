#include "engine/app_context.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace snapback {
namespace {

constexpr std::array<const char*, 6> kBrowsers = {
    "chrome", "msedge", "firefox", "brave", "opera", "safari"};
constexpr std::array<const char*, 8> kIdes = {
    "code", "cursor", "devenv", "idea", "pycharm", "clion", "rider", "xcode"};
constexpr std::array<const char*, 6> kCommunication = {
    "slack", "discord", "teams", "outlook", "zoom", "messages"};
constexpr std::array<const char*, 5> kEntertainmentApps = {
    "spotify", "steam", "vlc", "netflix", "youtube"};
constexpr std::array<const char*, 7> kProductivity = {
    "word", "excel", "powerpnt", "notion", "obsidian", "pages", "figma"};
constexpr std::array<const char*, 6> kTerminals = {
    "terminal", "iterm", "warp", "alacritty", "kitty", "wezterm"};
constexpr std::array<const char*, 11> kDistractingTitleKeywords = {
    "youtube", "netflix", "twitter", "reddit", "instagram", "tiktok",
    "twitch", "facebook", "hulu", "disney+", "prime video"};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

template <std::size_t N>
bool contains_any(const std::string& value, const std::array<const char*, N>& needles) {
    return std::any_of(needles.begin(), needles.end(),
                       [&](const char* needle) { return value.find(needle) != std::string::npos; });
}

bool matches_rule_pattern(const std::string& pattern,
                          const std::string& app_name,
                          const std::string& window_title) {
    return app_name.find(pattern) != std::string::npos ||
           window_title.find(pattern) != std::string::npos;
}

double alignment_score(const std::string& goal, const AppContext& ctx, const std::string& title) {
    const std::string g = lower(goal);
    const std::string t = lower(title);
    if (g.empty()) return 0.5;

    auto has = [&](std::initializer_list<const char*> words) {
        return std::any_of(words.begin(), words.end(),
                           [&](const char* word) { return g.find(word) != std::string::npos; });
    };

    double best = 0.5;
    if (has({"code", "coding", "bug", "fix", "implement", "rust", "api", "refactor",
             "test", "debug", "build", "feature", "compile", "merge", "pr",
             "pull request"})) {
        if (ctx.is_ide || ctx.is_terminal) best = std::max(best, 0.95);
        else if (ctx.is_browser &&
                 (t.find("github") != std::string::npos ||
                  t.find("stackoverflow") != std::string::npos ||
                  t.find("docs.rs") != std::string::npos ||
                  t.find("documentation") != std::string::npos)) best = std::max(best, 0.8);
        else if (ctx.is_communication) best = std::max(best, 0.35);
    }
    if (has({"write", "writing", "report", "essay", "document", "blog", "draft",
             "paper", "notes"})) {
        if (ctx.is_productivity) best = std::max(best, 0.95);
        else if (ctx.is_ide && (t.find(".md") != std::string::npos ||
                                t.find("readme") != std::string::npos)) best = std::max(best, 0.85);
        else if (ctx.is_browser && !ctx.title_is_distracting) best = std::max(best, 0.55);
    }
    if (has({"research", "read", "reading", "learn", "study", "article", "docs",
             "documentation"})) {
        if (ctx.is_browser && !ctx.title_is_distracting) best = std::max(best, 0.85);
        else if (ctx.is_productivity || ctx.is_ide) best = std::max(best, 0.65);
    }
    if (has({"email", "meeting", "call", "slack", "message", "reply", "interview",
             "present"})) {
        if (ctx.is_communication) best = std::max(best, 0.9);
        else if (ctx.is_browser && t.find("mail") != std::string::npos) best = std::max(best, 0.85);
    }
    return std::clamp(best, 0.0, 1.0);
}

}  // namespace

std::string AppContext::productivity_category() const {
    if (personal_block) return "Entertainment";
    if (is_ide) return "Building";
    if (is_productivity || personal_allow) return "Writing";
    if (is_browser) return "Browsing";
    if (is_communication) return "Communicating";
    if (is_entertainment) return "Entertainment";
    return "Unknown";
}

AppContext classify_app_context(const std::string& app_name,
                                const std::string& window_title,
                                const std::vector<AppRuleRecord>& rules) {
    const std::string name = lower(app_name);
    const std::string title = lower(window_title);

    AppContext ctx;
    ctx.is_browser = contains_any(name, kBrowsers);
    ctx.is_ide = contains_any(name, kIdes);
    ctx.is_communication = contains_any(name, kCommunication);
    ctx.is_entertainment = contains_any(name, kEntertainmentApps);
    ctx.is_productivity = contains_any(name, kProductivity);
    ctx.is_terminal = contains_any(name, kTerminals);
    ctx.title_is_distracting = contains_any(title, kDistractingTitleKeywords);

    bool allow = false;
    bool block = false;
    for (const auto& rule : rules) {
        const std::string pattern = lower(rule.pattern);
        if (!matches_rule_pattern(pattern, name, title)) continue;
        if (rule.rule_type == AppRuleKind::Block) block = true;
        if (rule.rule_type == AppRuleKind::Allow) allow = true;
    }

    if (block) {
        ctx.personal_block = true;
        ctx.personal_allow = false;
        ctx.is_entertainment = true;
        ctx.title_is_distracting = true;
    } else if (allow) {
        ctx.personal_allow = true;
        ctx.is_entertainment = false;
        ctx.title_is_distracting = false;
        ctx.is_productivity = true;
    }
    return ctx;
}

bool is_clearly_off_task(const AppContext& ctx) {
    if (ctx.personal_allow) return false;
    return ctx.personal_block || ctx.is_entertainment || ctx.title_is_distracting;
}

bool snapback_on_task(const AppContext& ctx,
                      const std::string& window_title,
                      const std::optional<std::string>& focus_state,
                      const std::optional<std::string>& session_goal) {
    if (is_clearly_off_task(ctx)) return false;
    if (focus_state == std::optional<std::string>("DISTRACTED")) return false;
    if (ctx.personal_allow) return true;

    if (session_goal && !session_goal->empty()) {
        const double score = alignment_score(*session_goal, ctx, window_title);
        if (score >= 0.72) return true;
        if (score <= 0.35) return false;
    }

    if (ctx.is_ide || ctx.is_productivity || ctx.is_terminal || ctx.is_communication) return true;
    if (focus_state == std::optional<std::string>("PRODUCTIVE") ||
        focus_state == std::optional<std::string>("DEEP_FOCUS") ||
        focus_state == std::optional<std::string>("PSEUDO_PRODUCTIVE")) return true;
    return false;
}

}  // namespace snapback
