#pragma once

#include "transport/ndjson_source.h"

#include <atomic>
#include <string>
#include <thread>

namespace skygraph::viewer {

// Replay source: reads a recorded NDJSON.gz (or raw NDJSON) file and feeds
// it through the same NdjsonSource interface that PipeClient uses, so every
// panel works identically in replay mode.
//
// The reader streams the file as fast as the UI can consume it, with a small
// rate-limit to avoid pegging the parser thread. The session's first record
// determines t0; subsequent records keep their original timestamps so the
// charts and stutter list reflect the recording's real-time layout.
class FileSource : public NdjsonSource {
public:
    explicit FileSource(std::string a_path);
    ~FileSource() override;

    void Start() override;
    void Stop() override;

private:
    std::string _path;
    std::atomic<bool> _running{ false };
    std::thread _thread;

    void Loop();
};

}  // namespace skygraph::viewer
