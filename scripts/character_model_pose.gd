extends Node3D

# Choose animation names from either imported library in the character scene.
@export var idle_animation: StringName = &"Idle_FoldArms"
@export var walk_animation: StringName = &"Walk"

const ANIMATION_SOURCES := [
	preload("res://assets/animations/quaternius_library_2/UAL2_Standard.glb"),
	preload("res://assets/animations/quaternius_library_1/UAL1_Standard.glb"),
]
const TRANSITION_TIME := 0.15

var _player: AnimationPlayer


func _ready() -> void:
	var skeletons := find_children("*", "Skeleton3D", true, false)
	if skeletons.is_empty():
		return
	var skeleton: Skeleton3D = skeletons[0]
	_attach_hair(skeleton)

	var library := AnimationLibrary.new()
	for source_scene in ANIMATION_SOURCES:
		var source: Node = source_scene.instantiate()
		var source_skeleton: Skeleton3D = source.find_children("*", "Skeleton3D", true, false)[0]
		var source_player: AnimationPlayer = source.find_children("*", "AnimationPlayer", true, false)[0]
		for animation_name in [idle_animation, walk_animation]:
			if library.has_animation(animation_name) or not source_player.has_animation(animation_name):
				continue
			var animation: Animation = source_player.get_animation(animation_name).duplicate(true)
			_retarget(animation, source_skeleton, skeleton)
			library.add_animation(animation_name, animation)
		source.free()
	for animation_name in [idle_animation, walk_animation]:
		if not library.has_animation(animation_name):
			push_warning("Animation not found: " + str(animation_name))

	_player = AnimationPlayer.new()
	_player.name = "CharacterAnimationPlayer"
	add_child(_player)
	_player.add_animation_library(&"", library)
	if _player.has_animation(idle_animation):
		_player.play(idle_animation)


func update_walk(_delta: float, speed_fraction: float) -> void:
	if not _player:
		return
	var moving := speed_fraction > 0.05
	var selected := walk_animation if moving else idle_animation
	if _player.has_animation(selected) and _player.current_animation != selected:
		_player.play(selected, TRANSITION_TIME)
	_player.speed_scale = clampf(speed_fraction, 0.4, 1.0) if moving else 1.0


func _retarget(animation: Animation, source: Skeleton3D, target: Skeleton3D) -> void:
	var target_path := str(get_path_to(target))
	for track in range(animation.get_track_count()):
		var path := animation.track_get_path(track)
		if path.get_subname_count() != 1:
			continue
		var bone := path.get_subname(0)
		var source_index := source.find_bone(bone)
		var target_index := target.find_bone(bone)
		if source_index < 0 or target_index < 0:
			animation.track_set_enabled(track, false)
			continue
		animation.track_set_path(track, NodePath(target_path + ":" + bone))
		var source_rest := source.get_bone_rest(source_index)
		var target_rest := target.get_bone_rest(target_index)
		for key in range(animation.track_get_key_count(track)):
			var value: Variant = animation.track_get_key_value(track, key)
			if animation.track_get_type(track) == Animation.TYPE_ROTATION_3D:
				var correction := target_rest.basis.get_rotation_quaternion() * source_rest.basis.get_rotation_quaternion().inverse()
				animation.track_set_key_value(track, key, correction * value)
			elif animation.track_get_type(track) == Animation.TYPE_POSITION_3D:
				animation.track_set_key_value(track, key, target_rest.origin + value - source_rest.origin)


func _attach_hair(skeleton: Skeleton3D) -> void:
	var hair := get_node_or_null("Hair") as Node3D
	var head := skeleton.find_bone("Head")
	if not hair or head < 0:
		return
	var head_rest := skeleton.global_transform * skeleton.get_bone_global_rest(head)
	var hair_in_head := head_rest.affine_inverse() * hair.global_transform
	var attachment := BoneAttachment3D.new()
	attachment.bone_name = "Head"
	skeleton.add_child(attachment)
	hair.reparent(attachment, false)
	hair.transform = hair_in_head
