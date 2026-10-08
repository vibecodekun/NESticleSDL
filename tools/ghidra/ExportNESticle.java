// Export decompiler output and string references for local source recovery.
// @category NESticle
import java.io.*;
import java.nio.charset.StandardCharsets;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.AddressSet;

public class ExportNESticle extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) throw new IllegalArgumentException("Expected export directory [address=name ...]");
        for (int i = 1; i < args.length; i++) {
            if (args[i].startsWith("+")) {
                disassemble(toAddr(args[i].substring(1)));
                continue;
            }
            if (args[i].startsWith("-")) {
                Function mistaken = getFunctionAt(toAddr(args[i].substring(1)));
                if (mistaken != null) removeFunction(mistaken);
                continue;
            }
            String[] pair = args[i].split("=", 2);
            ghidra.program.model.address.Address address = toAddr(pair[0]);
            Function function = getFunctionAt(address);
            if (function == null) {
                disassemble(address);
                function = createFunction(address, pair.length == 2 ? pair[1].split("@", 2)[0] : null);
            }
            if (function == null) throw new IllegalArgumentException("Cannot define function: " + args[i]);
            if (pair.length == 2) {
                String[] definition = pair[1].split("@", 2);
                function.setName(definition[0], SourceType.USER_DEFINED);
                if (definition.length == 2) {
                    Function continuation = getFunctionAt(toAddr(definition[1]));
                    if (continuation != null && continuation != function) {
                        AddressSet body = new AddressSet(function.getBody());
                        body.add(continuation.getBody());
                        removeFunction(continuation);
                        function.setBody(body);
                    }
                }
            }
        }
        File dir = new File(args[0], currentProgram.getName());
        dir.mkdirs();
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        int count = 0, failed = 0;
        try (PrintWriter index = new PrintWriter(new File(dir, "functions.tsv"), StandardCharsets.UTF_8);
             PrintWriter strings = new PrintWriter(new File(dir, "strings.tsv"), StandardCharsets.UTF_8)) {
            index.println("address\tname\tsize\tstatus");
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                if (function.isExternal()) continue;
                DecompileResults result = decompiler.decompileFunction(function, 20, monitor);
                String status = result.decompileCompleted() ? "ok" : result.getErrorMessage();
                index.printf("%s\t%s\t%d\t%s%n", function.getEntryPoint(), function.getName(), function.getBody().getNumAddresses(), status);
                if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                    try (PrintWriter file = new PrintWriter(new File(dir, function.getEntryPoint() + ".c"), StandardCharsets.UTF_8)) {
                        file.printf("// %s @ %s; %s%n", currentProgram.getName(), function.getEntryPoint(), function.getSignature());
                        file.print(result.getDecompiledFunction().getC());
                    }
                    try (PrintWriter file = new PrintWriter(new File(dir, function.getEntryPoint() + ".asm"), StandardCharsets.UTF_8)) {
                        InstructionIterator instructions = currentProgram.getListing().getInstructions(function.getBody(), true);
                        while (instructions.hasNext()) {
                            Instruction instruction = instructions.next();
                            file.printf("%s  %s%n", instruction.getAddress(), instruction);
                        }
                    }
                } else failed++;
                count++;
            }
            strings.println("address\tvalue\treference\tfunction");
            DataIterator data = currentProgram.getListing().getDefinedData(true);
            while (data.hasNext()) {
                Data item = data.next();
                if (!(item.getValue() instanceof String)) continue;
                String value = item.getValue().toString().replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n");
                ReferenceIterator references = currentProgram.getReferenceManager().getReferencesTo(item.getAddress());
                while (references.hasNext()) {
                    Reference reference = references.next();
                    Function function = currentProgram.getFunctionManager().getFunctionContaining(reference.getFromAddress());
                    strings.printf("%s\t%s\t%s\t%s%n", item.getAddress(), value, reference.getFromAddress(), function == null ? "" : function.getEntryPoint());
                }
            }
        } finally { decompiler.dispose(); }
        try (PrintWriter listing = new PrintWriter(new File(dir, "all-instructions.asm"), StandardCharsets.UTF_8)) {
            InstructionIterator instructions = currentProgram.getListing().getInstructions(true);
            while (instructions.hasNext()) {
                Instruction instruction = instructions.next();
                listing.printf("%s  %s%n", instruction.getAddress(), instruction);
            }
        }
        try (PrintWriter complete = new PrintWriter(new File(dir, "export.complete"), StandardCharsets.UTF_8)) {
            complete.printf("functions=%d failures=%d%n", count, failed);
        }
        println("NESticle export: " + count + " functions, " + failed + " failures, " + dir);
    }
}
