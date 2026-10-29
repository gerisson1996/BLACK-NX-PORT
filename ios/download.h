#pragma once
#include <atomic>
#include <string>

// iOS: downloads the player's own disc image from a direct link into dest_path, on the calling thread (the caller
// keeps the main thread pumping events). percent is written as it goes, 0..100; on failure error says why.
bool IosDownloadFile(const std::string& url, const std::string& dest_path, std::atomic<int>& percent, std::string& error);
