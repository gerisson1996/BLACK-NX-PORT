#pragma once
#include <filesystem>
#include <string>

// Extracts a ZIP made by the usual tools into dest, keeping its folders. Stored and deflated entries only; on error
// says why and leaves what it wrote.
bool UnzipTo(const std::filesystem::path& zip, const std::filesystem::path& dest, std::string& error);
