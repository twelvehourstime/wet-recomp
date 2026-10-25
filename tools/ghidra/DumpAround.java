// Headless helper: dump the loaded image's memory blocks to <out>/image_<start>.bin
// and print a disassembly window around each address listed in <out>/addrs.txt.
// Usage (postScript): DumpAround.java <out_dir>
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class DumpAround extends GhidraScript {
    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0]);
        out.mkdirs();
        Memory mem = currentProgram.getMemory();
        try (PrintWriter seg = new PrintWriter(new File(out, "blocks.txt"))) {
            for (MemoryBlock b : mem.getBlocks()) {
                seg.printf("%s %s %s r%s w%s x%s init=%s%n", b.getName(), b.getStart(), b.getEnd(),
                        b.isRead(), b.isWrite(), b.isExecute(), b.isInitialized());
                if (!b.isInitialized()) continue;
                byte[] buf = new byte[(int) b.getSize()];
                b.getBytes(b.getStart(), buf);
                Files.write(new File(out, "image_" + b.getStart() + ".bin").toPath(), buf);
            }
        }
        File addrs = new File(out, "addrs.txt");
        if (!addrs.exists()) return;
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        Listing listing = currentProgram.getListing();
        try (PrintWriter pw = new PrintWriter(new File(out, "around.txt"))) {
            for (String line : Files.readAllLines(addrs.toPath())) {
                line = line.trim();
                if (line.isEmpty()) continue;
                String[] p = line.split("\s+");
                long a = Long.decode(p[0]);
                int before = p.length > 1 ? Integer.decode(p[1]) : 0x40;
                int after = p.length > 2 ? Integer.decode(p[2]) : 0x40;
                Address start = space.getAddress(a - before), end = space.getAddress(a + after);
                new DisassembleCommand(new AddressSet(start, end), null, false).applyTo(currentProgram);
                pw.printf("==== 0x%08X%n", a);
                for (Address x = start; x.compareTo(end) <= 0; x = x.add(4)) {
                    Instruction ins = listing.getInstructionAt(x);
                    int w = mem.getInt(x);
                    pw.printf("%s %s %08X  %s%n", x.getOffset() == a ? ">" : " ", x, w,
                            ins != null ? ins.toString() : "??");
                }
            }
        }
    }
}
