#include "asr_worker.h"

#include <cassert>
#include <string>

using namespace avsvc;

int main() {
    AsrWorker worker;
    int progress = -1;
    std::string message;

    const int rc = worker.run("/tmp/a.wav", "/tmp/a.srt", "/models", "ggml-base.en.bin", "en",
        [&](int p, const std::string& msg) {
            progress = p;
            message = msg;
        });

#ifdef HAVE_WHISPERCPP
    // With whisper enabled, nonexistent test files should fail but still produce a concrete error.
    assert(rc != 0);
    assert(progress == 0);
    assert(!message.empty());
#else
    assert(rc == -100);
    assert(progress == 0);
    assert(message == "failed: whisper.cpp C API not enabled");
#endif

    return 0;
}
