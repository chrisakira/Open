#include "open/character.hpp"
#include "open/combat_rules.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

#include "test_support.hpp"

// Task 2 combat-arithmetic and loadout tests. Break these catches: a formula
// that reads effective attributes or effective Toughness, a soak taken from the
// attacker's Toughness instead of the defender's, an exhausted swing
// that halves the final damage instead of only the offensive term, a nullify
// that wins on equality or fires while the defend clock is cooling, a profile
// that borrows a chosen specialisation for a weapon that is not that
// specialisation, and a broken limb that should stop a held weapon or shield
// being selected.

namespace {

open::CharacterConfig config(std::uint64_t id, int level, open::Attribute blessing) {
	open::CharacterConfig made;
	made.id = id;
	made.name = "";
	made.faction = open::Faction::Friendly;
	made.level = level;
	made.blessing = blessing;
	return made;
}

open::Character make_character(std::uint64_t id = 1, int level = 1,
							   open::Attribute blessing = open::Attribute::Strength) {
	const std::optional<open::Character> made = open::Character::create(config(id, level, blessing));
	expect_true("the test character is created", made.has_value());
	return *made;
}

open::ItemRef held(open::ItemId id, open::HandUse use, std::optional<open::BasicSkill> skill,
				   std::optional<open::AdvancedSkill> option) {
	open::ItemRef item;
	item.id = id;
	item.hand_use = use;
	// Every held record may go in either hand; Equipment::coherent checks the rest.
	item.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(open::Slot::LeftHand)));
	item.allowed_slots.set(static_cast<std::size_t>(static_cast<int>(open::Slot::RightHand)));
	item.combat_skill = skill;
	item.combat_option = option;
	return item;
}

open::ItemRef one_handed(open::ItemId id, std::optional<open::BasicSkill> skill = std::nullopt,
						 std::optional<open::AdvancedSkill> option = std::nullopt) {
	return held(id, open::HandUse::OneHanded, skill, option);
}

open::ItemRef two_handed(open::ItemId id, std::optional<open::BasicSkill> skill = std::nullopt,
						 std::optional<open::AdvancedSkill> option = std::nullopt) {
	return held(id, open::HandUse::TwoHanded, skill, option);
}

// Blocking is the shield tag, so a shield is a one-handed record under it.
open::ItemRef shield(open::ItemId id, std::optional<open::AdvancedSkill> option = std::nullopt) {
	return held(id, open::HandUse::OneHanded, open::BasicSkill::Blocking, option);
}

std::optional<open::AdvancedSkill> spec(open::AdvancedSkill skill) {
	return std::optional<open::AdvancedSkill>(skill);
}

// Training uses the existing progression APIs only: no test-only rank setter.
void train(open::Character &c, open::BasicSkill basic, std::optional<open::AdvancedSkill> option,
		   unsigned target) {
	for (int guard = 0; guard < 2000000 && c.resolve_skill(basic, option).rank < target; ++guard) {
		if (!c.record_use(basic, option, true)) {
			expect_true("training the combat track is accepted", false);
			return;
		}
	}
	const bool reached = c.resolve_skill(basic, option).rank >= target;
	expect_true("the track reached the rank this test needs", reached);
}

bool is_basic(const std::optional<open::AttackProfile> &got, open::BasicSkill want) {
	return got.has_value() && got->basic == want;
}

bool is_basic(const std::optional<open::DefendProfile> &got, open::BasicSkill want) {
	return got.has_value() && got->basic == want;
}

}  // namespace

int main() {
	{
		// cooldown_uses_trained_rank: the trained rank alone sets the clock, and
		// exhaustion doubles it inside the same band. Rank 0 is the floor of the
		// formula and is still pinned; no sheet can actually be at rank 0, because
		// basics start at 1 and a chosen specialisation now starts at 1 too.
		expect_close("rank 0 swings once every 5 s", open::melee_cooldown(0u, false), 5.0f);
		expect_close("rank 1 swings at 1/0.214", open::melee_cooldown(1u, false), 1.0f / 0.214f);
		expect_close("rank 100 swings at 0.625", open::melee_cooldown(100u, false), 0.625f);
		expect_close("rank 200 is clamped to the fastest", open::melee_cooldown(200u, false), 1.0f / 3.0f);
		expect_close("an exhausted rank 0 is still clamped to 5", open::melee_cooldown(0u, true), 5.0f);
		expect_close("an exhausted rank 1 is clamped back to 5", open::melee_cooldown(1u, true), 5.0f);
		expect_close("an exhausted rank 100 doubles to 1.25", open::melee_cooldown(100u, true), 1.25f);
		expect_close("an exhausted rank 200 doubles inside the band", open::melee_cooldown(200u, true),
					 2.0f / 3.0f);
		// The four ranks above already pin both band ends, doubled and not, so no
		// separate bound check belongs here. That defence shares this formula and
		// pays no exhaustion penalty of its own is a real-sheet claim: it lives in
		// the swing tests, where a defender exists to be tired.
	}
	{
		// damage_uses_base_soak_and_offensive_half: soak is toughness_base x armour,
		// and exhaustion halves only weapon x attack_skill.
		expect_close("rank 1 with an 8 weapon does 0.04", open::melee_damage(1u, 8.0f, 1.0f, 0.0f, false),
					 0.04f);
		expect_close("the same swing unpaid does 0.02", open::melee_damage(1u, 8.0f, 1.0f, 0.0f, true),
					 0.02f);
		expect_close("rank 100 with base Toughness 2 and armour 0.5 does 3",
					 open::melee_damage(100u, 8.0f, 2.0f, 0.5f, false), 3.0f);
		expect_close("exhaustion halves the offensive term, not the result",
					 open::melee_damage(100u, 8.0f, 2.0f, 0.5f, true), 1.0f);
		expect_close("soak is never healed into damage", open::melee_damage(0u, 8.0f, 100.0f, 1.0f, false),
					 0.0f);
		expect_close("excessive soak clamps to zero", open::melee_damage(1u, 8.0f, 2.0f, 9.0f, false), 0.0f);
		expect_close("a rank-0 job has no offensive term", open::melee_damage(0u, 8.0f, 0.0f, 0.0f, false),
					 0.0f);  // rank 0 is a formula input here, not a sheet state
	}
	{
		// defender_toughness_is_the_soak: the soak belongs to the sheet being hit.
		// A float-only arithmetic test cannot show which character supplied the
		// toughness, so this one names the sheets at a nonzero armour where the
		// number actually bites: one attacker, two defenders, and the attacker's
		// own Toughness must never protect it from its own swing.
		open::Character attacker = make_character(1);
		train(attacker, open::BasicSkill::UnarmedCombat, std::nullopt, 100u);
		open::Character soft = make_character(2);
		open::Character hard = make_character(3);
		expect_true("the soft defender's base toughness is set to 2",
					soft.set_base(open::Attribute::Toughness, 2.0f));
		expect_true("the hard defender's base toughness is set to 100",
					hard.set_base(open::Attribute::Toughness, 100.0f));
		const std::optional<open::AttackProfile> attack = open::attack_profile(attacker);
		expect_true("the trained attacker has a swing", attack.has_value());
		if (attack.has_value()) {
			// trained 100 x weapon 8 gives an offensive term of 4; armour 0.5 makes
			// the defender's own base toughness the soak.
			expect_close("against base Toughness 2 at armour 0.5 the blow does 3",
						 open::melee_damage(*attack, soft, 8.0f, 0.5f, false), 3.0f);
			expect_close("against base Toughness 100 the same blow is soaked to nothing",
						 open::melee_damage(*attack, hard, 8.0f, 0.5f, false), 0.0f);
			expect_close("exhaustion halves only the offensive term, against that defender",
						 open::melee_damage(*attack, soft, 8.0f, 0.5f, true), 1.0f);
			expect_close("and an exhausted blow on the hard defender is still nothing",
						 open::melee_damage(*attack, hard, 8.0f, 0.5f, true), 0.0f);

			// The sheet that owns the swing is not a party to the soak.
			expect_true("the attacker is made as tough as the hard defender",
						attacker.set_base(open::Attribute::Toughness, 100.0f));
			expect_true("their base toughness really does match",
						close(attacker.base(open::Attribute::Toughness), hard.base(open::Attribute::Toughness)));
			expect_close("the soft defender still feels the whole 3",
						 open::melee_damage(*attack, soft, 8.0f, 0.5f, false), 3.0f);
			expect_close("the hard defender is still the one that soaks it",
						 open::melee_damage(*attack, hard, 8.0f, 0.5f, false), 0.0f);
			expect_close("and the helper follows the sheet it is handed, not the swing's owner",
						 open::melee_damage(*attack, attacker, 8.0f, 0.5f, false), 0.0f);
		}
	}
	{
		// nullify_is_ready_and_strictly_greater: equality loses, and a cooling
		// defence never wins however high its shield is. The rank-0 pairs are
		// formula inputs, not states a sheet can hold.
		expect_true("defence equal to the attack does not nullify",
					!open::melee_nullifies(true, 0.0f, 200u, 25u, 8.0f));
		expect_true("a better defence nullifies", open::melee_nullifies(true, 0.0f, 200u, 24u, 8.0f));
		expect_true("a cooling defence cannot nullify a weaker attack",
					!open::melee_nullifies(false, 0.0f, 200u, 24u, 8.0f));
		expect_true("a high shield with a cooling defence still loses",
					!open::melee_nullifies(false, 8.0f, 200u, 25u, 8.0f));
		expect_true("a high shield with a ready defence wins",
					open::melee_nullifies(true, 8.0f, 0u, 25u, 8.0f));
		expect_true("a rank-1 dodge cannot stop a rank-1 8-weapon swing",
					!open::melee_nullifies(true, 0.0f, 1u, 1u, 8.0f));
		expect_true("an unshielded untrained defence loses to an untrained attack",
					!open::melee_nullifies(true, 0.0f, 0u, 0u, 8.0f));
	}
	{
		// unarmed_uses_chosen_track: bare hands are Unarmed Combat, or the chosen
		// Boxing/Taekwondo track and its own rank.
		open::Character c = make_character(1);
		const std::optional<open::AttackProfile> bare = open::attack_profile(c);
		expect_true("an unarmed character can attack", bare.has_value());
		expect_true("bare hands are Unarmed Combat", is_basic(bare, open::BasicSkill::UnarmedCombat));
		expect_true("with no specialisation chosen", bare.has_value() && !bare->option.has_value());
		expect_true("at the fresh basic rank", bare.has_value() && bare->trained == 1u);

		train(c, open::BasicSkill::UnarmedCombat, std::nullopt, 100u);
		const std::optional<open::AttackProfile> trained = open::attack_profile(c);
		expect_true("the basic rank keeps rising while unadvanced",
					trained.has_value() && trained->trained == 100u);

		expect_true("boxing is chosen", c.advance(open::AdvancedSkill::Boxing));
		const std::optional<open::AttackProfile> fresh = open::attack_profile(c);
		expect_true("the profile still names the basic",
					is_basic(fresh, open::BasicSkill::UnarmedCombat));
		expect_true("and carries the chosen track",
					fresh.has_value() && fresh->option == spec(open::AdvancedSkill::Boxing));
		expect_true("a freshly chosen specialisation is rank 1", fresh.has_value() && fresh->trained == 1u);
		const open::Training fresh_track =
			c.resolve_skill(open::BasicSkill::UnarmedCombat, spec(open::AdvancedSkill::Boxing));
		expect_true("and the sheet resolves that job to rank 1 with nothing banked",
					fresh_track.rank == 1u && fresh_track.xp == 0u);

		train(c, open::BasicSkill::UnarmedCombat, spec(open::AdvancedSkill::Boxing), 5u);
		const std::optional<open::AttackProfile> boxing = open::attack_profile(c);
		expect_true("bare hands now use the boxing rank", boxing.has_value() && boxing->trained == 5u);
		expect_true("the rank used is the one Skills resolves",
					boxing.has_value() &&
						boxing->trained == c.resolve_skill(open::BasicSkill::UnarmedCombat,
														  spec(open::AdvancedSkill::Boxing))
												.rank);

		open::Character kicker = make_character(2);
		train(kicker, open::BasicSkill::UnarmedCombat, std::nullopt, 100u);
		expect_true("taekwondo is chosen", kicker.advance(open::AdvancedSkill::Taekwondo));
		const std::optional<open::AttackProfile> kick = open::attack_profile(kicker);
		expect_true("the kick uses taekwondo",
					kick.has_value() && kick->option == spec(open::AdvancedSkill::Taekwondo));
		expect_true("at its own fresh rank of 1", kick.has_value() && kick->trained == 1u);

		// A specialisation under another skill cannot leak into bare hands.
		open::Character dodger = make_character(3);
		train(dodger, open::BasicSkill::Dodging, std::nullopt, 100u);
		expect_true("footwork is chosen", dodger.advance(open::AdvancedSkill::Footwork));
		const open::Training fresh_footwork =
			dodger.resolve_skill(open::BasicSkill::Dodging, spec(open::AdvancedSkill::Footwork));
		expect_true("a fresh dodge specialisation resolves to rank 1 with nothing banked",
					fresh_footwork.rank == 1u && fresh_footwork.xp == 0u);
		train(dodger, open::BasicSkill::Dodging, spec(open::AdvancedSkill::Footwork), 9u);
		const std::optional<open::AttackProfile> unrelated = open::attack_profile(dodger);
		expect_true("an unrelated chosen track is not carried into the attack",
					unrelated.has_value() && !unrelated->option.has_value());
		expect_true("and the attack still uses the Unarmed Combat basic",
					unrelated.has_value() && unrelated->trained == 1u);
	}
	{
		// matching_weapon_or_frozen_parent: the weapon's own job, its own subtype
		// only when that subtype is the chosen one, otherwise the frozen parent.
		open::Character c = make_character(1);
		train(c, open::BasicSkill::OneHandedWeapons, std::nullopt, 100u);
		expect_true("katana is chosen", c.advance(open::AdvancedSkill::Katana));
		const open::Training fresh_katana =
			c.resolve_skill(open::BasicSkill::OneHandedWeapons, spec(open::AdvancedSkill::Katana));
		expect_true("a fresh weapon specialisation resolves to rank 1 with nothing banked",
					fresh_katana.rank == 1u && fresh_katana.xp == 0u);
		train(c, open::BasicSkill::OneHandedWeapons, spec(open::AdvancedSkill::Katana), 20u);

		expect_true("the katana is carried",
					c.add_item(one_handed(701, open::BasicSkill::OneHandedWeapons,
										 spec(open::AdvancedSkill::Katana))));
		expect_true("the katana is held", c.equip(701, open::Slot::LeftHand));
		const std::optional<open::AttackProfile> katana = open::attack_profile(c);
		expect_true("the katana uses One-Handed Weapons",
					is_basic(katana, open::BasicSkill::OneHandedWeapons));
		expect_true("and the katana track", katana.has_value() && katana->option == spec(open::AdvancedSkill::Katana));
		expect_true("at the trained katana rank", katana.has_value() && katana->trained == 20u);

		expect_true("the katana comes off", c.unequip(open::Slot::LeftHand));
		expect_true("an axe is carried",
					c.add_item(one_handed(702, open::BasicSkill::OneHandedWeapons,
										 spec(open::AdvancedSkill::Axe))));
		expect_true("the axe is held", c.equip(702, open::Slot::LeftHand));
		const std::optional<open::AttackProfile> axe = open::attack_profile(c);
		expect_true("the axe carries its own tag", axe.has_value() && axe->option == spec(open::AdvancedSkill::Axe));
		expect_true("but an unchosen sibling uses the frozen parent",
					axe.has_value() && axe->trained == 100u);

		expect_true("the axe comes off", c.unequip(open::Slot::LeftHand));
		expect_true("a plain sword is carried", c.add_item(one_handed(703)));
		expect_true("the plain sword is held", c.equip(703, open::Slot::LeftHand));
		const std::optional<open::AttackProfile> plain = open::attack_profile(c);
		expect_true("a generic weapon names no subtype", plain.has_value() && !plain->option.has_value());
		expect_true("and uses the parent basic", plain.has_value() && plain->trained == 100u);

		// RightHand is inspected as well as LeftHand.
		open::Character righty = make_character(2);
		expect_true("a sword is carried", righty.add_item(one_handed(704, open::BasicSkill::OneHandedWeapons)));
		expect_true("the sword goes in the right hand", righty.equip(704, open::Slot::RightHand));
		const std::optional<open::AttackProfile> right = open::attack_profile(righty);
		expect_true("the right hand is searched", is_basic(right, open::BasicSkill::OneHandedWeapons));
		expect_true("at that basic rank", right.has_value() && right->trained == 1u);

		// An untagged two-handed record is the generic two-handed job.
		open::Character big = make_character(3);
		expect_true("a plain great sword is carried", big.add_item(two_handed(705)));
		expect_true("it is held", big.equip(705, open::Slot::LeftHand));
		const std::optional<open::AttackProfile> generic_two = open::attack_profile(big);
		expect_true("an untagged two-handed record uses Two-Handed Weapons",
					is_basic(generic_two, open::BasicSkill::TwoHandedWeapons));
		expect_true("with no subtype", generic_two.has_value() && !generic_two->option.has_value());
		expect_true("a two-handed record is selected once, not twice",
					generic_two.has_value() && generic_two->trained == 1u);

		// A tagged polearm whose spec is unchosen uses the frozen Polearms basic.
		open::Character pole = make_character(4);
		expect_true("a spear is carried",
					pole.add_item(two_handed(706, open::BasicSkill::Polearms, spec(open::AdvancedSkill::Spear))));
		expect_true("the spear is held", pole.equip(706, open::Slot::LeftHand));
		const std::optional<open::AttackProfile> unfocused_spear = open::attack_profile(pole);
		expect_true("a polearm uses the Polearms job",
					is_basic(unfocused_spear, open::BasicSkill::Polearms));
		expect_true("and still carries its tag",
					unfocused_spear.has_value() &&
						unfocused_spear->option == spec(open::AdvancedSkill::Spear));
		expect_true("but reads the frozen parent rank",
					unfocused_spear.has_value() && unfocused_spear->trained == 1u);

		// The same record once its own spec is chosen reads the advanced rank.
		open::Character expert_pole = make_character(5);
		train(expert_pole, open::BasicSkill::Polearms, std::nullopt, 100u);
		expect_true("spear is chosen", expert_pole.advance(open::AdvancedSkill::Spear));
		const open::Training fresh_spear =
			expert_pole.resolve_skill(open::BasicSkill::Polearms, spec(open::AdvancedSkill::Spear));
		expect_true("a fresh polearm specialisation resolves to rank 1 with nothing banked",
					fresh_spear.rank == 1u && fresh_spear.xp == 0u);
		train(expert_pole, open::BasicSkill::Polearms, spec(open::AdvancedSkill::Spear), 7u);
		expect_true("a spear is carried", expert_pole.add_item(
												two_handed(707, open::BasicSkill::Polearms,
														 spec(open::AdvancedSkill::Spear))));
		expect_true("the spear is held", expert_pole.equip(707, open::Slot::LeftHand));
		const std::optional<open::AttackProfile> chosen_spear = open::attack_profile(expert_pole);
		expect_true("the chosen polearm track is used",
					chosen_spear.has_value() && chosen_spear->trained == 7u);

		// A chosen two-handed spec is used for that weapon only.
		open::Character breaker = make_character(6);
		train(breaker, open::BasicSkill::TwoHandedWeapons, std::nullopt, 100u);
		expect_true("greatsword is chosen", breaker.advance(open::AdvancedSkill::Greatsword));
		const open::Training fresh_greatsword =
			breaker.resolve_skill(open::BasicSkill::TwoHandedWeapons, spec(open::AdvancedSkill::Greatsword));
		expect_true("a fresh two-handed specialisation resolves to rank 1 with nothing banked",
					fresh_greatsword.rank == 1u && fresh_greatsword.xp == 0u);
		train(breaker, open::BasicSkill::TwoHandedWeapons, spec(open::AdvancedSkill::Greatsword), 3u);
		expect_true("a great sword is carried", breaker.add_item(
													two_handed(708, open::BasicSkill::TwoHandedWeapons,
															   spec(open::AdvancedSkill::Greatsword))));
		expect_true("the great sword is held", breaker.equip(708, open::Slot::LeftHand));
		const std::optional<open::AttackProfile> greatsword = open::attack_profile(breaker);
		expect_true("the great sword uses its own rank",
					greatsword.has_value() && greatsword->trained == 3u);

		// Two one-handed weapons: the left hand's job is the one this slice uses.
		open::Character dual = make_character(9);
		train(dual, open::BasicSkill::OneHandedWeapons, std::nullopt, 100u);
		expect_true("katana is chosen", dual.advance(open::AdvancedSkill::Katana));
		train(dual, open::BasicSkill::OneHandedWeapons, spec(open::AdvancedSkill::Katana), 20u);
		expect_true("a katana is carried",
					dual.add_item(one_handed(713, open::BasicSkill::OneHandedWeapons,
											spec(open::AdvancedSkill::Katana))));
		expect_true("an axe is carried",
					dual.add_item(one_handed(714, open::BasicSkill::OneHandedWeapons,
											spec(open::AdvancedSkill::Axe))));
		expect_true("the katana goes in the left hand", dual.equip(713, open::Slot::LeftHand));
		expect_true("the axe goes in the right hand", dual.equip(714, open::Slot::RightHand));
		const std::optional<open::AttackProfile> first_hand = open::attack_profile(dual);
		expect_true("the left hand is the one that swings",
					first_hand.has_value() && first_hand->option == spec(open::AdvancedSkill::Katana));
		expect_true("and one swing, at that weapon's rank", first_hand.has_value() && first_hand->trained == 20u);

		// A shield is not a weapon: bare hands take over.
		open::Character shielded = make_character(7);
		expect_true("a shield is carried", shielded.add_item(shield(709)));
		expect_true("the shield is held", shielded.equip(709, open::Slot::LeftHand));
		const std::optional<open::AttackProfile> shield_only = open::attack_profile(shielded);
		expect_true("a shield alone means an unarmed attack",
					is_basic(shield_only, open::BasicSkill::UnarmedCombat));

		// LeftHand then RightHand: a shield cannot hide the sword in the other hand.
		open::Character sword_and_board = make_character(8);
		expect_true("a shield is carried", sword_and_board.add_item(shield(710)));
		expect_true("a sword is carried",
					sword_and_board.add_item(one_handed(711, open::BasicSkill::OneHandedWeapons)));
		expect_true("the shield goes in the left hand", sword_and_board.equip(710, open::Slot::LeftHand));
		expect_true("the sword goes in the right hand", sword_and_board.equip(711, open::Slot::RightHand));
		const std::optional<open::AttackProfile> both_hands = open::attack_profile(sword_and_board);
		expect_true("the weapon wins over the shield for the attack",
					is_basic(both_hands, open::BasicSkill::OneHandedWeapons));

		// A dead attacker has no loadout at all.
		open::Character corpse = make_character(10);
		expect_true("the corpse once held a sword",
					(corpse.add_item(one_handed(712, open::BasicSkill::OneHandedWeapons)) &&
					 corpse.equip(712, open::Slot::LeftHand)));
		expect_true("the corpse dies", corpse.damage(open::BodyPart::Torso, 500.0f));
		expect_true("a dead character has no attack profile", !open::attack_profile(corpse).has_value());
		expect_true("a dead character has no defence profile", !open::defend_profile(corpse).has_value());
	}
	{
		// defense_priority_and_limb_gates: shield, then two-handed weapon, then
		// one-handed weapon, then dodge; broken arms take the held gear out of use.
		open::Character c = make_character(1);
		train(c, open::BasicSkill::Blocking, std::nullopt, 100u);
		expect_true("small shields are chosen", c.advance(open::AdvancedSkill::SmallShields));
		const open::Training fresh_shields =
			c.resolve_skill(open::BasicSkill::Blocking, spec(open::AdvancedSkill::SmallShields));
		expect_true("a fresh shield specialisation resolves to rank 1 with nothing banked",
					fresh_shields.rank == 1u && fresh_shields.xp == 0u);
		train(c, open::BasicSkill::Blocking, spec(open::AdvancedSkill::SmallShields), 12u);

		expect_true("a small shield is carried", c.add_item(shield(801, spec(open::AdvancedSkill::SmallShields))));
		expect_true("the small shield is held", c.equip(801, open::Slot::LeftHand));
		const std::optional<open::DefendProfile> board = open::defend_profile(c);
		expect_true("a usable shield blocks",
					board.has_value() && board->action == open::DefendAction::Block);
		expect_true("a shield block is Blocking", is_basic(board, open::BasicSkill::Blocking));
		expect_true("and its matching specialisation",
					board.has_value() && board->option == spec(open::AdvancedSkill::SmallShields));
		expect_true("at that specialisation's rank", board.has_value() && board->trained == 12u);
		expect_close("a shield's placeholder block value is 8", board.has_value() ? board->shield : 0.0f, 8.0f);

		expect_true("the small shield comes off", c.unequip(open::Slot::LeftHand));
		expect_true("a big shield is carried", c.add_item(shield(802, spec(open::AdvancedSkill::BigShields))));
		expect_true("the big shield is held", c.equip(802, open::Slot::LeftHand));
		const std::optional<open::DefendProfile> mismatched = open::defend_profile(c);
		expect_true("a mismatched shield still blocks",
					mismatched.has_value() && mismatched->action == open::DefendAction::Block);
		expect_true("it carries its own tag",
					mismatched.has_value() && mismatched->option == spec(open::AdvancedSkill::BigShields));
		expect_true("but reads the frozen Blocking rank",
					mismatched.has_value() && mismatched->trained == 100u);
		expect_true("the tag is the one Skills resolves",
					mismatched.has_value() &&
						mismatched->trained ==
							c.resolve_skill(open::BasicSkill::Blocking, spec(open::AdvancedSkill::BigShields))
								.rank);

		expect_true("the big shield comes off", c.unequip(open::Slot::LeftHand));
		expect_true("a sword is carried",
					c.add_item(one_handed(803, open::BasicSkill::OneHandedWeapons)));
		expect_true("the sword is held", c.equip(803, open::Slot::LeftHand));
		const std::optional<open::DefendProfile> weapon_block = open::defend_profile(c);
		expect_true("a weapon blocks too",
					weapon_block.has_value() && weapon_block->action == open::DefendAction::Block);
		expect_true("a weapon block borrows no specialisation",
					weapon_block.has_value() && !weapon_block->option.has_value());
		expect_true("a weapon block uses the frozen Blocking basic",
					weapon_block.has_value() && weapon_block->trained == 100u);
		expect_close("a weapon block has the same placeholder value",
					 weapon_block.has_value() ? weapon_block->shield : 0.0f, 8.0f);

		expect_true("a shield is carried as well", c.add_item(shield(804, spec(open::AdvancedSkill::WallShields))));
		expect_true("the shield takes the free hand", c.equip(804, open::Slot::RightHand));
		const std::optional<open::DefendProfile> priority = open::defend_profile(c);
		expect_true("a shield outranks the weapon",
					priority.has_value() && priority->option == spec(open::AdvancedSkill::WallShields));
		expect_true("while the attack still uses the sword",
					is_basic(open::attack_profile(c), open::BasicSkill::OneHandedWeapons));

		expect_true("the shield comes off", c.unequip(open::Slot::RightHand));
		expect_true("the sword comes off", c.unequip(open::Slot::LeftHand));
		expect_true("a two-handed weapon is carried",
					c.add_item(two_handed(805, open::BasicSkill::TwoHandedWeapons,
										  spec(open::AdvancedSkill::Warhammer))));
		expect_true("the two-handed weapon is held", c.equip(805, open::Slot::LeftHand));
		const std::optional<open::DefendProfile> two_hand_block = open::defend_profile(c);
		expect_true("a two-handed weapon blocks with Blocking",
					two_hand_block.has_value() &&
						two_hand_block->action == open::DefendAction::Block &&
						is_basic(two_hand_block, open::BasicSkill::Blocking));
		expect_true("and never the weapon's own spec",
					two_hand_block.has_value() && !two_hand_block->option.has_value());
		expect_true("still at the frozen Blocking rank",
					two_hand_block.has_value() && two_hand_block->trained == 100u);

		// One arm gone takes the two-handed weapon out of use for both jobs.
		expect_true("one arm breaks", c.damage(open::BodyPart::LeftArm, 40.0f));
		expect_true("the two-handed weapon is still equipped",
					c.equipped(open::Slot::LeftHand) == std::optional<open::ItemId>(805));
		expect_true("but it is no longer usable", !c.held_usable(805));
		const std::optional<open::DefendProfile> maimed = open::defend_profile(c);
		expect_true("with no usable gear the character dodges",
					maimed.has_value() && maimed->action == open::DefendAction::Dodge);
		expect_close("a dodge has no shield value", maimed.has_value() ? maimed->shield : 8.0f, 0.0f);
		const std::optional<open::AttackProfile> maimed_attack = open::attack_profile(c);
		expect_true("the attack falls back to unarmed",
					maimed_attack.has_value() &&
						is_basic(maimed_attack, open::BasicSkill::UnarmedCombat));

		// Both arms gone: legs still dodge, and an unarmed swing is still there.
		expect_true("the other arm breaks", c.damage(open::BodyPart::RightArm, 40.0f));
		expect_true("still alive with no arms", c.alive());
		const std::optional<open::DefendProfile> armless = open::defend_profile(c);
		expect_true("no arms means no block",
					armless.has_value() && armless->action == open::DefendAction::Dodge);
		expect_true("healthy legs still let a character attack unarmed",
					open::attack_profile(c).has_value());

		// The dodge track itself: the chosen Footwork/Acrobat Evasion rank.
		open::Character feet = make_character(2);
		train(feet, open::BasicSkill::Dodging, std::nullopt, 100u);
		expect_true("acrobat evasion is chosen", feet.advance(open::AdvancedSkill::AcrobatEvasion));
		const open::Training fresh_evasion =
			feet.resolve_skill(open::BasicSkill::Dodging, spec(open::AdvancedSkill::AcrobatEvasion));
		expect_true("a fresh evasion specialisation resolves to rank 1 with nothing banked",
					fresh_evasion.rank == 1u && fresh_evasion.xp == 0u);
		train(feet, open::BasicSkill::Dodging, spec(open::AdvancedSkill::AcrobatEvasion), 6u);
		const std::optional<open::DefendProfile> dodge = open::defend_profile(feet);
		expect_true("bare defence is a dodge",
					dodge.has_value() && dodge->action == open::DefendAction::Dodge);
		expect_true("a dodge is Dodging", is_basic(dodge, open::BasicSkill::Dodging));
		expect_true("with the chosen evasion track",
					dodge.has_value() && dodge->option == spec(open::AdvancedSkill::AcrobatEvasion));
		expect_true("at its own rank", dodge.has_value() && dodge->trained == 6u);
		expect_close("an unarmed dodge has no shield", dodge.has_value() ? dodge->shield : 8.0f, 0.0f);

		// Both legs gone still leaves an armed swing and a block.
		open::Character legs = make_character(3);
		expect_true("a sword is carried", legs.add_item(one_handed(806, open::BasicSkill::OneHandedWeapons)));
		expect_true("the sword is held", legs.equip(806, open::Slot::LeftHand));
		expect_true("one leg breaks", legs.damage(open::BodyPart::LeftLeg, 40.0f));
		expect_true("the other leg breaks", legs.damage(open::BodyPart::RightLeg, 40.0f));
		expect_true("still alive with no legs", legs.alive());
		expect_true("arms still swing", is_basic(open::attack_profile(legs), open::BasicSkill::OneHandedWeapons));
		expect_true("arms still block",
					open::defend_profile(legs).has_value() &&
						open::defend_profile(legs)->action == open::DefendAction::Block);
	}
	{
		// effective_attributes_do_not_enter_combat: level, attribute and title
		// multipliers move the sheet, never the combat rank or its clock.
		open::Character plain = make_character(1);
		train(plain, open::BasicSkill::OneHandedWeapons, std::nullopt, 100u);
		expect_true("a sword is carried", plain.add_item(one_handed(901, open::BasicSkill::OneHandedWeapons)));
		expect_true("the sword is held", plain.equip(901, open::Slot::LeftHand));

		open::Character hero = make_character(2, 7, open::Attribute::Toughness);
		expect_true("strength is raised", hero.set_base(open::Attribute::Strength, 100.0f));
		expect_true("agility is raised", hero.set_base(open::Attribute::Agility, 100.0f));
		train(hero, open::BasicSkill::OneHandedWeapons, std::nullopt, 100u);
		for (int i = 0; i < 100; ++i) {
			hero.record_title_progress(open::Title::Duelist, true);
		}
		expect_true("duelist is held", hero.has_title(open::Title::Duelist));
		expect_true("a sword is carried", hero.add_item(one_handed(902, open::BasicSkill::OneHandedWeapons)));
		expect_true("the sword is held", hero.equip(902, open::Slot::LeftHand));

		expect_true("the sheet itself really is different",
					!close(hero.effective_skill(open::BasicSkill::OneHandedWeapons),
						   plain.effective_skill(open::BasicSkill::OneHandedWeapons)));
		const std::optional<open::AttackProfile> plain_attack = open::attack_profile(plain);
		const std::optional<open::AttackProfile> hero_attack = open::attack_profile(hero);
		expect_true("both use the same trained rank",
					plain_attack.has_value() && hero_attack.has_value() &&
						plain_attack->trained == hero_attack->trained);
		expect_true("and that rank is the trained number, not an effective one",
					hero_attack.has_value() && hero_attack->trained == 100u);
		expect_close("level and titles cannot change the clock",
					 open::melee_cooldown(hero_attack.has_value() ? hero_attack->trained : 0u, false),
					 open::melee_cooldown(plain_attack.has_value() ? plain_attack->trained : 0u, false));
		expect_close("the clock is the trained-rank formula",
					 open::melee_cooldown(hero_attack.has_value() ? hero_attack->trained : 0u, false),
					 0.625f);

		// Soak reads base Toughness, never the level- and blessing-scaled value.
		expect_true("toughness base is raised", hero.set_base(open::Attribute::Toughness, 2.0f));
		expect_close("the base toughness is two", hero.base(open::Attribute::Toughness), 2.0f);
		expect_true("the effective toughness is far larger",
					hero.attribute(open::Attribute::Toughness) > 200.0f);
		expect_close("soak uses the base toughness",
					 open::melee_damage(100u, open::kPlaceholderWeapon, hero.base(open::Attribute::Toughness),
										0.5f, false),
					 3.0f);
		expect_true("the effective toughness would soak a different amount",
					!close(open::melee_damage(100u, open::kPlaceholderWeapon,
											  hero.attribute(open::Attribute::Toughness), 0.5f, false),
						   3.0f));
		expect_close("the placeholder weapon is 8", open::kPlaceholderWeapon, 8.0f);
	}

	return test_summary();
}
