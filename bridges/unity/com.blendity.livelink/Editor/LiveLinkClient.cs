// SPDX-License-Identifier: GPL-2.0-or-later
// The connection: a TcpClient read from EditorApplication.update, at most 20 ms of applying a frame so the Editor
// stays responsive; what isn't read stays in the socket, where Blendity replaces stale updates with newer ones.
// A script recompile or entering Play mode reloads the Editor's code: the link closes cleanly before and connects
// again after (Blendity sends the whole scene to a new connection).
using System;
using System.Diagnostics;
using System.Net.Sockets;
using UnityEditor;

namespace Blendity.LiveLink
{
    [InitializeOnLoad]
    public static class LiveLinkClient
    {
        const double BudgetMs = 20.0;
        const string ReconnectKey = "Blendity.LiveLink.ReconnectPort";
        static TcpClient client;
        static NetworkStream net;
        static readonly byte[] chunk = new byte[1 << 20];
        static LiveLinkStream stream = new LiveLinkStream();

        public static LiveLinkApplier Applier { get; private set; }
        public static string Status = "Not connected";
        public static int Messages;
        public static bool Connected { get { return client != null; } }

        static LiveLinkClient()
        {
            AssemblyReloadEvents.beforeAssemblyReload += BeforeReload;
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

        public static bool Connect(int port)
        {
            Disconnect();
            if (port < 1 || port > 65535)
            {
                Status = "The port must be 1 to 65535.";
                return false;
            }
            var c = new TcpClient();
            try
            {
                var pending = c.BeginConnect("127.0.0.1", port, null, null);
                if (!pending.AsyncWaitHandle.WaitOne(500) || !c.Connected)
                    throw new SocketException((int)SocketError.ConnectionRefused);
                c.EndConnect(pending);
            }
            catch (Exception e)
            {
                c.Close();
                Status = "Couldn't connect to 127.0.0.1:" + port + " (" + e.Message + "). Start Live Link in Blendity first.";
                return false;
            }
            client = c;
            client.NoDelay = true;
            net = client.GetStream();
            stream = new LiveLinkStream();
            Applier = new LiveLinkApplier();
            Messages = 0;
            Status = "Connected to 127.0.0.1:" + port;
            EditorApplication.update -= Tick;
            EditorApplication.update += Tick;
            return true;
        }

        public static void Disconnect(string why = "Not connected")
        {
            EditorApplication.update -= Tick;
            if (client != null)
                client.Close();
            client = null;
            net = null;
            Status = why;
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
                    Disconnect("Blendity closed the connection");
                return false;
            }
            int n = net.Read(chunk, 0, Math.Min(chunk.Length, client.Available));
            stream.Feed(chunk, n);
            return true;
        }
    }
}
