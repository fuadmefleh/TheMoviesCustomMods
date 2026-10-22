// Prints disassembly. Each spec is <addr>:<count> (instructions from addr).
// Usage (headless): -postScript Disasm.java <output.txt> <addr:count> [...]
//@category MoviesMod

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Instruction;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class Disasm extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                String[] spec = args[i].split(":");
                Instruction ins = getInstructionAt(toAddr(spec[0]));
                if (ins == null)
                    ins = getInstructionAfter(toAddr(spec[0]));
                out.println("; ---- " + args[i]);
                for (int n = 0; n < Integer.parseInt(spec[1]) && ins != null; n++, ins = ins.getNext())
                    out.println(ins.getAddress() + "  " + ins);
            }
        }
    }
}
