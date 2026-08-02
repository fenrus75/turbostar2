#include "agentlib/tool_registry.h"

extern "C" {

const char *plugin_name(void)
{
	return "SQLite Tools";
}

const char *plugin_description(void)
{
	return "Provides local persistent SQLite database management tools.";
}

void register_sqlite_create_db(void);
void unregister_sqlite_create_db(void);
void register_sqlite_delete_db(void);
void unregister_sqlite_delete_db(void);
void register_sqlite_list_db(void);
void unregister_sqlite_list_db(void);
void register_sqlite_perform(void);
void unregister_sqlite_perform(void);

void plugin_run(void)
{
	register_sqlite_create_db();
	register_sqlite_delete_db();
	register_sqlite_list_db();
	register_sqlite_perform();

	agentlib::tool_registry::get_instance().register_tool_family(
		"sqlite",
		"Activate when inspecting, creating, or querying local SQLite databases, or if you want to keep a todo list in a database",
		"The 'sqlite' tool family enables persistent SQL database creation, schema management, and query execution within the project workspace without requiring external CLI utilities or Python scripts.\n\n"
		"### Key Concepts & Rules\n"
		"- **Database Naming**: Pass simple identifiers (e.g., `todos` or `analytics`) for the `database` parameter. Do **not** include directory paths or file extensions (e.g., use `todos`, NOT `todos.db` or `tmp://todos.db`).\n"
		"- **Persistence**: Databases are automatically stored in the project's internal SQLite database store and persist across sessions.\n"
		"- **Data Types & Formatting**: Standard SQLite datatypes (`TEXT`, `INTEGER`, `REAL`, `BLOB`, `BOOLEAN`) are fully supported. Query outputs are returned as Markdown formatted tables.\n\n"
		"### Workflow Example: Creating a Table, Inserting Records & Querying Data\n\n"
		"1. **List Existing Databases**:\n"
		"   `sqlite_list_db()`\n"
		"   *Checks if target database already exists in the workspace.*\n\n"
		"2. **Create Database**:\n"
		"   `sqlite_create_db(database=\"todos\")` \n"
		"   *Creates a persistent database named `todos` (simple identifier without `.db` extension).*\n\n"
		"3. **Initialize Schema**:\n"
		"   `sqlite_perform(database=\"todos\", query=\"CREATE TABLE IF NOT EXISTS todos (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, completed BOOLEAN NOT NULL DEFAULT 0);\")` \n\n"
		"4. **Insert Record**:\n"
		"   `sqlite_perform(database=\"todos\", query=\"INSERT INTO todos (name, completed) VALUES ('Buy milk', 0);\")` \n\n"
		"5. **Query & Inspect Data**:\n"
		"   `sqlite_perform(database=\"todos\", query=\"SELECT * FROM todos WHERE completed = 0;\")` \n"
		"   *Returns a formatted Markdown table of matching rows.*"
	);
}

void plugin_unload(void)
{
	unregister_sqlite_create_db();
	unregister_sqlite_delete_db();
	unregister_sqlite_list_db();
	unregister_sqlite_perform();
	agentlib::tool_registry::get_instance().unregister_tool_family("sqlite");
}

}
