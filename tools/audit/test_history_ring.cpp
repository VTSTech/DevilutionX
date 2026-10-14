// Standalone logic test for the history-ring fix (no SDL needed).
// Emulates the runner's ring: blit current frame at slot S, advance S each
// frame, then check what OriginalHistoryN resolves to for every frame.
#include <cstdio>
#include <vector>
#include <string>

static int failures = 0;

static void CheckCase(const char *label, int depth, int ringSize, bool newStyle)
{
        // ring[t] stores the frame number written into it; -1 = never written.
        std::vector<int> ring(static_cast<size_t>(ringSize), -1);
        int slot = 0;
        const int kFrames = 12;
        std::vector<int> got(kFrames, -2);
        for (int T = 0; T < kFrames; ++T) {
                ring[static_cast<size_t>(slot)] = T; // blit current input
                // What OriginalHistory1 resolves to this frame:
                if (newStyle) {
                        const size_t s = (static_cast<size_t>(slot) + ring.size() - 1) % ring.size();
                        got[static_cast<size_t>(T)] = ring[s];
                } else {
                        const size_t s = (static_cast<size_t>(slot) + 1) % ring.size();
                        got[static_cast<size_t>(T)] = ring[s];
                }
                slot = (slot + 1) % ringSize;
        }
        for (int T = 2; T < kFrames; ++T) { // skip warmup frames 0..1
                const int want = T - 1;
                if (got[static_cast<size_t>(T)] != want) {
                        printf("FAIL %s depth=%d size=%d frame T=%d: OH1=%d want %d\n",
                            label, depth, ringSize, T, got[static_cast<size_t>(T)], want);
                        ++failures;
                        return;
                }
        }
        printf("PASS %s depth=%d size=%d (OH1 == T-1 steady-state)\n", label, depth, ringSize);
}

int main()
{
        // Old code: ring size = depth, index (slot + N) % depth.
        // depth=1 is what 13 real presets use (motion-adaptive deinterlacing,
        // mix_frames, nnedi3, shutter-to-anaglyph); depth=3/7 cover the
        // Mega_Bezel-style chains (168 with max_hist=3, 73 with max_hist=7).
        CheckCase("OLD", 1, 1, false);
        CheckCase("OLD", 3, 3, false);
        CheckCase("OLD", 7, 7, false);
        // New code: ring size = depth+1, index (slot + size - N) % size.
        for (int depth = 1; depth <= 8; ++depth)
                CheckCase("NEW", depth, depth + 1, true);

        // Full correctness: every N in 1..depth must resolve to T-N.
        int failuresBefore = failures;
        for (int depth = 1; depth <= 8; ++depth) {
                const size_t size = static_cast<size_t>(depth) + 1;
                std::vector<int> ring(size, -1);
                int slot = 0;
                for (int T = 0; T < 24; ++T) {
                        ring[static_cast<size_t>(slot)] = T;
                        for (int N = 1; N <= depth; ++N) {
                                const size_t s = (static_cast<size_t>(slot) + size - static_cast<size_t>(N)) % size;
                                const int want = T - N;
                                if (T >= N + 2 && ring[s] != want) { // warmup guard
                                        printf("FAIL NEW full depth=%d N=%d T=%d: got %d want %d\n",
                                            depth, N, T, ring[s], want);
                                        ++failures;
                                }
                        }
                        slot = (slot + 1) % static_cast<int>(size);
                }
        }
        printf("NEW full-matrix (N=1..depth, T-N for all frames): %s\n",
            failures == failuresBefore ? "PASS" : "FAIL");
        // Old-code failures above are intentional demonstrations; only NEW
        // failures count against the exit code.
        return failures == failuresBefore ? 0 : 1;
}
