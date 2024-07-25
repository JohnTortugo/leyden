/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

/*
 * @test
 * @summary "AOT" aliases for traditional CDS command-line options
 * @requires vm.cds
 * @library /test/lib /test/hotspot/jtreg/runtime/cds/appcds/test-classes
 * @build Hello
 * @run driver jdk.test.lib.helpers.ClassFileInstaller -jar hello.jar Hello
 * @run driver AOTFlags
 */

import jdk.test.lib.cds.CDSTestUtils;
import jdk.test.lib.helpers.ClassFileInstaller;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.process.ProcessTools;

public class AOTFlags {
    static String appJar = ClassFileInstaller.getJarPath("hello.jar");
    static String aotConfigFile = "hello.aotconfig";
    static String aotCacheFile = "hello.aot";
    static String helloClass = "Hello";

    public static void main(String[] args) throws Exception {
        positiveTests();
        negativeTests();
    }

    static void positiveTests() throws Exception {
        // (1) Training Run
        ProcessBuilder pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:AOTMode=record",
            "-XX:AOTConfiguration=" + aotConfigFile,
            "-cp", appJar, helloClass);

        OutputAnalyzer out = CDSTestUtils.executeAndLog(pb, "train");
        out.shouldContain("Hello World");

        // (2) Assembly Phase
        pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:AOTMode=create",
            "-XX:AOTConfiguration=" + aotConfigFile,
            "-XX:AOTCache=" + aotCacheFile,
            "-Xlog:cds",
            "-cp", appJar);
        out = CDSTestUtils.executeAndLog(pb, "asm");
        out.shouldContain("Dumping shared data to file:");
        out.shouldMatch("cds.*hello[.]aot");

        // (3) Production Run with AOTCache
        pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:AOTCache=" + aotCacheFile,
            "-Xlog:cds",
            "-cp", appJar, helloClass);
        out = CDSTestUtils.executeAndLog(pb, "prod");
        out.shouldContain("Opened archive hello.aot.");
        out.shouldContain("Hello World");
    }

    static void negativeTests() throws Exception {
        // (1) Mixing old and new options
        String mixOldNewErrSuffix = " cannot be used at the same time with -Xshare:on, -Xshare:auto, "
            + "-Xshare:off, -Xshare:dump, DumpLoadedClassList, SharedClassListFile, "
            + "or SharedArchiveFile";

        ProcessBuilder pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-Xshare:off",
            "-XX:AOTConfiguration=" + aotConfigFile,
            "-cp", appJar, helloClass);

        OutputAnalyzer out = CDSTestUtils.executeAndLog(pb, "neg");
        out.shouldContain("Option AOTConfiguration" + mixOldNewErrSuffix);

        pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:SharedArchiveFile=" + aotCacheFile,
            "-XX:AOTCache=" + aotCacheFile,
            "-cp", appJar, helloClass);
        out = CDSTestUtils.executeAndLog(pb, "neg");
        out.shouldContain("Option AOTCache" + mixOldNewErrSuffix);

        // (2) Use AOTConfiguration without AOTMode
        pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:AOTConfiguration=" + aotConfigFile,
            "-cp", appJar, helloClass);

        out = CDSTestUtils.executeAndLog(pb, "neg");
        out.shouldContain("AOTConfiguration cannot be used without setting AOTMode");

        // (3) Use AOTMode without AOTConfiguration
        pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:AOTMode=create",
            "-cp", appJar, helloClass);

        out = CDSTestUtils.executeAndLog(pb, "neg");
        out.shouldContain("AOTMode cannot be used without setting AOTConfiguration");

        // (4) Bad AOTMode
        pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:AOTMode=foo",
            "-XX:AOTConfiguration=" + aotConfigFile,
            "-cp", appJar, helloClass);

        out = CDSTestUtils.executeAndLog(pb, "neg");
        out.shouldContain("Unrecognized AOTMode foo: must be record or create");

        // (5) AOTCache specified with -XX:AOTMode=record
        pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:AOTMode=record",
            "-XX:AOTConfiguration=" + aotConfigFile,
            "-XX:AOTCache=" + aotCacheFile,
            "-cp", appJar, helloClass);

        out = CDSTestUtils.executeAndLog(pb, "neg");
        out.shouldContain("AOTCache must not be specified when using -XX:AOTMode=record");

        // (5) AOTCache not specified with -XX:AOTMode=create
        pb = ProcessTools.createLimitedTestJavaProcessBuilder(
            "-XX:AOTMode=create",
            "-XX:AOTConfiguration=" + aotConfigFile,
            "-cp", appJar, helloClass);

        out = CDSTestUtils.executeAndLog(pb, "neg");
        out.shouldContain("AOTCache must be specified when using -XX:AOTMode=create");
    }
}
