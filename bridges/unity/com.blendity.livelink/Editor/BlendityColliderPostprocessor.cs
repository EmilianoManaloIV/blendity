// SPDX-License-Identifier: GPL-2.0-or-later
// Blendity's exported colliders (round 362, task 0339): a model exported with Colliders = Unity carries each object's
// Collider as its own mesh - "<name>_Collider" for a Box, Sphere, Capsule or Convex shape, "<name>_MeshCollider" for a
// Mesh one. On import this makes that mesh a MeshCollider (convex for the first, the triangles themselves for the
// second) and takes its renderer away, so it collides and isn't drawn. Not yet run inside Unity: Blendity's own tests
// check the names the export writes.
using UnityEditor;
using UnityEngine;

namespace Blendity.LiveLink
{
    public sealed class BlendityColliderPostprocessor : AssetPostprocessor
    {
        void OnPostprocessModel(GameObject root)
        {
            foreach (Transform t in root.GetComponentsInChildren<Transform>(true))
            {
                bool concave = t.name.EndsWith("_MeshCollider");
                if (!concave && !t.name.EndsWith("_Collider")) continue;
                MeshFilter filter = t.GetComponent<MeshFilter>();
                if (filter == null || filter.sharedMesh == null) continue;
                MeshCollider collider = t.gameObject.AddComponent<MeshCollider>();
                collider.sharedMesh = filter.sharedMesh;
                collider.convex = !concave;
                MeshRenderer renderer = t.GetComponent<MeshRenderer>();
                if (renderer != null) Object.DestroyImmediate(renderer, true);
            }
        }
    }
}
