@tool
extends SkeletonModifier3D
## Round 379 (review): a Live Link skin's bones follow their joints' nodes each frame - the bone nodes Live Link poses,
## or an AnimationPlayer playing a clip from Blendity on them - so the mesh moves either way.

@export var joints: Array[NodePath] = []  # (each bone's joint node, from the skeleton)


func _process_modification() -> void:
	follow(get_skeleton())


## The skeleton's bones posed from its joints' nodes, in the mesh's space (the skeleton's parent's).
func follow(sk: Skeleton3D) -> void:
	if sk == null or not (sk.get_parent() is Node3D):
		return
	var to_mesh := chain(sk.get_parent()).affine_inverse()
	for b in mini(joints.size(), sk.get_bone_count()):
		var j := sk.get_node_or_null(joints[b]) as Node3D
		if j != null:
			sk.set_bone_pose(b, to_mesh * chain(j))


## The world transform through the parents (headless too: the nodes needn't be in a tree).
static func chain(n: Node) -> Transform3D:
	var t := Transform3D.IDENTITY
	while n is Node3D:
		t = (n as Node3D).transform * t
		n = n.get_parent()
	return t
