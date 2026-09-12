#include "lsp_manager.h"
#include "event_logger.h"
#include "project_manager.h"
#include "semcode_backend.h"
#include "standard_lsp_backend.h"

std::unique_ptr<lsp_backend> lsp_manager::create_default_backend(const std::string &project_root)
{
	std::string root = project_root;
	if (root.empty()) {
		root = project_manager::get_instance().get_project_root();
	}
	if (!root.empty() && semcode_backend::is_available(root)) {
		event_logger::get_instance().log("semcode index detected in '{}' with semcode-lsp available. Activating semcode_backend.", root);
		return std::make_unique<semcode_backend>(root);
	}
	return std::make_unique<standard_lsp_backend>();
}

lsp_manager::lsp_manager()
	: backend_(create_default_backend())
{
}

lsp_manager::lsp_manager(std::string_view project_root)
	: backend_(create_default_backend(std::string(project_root)))
{
}

lsp_manager::lsp_manager(std::unique_ptr<lsp_backend> backend)
	: backend_(std::move(backend))
{
}

lsp_manager::~lsp_manager()
{
	stop();
}

void lsp_manager::set_backend(std::unique_ptr<lsp_backend> backend)
{
	if (backend_) {
		backend_->stop();
	}
	backend_ = std::move(backend);
}

lsp_backend *lsp_manager::get_backend() const noexcept
{
	return backend_.get();
}

void lsp_manager::start(event_queue &queue)
{
	if (backend_) {
		backend_->start(queue);
	}
}

void lsp_manager::stop()
{
	if (backend_) {
		backend_->stop();
	}
}

void lsp_manager::open_document(const std::string &filepath, const std::string &text)
{
	if (backend_) {
		backend_->open_document(filepath, text);
	}
}

void lsp_manager::update_document(const std::string &filepath, const std::string &text)
{
	if (backend_) {
		backend_->update_document(filepath, text);
	}
}

void lsp_manager::request_hover(const std::string &filepath, int line, int character)
{
	if (backend_) {
		backend_->request_hover(filepath, line, character);
	}
}

void lsp_manager::request_document_highlight(const std::string &filepath, int line, int character)
{
	if (backend_) {
		backend_->request_document_highlight(filepath, line, character);
	}
}

void lsp_manager::request_selection_range(const std::string &filepath, int line, int character)
{
	if (backend_) {
		backend_->request_selection_range(filepath, line, character);
	}
}

bool lsp_manager::is_supported_file(const std::string &filepath) const
{
	if (backend_) {
		return backend_->is_supported_file(filepath);
	}
	return false;
}

std::vector<text_range> lsp_manager::query_selection_ranges(const std::string &filepath, int line, int character)
{
	if (backend_) {
		return backend_->query_selection_ranges(filepath, line, character);
	}
	return {};
}

std::vector<lsp_manager::location_info> lsp_manager::query_definition(const std::string &filepath, int line, int character)
{
	if (backend_) {
		return backend_->query_definition(filepath, line, character);
	}
	return {};
}

std::vector<lsp_manager::location_info> lsp_manager::query_references(const std::string &filepath, int line, int character)
{
	if (backend_) {
		return backend_->query_references(filepath, line, character);
	}
	return {};
}

std::vector<lsp_manager::symbol_info> lsp_manager::query_workspace_symbols(const std::string &query)
{
	if (backend_) {
		return backend_->query_workspace_symbols(query);
	}
	return {};
}

std::vector<lsp_manager::symbol_node> lsp_manager::query_document_symbols(const std::string &filepath)
{
	if (backend_) {
		return backend_->query_document_symbols(filepath);
	}
	return {};
}

void lsp_manager::invalidate_symbol_cache(const std::string &filepath)
{
	if (backend_) {
		backend_->invalidate_symbol_cache(filepath);
	}
}

std::vector<lsp_manager::call_hierarchy_item> lsp_manager::query_call_hierarchy_outgoing(const std::string &filepath, int line, int character)
{
	if (backend_) {
		return backend_->query_call_hierarchy_outgoing(filepath, line, character);
	}
	return {};
}

std::vector<lsp_manager::outgoing_call_item> lsp_manager::query_call_hierarchy_outgoing_batch(
	const std::string &filepath,
	const std::vector<std::pair<int, int>> &positions,
	std::chrono::steady_clock::time_point deadline)
{
	if (backend_) {
		return backend_->query_call_hierarchy_outgoing_batch(filepath, positions, deadline);
	}
	return {};
}

std::vector<lsp_manager::type_hierarchy_item> lsp_manager::query_type_hierarchy_supertypes(const std::string &filepath, int line, int character)
{
	if (backend_) {
		return backend_->query_type_hierarchy_supertypes(filepath, line, character);
	}
	return {};
}

std::optional<std::vector<diagnostic_info>> lsp_manager::query_file_diagnostics(const std::string &filepath)
{
	if (backend_) {
		return backend_->query_file_diagnostics(filepath);
	}
	return std::nullopt;
}

void lsp_manager::store_file_diagnostics(const std::string &filepath, const std::vector<diagnostic_info> &diags)
{
	if (backend_) {
		backend_->store_file_diagnostics(filepath, diags);
	}
}
