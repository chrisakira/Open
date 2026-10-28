extends Node3D

const WALK_CYCLE := 9.0
const BLEND_SPEED := 8.0

var _skeleton: Skeleton3D
var _walk_phase := 0.0
var _walk_blend := 0.0


# The base characters ship in a T-pose. Posing is visual only.
func _ready() -> void:
	var skeletons := find_children("*", "Skeleton3D", true, false)
	if skeletons.is_empty():
		return
	_skeleton = skeletons[0]
	_apply_pose(0.0)


func update_walk(delta: float, speed_fraction: float) -> void:
	if not _skeleton:
		return
	var target := clampf(speed_fraction, 0.0, 1.0)
	_walk_blend = move_toward(_walk_blend, target, BLEND_SPEED * delta)
	if _walk_blend > 0.0:
		_walk_phase += delta * WALK_CYCLE * maxf(target, 0.4)
	else:
		_walk_phase = 0.0
	_apply_pose(sin(_walk_phase) * _walk_blend)


func _apply_pose(swing: float) -> void:
	for side in ["l", "r"]:
		var sign_factor := 1.0 if side == "l" else -1.0
		var arm := _skeleton.find_bone("upperarm_" + side)
		if arm >= 0:
			var rest := _skeleton.get_bone_rest(arm).basis.get_rotation_quaternion()
			var idle_angle := -1.15 if side == "l" else 1.15
			_skeleton.set_bone_pose_rotation(arm, rest * Quaternion(Vector3.UP, swing * 0.35) * Quaternion(Vector3.BACK, idle_angle))
		var thigh := _skeleton.find_bone("thigh_" + side)
		if thigh >= 0:
			var rest := _skeleton.get_bone_rest(thigh).basis.get_rotation_quaternion()
			_skeleton.set_bone_pose_rotation(thigh, rest * Quaternion(Vector3.RIGHT, swing * sign_factor * 0.5))
		var calf := _skeleton.find_bone("calf_" + side)
		if calf >= 0:
			var rest := _skeleton.get_bone_rest(calf).basis.get_rotation_quaternion()
			_skeleton.set_bone_pose_rotation(calf, rest * Quaternion(Vector3.RIGHT, maxf(0.0, -swing * sign_factor) * 0.35))
