#include "audio_convert_worker.h"

#include <cassert>
#include <string>

int main() {
    using namespace avsvc;

    const std::string fp1 = build_audio_convert_fingerprint("/tmp/a.m4a", "/tmp/a.wav");
    const std::string fp2 = build_audio_convert_fingerprint("/tmp/a.m4a", "/tmp/a.wav");
    const std::string fp3 = build_audio_convert_fingerprint("/tmp/b.m4a", "/tmp/b.wav");
    assert(fp1 == fp2);
    assert(fp1 != fp3);

    AudioConvertWorker worker;
    int last_progress = -1;
    std::string last_msg;

    const int rc = worker.run_m4a_to_wav("/tmp/not-exists-audio.m4a", "/tmp/out.wav",
                                         [&](int p, const std::string& msg) {
                                             last_progress = p;
                                             last_msg = msg;
                                         });
    assert(rc != 0);
    assert(last_progress == 0);
    assert(!last_msg.empty());

    return 0;
}
