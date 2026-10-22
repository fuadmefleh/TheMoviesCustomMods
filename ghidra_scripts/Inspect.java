// Decompiles the functions at the given addresses and lists their callers.
// Usage (headless): -postScript Inspect.java <output.txt> <addr> [<addr> ...]
//@category MoviesMod

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class Inspect extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                Address addr = toAddr(args[i]);
                Function f = getFunctionContaining(addr);
                if (f == null) {
                    out.println("// no function at " + addr);
                    continue;
                }
                Set<String> callers = new TreeSet<>();
                int refCount = 0;
                for (Reference ref : getReferencesTo(f.getEntryPoint())) {
                    refCount++;
                    Function c = getFunctionContaining(ref.getFromAddress());
                    callers.add(c != null ? c.getName() + "@" + c.getEntryPoint() : ref.getFromAddress().toString());
                }
                out.println("// ==== " + f.getName() + " @ " + f.getEntryPoint() + " sig: " + f.getSignature());
                out.println("// " + refCount + " refs from " + callers.size() + " functions: "
                        + String.join(", ", new ArrayList<>(callers).subList(0, Math.min(40, callers.size()))));
                DecompileResults res = decomp.decompileFunction(f, 120, monitor);
                out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed: " + res.getErrorMessage());
            }
        }
        decomp.dispose();
    }
}
