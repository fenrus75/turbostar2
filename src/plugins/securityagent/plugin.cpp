#include "agentlib/tool_registry.h"
#include "agentlib/agent_animation.h"
#include "security_movie.h"

extern "C" {

const char *plugin_name(void)
{
	return "Security Agent";
}

const char *plugin_description(void)
{
	return "Provides tools for security audits and vulnerability scanning.";
}

void register_security_scan_python(void);
void unregister_security_scan_python(void);
void register_security_scan_c(void);
void unregister_security_scan_c(void);
void register_security_scan_semgrep(void);
void unregister_security_scan_semgrep(void);
void register_security_review_with_agent(void);
void unregister_security_review_with_agent(void);

/*
 * plugin_run serves as the entry point when the plugin is loaded dynamically.
 * extern "C" prevents C++ name mangling so the host can resolve the symbol.
 */
void plugin_run(void)
{
	register_security_scan_python();
	register_security_scan_c();
	register_security_scan_semgrep();
	register_security_review_with_agent();
	register_agent_animation("securityagent", reinterpret_cast<const char *>(security_movie_json));
	// Reminder: We will not register a new tool family.
	// But we will use the (hidden) ":plugin:securityagent" tool family
	// for all (except one) of the tools in this plugin.
}

/*
 * plugin_unload is called programmatically before the plugin is closed.
 */
void plugin_unload(void)
{
	unregister_security_scan_python();
	unregister_security_scan_c();
	unregister_security_scan_semgrep();
	unregister_security_review_with_agent();
	unregister_agent_animation("securityagent");
}
}

