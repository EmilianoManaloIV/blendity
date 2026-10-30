// SPDX-License-Identifier: GPL-2.0-or-later
// The Unity bridge's discovery and reconnect, without Unity (task 0275): ReplayCheck.exe --live <python> <repo root>
// starts tools/livelink_fake_server.py (a recorded session) twice on free ports, finds each through the discovery
// file with LiveLinkDiscovery (the package's own code), connects as LiveLinkClient does, sends the hello and reads
// the whole session; the second server's port differs and is found the same way. C# 5, for the .NET Framework's csc.
using System;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using Blendity.LiveLink;

static class DiscoveryCheck
{
    static int failures;

    static void Check(bool ok, string what)
    {
        Console.WriteLine((ok ? "  ok: " : "  FAILED: ") + what);
        if (!ok)
            failures++;
    }

    static Process StartServer(string python, string root, string dir, string hellos, out int port)
    {
        var info = new ProcessStartInfo(python, "-I \"" + Path.Combine(root, "tools", "livelink_fake_server.py") + "\" --port 0 --dir \"" + dir +
                                                "\" --seconds 20 --close-after 1 --hellos \"" + hellos + "\"");
        info.UseShellExecute = false;
        info.RedirectStandardOutput = true;
        var p = Process.Start(info);
        string line = p.StandardOutput.ReadLine() ?? "";
        port = line.StartsWith("ready ") ? int.Parse(line.Substring(6)) : 0;
        return p;
    }

    /// <summary>Connects as the package does, says hello, reads until the server closes: the messages framed.</summary>
    static int Follow(int port)
    {
        var c = new TcpClient();
        var pending = c.BeginConnect("127.0.0.1", port, null, null);
        if (!pending.AsyncWaitHandle.WaitOne(2000) || !c.Connected)
            return -1;
        c.EndConnect(pending);
        var net = c.GetStream();
        var hello = LiveLinkDiscovery.Hello("ReplayCheck");
        net.Write(hello, 0, hello.Length);
        var s = new LiveLinkStream();
        var buf = new byte[1 << 16];
        int n = 0, got;
        net.ReadTimeout = 5000;
        while ((got = net.Read(buf, 0, buf.Length)) > 0)
            s.Feed(buf, got);
        for (var m = s.Next(); m != null; m = s.Next())
            n++;
        c.Close();
        return n;
    }

    public static int Run(string python, string root)
    {
        string dir = Path.Combine(Path.GetTempPath(), "blendity-unity-discovery-" + Process.GetCurrentProcess().Id);
        Directory.CreateDirectory(dir);
        string file = Path.Combine(dir, "livelink.json"), hellos = Path.Combine(dir, "hellos.txt");
        Check(LiveLinkDiscovery.Read(file) == null, "no discovery file: nothing found");
        File.WriteAllText(file, "{\"protocol\":\"another/9\",\"port\":40100}");
        Check(LiveLinkDiscovery.Read(file) == null, "another protocol's file is not followed");
        int port1, port2;
        var p1 = StartServer(python, root, dir, hellos, out port1);
        var found1 = LiveLinkDiscovery.Read(file);
        Check(port1 > 0 && found1 != null && found1.Port == port1 && found1.Scene == "FakeScene", "found through the file: port " + port1);
        int n1 = found1 != null ? Follow(found1.Port) : -1;
        Check(n1 > 20, "the session followed: " + n1 + " messages, then the link closed");
        p1.Kill();
        p1.WaitForExit();
        var p2 = StartServer(python, root, dir, hellos, out port2);
        var found2 = LiveLinkDiscovery.Read(file);
        Check(found2 != null && found2.Port == port2 && port2 != port1, "found again after the restart: port " + port2);
        int n2 = found2 != null ? Follow(found2.Port) : -1;
        Check(n2 == n1, "the second session whole: " + n2 + " messages");
        p2.Kill();
        p2.WaitForExit();
        string said = File.Exists(hellos) ? File.ReadAllText(hellos) : "";
        Check(said.Contains("\"app\":\"ReplayCheck\"") && said.Contains("\"bridge\":\"" + LiveLinkDiscovery.BridgeVersion + "\""), "the hello said who it is: " + said.Split('\n')[0]);
        Console.WriteLine(failures == 0 ? "DISCOVERY OK" : "DISCOVERY FAILED: " + failures + " problem(s)");
        return failures == 0 ? 0 : 1;
    }
}
