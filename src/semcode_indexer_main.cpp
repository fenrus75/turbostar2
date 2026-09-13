#include <CLI11.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include "fs_utils.h"
#include "semcode_indexer.h"

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
	CLI::App app{"Turbostar Semcode Indexer - converts semcode JSON dumps into local SQLite database"};

	std::string functions_json;
	std::string types_json;
	std::string output_db;
	std::string project_dir;
	std::string git_head;
	std::string semcode_bin = "semcode";

	std::string query_fn;
	std::string query_type;
	size_t max_dbs = 2;
	bool prune_only = false;

	app.add_option("-f,--functions", functions_json, "Path to functions JSON dump file");
	app.add_option("-t,--types", types_json, "Path to types JSON dump file");
	app.add_option("-o,--output,-d,--database", output_db, "SQLite database file path")->required();
	app.add_option("-p,--project", project_dir, "Project directory (runs semcode dump commands automatically)");
	app.add_option("--git-head", git_head, "Git commit hash to associate with the database");
	app.add_option("--semcode-bin", semcode_bin, "Path to semcode CLI binary (default: 'semcode')");
	app.add_option("--query-fn", query_fn, "Query a function by name in the database");
	app.add_option("--query-type", query_type, "Query a type by name in the database");
	app.add_option("--max-dbs", max_dbs, "Maximum number of database files to retain in cache directory (default: 2, 0 to disable)");
	app.add_flag("--prune", prune_only, "Prune old databases in the specified directory/database location and exit");

	CLI11_PARSE(app, argc, argv);

	if (prune_only) {
		fs::path target_path(output_db);
		std::error_code ec;
		fs::path target_dir = fs::is_directory(target_path, ec) ? target_path : target_path.parent_path();
		if (target_dir.empty()) {
			target_dir = fs::current_path();
		}
		size_t pruned = semcode_indexer::prune_cache_directory(target_dir.string(), max_dbs);
		std::cout << std::format("Pruned {} old semcode database(s) in {} (retaining <= {})\n", pruned, target_dir.string(),
					 max_dbs);
		return 0;
	}

	if (!query_fn.empty() || !query_type.empty()) {
		semcode_indexer indexer(output_db);
		if (!indexer.open()) {
			std::cerr << "Error: Could not open database for querying: " << output_db << std::endl;
			return 1;
		}

		if (!query_fn.empty()) {
			auto q_start = std::chrono::steady_clock::now();
			auto results = indexer.lookup_function(query_fn);
			auto q_us =
			    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - q_start).count();
			std::cout << std::format("Function query for '{}' took {}us ({} results):\n", query_fn, q_us, results.size());
			for (const auto &fn : results) {
				std::cout << std::format("  {} ({}: {}-{})\n", fn.name, fn.file_path, fn.line_start, fn.line_end);
				if (!fn.calls.empty()) {
					std::cout << "    Calls (" << fn.calls.size() << "): ";
					for (size_t i = 0; i < fn.calls.size(); ++i) {
						std::cout << (i > 0 ? ", " : "") << fn.calls[i];
					}
					std::cout << "\n";
				}
				if (!fn.types.empty()) {
					std::cout << "    Types (" << fn.types.size() << "): ";
					for (size_t i = 0; i < fn.types.size(); ++i) {
						std::cout << (i > 0 ? ", " : "") << fn.types[i];
					}
					std::cout << "\n";
				}
			}
		}

		if (!query_type.empty()) {
			auto q_start = std::chrono::steady_clock::now();
			auto results = indexer.lookup_type(query_type);
			auto q_us =
			    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - q_start).count();
			std::cout << std::format("Type query for '{}' took {}us ({} results):\n", query_type, q_us, results.size());
			for (const auto &ty : results) {
				std::cout << std::format("  {} [kind: {}] ({}: {}-{})", ty.name, ty.kind, ty.file_path, ty.line_start,
							 ty.line_end);
				if (!ty.underlying_type.empty()) {
					std::cout << " -> " << ty.underlying_type;
				}
				std::cout << "\n";
			}
		}

		return 0;
	}

	auto t_start = std::chrono::steady_clock::now();

	semcode_indexer indexer(output_db);

	if (!project_dir.empty() && functions_json.empty() && types_json.empty()) {
		std::cout << std::format("Building semcode index for project {} using {}...\n", project_dir, semcode_bin);
		if (!indexer.build_from_project(project_dir, semcode_bin, max_dbs)) {
			std::cerr << "Error: Failed to build index from project: " << project_dir << std::endl;
			return 1;
		}
		auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count();
		std::error_code ec;
		auto db_size = fs::file_size(output_db, ec);
		std::cout << std::format("Done! Total elapsed: {}ms. Database: {} (size: {} bytes)\n", total_ms, output_db, db_size);
		return 0;
	}

	std::string temp_dir;
	if (!project_dir.empty()) {
		temp_dir = fs_utils::get_project_tmp_dir() + "/semcode_indexer_" +
			   std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
		fs::create_directories(temp_dir);

		if (functions_json.empty()) {
			std::string fn_tmp = temp_dir + "/functions.json";
			std::string cmd = std::format("{} -d {} --git-repo {} -q \"dump-functions {}\"",
						      fs_utils::escape_shell_arg(semcode_bin), fs_utils::escape_shell_arg(project_dir),
						      fs_utils::escape_shell_arg(project_dir), fs_utils::escape_shell_arg(fn_tmp));
			std::cout << "Executing: " << cmd << std::endl;
			auto dump_start = std::chrono::steady_clock::now();
			std::string out = fs_utils::execute_command_sync(cmd, 120);
			auto dump_ms =
			    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dump_start).count();
			std::cout << "Dump functions completed in " << dump_ms << "ms" << std::endl;
			if (fs_utils::is_regular_file(fn_tmp)) {
				functions_json = fn_tmp;
			}
		}

		if (types_json.empty()) {
			std::string ty_tmp = temp_dir + "/types.json";
			std::string cmd = std::format("{} -d {} --git-repo {} -q \"dump-types {}\"",
						      fs_utils::escape_shell_arg(semcode_bin), fs_utils::escape_shell_arg(project_dir),
						      fs_utils::escape_shell_arg(project_dir), fs_utils::escape_shell_arg(ty_tmp));
			std::cout << "Executing: " << cmd << std::endl;
			auto dump_start = std::chrono::steady_clock::now();
			std::string out = fs_utils::execute_command_sync(cmd, 120);
			auto dump_ms =
			    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dump_start).count();
			std::cout << "Dump types completed in " << dump_ms << "ms" << std::endl;
			if (fs_utils::is_regular_file(ty_tmp)) {
				types_json = ty_tmp;
			}
		}

		if (git_head.empty()) {
			std::string cmd = std::format("git -C {} rev-parse HEAD 2>/dev/null", fs_utils::escape_shell_arg(project_dir));
			std::string head_out = fs_utils::execute_command_sync(cmd, 5);
			size_t nl = head_out.find_first_of("\r\n");
			git_head = (nl == std::string::npos) ? head_out : head_out.substr(0, nl);
		}

		(void)indexer.load_valid_blobs_from_git(project_dir, git_head.empty() ? "HEAD" : git_head);
	}

	// Remove target output database if it already exists to start clean
	std::error_code ec;
	fs::remove(output_db, ec);

	if (!indexer.open()) {
		std::cerr << "Error: Could not open output database: " << output_db << std::endl;
		if (!temp_dir.empty()) {
			fs::remove_all(temp_dir, ec);
		}
		return 1;
	}

	size_t func_count = 0;
	if (!functions_json.empty()) {
		std::cout << "Ingesting functions from " << functions_json << "..." << std::endl;
		auto fn_start = std::chrono::steady_clock::now();
		func_count = indexer.ingest_functions_file(functions_json);
		auto fn_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - fn_start).count();
		std::cout << "  Ingested " << func_count << " functions in " << fn_ms << "ms" << std::endl;
	}

	size_t type_count = 0;
	if (!types_json.empty()) {
		std::cout << "Ingesting types from " << types_json << "..." << std::endl;
		auto ty_start = std::chrono::steady_clock::now();
		type_count = indexer.ingest_types_file(types_json);
		auto ty_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - ty_start).count();
		std::cout << "  Ingested " << type_count << " types in " << ty_ms << "ms" << std::endl;
	}

	std::cout << "Building database indices..." << std::endl;
	auto idx_start = std::chrono::steady_clock::now();
	if (!indexer.build_indices()) {
		std::cerr << "Warning: Failed to build some database indices." << std::endl;
	}
	auto idx_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - idx_start).count();
	std::cout << "  Indices built in " << idx_ms << "ms" << std::endl;

	if (!git_head.empty()) {
		(void)indexer.set_metadata("git_head", git_head);
	}
	(void)indexer.set_metadata("functions_count", std::to_string(func_count));
	(void)indexer.set_metadata("types_count", std::to_string(type_count));
	(void)indexer.set_metadata("created_at", std::to_string(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));

	indexer.close();

	if (max_dbs > 0) {
		fs::path target_path(output_db);
		fs::path target_dir = target_path.parent_path();
		if (target_dir.empty()) {
			target_dir = fs::current_path();
		}
		size_t pruned = semcode_indexer::prune_cache_directory(target_dir.string(), max_dbs);
		if (pruned > 0) {
			std::cout << std::format("Pruned {} old semcode database(s) in {} (retaining <= {})\n", pruned, target_dir.string(),
						 max_dbs);
		}
	}

	if (!temp_dir.empty()) {
		fs::remove_all(temp_dir, ec);
	}

	auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count();
	auto db_size = fs::file_size(output_db, ec);

	std::cout << std::format("Done! Total elapsed: {}ms. Database: {} (size: {} bytes, {} functions, {} types)\n", total_ms, output_db,
				 db_size, func_count, type_count);

	return 0;
}
