// SPDX-License-Identifier: GPL-2.0-or-later
// Window > Blendity Live Link: Auto Connect (on: the link finds Blendity by itself), the port (0: the one Blendity's
// discovery file names), Connect / Disconnect, the status.
using UnityEditor;
using UnityEngine;

namespace Blendity.LiveLink
{
    public sealed class LiveLinkWindow : EditorWindow
    {
        [MenuItem("Window/Blendity Live Link")]
        static void Open()
        {
            GetWindow<LiveLinkWindow>("Blendity Live Link");
        }

        void OnGUI()
        {
            bool auto = EditorGUILayout.Toggle(new GUIContent("Auto Connect", "Connect by itself whenever Blendity's Live Link runs (it writes ~/.blendity/livelink.json)."), LiveLinkClient.AutoConnect);
            if (auto != LiveLinkClient.AutoConnect)
                LiveLinkClient.AutoConnect = auto;
            int port = EditorPrefs.GetInt(LiveLinkClient.PortKey, 0);
            int typed = Mathf.Clamp(EditorGUILayout.IntField(new GUIContent("Port", "0: the port Blendity's discovery file names. Another: always this one."), port), 0, 65535);
            if (typed != port)
                EditorPrefs.SetInt(LiveLinkClient.PortKey, typed);
            if (GUILayout.Button(LiveLinkClient.Busy ? "Disconnect" : "Connect"))
            {
                if (LiveLinkClient.Busy)
                    LiveLinkClient.Hold();
                else
                {
                    LiveLinkClient.Held = false;
                    if (!LiveLinkClient.TryAutoConnect() && !LiveLinkClient.Busy && typed == 0)
                        LiveLinkClient.Connect(40100);
                }
                GUIUtility.ExitGUI();  // (the layout below changes with the state: start the next frame afresh)
            }
            EditorGUILayout.HelpBox(LiveLinkClient.Status, MessageType.None);
            var a = LiveLinkClient.Applier;
            if (a == null)
                return;
            EditorGUILayout.LabelField("Messages", LiveLinkClient.Messages.ToString());
            EditorGUILayout.LabelField("Objects", a.Objects.Count.ToString());
            if (a.Playing)
                EditorGUILayout.LabelField("Blendity is in Play mode");
        }

        void OnInspectorUpdate()
        {
            Repaint();
        }
    }
}
