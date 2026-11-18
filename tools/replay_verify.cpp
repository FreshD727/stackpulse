#include <stackpulse/replay.hpp>
#include <iostream>
int main(int argc,char** argv) {
 if(argc!=2) { std::cerr << "Usage: stackpulse_replay_verify FILE.spcr\n"; return 2; }
 stackpulse::replay::Replay replay; stackpulse::classic::State final; std::string error;
 if(!stackpulse::replay::load(argv[1],replay,error) || !stackpulse::replay::verify(replay,final,error)) { std::cerr << "Replay rejected: " << error << '\n'; return 1; }
 std::cout << "Verified Classic provisional v1: frames=" << replay.inputs.size() << " score=" << final.score << " lines=" << final.lines << " final_hash=" << stackpulse::classic::state_hash(final) << '\n';
}
