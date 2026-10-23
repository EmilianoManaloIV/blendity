// SPDX-License-Identifier: GPL-2.0-or-later
// Window > Blendity Live Link: the port (Blendity's Window > Live Link shows it), Connect / Disconnect, the status.
using UnityEditor;
using UnityEngine;

namespace Blendity.LiveLink
{
    public sealed class LiveLinkWindow : EditorWindow
    {
        int port = 40100;

        [MenuItem("Window/Blendity Live Link")]
        static void Open()
        {
            GetWindow<LiveLinkWindow>("Blendity Live Link");
        }

        void OnEnable()
        {
            port = EditorPrefs.GetInt("Blendity.LiveLink.Port", 40100);
        }

        void OnGUI()
        {
            port = Mathf.Clamp(EditorGUILayout.IntField("Port", port), 1, 65535);
            EditorPrefs.SetInt("Blendity.LiveLink.Port", port);
            if (GUILayout.Button(LiveLinkClient.Connected ? "Disconnect" : "Connect"))
            {
                if (LiveLinkClient.Connected)
                    LiveLinkClient.Disconnect();
                else
                    LiveLinkClient.Connect(port);
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
