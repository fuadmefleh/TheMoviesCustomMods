// Decompiles the first N virtual functions of each vtable given.
// Usage (headless): -postScript Vtable.java <output.txt> <count> <vtable_addr> [...]
//@category MoviesMod

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class Vtable extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int count = Integer.parseInt(args[1]);
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
            for (int i = 2; i < args.length; i++) {
                Address vt = toAddr(args[i]);
                out.println("// ######## vtable " + vt);
                for (int slot = 0; slot < count; slot++) {
                    Address fn = toAddr(getInt(vt.add(slot * 4L)) & 0xffffffffL);
                    Function f = getFunctionAt(fn);
                    if (f == null) {
                        new CreateFunctionCmd(fn).applyTo(currentProgram, monitor);
                        f = getFunctionAt(fn);
                    }
                    out.println("// ---- slot " + slot + " -> " + fn + (f != null ? " " + f.getName() : " (not code)"));
                    if (f == null)
                        continue;
                    DecompileResults res = decomp.decompileFunction(f, 60, monitor);
                    out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed");
                }
            }
        }
        decomp.dispose();
    }
}
