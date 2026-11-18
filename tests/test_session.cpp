#include "observatory/session.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
using namespace observatory;
void require(bool b) { if (!b) throw Error("test assertion failed"); }
template<class F> void rejects(F f) { bool failed=false; try { f(); } catch (const std::exception&) { failed=true; } require(failed); }
const std::string good = "RTOS-OBSERVATORY\t1\tbuild-abc\tzephyr-4\tmono\t1000000\n0\t0\tSESSION\t-\ttest\t0\n1\t0\tCREATE\tworker\t-\t5\n2\t10\tSWITCH_IN\tworker\tcpu0\t0\n3\t20\tSWITCH_OUT\tworker\tcpu0\t0\n";
DecodedTrace decode(std::string s, Limits l={}) { std::istringstream in(s); return default_decoders().decode("universal-v1",in,l); }
int main() {
    const auto dir=std::filesystem::current_path()/"test-session-store";
    try {
        auto t=decode(good); require(t.identity.firmware=="build-abc"); require(t.capture.events.size()==4);
        require(t.evidence.sequence_available && t.evidence.missing_events==0);
        auto gap=good; gap.replace(gap.find("3\t20"),1,"5");
        auto g=decode(gap); require(g.evidence.missing_events==2); require(!analyze(g.capture).complete);
        rejects([&]{ auto s=good; s.replace(s.find("3\t20"),1,"2"); decode(s); });
        rejects([&]{ auto s=good; s.replace(s.find("\t1\t"),3,"\t99\t"); decode(s); });
        rejects([&]{auto s=good; s.replace(s.find("1000000"),7,"32768"); decode(s);});
        rejects([&]{decode(good,Limits{10,100,4096});});
        rejects([&]{decode(good,Limits{10000,2,4096});});
        rejects([&]{decode(good,Limits{10000,100,8});});
        rejects([&]{std::istringstream in("#"+std::string(5000,'x')); parse_trace(in);});
        rejects([&]{ std::istringstream in(good); default_decoders().decode("unknown",in); });
        std::filesystem::remove_all(dir);
        std::istringstream raw(good); store_session(dir,"universal-v1",raw);
        auto replay=replay_session(dir);
        require(report_json(analyze(replay.capture))==report_json(analyze(t.capture)));
        rejects([&]{std::istringstream in(good); store_session(dir,"universal-v1",in);});
        { std::ofstream out(dir/"index.tsv",std::ios::app); out << "corrupt"; }
        rejects([&]{replay_session(dir);});
        std::filesystem::remove_all(dir);
        {std::istringstream in(good); store_session(dir,"universal-v1",in);}
        {std::ofstream out(dir/"metadata.tsv",std::ios::app); out << "corrupt";}
        rejects([&]{replay_session(dir);});
        std::filesystem::remove_all(dir);
        {std::istringstream in(good); store_session(dir,"universal-v1",in);}
        {std::fstream file(dir/"raw.trace",std::ios::in|std::ios::out|std::ios::binary);
         auto offset=good.find("worker"); file.seekp(static_cast<std::streamoff>(offset)); file << "Worker";}
        rejects([&]{replay_session(dir);});
        std::filesystem::remove_all(dir);
        rejects([&]{std::istringstream in("bad");store_session(dir,"universal-v1",in);});
        require(!std::filesystem::exists(dir));
        std::cout << "session tests passed\n";
    } catch(const std::exception& e) { std::filesystem::remove_all(dir); std::cerr<<e.what()<<'\n'; return 1; }
}
