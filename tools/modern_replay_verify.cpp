#include <iostream>
#include <stackpulse/modern_replay.hpp>
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: stackpulse_modern_replay_verify FILE.spmr\n";
        return 2;
    }
    stackpulse::modern_replay::Replay replay;
    stackpulse::modern::State final;
    std::string error;
    if (!stackpulse::modern_replay::load(argv[1], replay, error) ||
        !stackpulse::modern_replay::verify(replay, final, error)) {
        std::cerr << "Replay rejected: " << error << '\n';
        return 1;
    }
    std::cout << "Verified Modern/Fusion M2 v1: frames=" << replay.inputs.size()
              << " score=" << final.score << " lines=" << final.lines
              << " final_hash=" << stackpulse::modern::state_hash(final) << '\n';
}
