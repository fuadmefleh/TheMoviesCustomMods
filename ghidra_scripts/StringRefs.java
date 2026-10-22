// For each string given, lists the code that references it and decompiles those functions.
// Usage (headless): -postScript StringRefs.java <output.txt> <string> [<string> ...]
//@category MoviesMod

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class StringRefs extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        Memory mem = currentProgram.getMemory();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        Map<Function, Set<String>> funcs = new LinkedHashMap<>();

        try (PrintWriter out = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                byte[] needle = (args[i] + "\0").getBytes(StandardCharsets.US_ASCII);
                // Match whole strings only: the byte before must be a terminator.
                Address addr = currentProgram.getMinAddress();
                List<String> hits = new ArrayList<>();
                while ((addr = mem.findBytes(addr, needle, null, true, monitor)) != null) {
                    byte prev = mem.getByte(addr.subtract(1));
                    if (prev == 0) {
                        List<String> sites = new ArrayList<>();
                        for (Reference ref : getReferencesTo(addr)) {
                            Function f = getFunctionContaining(ref.getFromAddress());
                            sites.add(ref.getFromAddress() + (f != null ? " in " + f.getName() : " (data)"));
                            if (f != null)
                                funcs.computeIfAbsent(f, k -> new TreeSet<>()).add(args[i]);
                        }
                        hits.add(addr + " <- " + (sites.isEmpty() ? "no refs" : String.join("; ", sites)));
                    }
                    addr = addr.add(1);
                }
                out.println(args[i] + ": " + (hits.isEmpty() ? "not found" : String.join(" | ", hits)));
            }
            for (Map.Entry<Function, Set<String>> e : funcs.entrySet()) {
                Function f = e.getKey();
                out.println();
                out.println("// ---- " + f.getName() + " @ " + f.getEntryPoint() + " uses " + e.getValue());
                DecompileResults res = decomp.decompileFunction(f, 120, monitor);
                out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed");
            }
        }
        decomp.dispose();
    }
}
