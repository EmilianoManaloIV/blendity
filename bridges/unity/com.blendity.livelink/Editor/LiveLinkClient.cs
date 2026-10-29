// SPDX-License-Identifier: GPL-2.0-or-later
// The connection: a TcpClient read from EditorApplication.update, at most 20 ms of applying a frame so the Editor
// stays responsive; what isn't read stays in the socket, where Blendity replaces stale updates with newer ones.
// A script recompile or entering Play mode reloads the Editor's code: the link closes cleanly before and connects
// again after (Blendity sends the whole scene to a new connection).
//
// It connects by itself (task 0275): with Auto Connect on (EditorPrefs, on by default) it reads Blendity's discovery
// file every 2 seconds while not connected, and after Blendity closes or restarts waits for it again. Disconnect holds
// it off until Connect.
using System;
using System.Diagnostics;
using System.Net.Sockets;
using UnityEditor;
using UnityEngine;

namespace Blendity.LiveLink
{
    [InitializeOnLoad]
    public static class LiveLinkClient
    {
        const double BudgetMs = 20.0;
        const double WatchSeconds = 2.0;
        const double ConnectSeconds = 1.5;
        const string ReconnectKey = "Blendity.LiveLink.ReconnectPort";
        const string HeldKey = "Blendity.LiveLink.Held";
        public const string AutoConnectKey = "Blendity.LiveLink.AutoConnect";
        public const string PortKey = "Blendity.LiveLink.ManualPort";  // 0: the discovery file's port
        static TcpClient client;
        static NetworkStream net;
        static readonly byte[] chunk = new byte[1 << 20];
        static LiveLinkStream stream = new LiveLinkStream();
        static double nextWatch;
        static TcpClient pending;  // a connection under way, finished on the Editor's update (never waited for)
        static IAsyncResult pendingResult;
        static double pendingDeadline;
        static int pendingPort;
        static string pendingScene = "";

        public static LiveLinkApplier Applier { get; private set; }
        public static string Status = "Not connected";
        public static int Messages;
        public static bool Connected { get { return client != null; } }
        public static bool Busy { get { return client != null || pending != null; } }

        public static bool AutoConnect
        {
            get { return EditorPrefs.GetBool(AutoConnectKey, true); }
            set { EditorPrefs.SetBool(AutoConnectKey, value); }
        }

        public static bool Held
        {
            get { return SessionState.GetBool(HeldKey, false); }
            set { SessionState.SetBool(HeldKey, value); }
        }

        static LiveLinkClient()
        {
            AssemblyReloadEvents.beforeAssemblyReload += BeforeReload;
            EditorApplication.update += Watch;
            int port = SessionState.GetInt(ReconnectKey, 0);
            if (port == 0)
                return;
            SessionState.EraseInt(ReconnectKey);
            EditorApplication.delayCall += () => Connect(port);
        }

        static void BeforeReload()
        {
            if (client == null)
                return;
            SessionState.SetInt(ReconnectKey, ((System.Net.IPEndPoint)client.Client.RemoteEndPoint).Port);
            Disconnect("Reconnecting after the reload");
        }

        /// <summary>Every 2 s while not connected: Blendity found through its discovery file (or the typed port).</summary>
        static void Watch()
        {
            if (pending != null)
            {
                FinishPending();
                return;
            }
            if (client != null || Held || !AutoConnect || EditorApplication.timeSinceStartup < nextWatch)
                return;
            nextWatch = EditorApplication.timeSinceStartup + WatchSeconds;
            TryAutoConnect();
        }

        public static bool TryAutoConnect()
        {
            var found = LiveLinkDiscovery.Read();
            int port = EditorPrefs.GetInt(PortKey, 0);
            if (port == 0 && found != null)
                port = found.Port;
            if (port == 0)
            {
                Status = "Waiting for Blendity... (start it, or Window > Live Link > Start)";
                return false;
            }
            if (!Connect(port, found != null ? found.Scene : ""))
                Status = "Waiting for Blendity... (port " + port + " not answering)";
            return false;
        }

        /// <summary>Starts connecting to `port`; the Editor's updates finish it (a refused port never holds the Editor).</summary>
        public static bool Connect(int port, string scene = "")
        {
            Disconnect(Status);
            if (port < 1 || port > 65535)
            {
                Status = "The port must be 1 to 65535.";
                return false;
            }
            var c = new TcpClient();
            try
            {
                pendingResult = c.BeginConnect("127.0.0.1", port, null, null);
            }
            catch (Exception e)
            {
                c.Close();
                Status = "Couldn't connect to 127.0.0.1:" + port + " (" + e.Message + "). Start Live Link in Blendity first.";
                return false;
            }
            pending = c;
            pendingPort = port;
            pendingScene = scene;
            pendingDeadline = EditorApplication.timeSinceStartup + ConnectSeconds;
            Status = "Connecting to 127.0.0.1:" + port + "...";
            return true;
        }

        static void FinishPending()
        {
            if (!pendingResult.IsCompleted && EditorApplication.timeSinceStartup < pendingDeadline)
                return;
            var c = pending;
            pending = null;
            try
            {
                if (!pendingResult.IsCompleted)
                    throw new SocketException((int)SocketError.TimedOut);
                c.EndConnect(pendingResult);
                var hello = LiveLinkDiscovery.Hello("Unity " + Application.unityVersion);
                c.GetStream().Write(hello, 0, hello.Length);
            }
            catch (Exception)
            {
                c.Close();
                Status = AutoConnect ? "Waiting for Blendity... (port " + pendingPort + " not answering)"
                                     : "Couldn't connect to 127.0.0.1:" + pendingPort + ". Start Live Link in Blendity first.";
                return;
            }
            Opened(c, pendingPort, pendingScene);
        }

        static void Opened(TcpClient c, int port, string scene)
        {
            client = c;
            client.NoDelay = true;
            net = client.GetStream();
            stream = new LiveLinkStream();
            Applier = new LiveLinkApplier();
            Messages = 0;
            Held = false;
            Status = scene.Length > 0 ? "Connected to Blendity (" + scene + ")" : "Connected to 127.0.0.1:" + port;
            EditorApplication.update -= Tick;
            EditorApplication.update += Tick;
        }

        public static void Disconnect(string why = "Not connected")
        {
            EditorApplication.update -= Tick;
            if (client != null)
                client.Close();
            if (pending != null)
                pending.Close();
            client = null;
            pending = null;
            net = null;
            Status = why;
        }

        /// <summary>Disconnect pressed: no auto-connect until Connect.</summary>
        public static void Hold()
        {
            Disconnect("Disconnected (Connect to follow Blendity again)");
            Held = true;
        }

        static void Tick()
        {
            if (client == null)
                return;
            try
            {
                Pump();
            }
            catch (Exception e)
            {
                Disconnect("Stopped: " + e.Message);  // (a bad message must not leave a dead update with the socket open)
                throw;
            }
        }

        /// <summary>Applies whole messages, reading more only when none is buffered, until the budget is spent.</summary>
        static void Pump()
        {
            var clock = Stopwatch.StartNew();
            while (clock.Elapsed.TotalMilliseconds < BudgetMs)
            {
                var m = stream.Next();
                if (m != null)
                {
                    Applier.Apply(m);
                    Messages++;
                    continue;
                }
                if (!ReadAvailable())
                    return;
            }
        }

        /// <summary>More bytes into the stream: false when none are waiting (or Blendity closed the connection).</summary>
        static bool ReadAvailable()
        {
            if (!net.DataAvailable)
            {
                if (client.Client.Poll(0, SelectMode.SelectRead) && client.Available == 0)
                    Disconnect(AutoConnect ? "Blendity closed the connection - waiting for it again" : "Blendity closed the connection");
                return false;
            }
            int n = net.Read(chunk, 0, Math.Min(chunk.Length, client.Available));
            stream.Feed(chunk, n);
            return true;
        }
    }
}
