// ExportFunctions.java - Ghidra headless script.
//
// Dumps everything a decompilation worker needs for a range of functions, so
// the work itself never has to touch Ghidra: one JSON file per function plus an
// index. Java rather than Python deliberately - headless runs Java scripts with
// no extra setup, whereas PyGhidra needs Ghidra launched a particular way and a
// pip package installed.
//
//   analyzeHeadless <projdir> <projname> -import Fellowship.rfl \
//       -scriptPath <repo>\decomp\tools \
//       -postScript ExportFunctions.java <outdir> [startHex] [endHex]
//
// Omit the addresses to export the whole image - thousands of files from the
// rfl, so a range is usually what you want. Pass `only <hex> <hex> ...` in
// place of a range to export a scattered handful by entry point.
//
//@category OpenFellowship

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;

public class ExportFunctions extends GhidraScript {

    private static String esc(String s) {
        if (s == null) {
            return "null";
        }
        StringBuilder b = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '\\': b.append("\\\\"); break;
                case '"':  b.append("\\\""); break;
                case '\n': b.append("\\n");  break;
                case '\r': b.append("\\r");  break;
                case '\t': b.append("\\t");  break;
                default:
                    if (c < 0x20 || c > 0x7e) {
                        b.append(String.format("\\u%04x", (int) c));
                    } else {
                        b.append(c);
                    }
            }
        }
        return b.append('"').toString();
    }

    private static String arr(List<String> items) {
        StringBuilder b = new StringBuilder("[");
        for (int i = 0; i < items.size(); i++) {
            if (i > 0) {
                b.append(",");
            }
            b.append("\n      ").append(esc(items.get(i)));
        }
        if (!items.isEmpty()) {
            b.append("\n    ");
        }
        return b.append("]").toString();
    }

    /** Every byte of every executable section, sorted into what it actually is.
     *
     * Five buckets, and they partition the sections exactly - the totals are
     * asserted to add up, because a census that quietly loses bytes is worse
     * than no census.
     *
     *   body      inside a function Ghidra defined, excluding thunks
     *   thunk     inside a thunk - real code, but forwarding, and never a
     *             decompilation target, so it is counted apart rather than
     *             folded in
     *   orphan    an instruction that is in no function at all. Usually code
     *             reached only through a vtable or a jump table. This is the
     *             bucket that makes the denominator honest: it is code, it is
     *             Surreal's, and summing function bodies misses all of it
     *   padding   CC / 90 / 00 runs between functions, i.e. the alignment the
     *             linker inserted. Not code and not a target
     *   other     anything left: data the compiler put in .text (jump tables,
     *             string literals) and bytes Ghidra never disassembled
     */
    private void writeCensus(File out) throws Exception {
        Listing listing = currentProgram.getListing();
        ghidra.program.model.mem.Memory mem = currentProgram.getMemory();
        StringBuilder j = new StringBuilder("{\n  \"sections\": [");
        boolean first = true;
        long tBody = 0, tThunk = 0, tOrphan = 0, tPad = 0, tOther = 0;
        long nThunk = 0, nOrphanRuns = 0;
        java.util.Set<Long> bodies = new java.util.HashSet<Long>();
        // Where each orphan run is and what precedes it. A run that starts at
        // the byte after a function's last is a body that ended early - most
        // often at a mid-body INT3 - and that is a different problem from a run
        // standing on its own, which is simply a function Ghidra never found.
        // The first kind means a function we may have already "matched" is
        // larger than we think; the second only means the denominator is short.
        List<String> orphans = new ArrayList<String>();
        long runStart = 0, runLen = 0;
        String runAfter = null;

        for (ghidra.program.model.mem.MemoryBlock block : mem.getBlocks()) {
            if (!block.isExecute()) {
                continue;
            }
            long size = block.getSize();
            long body = 0, thunk = 0, orphan = 0, pad = 0, other = 0;
            Address a = block.getStart();
            Address end = block.getEnd();
            boolean inOrphan = false;
            // The last function we walked out of, and the address one past its
            // last byte. An orphan run starting exactly there is a continuation
            // of that function rather than a find of its own.
            String lastFunction = null;
            long lastFunctionEndAddr = -1;

            while (a.compareTo(end) <= 0 && !monitor.isCancelled()) {
                Function f = currentProgram.getFunctionManager().getFunctionContaining(a);
                Instruction ins = listing.getInstructionContaining(a);
                long step;
                if (f != null) {
                    // Jump to the end of the body rather than walking it: these
                    // are the overwhelming majority of the bytes.
                    //
                    // To the end of the RANGE containing `a`, not the end of the
                    // whole body. A Ghidra function body need not be contiguous,
                    // and taking getMaxAddress() steps straight over the hole -
                    // which counts the hole as this function's bytes and skips
                    // whatever is in it. That is how this first ran: 4,890
                    // functions and 3,106 bytes more body than the export sees,
                    // both wrong in the flattering direction.
                    Address fend = f.getBody().getRangeContaining(a).getMaxAddress();
                    step = fend.subtract(a) + 1;
                    if (f.isThunk()) {
                        thunk += step;
                        nThunk++;
                    } else {
                        body += step;
                        bodies.add(f.getEntryPoint().getOffset());
                    }
                    lastFunction = String.format("%s@%08x+%d", f.getName(),
                            f.getEntryPoint().getOffset(), f.getBody().getNumAddresses());
                    lastFunctionEndAddr = fend.getOffset() + 1;
                    if (inOrphan) {
                        orphans.add(String.format("%08x\t%d\t%s", runStart, runLen,
                                runAfter == null ? "-" : runAfter));
                    }
                    inOrphan = false;
                } else if (ins != null) {
                    step = ins.getLength();
                    orphan += step;
                    if (!inOrphan) {
                        nOrphanRuns++;
                        inOrphan = true;
                        runStart = a.getOffset();
                        runLen = 0;
                        // Only call it a continuation if nothing but the
                        // function itself is behind us - a padding run in
                        // between means the body genuinely ended.
                        runAfter = (runStart == lastFunctionEndAddr) ? lastFunction : null;
                    }
                    runLen += step;
                } else {
                    step = 1;
                    int b = mem.getByte(a) & 0xff;
                    if (b == 0xcc || b == 0x90 || b == 0x00) {
                        pad += 1;
                    } else {
                        other += 1;
                    }
                    if (inOrphan) {
                        orphans.add(String.format("%08x\t%d\t%s", runStart, runLen,
                                runAfter == null ? "-" : runAfter));
                    }
                    inOrphan = false;
                    lastFunction = null;
                    lastFunctionEndAddr = -1;
                }
                if (step <= 0) {
                    step = 1;
                }
                if (end.subtract(a) + 1 <= step) {
                    break;
                }
                a = a.add(step);
            }
            if (inOrphan) {
                orphans.add(String.format("%08x\t%d\t%s", runStart, runLen,
                        runAfter == null ? "-" : runAfter));
            }

            if (body + thunk + orphan + pad + other != size) {
                println("WARNING: " + block.getName() + " buckets sum to "
                        + (body + thunk + orphan + pad + other) + " not " + size);
            }
            if (!first) {
                j.append(",");
            }
            first = false;
            j.append(String.format(
                    "\n    {\"name\": %s, \"start\": \"0x%08x\", \"size\": %d,"
                    + " \"body\": %d, \"thunk\": %d, \"orphan\": %d,"
                    + " \"padding\": %d, \"other\": %d}",
                    esc(block.getName()), block.getStart().getOffset(), size,
                    body, thunk, orphan, pad, other));
            tBody += body; tThunk += thunk; tOrphan += orphan;
            tPad += pad; tOther += other;
        }

        j.append("\n  ],\n");
        j.append(String.format(
                "  \"total\": {\"body\": %d, \"thunk\": %d, \"orphan\": %d,"
                + " \"padding\": %d, \"other\": %d},\n",
                tBody, tThunk, tOrphan, tPad, tOther));
        j.append(String.format(
                "  \"functions\": %d, \"thunks\": %d, \"orphan_runs\": %d\n",
                bodies.size(), nThunk, nOrphanRuns));
        j.append("}");

        PrintWriter w = new PrintWriter(new File(out, "census.json"));
        w.println(j.toString());
        w.close();

        // The runs themselves, as a plain table rather than JSON: this is a
        // worklist, and a worklist wants to be greppable and sortable.
        w = new PrintWriter(new File(out, "orphans.tsv"));
        w.println("# start\tbytes\tcontinues");
        w.println("# `continues` names the function whose body ends exactly where");
        w.println("# this run begins, if any - those are truncations, not finds.");
        for (String o : orphans) {
            w.println(o);
        }
        w.close();
        println("census: " + tBody + " body, " + tThunk + " thunk, " + tOrphan
                + " orphan, " + tPad + " padding, " + tOther + " other");
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            println("ERROR: first argument must be the output directory");
            return;
        }
        File out = new File(args[0]);
        out.mkdirs();

        // "index-only" anywhere in the arguments writes just index.json and no
        // per-function files - what you want to count a whole image, where the
        // full export would be thousands of files and most of the run time.
        boolean indexOnly = false;
        // "census" accounts for every byte of every executable section rather
        // than only the ones inside a function body, and writes census.json.
        //
        // Worth having as its own mode because the function-body total is not
        // the size of the image's code and it is easy to assume it is. Ghidra
        // does not put every instruction in a function: code reached only
        // through a vtable or a jump table often stays loose, thunks are
        // deliberately excluded from the export, and a mid-body INT3 can end a
        // body early. Summing bodies therefore understates the denominator, and
        // an understated denominator makes coverage look better than it is -
        // the one direction a progress measure must not be wrong in.
        boolean census = false;
        // The argument "only" makes every argument after it an entry point to
        // export, and nothing else is exported. A range is the wrong shape when
        // the targets are scattered across the image, as they are when they
        // come from a list of known patch sites rather than one module.
        //
        // A list rather than "only=a,b,c" because analyzeHeadless splits script
        // arguments on '=' and ',' before the script ever sees them.
        java.util.Set<Long> only = new java.util.HashSet<Long>();
        boolean collecting = false;
        List<String> rest = new ArrayList<String>();
        for (int i = 1; i < args.length; i++) {
            if ("index-only".equals(args[i])) {
                indexOnly = true;
            } else if ("census".equals(args[i])) {
                census = true;
            } else if ("only".equals(args[i])) {
                collecting = true;
            } else if (collecting) {
                only.add(Long.parseLong(args[i].trim().replace("0x", ""), 16));
            } else {
                rest.add(args[i]);
            }
        }
        long lo = rest.size() > 0 ? Long.parseLong(rest.get(0).replace("0x", ""), 16) : Long.MIN_VALUE;
        long hi = rest.size() > 1 ? Long.parseLong(rest.get(1).replace("0x", ""), 16) : Long.MAX_VALUE;

        if (census) {
            writeCensus(out);
            return;
        }

        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);

        Listing listing = currentProgram.getListing();
        StringBuilder index = new StringBuilder("[");
        int count = 0, leaves = 0;

        Iterator<Function> it = currentProgram.getFunctionManager().getFunctions(true).iterator();
        while (it.hasNext() && !monitor.isCancelled()) {
            Function f = it.next();
            long entry = f.getEntryPoint().getOffset();
            if (f.isExternal() || f.isThunk()) {
                continue;
            }
            if (only.isEmpty() ? (entry < lo || entry > hi) : !only.contains(entry)) {
                continue;
            }
            // Ghidra's body excludes the alignment padding that follows, which
            // is exactly the size matchtool needs - a size running into the
            // padding would compare bytes that are not part of the function.
            long size = f.getBody().getNumAddresses();
            if (size == 0) {
                continue;
            }

            List<String> dis = new ArrayList<String>();
            List<String> calls = new ArrayList<String>();
            List<String> data = new ArrayList<String>();

            if (indexOnly) {
                // Still needs the call count, which is cheap; skip disassembly
                // text and the decompiler, which are not.
                for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
                    for (Reference ref : ins.getReferencesFrom()) {
                        if (ref.getReferenceType().isCall()) {
                            String e = String.format("%08x", ref.getToAddress().getOffset());
                            if (!calls.contains(e)) {
                                calls.add(e);
                            }
                        }
                    }
                }
                if (count > 0) {
                    index.append(",");
                }
                index.append(String.format(
                        "\n  {\"entry\": \"0x%08x\", \"size\": %d, \"name\": %s, \"calls\": %d, \"leaf\": %s}",
                        entry, size, esc(f.getName()), calls.size(),
                        calls.isEmpty() ? "true" : "false"));
                count++;
                if (calls.isEmpty()) {
                    leaves++;
                }
                if (count % 500 == 0) {
                    println("counted " + count + " functions");
                }
                continue;
            }

            for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
                StringBuilder raw = new StringBuilder();
                for (byte b : ins.getBytes()) {
                    raw.append(String.format("%02x", b & 0xff));
                }
                dis.add(String.format("%08x  %-24s %s",
                        ins.getAddress().getOffset(), raw.toString(), ins.toString()));

                for (Reference ref : ins.getReferencesFrom()) {
                    Address to = ref.getToAddress();
                    if (ref.getReferenceType().isCall()) {
                        Function callee = currentProgram.getFunctionManager().getFunctionAt(to);
                        String e = String.format("%08x %s", to.getOffset(),
                                callee == null ? "?" : callee.getName());
                        if (!calls.contains(e)) {
                            calls.add(e);
                        }
                    } else {
                        Data d = listing.getDataAt(to);
                        if (d != null && d.hasStringValue()) {
                            String e = String.format("%08x %s", to.getOffset(),
                                    String.valueOf(d.getValue()));
                            if (!data.contains(e)) {
                                data.add(e);
                            }
                        }
                    }
                }
            }

            String c = null;
            DecompileResults res = ifc.decompileFunction(f, 60, monitor);
            if (res != null && res.decompileCompleted()) {
                c = res.getDecompiledFunction().getC();
            }

            PrintWriter w = new PrintWriter(new File(out, String.format("%08x.json", entry)));
            w.println("{");
            w.println("  \"name\": " + esc(f.getName()) + ",");
            w.println("  \"entry\": " + esc(String.format("0x%08x", entry)) + ",");
            w.println("  \"size\": " + size + ",");
            w.println("  \"calling_convention\": " + esc(f.getCallingConventionName()) + ",");
            w.println("  \"signature\": " + esc(f.getSignature().getPrototypeString()) + ",");
            w.println("  \"calls\": " + arr(calls) + ",");
            w.println("  \"data\": " + arr(data) + ",");
            w.println("  \"disassembly\": " + arr(dis) + ",");
            w.println("  \"decompiled\": " + esc(c));
            w.println("}");
            w.close();

            if (count > 0) {
                index.append(",");
            }
            index.append(String.format(
                    "\n  {\"entry\": \"0x%08x\", \"size\": %d, \"name\": %s, \"calls\": %d, \"leaf\": %s}",
                    entry, size, esc(f.getName()), calls.size(), calls.isEmpty() ? "true" : "false"));
            count++;
            if (calls.isEmpty()) {
                leaves++;
            }
            if (count % 100 == 0) {
                println("exported " + count + " functions");
            }
        }

        index.append("\n]");
        PrintWriter w = new PrintWriter(new File(out, "index.json"));
        w.println(index.toString());
        w.close();

        ifc.dispose();
        println("exported " + count + " functions to " + out + " (" + leaves + " leaf)");
    }
}
