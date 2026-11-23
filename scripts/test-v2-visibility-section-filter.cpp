// Standalone C++20 regression test; no Geode or GL required.
#include <cassert>
#include <random>
#include <set>
// Exercise legacy Windows macro collision even on non-Windows compilers.
#define near
#define far
#include "../src/v2/VisibilitySectionFilter.hpp"
#include "../src/v2/VisibilityMirrorWorklist.hpp"
using namespace vulkanizer::v2;

int main() {
    VisibilitySectionFilter index;
    index.initialize(256);
    assert(index.addSpace() == 0 && index.addSpace() == 1);
    const VisibilitySectionRange view{10,20,10,20,false};
    auto collect = [&](unsigned space, VisibilitySectionRange range) {
        std::set<unsigned> out;
        index.collect(space, range, [&](unsigned id) { out.insert(id); });
        return out;
    };
    assert(index.defer(7,0,{30,30},{40,40}));
    assert(!index.defer(7,0,{30,30},{40,40}));
    assert(collect(0,view).empty());
    // Camera-only entry at the old stale mirror position must also wake it.
    assert(collect(0,{39,41,39,41,false}) == std::set<unsigned>{7});
    index.move(7,{11,11});
    assert(collect(0,view) == std::set<unsigned>{7});
    assert(index.erase(7) && !index.erase(7));
    assert(collect(0,{}).empty());
    // The same deferred decoration can detach from one node and attach to
    // another/rescale into another radius bucket. Re-home both memberships
    // without discarding its outstanding replay or keeping a ghost entry.
    assert(index.defer(8,0,{30,30},{40,40}));
    assert(!index.defer(8,1,{30,30},{40,40}));
    assert(collect(0,{}).empty());
    assert(collect(1,{39,41,39,41,false}) == std::set<unsigned>{8});
    assert(collect(1,{29,31,29,31,false}) == std::set<unsigned>{8});
    assert(index.size()==1);
    assert(index.erase(8));
    assert(collect(1,{}).empty());
    assert(visibilitySectionRange({100,200,100,200},0,.1f,.1f,10000).contains({10,10}));
    assert(!visibilitySectionRange({100,200,100,200},0,.1f,.1f,10000).contains({40,40}));
    assert(visibilitySectionRange({-200,-100,-200,-100},0,.1f,.1f,10000).contains({0,0}));
    assert(visibilitySectionRange({100,200,100,200},0,0,.1f,10000).contains({99999,10}));
    assert(!visibilitySectionRange({100,200,100,200},0,0,.1f,10000).contains({99999,40}));
    assert(visibilitySectionRange({100,200,100,200},0,0,0,10000).all);
    assert(visibilitySectionRange({0,0,0,0},0,.1f,.1f,10000).all);
    struct State { bool active = false; unsigned space = 0; VisibilitySection current{}, old{}; };
    std::vector<State> expected(256);
    std::mt19937 random(815);
    auto section = [&] { return VisibilitySection{int(random()%50),int(random()%50)}; };
    for (unsigned step=0;step<100000;++step) {
        unsigned id=random()%expected.size();
        auto& p=expected[id];
        switch(random()%3) {
        case 0:
            p={true, unsigned(random()%2), section(), section()};
            index.defer(id,p.space,p.current,p.old); break;
        case 1:
            p.current=section(); index.move(id,p.current); break;
        case 2:
            p.active=false; index.erase(id); break;
        }
        if (step%31 != 0) continue;
        auto xy=section();
        VisibilitySectionRange range{xy.first,xy.first+10,xy.second,xy.second+10,false};
        if(step%127==0) range={};
        for(unsigned s=0;s<2;++s) {
            std::set<unsigned> oracle;
            for(unsigned i=0;i<expected.size();++i) {
                const auto& q=expected[i];
                if(q.active && q.space==s && (range.contains(q.current)||range.contains(q.old))) oracle.insert(i);
            }
            assert(collect(s,range)==oracle);
        }
    }
    VisibilityMirrorWorklist work;
    work.initialize(1,256);
    work.mergeObject(77); work.mergeObject(4); work.mergeObject(77);
    std::vector<unsigned> ordered;
    work.consume([&](unsigned id){ordered.push_back(id);});
    assert((ordered==std::vector<unsigned>{4,77}));
}
