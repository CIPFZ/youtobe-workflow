#include "subtitle_embed_worker.h"

#include <cassert>
#include <string>

int main() {
    using namespace avsvc;

    const std::string fp1 = build_subtitle_embed_fingerprint("/tmp/v.mp4", "/tmp/a.ass", "/tmp/o.mp4");
    const std::string fp2 = build_subtitle_embed_fingerprint("/tmp/v.mp4", "/tmp/a.ass", "/tmp/o.mp4");
    const std::string fp3 = build_subtitle_embed_fingerprint("/tmp/v2.mp4", "/tmp/a.ass", "/tmp/o.mp4");
    assert(fp1 == fp2);
    assert(fp1 != fp3);

    SubtitleEmbedWorker worker;
    int last_p = -1;
    std::string last_msg;
    const int rc = worker.run_ass_to_mp4("/tmp/notfound.mp4", "/tmp/notfound.ass", "/tmp/out.mp4",
                                         [&](int p, const std::string& msg) {
                                             last_p = p;
                                             last_msg = msg;
                                         });
    assert(rc != 0);
    assert(last_p == 0);
    assert(!last_msg.empty());
    return 0;
}
