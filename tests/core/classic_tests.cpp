#include <stackpulse/classic.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>
#include <limits>
using namespace stackpulse::classic;
TEST_CASE("Classic all seven base geometries and rotations") {
 const std::array<std::array<Cell,4>,7> expected{{
 {{{0,1},{1,1},{2,1},{3,1}}}, {{{1,0},{2,0},{1,1},{2,1}}},
 {{{1,0},{0,1},{1,1},{2,1}}}, {{{1,0},{2,0},{0,1},{1,1}}},
 {{{0,0},{1,0},{1,1},{2,1}}}, {{{0,0},{0,1},{1,1},{2,1}}}, {{{2,0},{0,1},{1,1},{2,1}}}
 }};
 for(unsigned piece=0;piece<7;++piece) {
  Active a{static_cast<Piece>(piece),0,0,0}; auto base=expected[piece]; if(piece==0 || piece==3 || piece==4) for(auto& c:base) ++c.y; REQUIRE(cells(a)==base);
  for(unsigned r=1;r<4;++r) {
   a.rotation=static_cast<std::uint8_t>(r); auto rotated=expected[piece];
   if(piece!=1) for(auto& c:rotated) for(unsigned n=0;n<r%rotation_count(a.piece);++n) c={static_cast<int>(piece==0?4:3)-1-c.y,c.x};
   if((piece==0 || piece==3 || piece==4) && r%2==0) for(auto& c:rotated) ++c.y;
   REQUIRE(cells(a)==rotated);
  }
 }
}
TEST_CASE("Classic flipped spawn orientation agrees for initial and delayed spawns") {
 const std::array<std::uint8_t,7> rotations{0,0,2,0,0,2,2};
 std::array<bool,7> seen{};
 for(std::uint32_t seed=1;seed<=256;++seed) {
  const auto s=initial_state(seed);
  const auto piece=static_cast<std::size_t>(s.active.piece); seen[piece]=true;
  REQUIRE(s.active.rotation==rotations[piece]);
  REQUIRE(s.active.x==3); REQUIRE(s.active.y==-1);
 }
 for(bool present:seen) REQUIRE(present);
 for(unsigned piece=0;piece<7;++piece) {
  auto s=initial_state(1); s.next=static_cast<Piece>(piece); s.entry_timer=1;
  Events e; REQUIRE(step(s,0,&e)); REQUIRE(e.spawned);
  const Active expected{static_cast<Piece>(piece),rotations[piece],3,-1};
  REQUIRE(s.active==expected); REQUIRE(spawn_rotation(s.active.piece)==rotations[piece]);
  // A clockwise turn still advances in the same matrix direction after spawning.
  REQUIRE(step(s,clockwise,&e)); REQUIRE(e.rotated);
  REQUIRE(s.active.rotation==(rotations[piece]+1)%rotation_count(s.active.piece));
 }
}
TEST_CASE("Classic movement events distinguish input from gravity and blocked actions") {
 auto s=initial_state(1); s.active={Piece::O,0,3,3}; Events e;
 s.gravity_timer=47; REQUIRE(step(s,0,&e));
 REQUIRE(e.moved); REQUIRE_FALSE(e.shifted); REQUIRE_FALSE(e.soft_dropped);
 REQUIRE(step(s,left,&e)); REQUIRE(e.shifted); REQUIRE_FALSE(e.soft_dropped);
 REQUIRE(step(s,left,&e)); REQUIRE_FALSE(e.shifted); REQUIRE_FALSE(e.moved);
 s.gravity_timer=1; REQUIRE(step(s,down,&e)); REQUIRE(e.soft_dropped); REQUIRE_FALSE(e.shifted);
 s.active.x=-1; s.held=0; s.direction=0; REQUIRE(step(s,left,&e));
 REQUIRE_FALSE(e.shifted); REQUIRE_FALSE(e.rotated); REQUIRE_FALSE(e.soft_dropped);
 s.active={Piece::I,1,-2,3}; s.held=0; REQUIRE(step(s,clockwise,&e)); REQUIRE_FALSE(e.rotated);
 s.entry_timer=2; s.held=0; REQUIRE(step(s,right|clockwise|down,&e));
 REQUIRE_FALSE(e.shifted); REQUIRE_FALSE(e.rotated); REQUIRE_FALSE(e.soft_dropped);
}
TEST_CASE("Classic collisions and no wall kicks") {
 auto s=initial_state(1); s.active={Piece::I,1,-2,3}; REQUIRE(valid_state(s));
 REQUIRE_FALSE(collides(s,s.active)); auto candidate=s.active; candidate.rotation=2; REQUIRE(collides(s,candidate));
 Events e; REQUIRE(step(s,clockwise,&e)); REQUIRE_FALSE(e.rotated); REQUIRE(s.active.rotation==1); REQUIRE(s.active.x==-2);
 candidate={Piece::O,0,3,19}; REQUIRE(collides(s,candidate)); candidate.y=5; s.board[54]=1; REQUIRE(collides(s,candidate));
}
TEST_CASE("Classic gravity soft scoring and DAS cadence") {
 auto s=initial_state(1); s.active={Piece::O,0,3,3};
 for(unsigned i=0;i<47;++i) REQUIRE(step(s,0)); REQUIRE(s.active.y==3); REQUIRE(step(s,0)); REQUIRE(s.active.y==4);
 REQUIRE(step(s,down)); REQUIRE(step(s,down)); REQUIRE(s.active.y==5); REQUIRE(s.score==1);
 s=initial_state(1); s.active={Piece::O,0,3,3};
 REQUIRE(step(s,left)); REQUIRE(s.active.x==2);
 for(unsigned i=0;i<15;++i) REQUIRE(step(s,left)); REQUIRE(s.active.x==2);
 REQUIRE(step(s,left)); REQUIRE(s.active.x==1);
 for(unsigned i=0;i<5;++i) REQUIRE(step(s,left)); REQUIRE(s.active.x==1);
 REQUIRE(step(s,left)); REQUIRE(s.active.x==0);
 REQUIRE(step(s,left|right)); REQUIRE(s.das_timer==0); REQUIRE(s.direction==0);
 REQUIRE(step(s,left)); REQUIRE(s.active.x==-1);
}
TEST_CASE("Classic failed gravity locks and nonclear entry delay") {
 auto s=initial_state(1); s.active={Piece::O,0,3,18}; s.gravity_timer=47; Events e;
 REQUIRE(step(s,0,&e)); REQUIRE(e.locked); REQUIRE(e.cleared==0); REQUIRE(s.entry_timer==10);
 for(unsigned i=0;i<9;++i) { REQUIRE(step(s,0,&e)); REQUIRE_FALSE(e.spawned); }
 REQUIRE(step(s,0,&e)); REQUIRE(e.spawned); REQUIRE(s.entry_timer==0); REQUIRE(s.active.y==-1);
}
TEST_CASE("Classic one through four line scoring and level transitions") {
 const std::array<unsigned,5> points{0,40,100,300,1200};
 for(unsigned lines=1;lines<=4;++lines) {
  auto s=initial_state(1,2); s.lines=29; s.level=2; s.active={Piece::I,1,3,16};
  for(unsigned row=20-lines;row<20;++row) for(unsigned x=0;x<10;++x) if(x!=5) s.board[row*10+x]=1;
  s.gravity_timer=gravity_frames(s.level)-1; Events e; REQUIRE(step(s,0,&e));
  REQUIRE(e.locked); REQUIRE(e.cleared==lines); REQUIRE(s.lines==29+lines); REQUIRE(s.level==3); REQUIRE(s.score==points[lines]*4); REQUIRE(s.entry_timer==20);
  for(unsigned i=0;i<19;++i) { REQUIRE(step(s,0,&e)); REQUIRE_FALSE(e.spawned); }
  REQUIRE(step(s,0,&e)); REQUIRE(e.spawned);
 }
}
TEST_CASE("Classic top out above ceiling and blocked spawn") {
 auto s=initial_state(1); s.active={Piece::O,0,3,-1}; s.board[14]=1; s.gravity_timer=47; Events e;
 REQUIRE(step(s,0,&e)); REQUIRE(e.locked); REQUIRE(e.topped_out); REQUIRE(s.game_over);
 s=initial_state(1); s.entry_timer=1; s.next=Piece::O; s.board[4]=1;
 REQUIRE(step(s,0,&e)); REQUIRE(e.spawned); REQUIRE(e.topped_out); REQUIRE(s.game_over);
}
TEST_CASE("Classic complete state codec restores hidden timers") {
 auto s=clear_fixture(5); s.held=left; s.direction=-1; s.das_timer=11; s.gravity_timer=1;
 auto bytes=canonical_bytes(s); REQUIRE(bytes.size()==268); State restored; REQUIRE(decode_state(bytes,restored)); REQUIRE(restored==s);
 for(unsigned i=0;i<1500;++i) { REQUIRE(step(s,i%10<5?down:0u)); REQUIRE(step(restored,i%10<5?down:0u)); REQUIRE(state_hash(s)==state_hash(restored)); }
 bytes.push_back(0); REQUIRE_FALSE(decode_state(bytes,restored));
}
TEST_CASE("Classic rejects malformed resumable states without mutation") {
 auto initial=initial_state(1); auto s=initial;
 for(unsigned x=0;x<10;++x) s.board[190+x]=1;
 auto before=s; REQUIRE_FALSE(step(s,0)); REQUIRE(s==before);
 s=initial; s.das_timer=17; before=s; REQUIRE_FALSE(step(s,0)); REQUIRE(s==before);
 s=initial; before=s; REQUIRE_FALSE(step(s,32)); REQUIRE(s==before);
}
TEST_CASE("Classic state hash matches the canonical byte stream") {
 auto s=clear_fixture(991);
 for(unsigned i=0;i<1000;++i) {
  std::uint64_t expected=14695981039346656037ULL;
  for(auto b:canonical_bytes(s)) { expected^=b; expected*=1099511628211ULL; }
  REQUIRE(state_hash(s)==expected); REQUIRE(step(s,i%20<10?down:0u));
 }
}
TEST_CASE("Classic line count boundaries preserve valid resumable states") {
 for(const auto lines:{999999u,std::numeric_limits<std::uint32_t>::max()-1}) {
  auto s=clear_fixture(1); s.active.y=16; s.lines=lines; s.level=level_for_lines(s.start_level,s.lines);
  REQUIRE(valid_state(s)); Events events; REQUIRE(step(s,0,&events)); REQUIRE(events.cleared==4);
  const auto expected=static_cast<std::uint32_t>(std::min<std::uint64_t>(static_cast<std::uint64_t>(lines)+4,std::numeric_limits<std::uint32_t>::max()));
  REQUIRE(s.lines==expected); REQUIRE(s.level==level_for_lines(s.start_level,expected)); REQUIRE(valid_state(s));
  const auto bytes=canonical_bytes(s); State restored; REQUIRE(decode_state(bytes,restored)); REQUIRE(restored==s);
  REQUIRE(step(s,0)); REQUIRE(valid_state(s));
 }
}

TEST_CASE("Classic starting levels have a separate initial transition") {
 const std::array<unsigned,20> first{10,20,30,40,50,60,70,80,90,100,100,100,100,100,100,100,110,120,130,140};
 for(unsigned start=0;start<first.size();++start) {
  CAPTURE(start); REQUIRE(transition_lines(start)==first[start]);
  REQUIRE(level_for_lines(start,first[start]-1)==start);
  REQUIRE(level_for_lines(start,first[start])==start+1);
  REQUIRE(level_for_lines(start,first[start]+9)==start+1);
  REQUIRE(level_for_lines(start,first[start]+10)==start+2);
 }
 auto s=clear_fixture(1); s.start_level=15; s.level=15; s.lines=96; s.active.y=16;
 s.gravity_timer=3; Events e; REQUIRE(step(s,0,&e));
 REQUIRE(e.cleared==4); REQUIRE(s.lines==100); REQUIRE(s.level==16); REQUIRE(s.score==1200*17);
 REQUIRE(valid_state(s));
}
TEST_CASE("Classic screenshot tuck retries a charged blocked shift after the drop") {
 auto s=initial_state(1,15);
 // Red Z from successful-tuck.png; blue J approaches from the right.
 for(const Cell c:std::array<Cell,4>{{{1,17},{0,18},{1,18},{0,19}}}) s.board[c.y*10+c.x]=5;
 s.active={Piece::J,3,2,16}; s.held=left; s.direction=-1; s.das_timer=16; s.gravity_timer=3;
 Events e; REQUIRE(step(s,left,&e)); REQUIRE_FALSE(e.shifted); REQUIRE_FALSE(e.locked);
 REQUIRE(s.active.y==17); REQUIRE(s.das_timer==16);
 REQUIRE(step(s,left,&e)); REQUIRE(e.shifted); REQUIRE_FALSE(e.locked); REQUIRE(s.active.x==1);
 const std::array<Cell,4> expected{{{1,19},{2,19},{2,18},{2,17}}}; REQUIRE(cells(s.active)==expected);
 for(unsigned i=0;i<3;++i) REQUIRE(step(s,left,&e));
 REQUIRE(e.locked); REQUIRE(s.board[191]==6); REQUIRE(s.board[192]==6);
}
TEST_CASE("Classic Z tuck reaches the screenshot with either rotation button") {
 for(const auto button:{clockwise,counterclockwise}) {
  auto s=initial_state(1,15);
  for(const Cell c:std::array<Cell,4>{{{2,17},{2,18},{1,19},{2,19}}}) s.board[c.y*10+c.x]=6;
  s.active={Piece::Z,0,0,14}; Events e;
  REQUIRE(step(s,button,&e)); REQUIRE(e.rotated); REQUIRE(s.active.rotation==1);
  REQUIRE(step(s,left,&e)); REQUIRE(e.shifted); REQUIRE(s.active.x==-1);
  for(unsigned i=0;i<18 && !e.locked;++i) REQUIRE(step(s,0,&e));
  REQUIRE(e.locked);
  for(const Cell c:std::array<Cell,4>{{{1,17},{0,18},{1,18},{0,19}}}) REQUIRE(s.board[c.y*10+c.x]==5);
 }
}
TEST_CASE("Classic fully charged entry DAS is available on first active tick") {
 auto s=initial_state(1); s.entry_timer=3; s.next=Piece::T; s.held=left; s.direction=-1; s.das_timer=15;
 for(unsigned i=0;i<3;++i) REQUIRE(step(s,left));
 REQUIRE(s.das_timer==16); REQUIRE(s.active.x==3);
 Events e; REQUIRE(step(s,left,&e)); REQUIRE(e.shifted); REQUIRE(s.active.x==2); REQUIRE(s.das_timer==10);
}
