#include "user_message.h"

namespace agentlib {

std::vector<interaction_line> interaction_user_message::format_lines(int width) const {
    return wrap_text("> ", text_, width, 1);
}

} // namespace agentlib
