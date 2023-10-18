/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package jdk.test.lib;

import java.io.File;
import jdk.test.lib.cds.CDSTestUtils;
import jdk.test.lib.process.ProcessTools;
import jdk.test.lib.process.OutputAnalyzer;

abstract public class LeydenTester {
    public enum RunMode {
        // OLD workflow
        CLASSLIST,
        DUMP_STATIC,
        DUMP_DYNAMIC,
        DUMP_CODECACHE,
        OLD_PRODUCTION,

        // New workflow
        TRAINING,
        PRODUCTION,
    };

    // must override
    abstract public String name();

    // must override
    abstract public void checkExecution(OutputAnalyzer out, RunMode runMode);

    // must override
    // main class, followed by arguments to the main class
    abstract public String[] appCommandLine(RunMode runMode);

    // optional
    public String[] vmArgs(RunMode runMode) {
        return new String[0];
    }

    // optional
    public String classpath(RunMode runMode) {
        return null;
    }

    // optional
    public String classListLog() {
        return "-Xlog:class+load=debug:file=" + classListFile + ".log";
    }
    // optional
    public String staticDumpLog() {
        return "-Xlog:cds=debug,cds+class=debug,cds+heap=warning,cds+resolve=debug:file=" + staticArchiveFile + ".log";
    }
    // optional
    public String dynamicDumpLog() {
        return "-Xlog:cds=debug,cds+class=debug,cds+resolve=debug,class+load=debug:file=" + dynamicArchiveFile + ".log";
    }
    // optional
    public String codeCacheDumpLog() {
        return "-Xlog:scc:file=" + codeCacheFile + ".log";
    }
    public String oldProductionRunLog() {
        return "-Xlog:scc*=warning,cds:file=" + name() + ".old-production.log";
    }
    // ============================== new workflow
    public String trainingLog() {
        return "-Xlog:cds=debug,cds+class=debug,cds+heap=warning,cds+resolve=debug:file=" + cdsFile + ".log";
    }
    public String productionRunLog() {
        return "-Xlog:scc*=warning,cds:file=" + name() + ".production.log";
    }

    public final String classListFile;
    public final String staticArchiveFile;
    public final String dynamicArchiveFile;
    public final String codeCacheFile;

    public final String cdsFile; // -XX:CacheDataStore=<foo>.cds
    public final String aotFile; // = cdsFile + ".code"

    public LeydenTester() {
        // Old workflow
        classListFile = name() + ".classlist";
        staticArchiveFile = name() + ".static.jsa";
        dynamicArchiveFile = name() + ".dynamic.jsa";
        codeCacheFile = name() + ".code.jsa";

        // New workflow
        cdsFile = name() + ".cds";
        aotFile = cdsFile + ".code";
    }

    private void listOutputFile(String file) {
        File f = new File(file);
        if (f.exists()) {
            System.out.println("[output file: " + file + " " + f.length() + " bytes]");
        } else {
            System.out.println("[output file: " + file + " does not exist]");
        }
    }

    //========================================
    // Old workflow
    //========================================

    public OutputAnalyzer createClassList() throws Exception {
        RunMode runMode = RunMode.CLASSLIST;
        String[] cmdLine = StringArrayUtils.concat(vmArgs(runMode), classListLog(),
                                                   "-Xshare:off",
                                                   "-XX:DumpLoadedClassList=" + classListFile,
                                                   "-cp", classpath(runMode));
        cmdLine = StringArrayUtils.concat(cmdLine, appCommandLine(runMode));

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(cmdLine);
        Process process = pb.start();
        OutputAnalyzer output = CDSTestUtils.executeAndLog(process, "classlist");
        listOutputFile(classListFile);
        listOutputFile(classListFile + ".log");
        output.shouldHaveExitValue(0);
        checkExecution(output, runMode);
        return output;
    }

    public OutputAnalyzer dumpStaticArchive() throws Exception {
        RunMode runMode = RunMode.DUMP_STATIC;
        String[] cmdLine = StringArrayUtils.concat(vmArgs(runMode), staticDumpLog(),
                                                   "-Xlog:cds",
                                                   "-Xlog:cds+heap=error",
                                                   "-Xshare:dump",
                                                   "-XX:SharedArchiveFile=" + staticArchiveFile,
                                                   "-XX:+ArchiveInvokeDynamic",
                                                   "-XX:SharedClassListFile=" + classListFile,
                                                   "-cp", classpath(runMode));
        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(cmdLine);
        Process process = pb.start();
        OutputAnalyzer output = CDSTestUtils.executeAndLog(process, "static");
        listOutputFile(staticArchiveFile);
        listOutputFile(staticArchiveFile + ".log");
        output.shouldHaveExitValue(0);
        checkExecution(output, runMode);
        return output;
    }

    public OutputAnalyzer dumpDynamicArchive() throws Exception {
        RunMode runMode = RunMode.DUMP_DYNAMIC;
        String[] cmdLine = StringArrayUtils.concat(vmArgs(runMode), dynamicDumpLog(),
                                                   "-Xlog:cds",
                                                   "-XX:ArchiveClassesAtExit=" + dynamicArchiveFile,
                                                   "-XX:SharedArchiveFile=" + staticArchiveFile,
                                                   "-XX:+RecordTraining",
                                                   "-cp", classpath(runMode));
        cmdLine = StringArrayUtils.concat(cmdLine, appCommandLine(runMode));

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(cmdLine);
        Process process = pb.start();
        OutputAnalyzer output = CDSTestUtils.executeAndLog(process, "dynamic");
        listOutputFile(dynamicArchiveFile);
        listOutputFile(dynamicArchiveFile + ".log");
        output.shouldHaveExitValue(0);
        checkExecution(output, runMode);
        return output;
    }

    public OutputAnalyzer dumpCodeCache() throws Exception {
        RunMode runMode = RunMode.DUMP_CODECACHE;
        String[] cmdLine = StringArrayUtils.concat(vmArgs(runMode), codeCacheDumpLog(),
                                                   "-XX:SharedArchiveFile=" + dynamicArchiveFile,
                                                   "-XX:+ReplayTraining",
                                                   "-XX:+StoreCachedCode",
                                                   "-XX:CachedCodeFile=" + codeCacheFile,
                                                   "-XX:CachedCodeMaxSize=512M",
                                                   "-cp", classpath(runMode));
        cmdLine = StringArrayUtils.concat(cmdLine, appCommandLine(runMode));

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(cmdLine);
        Process process = pb.start();
        OutputAnalyzer output = CDSTestUtils.executeAndLog(process, "code");
        listOutputFile(codeCacheFile);
        listOutputFile(codeCacheFile + ".log");
        output.shouldHaveExitValue(0);
        checkExecution(output, runMode);
        return output;
    }

    public OutputAnalyzer oldProductionRun() throws Exception {
        RunMode runMode = RunMode.OLD_PRODUCTION;
        String[] cmdLine = StringArrayUtils.concat(vmArgs(runMode), oldProductionRunLog(),
                                                   "-XX:SharedArchiveFile=" + dynamicArchiveFile,
                                                   "-XX:+ReplayTraining",
                                                   "-XX:+LoadCachedCode",
                                                   "-XX:CachedCodeFile=" + codeCacheFile,
                                                   "-cp", classpath(runMode));
        cmdLine = StringArrayUtils.concat(cmdLine, appCommandLine(runMode));



        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(cmdLine);
        Process process = pb.start();
        OutputAnalyzer output = CDSTestUtils.executeAndLog(process, "old-production");
        listOutputFile(name() + ".old-production.log");
        output.shouldHaveExitValue(0);
        checkExecution(output, runMode);
        return output;
    }

    //========================================
    // New workflow
    //========================================

    public OutputAnalyzer trainingRun() throws Exception {
        RunMode runMode = RunMode.TRAINING;
        File f = new File(cdsFile);
        f.delete();
        String[] cmdLine = StringArrayUtils.concat(vmArgs(runMode), trainingLog(),
                                                   "-XX:+ArchiveInvokeDynamic",
                                                   "-XX:CacheDataStore=" + cdsFile,
                                                   "-cp", classpath(runMode));
        cmdLine = StringArrayUtils.concat(cmdLine, appCommandLine(runMode));

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(cmdLine);
        Process process = pb.start();
        OutputAnalyzer output = CDSTestUtils.executeAndLog(process, "training");
        listOutputFile(cdsFile);
        listOutputFile(cdsFile + ".log");   // The final dump
        listOutputFile(cdsFile + ".log.0"); // the preimage dump
        output.shouldHaveExitValue(0);
        checkExecution(output, runMode);
        return output;
    }

    public OutputAnalyzer productionRun() throws Exception {
        RunMode runMode = RunMode.PRODUCTION;
        String[] cmdLine = StringArrayUtils.concat(vmArgs(runMode), productionRunLog(),
                                                   "-XX:CacheDataStore=" + cdsFile,
                                                   "-cp", classpath(runMode));
        cmdLine = StringArrayUtils.concat(cmdLine, appCommandLine(runMode));

        ProcessBuilder pb = ProcessTools.createJavaProcessBuilder(cmdLine);
        Process process = pb.start();
        OutputAnalyzer output = CDSTestUtils.executeAndLog(process, "production");
        listOutputFile(name() + ".production.log");
        output.shouldHaveExitValue(0);
        checkExecution(output, runMode);
        return output;
    }

    public void run() throws Exception {
        runNew();
        runOld();
    }

    public void runOld() throws Exception {
        // Old Workflow
        createClassList();
        dumpStaticArchive();
        dumpDynamicArchive();
        dumpCodeCache();
        oldProductionRun();
    }

    public void runNew() throws Exception {
        // New Workflow
        trainingRun();
        productionRun();
    }
}
