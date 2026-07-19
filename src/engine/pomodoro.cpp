#include "engine/pomodoro.hpp"

#include <nlohmann/json.hpp>

namespace snapback {

void to_json(nlohmann::json& json, const PomodoroStatus& status) {
    json = nlohmann::json{{"running", status.running},
                          {"phase", pomodoro_phase_as_str(status.phase)},
                          {"completedWorkIntervals", status.completed_work_intervals},
                          {"remainingMs", status.remaining_ms}};
}

}  // namespace snapback
