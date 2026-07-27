#include "hex/hex_highlighter_registry.h"
#include "hex/elf.h"
#include "hex/png.h"
#include "hex/jpeg.h"
#include "hex/zip.h"
#include "hex/pdf.h"
#include "hex/tar.h"

hex_highlighter_registry &hex_highlighter_registry::get_instance()
{
	static hex_highlighter_registry inst;
	return inst;
}

hex_highlighter_registry::hex_highlighter_registry()
{
	// Register ELF highlighter
	highlighters_.push_back(std::make_shared<elf_hex_highlighter>());
	// Register PNG highlighter
	highlighters_.push_back(std::make_shared<png_hex_highlighter>());
	// Register JPEG highlighter
	highlighters_.push_back(std::make_shared<jpeg_hex_highlighter>());
	// Register ZIP highlighter
	highlighters_.push_back(std::make_shared<zip_hex_highlighter>());
	// Register PDF highlighter
	highlighters_.push_back(std::make_shared<pdf_hex_highlighter>());
	// Register TAR highlighter
	highlighters_.push_back(std::make_shared<tar_hex_highlighter>());
}

std::shared_ptr<hex_highlighter> hex_highlighter_registry::detect_highlighter(std::span<const uint8_t> data) const
{
	for (const auto &hl : highlighters_) {
		if (hl->can_handle(data)) {
			return hl;
		}
	}
	return nullptr;
}
