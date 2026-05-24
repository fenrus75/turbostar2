#pragma once

namespace agentlib {

enum class interaction_role {
    agent,
    user,
    thinking,
    system,
    error,
    diff_add,
    diff_remove,
    terminal,
    header
};

enum class background_mode {
    primary,    // White background
    alternate   // Cyan background
};

/**
 * @brief Returns the ncurses color pair for a given role and background mode.
 * 
 * Pairs 50-59 are for primary background.
 * Pairs 60-69 are for alternate background.
 */
inline int get_color_pair(interaction_role role, background_mode bg) {
    if (role == interaction_role::terminal) return 29;    // White on Black
    if (role == interaction_role::diff_add) return 30;    // Bright Green on Blue
    if (role == interaction_role::diff_remove) return 31; // Bright Red on Blue
    if (role == interaction_role::header) return 32;      // Bright Cyan on Blue
    
    int base = (bg == background_mode::primary) ? 50 : 60;
    
    switch (role) {
        case interaction_role::agent:       return base + 0; // Black
        case interaction_role::user:        return base + 1; // Blue
        case interaction_role::thinking:    return base + 2; // Green
        case interaction_role::system:      return base + 3; // Red
        case interaction_role::error:       return base + 3; // Red
        default: return 50; // Fallback to black on white
    }
}

} // namespace agentlib
