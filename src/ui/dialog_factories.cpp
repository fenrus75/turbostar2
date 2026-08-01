/* IMPORTANT:
 * Standard pattern for dialog boxes is to set their size automatically after a ->flow call:
 *	dlg->flow();
 *	dlg->set_width(flow_ptr->width());
 *	dlg->set_height(flow_ptr->height());
 */

/*
 * Notice: src/ui/dialog_factories.cpp was split into domain-specific source files to improve maintainability and build speed.
 * The dialog factory functions defined in src/ui/dialog_factories.h are implemented across:
 *
 * 1. src/ui/dialog_basic.cpp:
 *    Basic UI dialogs (save/reload prompts, input, message, welcome, crash, ask user, plan approval, force quit, search/replace).
 *
 * 2. src/ui/dialog_settings.cpp:
 *    Configuration & settings dialogs (general settings, editor settings, run settings, syntax colors).
 *
 * 3. src/ui/dialog_ai.cpp:
 *    AI & Agent dialogs (AI settings, task models, AI models & servers list/edit, MCP config & tools, Copilot connect, tool status).
 *
 * 4. src/ui/dialog_a2a.cpp:
 *    Agent-to-Agent (A2A) dialogs (A2A settings, A2A servers list & edit).
 *
 * 5. src/ui/dialog_project.cpp:
 *    Project & file dialogs (file dialog, new project wizard, image manager, code review edit).
 */

#include "ui/dialog_factories.h"