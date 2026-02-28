#include "merge_worker.h"

#include <cassert>
#include <string>
#include <vector>

using namespace avsvc;

int main() {
    const std::string fp1 = build_fingerprint("v.mp4", "a.m4a", "o.mp4");
    const std::string fp2 = build_fingerprint("v.mp4", "a.m4a", "o.mp4");
    const std::string fp3 = build_fingerprint("v2.mp4", "a.m4a", "o.mp4");

    assert(!fp1.empty());
    assert(fp1 == fp2);
    assert(fp1 != fp3);

    MergeWorker worker;
    std::vector<int> progress_points;
    int rc = worker.run("/tmp/video.mp4", "/tmp/audio.m4a", "/tmp/out.mp4",
                        [&](int p, const std::string&) { progress_points.push_back(p); });
    assert(rc == 0);
    assert(!progress_points.empty());
    assert(progress_points.back() == 100);

    return 0;
}
