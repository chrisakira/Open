extends Node3D

# Choose animation names from either imported library in the character scene.
@export var idle_animation: StringName = &"Idle_FoldArms"
@export var walk_animation: StringName = &"Walk"

const ANIMATION_SOURCES := [
	preload("res://assets/animations/quaternius_library_2/UAL2_Standard.glb"),
	preload("res://assets/animations/quaternius_library_1/UAL1_Standard.glb"),
]
const TRANSITION_TIME := 0.15

# What the combat layer adds to the pose script's own two picks: the armed idles of the
# locked rule and the attack set. Names resolve as the importer left them - the locked
# name first, then the `_Loop`-suffixed one - and every clip is retargeted through the
# same path idle and walk take. Sword_Idle imports non-looping and standing with a weapon
# is a standing thing, so its retargeted duplicate is set to loop; imported assets stay
# untouched, and the attack clips stay one-shot.
const EXTRA_CLIPS := ["Sword_Idle", "Idle_Shield", "Sword_Regular_A", "Sword_Regular_B",
		"Sword_Heavy_Combo", "Punch_Jab"]
const LOOPING_CLIPS := ["Sword_Idle"]

const WEAPON_IDLE := &"Sword_Idle"
const SHIELD_IDLE := &"Idle_Shield"
const ATTACK_BASIC := &"Sword_Regular_A"
const ATTACK_UNARMED := &"Punch_Jab"
# The locked map, and the technique ids C++ reports: basic and Slash take regular A,
# Thrust takes B, Heavy Strike the combo. Anything else a future technique becomes falls
# back to basic - a presentation answer, never a combat one.
const ATTACK_BY_TECHNIQUE := {0: &"Sword_Regular_A", 1: &"Sword_Regular_A",
		2: &"Sword_Regular_B", 3: &"Sword_Heavy_Combo"}

var _player: AnimationPlayer
# The stance the held-and-usable truth adds up to, the swing's grip on the player, and
# the latest walk reading: so a swing can borrow the body without the legs ever having
# to know, and hand it back to whatever the world is doing by then.
var _armed: StringName = &"unarmed"
var _attack_active := false
var _attack_clip: StringName = &""
var _loco := 0.0


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
		for clip_name in EXTRA_CLIPS:
			if library.has_animation(clip_name):
				continue
			var imported := ""
			for candidate in [clip_name, clip_name + "_Loop"]:
				if source_player.has_animation(candidate):
					imported = candidate
					break
			if imported.is_empty():
				continue
			var clip: Animation = source_player.get_animation(imported).duplicate(true)
			_retarget(clip, source_skeleton, skeleton)
			if clip_name in LOOPING_CLIPS:
				clip.loop_mode = Animation.LOOP_LINEAR
			library.add_animation(clip_name, clip)
		source.free()
	for animation_name in [idle_animation, walk_animation]:
		if not library.has_animation(animation_name):
			push_warning("Animation not found: " + str(animation_name))

	_player = AnimationPlayer.new()
	_player.name = "CharacterAnimationPlayer"
	add_child(_player)
	_player.add_animation_library(&"", library)
	_player.animation_finished.connect(_on_animation_finished)
	if _player.has_animation(idle_animation):
		_player.play(idle_animation)


func _idle_name() -> StringName:
	# The stance C++'s held-and-usable truth adds up to, resolved against the library:
	# a tuning clip that is not there may not steal the body's only standing answer.
	var wanted := idle_animation
	if _armed == &"weapon":
		wanted = WEAPON_IDLE
	elif _armed == &"shield":
		wanted = SHIELD_IDLE
	if _player == null or _player.has_animation(wanted):
		return wanted
	return idle_animation


func set_armed_idle(kind: StringName) -> void:
	# Every feedback tick re-reads the sheet, so this is the whole keep-up: an arm can
	# fall or a weapon rise without the equipment signature moving at all. During a
	# swing the new stance is simply pending - the next choice of idle will make it.
	if kind == _armed:
		return
	_armed = kind
	if _attack_active or _player == null:
		return
	if _loco <= 0.05 and _player.current_animation != _idle_name():
		var next := _idle_name()
		if _player.has_animation(next):
			_player.play(next, TRANSITION_TIME)


func update_walk(_delta: float, speed_fraction: float) -> void:
	if not _player:
		return
	_loco = speed_fraction
	if _attack_active:
		# The clip is performing what C++ already did; the legs keep their latest
		# reading and wait. Neither the animation nor the attack's own speed scale is
		# the walk's to move while it runs.
		return
	var moving := speed_fraction > 0.05
	var selected := walk_animation if moving else _idle_name()
	if _player.has_animation(selected) and _player.current_animation != selected:
		_player.play(selected, TRANSITION_TIME)
	_player.speed_scale = clampf(speed_fraction, 0.4, 1.0) if moving else 1.0


func play_attack(technique_id: int, duration: float, unarmed: bool) -> void:
	if not _player:
		return
	var clip: StringName = ATTACK_UNARMED if unarmed \
			else ATTACK_BY_TECHNIQUE.get(technique_id, ATTACK_BASIC)
	if not _player.has_animation(clip):
		push_warning("Character pose: no attack clip %s" % str(clip))
		_end_attack()
		return
	_attack_active = true
	_attack_clip = clip
	var repeated := _player.current_animation == clip
	_player.play(clip, 0.0)
	if repeated:
		# Even the same clip in a row is a new performed swing: frame zero, every time.
		_player.seek(0.0, true)
	# The duration belongs to this swing's own attack clock, C++'s number from the event;
	# the clip is warped to it, never the other way round. No duration means no claim:
	# speed one.
	_player.speed_scale = _player.get_animation(clip).length / duration \
			if duration > 0.0 and is_finite(duration) else 1.0


func _end_attack() -> void:
	_attack_active = false
	_attack_clip = &""
	update_walk(0.0, _loco)


func _on_animation_finished(finished: StringName) -> void:
	if _attack_active and finished == _attack_clip:
		_end_attack()


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
