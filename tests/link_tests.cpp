#include "h755/ethernet/lan8742.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
using namespace h755::ethernet;
namespace {
void check(bool ok, const char* what) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); } }
struct Bus {
    std::array<std::uint16_t, 8> values{};
    std::array<unsigned, 8> registers{};
    unsigned requests{}, consumed{};
    bool fail_begin{}, fail_poll{}, pending{};
    std::expected<void,MdioError> begin(PhyAddress,RegisterAddress reg,std::uint32_t,std::uint32_t)
    {
        if (fail_begin) return std::unexpected{MdioError::busy};
        check(requests < registers.size(),"bounded test request count");
        registers[requests++] = reg.value(); return {};
    }
    std::expected<std::optional<std::uint16_t>,MdioError> poll(std::uint32_t)
    {
        if (fail_poll) return std::unexpected{MdioError::timeout};
        if (pending) return std::optional<std::uint16_t>{};
        check(consumed < values.size(),"bounded test response count");
        return values[consumed++];
    }
};
using Monitor = Lan8742LinkMonitor<Bus>;
void scenario(const std::array<std::uint16_t,8>& values, LinkState expected, unsigned reads)
{
    Bus bus; bus.values=values; Monitor monitor{bus,*PhyAddress::from(0)};
    check(monitor.begin(0,20).has_value(),"snapshot starts");
    std::optional<LinkState> result;
    for (unsigned i=0;i<8&&!result;++i) {
        auto r=monitor.poll(i); check(r.has_value(),"valid sequence succeeds"); result=*r;
    }
    check(result && *result==expected,"independent expected link state");
    check(bus.requests==reads,"minimum bounded read sequence");
    check(bus.registers[0]==1&&bus.registers[1]==1,"BMSR latch cleared by two reads");
    if (reads>=3) check(bus.registers[2]==0,"read BMCR after live link");
    if (reads==5) check(bus.registers[3]==31&&bus.registers[4]==1,"mode followed by fresh link check");
    check(!monitor.poll(99),"snapshot consumed once");
    check(monitor.begin(100,20).has_value(),"new snapshot allowed");
}
void failures()
{
    Bus bus; Monitor m{bus,*PhyAddress::from(0)};
    check(!m.poll(0),"poll before begin rejected");
    check(!m.begin(0,0)&&!m.begin(0,0x80000000U),"invalid deadline rejected");
    check(bus.requests==0,"invalid request has no bus effects");
    bus.fail_begin=true; check(!m.begin(0,20),"initial transport error propagated");
    bus.fail_begin=false; check(m.begin(0,20).has_value(),"retry after bus error");
    check(!m.begin(1,20),"concurrent snapshot rejected");
    bus.pending=true; auto r=m.poll(1); check(r&&!r->has_value(),"pending preserved without extra reads");
    check(bus.requests==1,"pending cannot duplicate MDIO request");
    bus.pending=false; bus.fail_poll=true; check(!m.poll(2),"poll error propagated");
    bus.fail_poll=false; check(m.begin(3,20).has_value(),"monitor reusable after transport recovery");
    bus.fail_begin=true; check(!m.poll(4),"later begin error propagated");
}
void invalid(const std::array<std::uint16_t,8>& values)
{
    Bus bus;bus.values=values;Monitor m{bus,*PhyAddress::from(0)};
    check(m.begin(0,20).has_value(),"invalid-status probe starts");
    for (unsigned i=0;i<8;++i) {
        auto r=m.poll(i); if(!r) { check(r.error()==LinkError::invalid_status,"invalid hardware encoding rejected");return; }
        check(!r->has_value(),"invalid input must not publish link up");
    }
    check(false,"invalid encoding must terminate");
}

void real_transport()
{
    struct Registers {
        std::uint32_t address{},data{};
        std::array<std::uint32_t,8> commands{};
        unsigned writes{};
        std::uint32_t read_address() const { return address; }
        std::uint32_t read_data() const { return data; }
        void write_address(std::uint32_t word) { check(writes<commands.size(),"bounded commands");commands[writes++]=word;address=word; }
    } registers;
    MdioTransport reader{registers,*MdioClock::from_hclk(64'000'000)};
    Lan8742LinkMonitor monitor{reader,*PhyAddress::from(0)};
    check(monitor.begin(0,20).has_value(),"real MDIO begins");
    const auto pending=monitor.poll(0);
    check(pending&&!pending->has_value(),"real busy register defers monitor");
    std::array<std::uint16_t,5> values{0,4,0x1000,0x1018,4};
    for (unsigned i=0;i<values.size();++i) {
        registers.data=values[i];registers.address&=~1U;
        const auto state=monitor.poll(i+1U);
        check(state.has_value(),"real components cooperate");
        if(i==4U)check(state->has_value()&&**state==LinkState::mb100_full,"real transport resolves 100/full");
        else check(!state->has_value(),"intermediate snapshot not published");
    }
    check(registers.commands[0]==0x1000DU&&registers.commands[1]==0x1000DU&&registers.commands[2]==0xDU&&registers.commands[3]==0x1F000DU&&registers.commands[4]==0x1000DU,"exact Clause22 sequence");
    check(monitor.begin(10,20).has_value(),"timeout scenario begins");
    check(!monitor.poll(30),"MDIO timeout reaches monitor");
    check(!monitor.begin(31,20),"faulted reader cannot be silently reused");
    registers.address&=~1U;
    check(reader.recover_after_idle().has_value(),"explicit recovery owns hardware");
    check(monitor.begin(32,20).has_value(),"real monitor resumes after recovery");
}
}
int main(int argc,char** argv)
{
    if(argc==2&&std::strcmp(argv[1],"integration")==0){real_transport();return 0;}
    scenario({4,0},LinkState::down,2);
    scenario({0,4,0x1000,0},LinkState::negotiating,4);
    scenario({0,4,0x1000,0x1004,4},LinkState::mb10_half,5);
    scenario({4,4,0x1000,0x1014,4},LinkState::mb10_full,5);
    scenario({4,4,0x1000,0x1008,4},LinkState::mb100_half,5);
    scenario({4,4,0x1000,0x1018,4},LinkState::mb100_full,5);
    scenario({4,4,0x1000,0x1018,0},LinkState::down,5);
    scenario({4,4,0,4},LinkState::mb10_half,4);
    scenario({4,4,0x100,4},LinkState::mb10_full,4);
    scenario({4,4,0x2000,4},LinkState::mb100_half,4);
    scenario({4,4,0x2100,4},LinkState::mb100_full,4);
    scenario({4,4,0x800},LinkState::down,3);
    scenario({4,4,0x400},LinkState::down,3);
    scenario({4,4,0x8000},LinkState::down,3);
    invalid({0xFFFF}); invalid({4,0xFFFF}); invalid({4,4,0xFFFF});
    invalid({4,4,0x1000,0xFFFF}); invalid({4,4,0x1000,0x1000});
    invalid({4,4,0x1000,0x1018,0xFFFF});
    invalid({4,0x14}); invalid({4,4,0x1000,0x1018,0x14});
    scenario({4,4,0x4000},LinkState::down,3);
    failures();
}
