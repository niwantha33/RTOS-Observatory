// SPDX-License-Identifier: Apache-2.0
// W15 SOURCE PATCH. Requires integration and PDE/Tycho tests in owning ZEW plugin.
// No second GDB/OpenOCD client and no automatic memory/CPU register probing.
package io.github.niwantha33.zew.ui;

import java.io.IOException;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.Consumer;

import org.eclipse.cdt.core.IAddress;
import org.eclipse.cdt.dsf.concurrent.DataRequestMonitor;
import org.eclipse.cdt.dsf.datamodel.DMContexts;
import org.eclipse.cdt.dsf.datamodel.IDMContext;
import org.eclipse.cdt.dsf.debug.service.IFormattedValues;
import org.eclipse.cdt.dsf.debug.service.IMemory;
import org.eclipse.cdt.dsf.debug.service.IRunControl;
import org.eclipse.cdt.dsf.debug.service.IRegisters;
import org.eclipse.cdt.dsf.debug.service.IRegisters.IRegisterDMContext;
import org.eclipse.cdt.dsf.gdb.launching.GdbLaunch;
import org.eclipse.cdt.dsf.service.DsfServicesTracker;
import org.eclipse.cdt.dsf.service.DsfSession;
import org.eclipse.cdt.utils.Addr64;
import org.eclipse.debug.core.model.MemoryByte;
import org.osgi.framework.FrameworkUtil;

/**
 * Explicit, single-use, owning-DSF read adapter. Not enabled in W14.
 *
 * Call ONLY after the Eclipse UI has obtained explicit user consent and a
 * board/adapter has qualified ALL cores halted and each RAM range as
 * non-side-effectful. An arbitrary user-entered MMIO address must NEVER be
 * considered a QualifiedRamRegion.
 *
 * Every actual DSF operation rechecks the selected execution context on
 * the same DSF executor. No suspend, resume, reset, write, flash or GDB client.
 */
public final class DsfApprovedSnapshotCapture {
    public enum Kind { MEM, TCB, REG }
    public record QualifiedRamRegion(BigInteger base, BigInteger length,
                                     boolean verifiedReadSafe) {}
    public record Item(Kind kind,String label,BigInteger address,int length) {}
    public record Consent(String sessionNonce,long epoch,String approvalId,
                          long expiresUnixMs,boolean userConfirmed,
                          boolean allCoresQualifiedSuspended,
                          List<QualifiedRamRegion> ramRegions,List<Item> items) {}
    public record Result(Item item,boolean success,byte[] bytes,
                         BigInteger registerValue) {}

    private static final int MAX_ITEMS=32, MAX_BYTES=4096, MAX_PER_READ=256, MAX_REGS=16;
    private static final Set<String> CPU_REGS=Set.of(
        "PC","SP","LR","R0","R1","R2","R3","R4","R5","R6","R7",
        "R8","R9","R10","R11","R12","XPSR","MSP","PSP");
    private static final Set<String> CONSUMED=ConcurrentHashMap.newKeySet();

    private static boolean label(String s) {
        return s!=null && s.matches("[A-Za-z0-9_.-]{1,64}");
    }
    private static void validate(Consent c) {
        final long now=Instant.now().toEpochMilli();
        if(c==null || !c.userConfirmed() || !c.allCoresQualifiedSuspended() ||
           !label(c.sessionNonce()) || !label(c.approvalId()) ||
           c.epoch()<=0 || c.expiresUnixMs()<now ||
           c.expiresUnixMs()-now>30000 ||
           c.items()==null || c.items().isEmpty() ||
           c.items().size()>MAX_ITEMS || c.ramRegions()==null ||
           c.ramRegions().size()>64)
            throw new IllegalArgumentException("W15: explicit, short-lived approval and all-core stop evidence required");
        int total=0,registers=0;
        final Set<String> seen=ConcurrentHashMap.newKeySet();
        for(final Item item:c.items()) {
            if(item==null || item.kind()==null || !label(item.label()) ||
               !seen.add(item.kind()+":"+item.label()))
                throw new IllegalArgumentException("W15: invalid/duplicate item");
            if(item.kind()==Kind.REG) {
                if(item.length()!=0 || item.address()==null ||
                   item.address().signum()!=0 || !CPU_REGS.contains(item.label()) ||
                   ++registers>MAX_REGS)
                    throw new IllegalArgumentException("W15: unapproved register");
                continue;
            }
            if(item.address()==null || item.address().signum()<0 ||
               item.address().bitLength()>32 || item.length()<1 ||
               item.length()>MAX_PER_READ || total+item.length()>MAX_BYTES)
                throw new IllegalArgumentException("W15: address/size outside limits");
            final BigInteger end=item.address().add(BigInteger.valueOf(item.length()));
            if(end.compareTo(BigInteger.ONE.shiftLeft(32))>0)
                throw new IllegalArgumentException("W15: address range overflow");
            boolean safe=false;
            for(final QualifiedRamRegion ram:c.ramRegions()) {
                if(ram==null || !ram.verifiedReadSafe() || ram.base()==null ||
                   ram.length()==null || ram.base().signum()<0 ||
                   ram.length().signum()<=0)continue;
                if(item.address().compareTo(ram.base())>=0 &&
                   end.compareTo(ram.base().add(ram.length()))<=0) {
                    safe=true;break;
                }
            }
            if(!safe)throw new IllegalArgumentException("W15: RAM-only allowlist; MMIO denied");
            total+=item.length();
        }
    }

    /**
     * Existing DSF session ONLY; never create a GDB or probe connection.
     * @param selected Dsf context from the selected suspended debugger thread.
     * @param receiptFile User-approved local output file path (not symlink).
     * @param finished Completion on the DSF executor; UI caller must marshal.
     */
    public static void capture(GdbLaunch launch, IDMContext selected,
                               Consent consent, Path receiptFile,
                               Consumer<String> finished) {
        Objects.requireNonNull(finished,"W15 completion callback required");
        try {
            validate(consent);
            if(launch==null || selected==null || receiptFile==null ||
               finished==null || launch.isTerminated())
                throw new IllegalArgumentException("No existing DSF launch/context/output");
            final String key=consent.sessionNonce()+":"+consent.epoch()+":"+
                             consent.approvalId();
            if(CONSUMED.size()>=1024)
                throw new IllegalArgumentException("W15 approval registry full; restart the owner plugin");
            if(!CONSUMED.add(key))
                throw new IllegalArgumentException("W15 approval already consumed");
            final DsfSession session=launch.getSession();
            if(session==null || !DsfSession.isSessionActive(session.getId()) ||
               !session.getId().equals(selected.getSessionId()))
                throw new IllegalArgumentException("W15 DSF session mismatch");
            if(Files.isSymbolicLink(receiptFile))
                throw new IllegalArgumentException("Refusing symlinked receipt destination");
            session.getExecutor().execute(() -> {
                final DsfServicesTracker services=new DsfServicesTracker(
                    FrameworkUtil.getBundle(DsfApprovedSnapshotCapture.class).getBundleContext(),
                    session.getId());
                try {
                    final IRunControl control=services.getService(IRunControl.class);
                    final IMemory memory=services.getService(IMemory.class);
                    final IRegisters registers=services.getService(IRegisters.class);
                    final var execution=DMContexts.getAncestorOfType(
                        selected,IRunControl.IExecutionDMContext.class);
                    final var ramContext=DMContexts.getAncestorOfType(
                        selected,IMemory.IMemoryDMContext.class);
                    if(control==null || execution==null ||
                       !control.isSuspended(execution))
                        throw new IllegalArgumentException("Selected execution context is not suspended");
                    if(memory==null && consent.items().stream().anyMatch(i->i.kind()!=Kind.REG))
                        throw new IllegalArgumentException("DSF memory capability unavailable");
                    if(registers==null && consent.items().stream().anyMatch(i->i.kind()==Kind.REG))
                        throw new IllegalArgumentException("DSF registers capability unavailable");
                    if(ramContext==null && consent.items().stream().anyMatch(i->i.kind()!=Kind.REG))
                        throw new IllegalArgumentException("No owning DSF memory context");
                    step(session,launch,control,execution,memory,ramContext,registers,
                         selected,consent,receiptFile,new ArrayList<>(),0,
                         result -> {
                             services.dispose();
                             finished.accept(result);
                         });
                } catch(RuntimeException failure) {
                    services.dispose();
                    finished.accept("DENIED: "+failure.getMessage());
                }
            });
        } catch(RejectedExecutionException | IllegalArgumentException failure) {
            finished.accept("DENIED: "+failure.getMessage());
        }
    }

    private static void step(DsfSession session,GdbLaunch launch,IRunControl control,
                             IRunControl.IExecutionDMContext execution,
                             IMemory memory, IMemory.IMemoryDMContext ramContext,
                             IRegisters registers,IDMContext selected,Consent consent,
                             Path output,List<Result> results,int index,
                             Consumer<String> finished) {
        if(launch.isTerminated() || !DsfSession.isSessionActive(session.getId()) ||
           !control.isSuspended(execution) ||
           Instant.now().toEpochMilli()>consent.expiresUnixMs()) {
            // Do not publish a partial "successful" capture as coherent.
            finished.accept("DENIED: target resumed/terminated or approval expired");
            return;
        }
        if(index==consent.items().size()) {
            try { writeReceipt(output,consent,results);finished.accept("Saved unverified W15 receipt"); }
            catch(IOException failure) {finished.accept("FAILED: "+failure.getMessage());}
            return;
        }
        final Item item=consent.items().get(index);
        final Consumer<Result> next=result -> {
            results.add(result);
            step(session,launch,control,execution,memory,ramContext,registers,
                 selected,consent,output,results,index+1,finished);
        };
        if(item.kind()==Kind.REG) {
            registers.findRegister(selected,item.label().toLowerCase(Locale.ROOT),
                new DataRequestMonitor<IRegisterDMContext>(session.getExecutor(),null) {
                    @Override protected void handleCompleted() {
                        if(!isSuccess() || getData()==null) {
                            next.accept(new Result(item,false,new byte[0],null));return;
                        }
                        final var valueContext=registers.getFormattedValueContext(
                            getData(),IFormattedValues.HEX_FORMAT);
                        registers.getFormattedExpressionValue(valueContext,
                            new DataRequestMonitor<IFormattedValues.FormattedValueDMData>(
                                session.getExecutor(),null) {
                                @Override protected void handleCompleted() {
                                    if(!isSuccess() || getData()==null) {
                                        next.accept(new Result(item,false,new byte[0],null));return;
                                    }
                                    try {
                                        final String raw=getData().getFormattedValue()
                                            .replaceFirst("^0[xX]","");
                                        final BigInteger parsed=new BigInteger(raw,16);
                                        if(parsed.signum()<0 || parsed.bitLength()>64)
                                            throw new IllegalArgumentException("invalid CPU register");
                                        next.accept(new Result(item,true,new byte[0],parsed));
                                    } catch(RuntimeException invalid) {
                                        next.accept(new Result(item,false,new byte[0],null));
                                    }
                                }
                            });
                    }
                });
        } else {
            final IAddress base=new Addr64(item.address());
            memory.getMemory(ramContext,base,0,1,item.length(),
                new DataRequestMonitor<MemoryByte[]>(session.getExecutor(),null) {
                    @Override protected void handleCompleted() {
                        if(!isSuccess() || getData()==null || getData().length!=item.length()) {
                            next.accept(new Result(item,false,new byte[0],null));return;
                        }
                        final byte[] saved=new byte[item.length()];
                        for(int i=0;i<saved.length;++i) {
                            if(!getData()[i].isReadable()) {
                                next.accept(new Result(item,false,new byte[0],null));return;
                            }
                            saved[i]=getData()[i].getValue();
                        }
                        next.accept(new Result(item,true,saved,null));
                    }
                });
        }
    }

    private static void writeReceipt(Path file,Consent consent,List<Result> results)
        throws IOException {
        if(Files.isSymbolicLink(file))
            throw new IOException("Refusing symlink destination");
        final StringBuilder out=new StringBuilder();
        out.append("RTOS-OBSERVATORY-APPROVED-SNAPSHOT\t1\t")
           .append(consent.sessionNonce()).append('\t').append(consent.epoch())
           .append('\t').append(consent.approvalId())
           .append("\tEclipse-DSF-source\t32\tunverified\n");
        for(final Result r:results) {
            out.append(r.item().kind()).append('\t').append(r.item().label())
               .append("\t0x").append(r.item().address().toString(16))
               .append('\t').append(r.item().length());
            if(!r.success())out.append("\tfailed\t-\n");
            else if(r.item().kind()==Kind.REG)
                out.append("\tok\t0x").append(r.registerValue().toString(16)).append('\n');
            else {
                out.append("\tok\t");
                for(final byte b:r.bytes())
                    out.append(Character.forDigit((b>>4)&15,16))
                       .append(Character.forDigit(b&15,16));
                out.append('\n');
            }
        }
        final Path tmp=Files.createTempFile(file.toAbsolutePath().getParent(),
                                            "rtos-reads-", ".tmp");
        try {
            Files.writeString(tmp,out.toString(),StandardCharsets.UTF_8);
            Files.move(tmp,file,StandardCopyOption.ATOMIC_MOVE,
                       StandardCopyOption.REPLACE_EXISTING);
        } catch(AtomicMoveNotSupportedException unsupported) {
            throw new IOException("Atomic filesystem publication not supported",unsupported);
        } finally {
            Files.deleteIfExists(tmp);
        }
    }
}
