#pragma once
#include <string>

/**
 * @brief Manages project-level context, such as repository root and project-specific instructions (AGENT.md/GEMINI.md).
 */
class project_manager
{
      public:
	static project_manager &get_instance();

	/**
	 * @brief Initializes the project manager by locating the repository root and loading instructions.
	 */
	void initialize();

	/**
	 * @brief Returns the absolute path to the project/repository root.
	 */
	std::string get_repository_root() const { return repo_root_; }

	/**
	 * @brief Returns the content of AGENT.md or GEMINI.md if found at the root.
	 */
	std::string get_project_instructions() const { return instructions_; }

      private:
	project_manager() = default;
	
	void load_instructions();

	std::string repo_root_;
	std::string instructions_;
};
