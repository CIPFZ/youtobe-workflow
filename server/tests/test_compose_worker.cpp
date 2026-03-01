#include "compose_worker.h"

#include <cassert>
#include <string>

int main() {
    using namespace avsvc;

    const std::string fp1 = build_compose_fingerprint("/tmp/v.mp4", "/tmp/a.m4a", "/tmp/s.ass", "/tmp/o.mp4");
    const std::string fp2 = build_compose_fingerprint("/tmp/v.mp4", "/tmp/a.m4a", "/tmp/s.ass", "/tmp/o.mp4");
    const std::string fp3 = build_compose_fingerprint("/tmp/v2.mp4", "/tmp/a.m4a", "/tmp/s.ass", "/tmp/o.mp4");
    assert(fp1 == fp2);
    assert(fp1 != fp3);

    ComposeWorker worker;
    int last = -1;
    std::string msg;
    int rc = worker.run("/tmp/no-video.mp4", "/tmp/no-audio.m4a", "/tmp/no-sub.ass", "/tmp/out.mp4",
                        [&](int p, const std::string& m) { last = p; msg = m; });
    assert(rc != 0);
    assert(last == 0);
    assert(!msg.empty());
    return 0;
}
