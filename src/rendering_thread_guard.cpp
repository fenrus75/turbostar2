#include "rendering_thread_guard.h"
#include "event_logger.h"

rendering_thread_guard::rendering_thread_guard() noexcept
{
	is_rendering_thread_ = true;
}

rendering_thread_guard::~rendering_thread_guard() noexcept
{
	is_rendering_thread_ = false;
}

bool rendering_thread_guard::is_rendering_thread() noexcept
{
	return is_rendering_thread_;
}

void rendering_thread_guard::log_latency_warning(std::string_view tag, std::string_view details) noexcept
{
	event_logger::get_instance().log_warn("LATENCY WARNING [{}]: {} executed on rendering thread", tag, details);
}
