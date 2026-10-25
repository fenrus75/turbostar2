#pragma once

#include <functional>
#include "inspector/inspector_provider.h"

class document;

/**
 * @brief LSP and code analysis provider for function signatures, arguments, and declarations.
 */
class lsp_inspector_provider : public inspector_provider
{
      public:
	explicit lsp_inspector_provider(std::function<document *()> active_doc_fn = nullptr);
	~lsp_inspector_provider() override = default;

	std::string_view name() const noexcept override { return "lsp"; }
	inspector_result query(const inspector_query &q) override;
	void query_async(const inspector_query &q, std::function<void(inspector_result)> callback) override;

      private:
	std::function<document *()> active_doc_fn_;
};
