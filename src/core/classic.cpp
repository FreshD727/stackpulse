#include <stackpulse/classic.hpp>
#include <algorithm>
#include <limits>

namespace stackpulse::classic {
namespace {
constexpr std::array<std::array<Cell,4>,7> shapes{{
    {{{0,1},{1,1},{2,1},{3,1}}}, {{{1,0},{2,0},{1,1},{2,1}}},
    {{{1,0},{0,1},{1,1},{2,1}}}, {{{1,0},{2,0},{0,1},{1,1}}},
    {{{0,0},{1,0},{1,1},{2,1}}}, {{{0,0},{0,1},{1,1},{2,1}}},
    {{{2,0},{0,1},{1,1},{2,1}}}
}};
std::uint32_t random(std::uint32_t& x) noexcept { x^=x<<13; x^=x>>17; x^=x<<5; return x; }
Piece draw(std::uint32_t& rng) noexcept {
    // Reject the small incomplete modulo range; generator is independent of wall clock.
    std::uint32_t n{};
    do { n=random(rng); } while(n<4);
    return static_cast<Piece>(n%7);
}
void spawn(State& s, Events& e) noexcept {
    s.active={s.next,spawn_rotation(s.next),3,-1}; s.next=draw(s.rng); s.gravity_timer=0;
    e.spawned=true;
    if(collides(s,s.active)) { s.game_over=true; e.topped_out=true; }
}
void lock(State& s, Events& e) noexcept {
    e.locked=true;
    for(const auto c:cells(s.active)) {
        if(c.y<0) { s.game_over=true; e.topped_out=true; }
        else s.board[static_cast<std::size_t>(c.y*10+c.x)]=static_cast<std::uint8_t>(s.active.piece)+1;
    }
    if(s.game_over) return;
    int destination=19;
    for(int y=19;y>=0;--y) {
        bool full=true;
        for(int x=0;x<10;++x) if(s.board[static_cast<std::size_t>(y*10+x)]==0) full=false;
        if(full) ++e.cleared;
        else { for(int x=0;x<10;++x) s.board[static_cast<std::size_t>(destination*10+x)]=s.board[static_cast<std::size_t>(y*10+x)]; --destination; }
    }
    while(destination>=0) { for(int x=0;x<10;++x) s.board[static_cast<std::size_t>(destination*10+x)]=0; --destination; }
    constexpr std::array<std::uint32_t,5> points{0,40,100,300,1200};
    s.score+=static_cast<std::uint64_t>(points[e.cleared])*(s.level+1);
    s.lines=static_cast<std::uint32_t>(std::min<std::uint64_t>(
        static_cast<std::uint64_t>(s.lines)+e.cleared,std::numeric_limits<std::uint32_t>::max()));
    s.level=s.start_level+s.lines/10;
    s.entry_timer=e.cleared ? 20u : 10u; s.gravity_timer=0;
}
}
std::array<Cell,4> cells(const Active& a) noexcept {
    const auto index=static_cast<std::size_t>(a.piece);
    if(index>=shapes.size()) return {};
    auto result=shapes[index];
    if(a.piece!=Piece::O) for(auto& c:result) for(unsigned r=0;r<a.rotation%4u;++r) {
        const int size=a.piece==Piece::I ? 4 : 3;
        c={size-1-c.y,c.x};
    }
    for(auto& c:result) { c.x+=a.x; c.y+=a.y; }
    return result;
}
bool collides(const State& s,const Active& a) noexcept {
    for(const auto c:cells(a)) {
        if(c.x<0 || c.x>=10 || c.y>=20 || c.y< -4) return true;
        if(c.y>=0 && s.board[static_cast<std::size_t>(c.y*10+c.x)]!=0) return true;
    }
    return false;
}
std::uint32_t gravity_frames(std::uint32_t level) noexcept {
    constexpr std::array<std::uint32_t,30> table{48,43,38,33,28,23,18,13,8,6,5,5,5,4,4,4,3,3,3,2,2,2,2,2,2,2,2,2,2,1};
    return table[std::min<std::size_t>(level,29)];
}
State initial_state(std::uint32_t seed,std::uint32_t level) noexcept {
    State s; s.rng=seed ? seed : 1; s.start_level=std::min(level,29u); s.level=s.start_level;
    s.next=draw(s.rng); Events e; spawn(s,e); return s;
}
bool valid_state(const State& s) noexcept {
    if(s.rng==0 || (s.held&~action_mask)!=0 || s.start_level>29 || s.level!=s.start_level+s.lines/10 ||
       static_cast<unsigned>(s.active.piece)>6 || static_cast<unsigned>(s.next)>6 || s.active.rotation>3 ||
       s.active.x< -4 || s.active.x>10 || s.active.y< -4 || s.active.y>20 ||
       s.direction< -1 || s.direction>1 || s.das_timer>15 || s.gravity_timer>48 || s.entry_timer>20) return false;
    for(auto c:s.board) if(c>7) return false;
    // A resumable state is always after row compaction. Full rows can otherwise
    // let malformed snapshots clear more than four rows on the next lock.
    for(std::size_t y=0;y<20;++y) {
        bool full=true;
        for(std::size_t x=0;x<10;++x) if(s.board[y*10+x]==0) full=false;
        if(full && !s.game_over) return false;
    }
    return s.game_over || s.entry_timer!=0 || !collides(s,s.active);
}
bool step(State& s,std::uint32_t held,Events* output) noexcept {
    if((held&~action_mask)!=0 || !valid_state(s)) return false;
    Events e;
    const auto pressed=held&~s.held; s.held=held; ++s.tick;
    if(s.game_over) { if(output) *output=e; return true; }
    const int direction=((held&left)!=0)==((held&right)!=0) ? 0 : ((held&left)!=0 ? -1 : 1);
    bool move=false;
    if(direction==0) s.das_timer=0;
    else if(direction!=s.direction) { s.das_timer=0; move=true; }
    else { ++s.das_timer; if(s.das_timer==16) { move=true; s.das_timer=10; } }
    s.direction=direction;
    if(s.entry_timer!=0) {
        --s.entry_timer;
        if(s.entry_timer==0) spawn(s,e);
        if(output) *output=e;
        return true;
    }
    if(((pressed&clockwise)!=0)!=((pressed&counterclockwise)!=0)) {
        auto candidate=s.active;
        candidate.rotation=static_cast<std::uint8_t>((candidate.rotation+((pressed&clockwise)!=0 ? 1 : 3))%4);
        if(!collides(s,candidate)) { s.active=candidate; e.rotated=true; }
    }
    if(move) { auto candidate=s.active; candidate.x+=direction;
        if(!collides(s,candidate)) { s.active=candidate; e.moved=true; e.shifted=true; }
    }
    const auto interval=(held&down)!=0 ? std::min(2u,gravity_frames(s.level)) : gravity_frames(s.level);
    ++s.gravity_timer;
    if(s.gravity_timer>=interval) {
        s.gravity_timer=0; auto candidate=s.active; ++candidate.y;
        if(collides(s,candidate)) lock(s,e);
        else { s.active=candidate; e.moved=true; if((held&down)!=0) { ++s.score; e.soft_dropped=true; } }
    }
    if(output) *output=e;
    return true;
}
std::vector<std::uint8_t> canonical_bytes(const State& s) {
    std::vector<std::uint8_t> bytes; bytes.reserve(270);
    const auto put=[&](std::uint64_t n,unsigned count) { for(unsigned i=0;i<count;++i) bytes.push_back(static_cast<std::uint8_t>(n>>(i*8))); };
    put(rules_version,4); put(s.tick,8); put(s.score,8); put(s.rng,4); put(s.held,4); put(s.lines,4); put(s.level,4); put(s.start_level,4);
    for(auto c:s.board) put(c,1);
    put(static_cast<std::uint8_t>(s.active.piece),1); put(s.active.rotation,1); put(static_cast<std::uint32_t>(s.active.x),4); put(static_cast<std::uint32_t>(s.active.y),4);
    put(static_cast<std::uint8_t>(s.next),1); put(s.gravity_timer,4); put(s.das_timer,4); put(s.entry_timer,4); put(static_cast<std::uint32_t>(s.direction),4); put(s.game_over?1:0,1);
    return bytes;
}
bool decode_state(const std::vector<std::uint8_t>& b,State& out) noexcept {
    if(b.size()!=268) return false;
    std::size_t p=0; const auto get=[&](unsigned count) { std::uint64_t n=0; for(unsigned i=0;i<count;++i) n|=static_cast<std::uint64_t>(b[p++])<<(i*8); return n; };
    if(get(4)!=rules_version) return false;
    State s; s.tick=get(8); s.score=get(8); s.rng=static_cast<std::uint32_t>(get(4)); s.held=static_cast<std::uint32_t>(get(4)); s.lines=static_cast<std::uint32_t>(get(4)); s.level=static_cast<std::uint32_t>(get(4)); s.start_level=static_cast<std::uint32_t>(get(4));
    for(auto& c:s.board) c=static_cast<std::uint8_t>(get(1));
    s.active.piece=static_cast<Piece>(get(1)); s.active.rotation=static_cast<std::uint8_t>(get(1)); s.active.x=static_cast<std::int32_t>(get(4)); s.active.y=static_cast<std::int32_t>(get(4)); s.next=static_cast<Piece>(get(1));
    s.gravity_timer=static_cast<std::uint32_t>(get(4)); s.das_timer=static_cast<std::uint32_t>(get(4)); s.entry_timer=static_cast<std::uint32_t>(get(4)); s.direction=static_cast<std::int32_t>(get(4));
    const auto over=get(1); if(over>1) return false; s.game_over=over!=0;
    if(!valid_state(s)) return false;
    out=s; return true;
}
std::uint64_t state_hash(const State& s) noexcept {
    std::uint64_t hash=14695981039346656037ULL;
    const auto put=[&](std::uint64_t n,unsigned count) { for(unsigned i=0;i<count;++i) { hash^=static_cast<std::uint8_t>(n>>(i*8)); hash*=1099511628211ULL; } };
    put(rules_version,4); put(s.tick,8); put(s.score,8); put(s.rng,4); put(s.held,4); put(s.lines,4); put(s.level,4); put(s.start_level,4);
    for(auto c:s.board) put(c,1);
    put(static_cast<std::uint8_t>(s.active.piece),1); put(s.active.rotation,1); put(static_cast<std::uint32_t>(s.active.x),4); put(static_cast<std::uint32_t>(s.active.y),4);
    put(static_cast<std::uint8_t>(s.next),1); put(s.gravity_timer,4); put(s.das_timer,4); put(s.entry_timer,4); put(static_cast<std::uint32_t>(s.direction),4); put(s.game_over?1:0,1);
    return hash;
}
State clear_fixture(std::uint32_t seed) noexcept {
    auto s=initial_state(seed); for(int y=16;y<20;++y) for(int x=0;x<10;++x) if(x!=5) s.board[static_cast<std::size_t>(y*10+x)]=6;
    s.active={Piece::I,1,3,12}; return s;
}
}
