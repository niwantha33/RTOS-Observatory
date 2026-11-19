// SPDX-License-Identifier: Apache-2.0
#include "observatory/snapshot_acquisition.hpp"
#include <cassert>
#include <iostream>
using namespace observatory;

static SnapshotContext context() {
    SnapshotContext c;
    c.owner.session_nonce="session-A";
    c.owner.epoch=7;
    c.owner.state=BridgeState::Suspended;
    c.dsf_service_owner=true;
    c.debugger_suspended=true;
    c.all_cores_confirmed_suspended=true;
    c.memory_read_capability=true;
    c.register_read_capability=true;
    c.user_consent_confirmed=true;
    c.now_unix_ms=2000;
    return c;
}
static SnapshotRequest request() {
    SnapshotRequest r;
    r.session_nonce="session-A";
    r.epoch=7;
    r.user_approval_id="approved-1";
    r.expires_unix_ms=15000;
    r.address_bits=32;
    r.allowlist={{0x20000000,0x4000,true,true,"onchip-ram"},
                 {0x40000000,0x1000,false,false,"periph-mmio"},
                 {0x08000000,0x20000,false,false,"flash"}};
    r.items={{SnapshotKind::Tcb,"task-A",0x20000100,64},
             {SnapshotKind::Memory,"stack-A",0x20001000,128},
             {SnapshotKind::Register,"PC",0,0},
             {SnapshotKind::Register,"SP",0,0}};
    return r;
}
int main() {
    auto ctx=context();
    auto req=request();
    const auto ok=qualify_snapshot(req,ctx);
    assert(ok.allowed && ok.planned_memory_bytes==192 && ok.planned_registers==2);
    SnapshotConsent one_shot;
    assert(one_shot.consume(req,ctx).allowed);
    assert(!one_shot.consume(req,ctx).allowed);
    req.user_approval_id="approved-2";
    assert(one_shot.consume(req,ctx).allowed);

    auto denied=ctx;
    denied.user_consent_confirmed=false;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.dsf_service_owner=false;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.owner.state=BridgeState::Running;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.debugger_suspended=false;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.all_cores_confirmed_suspended=false;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.memory_read_capability=false;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.register_read_capability=false;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.owner.epoch=8;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.owner.session_nonce="other";
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.now_unix_ms=20000;
    assert(!qualify_snapshot(req,denied).allowed);
    denied=ctx;denied.now_unix_ms=1000;
    auto future=req;future.expires_unix_ms=50000;
    assert(!qualify_snapshot(future,denied).allowed);

    auto bad=req;
    bad.items[0].address=0x40000000; // peripheral reads forbidden
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items[0].address=0x08000000; // no flash reads
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items[0].address=0x20003ff0; // crosses RAM end
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items[0].address=0xfffffff0; // 32-bit overflow
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items[0].bytes=257;
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items[0].bytes=0;
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items[2].label="CFSR"; // unsupported special/memory mapped
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items[2].label="PC;reset";
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items.push_back(bad.items.back());
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items.clear();
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.address_bits=16;
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.user_approval_id="../../unsafe";
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.session_nonce="unknown";
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.allowlist[0].verified_read_safe=false;
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.allowlist[0].ram=false;
    assert(!qualify_snapshot(bad,ctx).allowed);
    bad=req;bad.items.resize(1);bad.items[0].bytes=128;
    assert(!qualify_snapshot(bad,ctx,SnapshotLimits{32,256,64,16,30000}).allowed);
    bad=req;bad.items={};
    for(int i=0;i<17;++i)
        bad.items.push_back({SnapshotKind::Register,"R"+std::to_string(i),0,0});
    assert(!qualify_snapshot(bad,ctx).allowed);
    assert(!qualify_snapshot(req,ctx,SnapshotLimits{0,256,4096,16,30000}).allowed);

    SnapshotCapture sample;
    sample.request=req;
    sample.owner_epoch=7;
    sample.source_label="synthetic-only";
    sample.samples={
        {req.items[0],true,std::vector<std::uint8_t>(64,0xaa),{},{}},
        {req.items[1],false,{},{},"READ_FAILED"},
        {req.items[2],true,{},0x08000100,{}},
        {req.items[3],false,{},{},"NOT_AVAILABLE"}
    };
    assert(verify_snapshot_capture(sample));
    sample.all_cores_coherent=true;
    assert(!verify_snapshot_capture(sample));sample.all_cores_coherent=false;
    sample.samples[1].data.push_back(0);
    assert(!verify_snapshot_capture(sample));sample.samples[1].data.clear();
    sample.samples[3].register_value=0;
    assert(!verify_snapshot_capture(sample));sample.samples[3].register_value.reset();
    sample.samples[0].data.pop_back();
    assert(!verify_snapshot_capture(sample));sample.samples[0].data.push_back(0xaa);
    sample.samples[2].register_value.reset();
    assert(!verify_snapshot_capture(sample));sample.samples[2].register_value=0x08000100;
    sample.owner_epoch=8;
    assert(!verify_snapshot_capture(sample));sample.owner_epoch=7;
    assert(verify_snapshot_capture(sample));
    std::cout<<"W15 suspended-owner, RAM safety, one-shot consent, bounded reads and unknown results passed\n";
}
