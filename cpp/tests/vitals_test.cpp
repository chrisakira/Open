#include "open/vitals.hpp"

#include <cmath>
#include <limits>
#include <optional>

#include "open/attributes.hpp"
#include "open/skills.hpp"
#include "test_support.hpp"

// Task 5 tests. Break these catches: a pool formula that uses an effective
// attribute (level leaking into a bar), a body maximum that is not a fraction
// of main HP, damage taken only once instead of twice, overkill bleeding into
// main HP, a spend that lands without funds, regen on a dead character, or a
// recalculation that heals.

namespace {

open::Attributes basic_sheet() {
	return open::Attributes(1, open::Attribute::Strength);
}

void train(open::Skills &skills, open::BasicSkill basic, unsigned times) {
	for (unsigned i = 0; i < times; ++i) {
		skills.record_use(basic, std::nullopt, true);
	}
}

}  // namespace

int main() {
	{
		// Break this catches: a fresh character that does not start full, or a
		// body part that is not a fraction of the main bar.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		const open::Vitals vitals(attributes, skills);
		expect_close("fresh main hp", vitals.pool(open::PoolKind::Hp).current, 100.0f);
		expect_close("fresh main hp max", vitals.pool(open::PoolKind::Hp).maximum, 100.0f);
		expect_close("fresh stamina", vitals.pool(open::PoolKind::Stamina).current, 100.0f);
		expect_close("fresh mana", vitals.pool(open::PoolKind::Mana).current, 100.0f);
		expect_close("fresh head", vitals.part(open::BodyPart::Head).current, 25.0f);
		expect_close("fresh head max", vitals.part(open::BodyPart::Head).maximum, 25.0f);
		expect_close("fresh torso", vitals.part(open::BodyPart::Torso).current, 50.0f);
		expect_close("fresh left arm", vitals.part(open::BodyPart::LeftArm).current, 40.0f);
		expect_close("fresh right arm max", vitals.part(open::BodyPart::RightArm).maximum, 40.0f);
		expect_close("fresh left leg", vitals.part(open::BodyPart::LeftLeg).current, 40.0f);
		expect_close("fresh right leg", vitals.part(open::BodyPart::RightLeg).current, 40.0f);
		expect_true("a fresh character is alive", vitals.alive());
		expect_close("a fresh character walks at full speed", vitals.leg_multiplier(), 1.0f);
		expect_true("a fresh character has one hand", vitals.one_hand());
		expect_true("a fresh character has two hands", vitals.two_hands());
		expect_true("a fresh character can carry", vitals.can_carry());
	}
	{
		// Break this catches: pools using effective attributes, or the stamina and
		// mana formulas ignoring the trained counter.
		open::Attributes attributes = basic_sheet();
		expect_true("constitution base raised", attributes.set_base(open::Attribute::Constitution, 2.0f));
		expect_true("profanity base raised", attributes.set_base(open::Attribute::Profanity, 4.0f));
		open::Skills skills;
		train(skills, open::BasicSkill::Endurance, 24u);	 // 8 + 16 -> rank 3
		train(skills, open::BasicSkill::ManaControl, 8u);  // rank 2
		expect_true("endurance trained to three", skills.basic(open::BasicSkill::Endurance).rank == 3u);
		expect_true("mana control trained to two", skills.basic(open::BasicSkill::ManaControl).rank == 2u);

		const open::Vitals vitals(attributes, skills);
		expect_close("hp max scales with the constitution base", vitals.pool(open::PoolKind::Hp).maximum, 200.0f);
		expect_close("stamina max scales with endurance and constitution",
					 vitals.pool(open::PoolKind::Stamina).maximum, 600.0f);
		expect_close("mana max scales with mana control and the constitution/profanity average",
					 vitals.pool(open::PoolKind::Mana).maximum, 600.0f);
		expect_close("head max follows main hp", vitals.part(open::BodyPart::Head).maximum, 50.0f);
		expect_close("torso max follows main hp", vitals.part(open::BodyPart::Torso).maximum, 100.0f);
		expect_close("limb max follows main hp", vitals.part(open::BodyPart::LeftLeg).maximum, 80.0f);
		expect_close("fresh bars start full", vitals.pool(open::PoolKind::Stamina).current, 600.0f);
	}
	{
		// Break this catches: level, blessing or titles multiplying a pool.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("level raised to seven", attributes.set_level(7));
		expect_true("recalculate accepted", vitals.recalculate(attributes, skills));
		expect_close("level does not move the hp bar", vitals.pool(open::PoolKind::Hp).maximum, 100.0f);
		expect_close("level does not move the stamina bar", vitals.pool(open::PoolKind::Stamina).maximum, 100.0f);
		expect_close("level does not move the mana bar", vitals.pool(open::PoolKind::Mana).maximum, 100.0f);
		expect_close("level does not move a body bar", vitals.part(open::BodyPart::Torso).maximum, 50.0f);
		expect_close("blessed constitution still gives the same bar",
					 vitals.pool(open::PoolKind::Hp).current, 100.0f);

		for (unsigned i = 0; i < open::title_rule(open::Title::IronBack).threshold; ++i) {
			skills.record_title_progress(open::Title::IronBack, true);
		}
		expect_true("a title is held", skills.has_title(open::Title::IronBack));
		expect_true("recalculate accepted", vitals.recalculate(attributes, skills));
		expect_close("a title does not multiply the stamina bar",
					 vitals.pool(open::PoolKind::Stamina).maximum, 100.0f);

		train(skills, open::BasicSkill::ManaControl, 39600u);
		expect_true("mana control capped", skills.basic(open::BasicSkill::ManaControl).rank == 100u);
		expect_true("a mana control specialization is chosen",
					skills.advance(open::AdvancedSkill::Channeling));
		expect_true("recalculate accepted", vitals.recalculate(attributes, skills));
		expect_close("the mana bar still uses the frozen basic counter",
					 vitals.pool(open::PoolKind::Mana).maximum, 10000.0f);
	}
	{
		// Break this catches: a spend cost that is wrong, or a per-second cost
		// charged as a flat one.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("sprint accepted", vitals.spend(open::ResourceUse::Sprint, 1.0f));
		expect_close("sprint costs ten stamina per second",
					 vitals.pool(open::PoolKind::Stamina).current, 90.0f);
		expect_true("swim accepted", vitals.spend(open::ResourceUse::Swimming, 3.0f));
		expect_close("swimming costs six stamina per second",
					 vitals.pool(open::PoolKind::Stamina).current, 72.0f);
		expect_true("melee swing accepted", vitals.spend(open::ResourceUse::MeleeSwing));
		expect_close("a melee swing costs eight stamina",
					 vitals.pool(open::PoolKind::Stamina).current, 64.0f);
		expect_true("labour accepted", vitals.spend(open::ResourceUse::Labour));
		expect_close("a labour action costs five stamina",
					 vitals.pool(open::PoolKind::Stamina).current, 59.0f);
		expect_true("spell accepted", vitals.spend(open::ResourceUse::Spell));
		expect_close("a spell costs twenty mana", vitals.pool(open::PoolKind::Mana).current, 80.0f);
		expect_close("a spell did not touch stamina",
					 vitals.pool(open::PoolKind::Stamina).current, 59.0f);
	}
	{
		// Break this catches: a flat action that is being repeated by quantity, or
		// a per-second action being charged flat.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		expect_true("a melee swing cannot be multiplied by quantity",
					!vitals.spend(open::ResourceUse::MeleeSwing, 2.0f));
		expect_true("a labour action cannot be fractional", !vitals.spend(open::ResourceUse::Labour, 0.5f));
		expect_true("a spell cannot be multiplied", !vitals.spend(open::ResourceUse::Spell, 2.0f));
		expect_true("a nan duration is refused", !vitals.spend(open::ResourceUse::Sprint, nan));
		expect_true("an infinite duration is refused", !vitals.spend(open::ResourceUse::Sprint, inf));
		expect_true("a negative duration is refused", !vitals.spend(open::ResourceUse::Sprint, -1.0f));
		expect_true("a zero duration is refused", !vitals.spend(open::ResourceUse::Sprint, 0.0f));
		expect_true("an overflowing duration is refused",
					!vitals.spend(open::ResourceUse::Sprint, 1.0e38f));
		expect_true("a bad resource id is refused",
					!vitals.spend(static_cast<open::ResourceUse>(7), 1.0f));
		expect_true("nothing was spent", vitals.pool(open::PoolKind::Stamina).current == 100.0f);
		expect_true("no mana was spent", vitals.pool(open::PoolKind::Mana).current == 100.0f);
	}
	{
		// Break this catches: a spend that lands without funds, or one that goes
		// below zero.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("exact funds are accepted", vitals.spend(open::ResourceUse::Sprint, 10.0f));
		expect_close("exact funds reach zero", vitals.pool(open::PoolKind::Stamina).current, 0.0f);
		expect_true("a further sprint is refused", !vitals.spend(open::ResourceUse::Sprint, 1.0f));
		expect_true("a melee swing without funds is refused", !vitals.spend(open::ResourceUse::MeleeSwing));
		expect_close("the refused spend left the bar alone",
					 vitals.pool(open::PoolKind::Stamina).current, 0.0f);
		for (int i = 0; i < 5; ++i) {
			expect_true("a spell is affordable until mana runs out", vitals.spend(open::ResourceUse::Spell));
		}
		expect_close("five spells drain mana exactly", vitals.pool(open::PoolKind::Mana).current, 0.0f);
		expect_true("a sixth spell without mana is refused", !vitals.spend(open::ResourceUse::Spell));
		expect_close("the refused spell left mana at zero", vitals.pool(open::PoolKind::Mana).current, 0.0f);
		expect_close("a refused spend never touches stamina",
					 vitals.pool(open::PoolKind::Stamina).current, 0.0f);
	}
	{
		// Break this catches: regen at the wrong rate, in the wrong pool, or after
		// the bar is full.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("spent to make room", vitals.spend(open::ResourceUse::Sprint, 10.0f));
		expect_true("spent mana to make room", vitals.spend(open::ResourceUse::Spell));
		expect_true("out of combat regen accepted", vitals.regenerate(1.0f, false, attributes));
		expect_close("stamina regens ten per second out of combat",
					 vitals.pool(open::PoolKind::Stamina).current, 10.0f);
		expect_close("mana regens four per second out of combat",
					 vitals.pool(open::PoolKind::Mana).current, 84.0f);
		expect_true("in combat regen accepted", vitals.regenerate(1.0f, true, attributes));
		expect_close("stamina regens five per second in combat",
					 vitals.pool(open::PoolKind::Stamina).current, 15.0f);
		expect_close("mana regens one per second in combat",
					 vitals.pool(open::PoolKind::Mana).current, 85.0f);
		expect_true("regen cannot overshoot the bar", vitals.regenerate(1.0e6f, false, attributes));
		expect_close("stamina is clamped at its maximum",
					 vitals.pool(open::PoolKind::Stamina).current,
					 vitals.pool(open::PoolKind::Stamina).maximum);
		expect_close("mana is clamped at its maximum", vitals.pool(open::PoolKind::Mana).current,
					 vitals.pool(open::PoolKind::Mana).maximum);

		expect_true("head damaged", vitals.damage(open::BodyPart::Head, 10.0f));
		expect_true("main hp damaged", vitals.damage(open::BodyPart::Torso, 10.0f));
		vitals.regenerate(1.0e6f, false, attributes);
		expect_close("hp never regens", vitals.pool(open::PoolKind::Hp).current, 80.0f);
		expect_close("the head never heals", vitals.part(open::BodyPart::Head).current, 15.0f);
		expect_close("the torso never heals", vitals.part(open::BodyPart::Torso).current, 40.0f);
	}
	{
		// Break this catches: mana regen not reading Intelligence, or the average
		// going wrong.
		open::Attributes attributes = basic_sheet();
		expect_true("intelligence base raised", attributes.set_base(open::Attribute::Intelligence, 3.0f));
		expect_true("profanity base raised", attributes.set_base(open::Attribute::Profanity, 5.0f));
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("spent mana to make room", vitals.spend(open::ResourceUse::Spell));
		expect_true("regen accepted", vitals.regenerate(1.0f, false, attributes));
		expect_close("mana regens sixteen per second out of combat against a three-hundred bar",
					 vitals.pool(open::PoolKind::Mana).current, 296.0f);
		expect_true("spent mana again", vitals.spend(open::ResourceUse::Spell));
		expect_true("combat regen accepted", vitals.regenerate(1.0f, true, attributes));
		expect_close("mana regens four per second in combat", vitals.pool(open::PoolKind::Mana).current,
					 280.0f);
	}
	{
		// Break this catches: fractional ticks drifting away from whole ticks.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals whole(attributes, skills);
		open::Vitals fractioned(attributes, skills);
		for (open::Vitals *vitals : {&whole, &fractioned}) {
			expect_true("room made", vitals->spend(open::ResourceUse::Sprint, 1.0f));
			expect_true("room made in mana", vitals->spend(open::ResourceUse::Spell));
		}
		expect_true("half a second of out-of-combat regen", whole.regenerate(0.5f, false, attributes));
		for (int i = 0; i < 25; ++i) {
			expect_true("a small regen tick", fractioned.regenerate(0.02f, false, attributes));
		}
		expect_close("half a second is a real amount of stamina",
					 whole.pool(open::PoolKind::Stamina).current, 95.0f);
		expect_close("twenty-five small ticks match one bigger tick for stamina",
					 whole.pool(open::PoolKind::Stamina).current,
					 fractioned.pool(open::PoolKind::Stamina).current, 0.0001f);
		expect_close("twenty-five small ticks match one bigger tick for mana",
					 whole.pool(open::PoolKind::Mana).current,
					 fractioned.pool(open::PoolKind::Mana).current, 0.0001f);
	}
	{
		// Break this catches: bad regen input landing on the bars.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		expect_true("a negative duration is refused", !vitals.regenerate(-1.0f, false, attributes));
		expect_true("a nan duration is refused", !vitals.regenerate(nan, false, attributes));
		expect_true("an infinite duration is refused", !vitals.regenerate(inf, false, attributes));
		expect_true("a zero duration is a valid no-op", vitals.regenerate(0.0f, false, attributes));
		expect_close("nothing moved", vitals.pool(open::PoolKind::Mana).current, 100.0f);
		// A bar with room to move, to prove the refusal is not just a full bar.
		expect_true("room made", vitals.spend(open::ResourceUse::Spell));
		expect_true("a negative duration is refused", !vitals.regenerate(-5.0f, false, attributes));
		expect_close("mana did not move backwards", vitals.pool(open::PoolKind::Mana).current, 80.0f);
		expect_close("the attribute sheet is untouched", attributes.base(open::Attribute::Constitution), 1.0f);
	}
	{
		// Break this catches: damage taken only from the part, or only from main.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("head hit accepted", vitals.damage(open::BodyPart::Head, 10.0f));
		expect_true("head loses ten", vitals.part(open::BodyPart::Head).current == 15.0f);
		expect_true("main HP also loses ten", vitals.pool(open::PoolKind::Hp).current == 90.0f);
		expect_true("the torso is untouched", vitals.part(open::BodyPart::Torso).current == 50.0f);
		expect_true("still alive", vitals.alive());
	}
	{
		// Break this catches: overkill on a part being charged again to main HP,
		// and death by a zero head.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("big head hit accepted", vitals.damage(open::BodyPart::Head, 30.0f));
		expect_close("the head clamps at zero", vitals.part(open::BodyPart::Head).current, 0.0f);
		expect_close("main HP pays the incoming amount once",
					 vitals.pool(open::PoolKind::Hp).current, 70.0f);
		expect_true("a zero head kills", !vitals.alive());
	}
	{
		// Break this catches: a zero torso not being death, and main HP surviving
		// that hit exactly once.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("torso hit accepted", vitals.damage(open::BodyPart::Torso, 50.0f));
		expect_close("the torso clamps at zero", vitals.part(open::BodyPart::Torso).current, 0.0f);
		expect_close("main hp is fifty", vitals.pool(open::PoolKind::Hp).current, 50.0f);
		expect_true("a zero torso kills", !vitals.alive());
	}
	{
		// Break this catches: the leg debuff ladder.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("first leg hit accepted", vitals.damage(open::BodyPart::LeftLeg, 40.0f));
		expect_close("that leg is gone", vitals.part(open::BodyPart::LeftLeg).current, 0.0f);
		expect_close("main hp paid once", vitals.pool(open::PoolKind::Hp).current, 60.0f);
		expect_close("one dead leg halves speed", vitals.leg_multiplier(), 0.5f);
		expect_true("still alive", vitals.alive());
		expect_true("still walking", vitals.leg_multiplier() > 0.0f);
		expect_true("second leg hit accepted", vitals.damage(open::BodyPart::RightLeg, 40.0f));
		expect_close("main hp is twenty", vitals.pool(open::PoolKind::Hp).current, 20.0f);
		expect_close("both legs gone stops walking", vitals.leg_multiplier(), 0.0f);
		expect_true("legs do not kill", vitals.alive());
		expect_true("the head is still up", vitals.part(open::BodyPart::Head).current > 0.0f);
		expect_true("the torso is still up", vitals.part(open::BodyPart::Torso).current > 0.0f);
	}
	{
		// Break this catches: the arm debuff ladder.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("first arm hit accepted", vitals.damage(open::BodyPart::LeftArm, 40.0f));
		expect_close("main hp is sixty", vitals.pool(open::PoolKind::Hp).current, 60.0f);
		expect_true("one arm still allows one-handed use", vitals.one_hand());
		expect_true("one arm forbids two-handed use", !vitals.two_hands());
		expect_true("one arm still allows carrying", vitals.can_carry());
		expect_true("second arm hit accepted", vitals.damage(open::BodyPart::RightArm, 40.0f));
		expect_close("main hp is twenty", vitals.pool(open::PoolKind::Hp).current, 20.0f);
		expect_true("both arms forbid carrying", !vitals.can_carry());
		expect_true("both arms forbid held use", !vitals.one_hand());
		expect_true("both arms forbid two-handing", !vitals.two_hands());
		expect_true("arms do not kill", vitals.alive());
		expect_close("legs are unaffected by arm hits", vitals.leg_multiplier(), 1.0f);
	}
	{
		// Break this catches: a body part other than head/torso being treated as
		// an instant kill, or main HP ignoring a lethal limb hit.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals vitals(attributes, skills);
		vitals.damage(open::BodyPart::LeftLeg, 40.0f);
		vitals.damage(open::BodyPart::RightLeg, 40.0f);
		expect_true("still alive on twenty hp", vitals.alive());
		expect_true("a third limb hit accepted", vitals.damage(open::BodyPart::LeftArm, 40.0f));
		expect_close("main hp is zero", vitals.pool(open::PoolKind::Hp).current, 0.0f);
		expect_true("empty main hp kills", !vitals.alive());
		expect_true("the head stayed positive", vitals.part(open::BodyPart::Head).current > 0.0f);
		expect_true("the torso stayed positive", vitals.part(open::BodyPart::Torso).current > 0.0f);
		expect_close("a dead character cannot walk", vitals.leg_multiplier(), 0.0f);
		expect_true("a dead character has no usable hand", !vitals.one_hand());
		expect_true("a dead character cannot carry", !vitals.can_carry());
	}
	{
		// Break this catches: a dead character spending or regenerating.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals vitals(attributes, skills);
		vitals.damage(open::BodyPart::Torso, 50.0f);
		expect_true("dead", !vitals.alive());
		expect_true("a dead character cannot spend", !vitals.spend(open::ResourceUse::MeleeSwing));
		expect_true("a dead character cannot sprint", !vitals.spend(open::ResourceUse::Sprint, 1.0f));
		expect_true("a dead character cannot cast", !vitals.spend(open::ResourceUse::Spell));
		expect_true("a dead character cannot regenerate", !vitals.regenerate(10.0f, false, attributes));
		expect_close("stamina did not move", vitals.pool(open::PoolKind::Stamina).current, 100.0f);
		expect_close("mana did not move", vitals.pool(open::PoolKind::Mana).current, 100.0f);
		expect_close("hp did not move", vitals.pool(open::PoolKind::Hp).current, 50.0f);
	}
	{
		// Break this catches: bad damage input reaching the pools.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals vitals(attributes, skills);
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float inf = std::numeric_limits<float>::infinity();
		expect_true("zero damage is a no-op", vitals.damage(open::BodyPart::Head, 0.0f));
		expect_close("zero damage left the head", vitals.part(open::BodyPart::Head).current, 25.0f);
		expect_close("zero damage left main hp", vitals.pool(open::PoolKind::Hp).current, 100.0f);
		expect_true("negative damage is refused", !vitals.damage(open::BodyPart::Head, -10.0f));
		expect_true("nan damage is refused", !vitals.damage(open::BodyPart::Head, nan));
		expect_true("infinite damage is refused", !vitals.damage(open::BodyPart::Head, inf));
		expect_true("a bad body part id is refused", !vitals.damage(static_cast<open::BodyPart>(6), 10.0f));
		expect_true("a negative body part id is refused", !vitals.damage(static_cast<open::BodyPart>(-1), 10.0f));
		expect_close("nothing was damaged", vitals.part(open::BodyPart::Head).current, 25.0f);
		expect_close("nothing was damaged in main hp", vitals.pool(open::PoolKind::Hp).current, 100.0f);
		expect_throws_invalid("pool rejects a bad id",
							  [&vitals] { vitals.pool(static_cast<open::PoolKind>(3)); });
		expect_throws_invalid("part rejects a bad id",
							  [&vitals] { vitals.part(static_cast<open::BodyPart>(6)); });
	}
	{
		// Break this catches: recalculation refilling, reviving, or moving current
		// instead of preserving it.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("hp damaged", vitals.damage(open::BodyPart::Torso, 30.0f));
		expect_close("hp is seventy", vitals.pool(open::PoolKind::Hp).current, 70.0f);
		expect_true("constitution base raised", attributes.set_base(open::Attribute::Constitution, 2.0f));
		expect_true("recalculate accepted", vitals.recalculate(attributes, skills));
		expect_close("the hp maximum grew", vitals.pool(open::PoolKind::Hp).maximum, 200.0f);
		expect_close("current hp was preserved, not refilled",
					 vitals.pool(open::PoolKind::Hp).current, 70.0f);
		expect_close("the head maximum grew", vitals.part(open::BodyPart::Head).maximum, 50.0f);
		expect_close("the head current was preserved", vitals.part(open::BodyPart::Head).current, 25.0f);

		expect_true("constitution base lowered", attributes.set_base(open::Attribute::Constitution, 1.0f));
		expect_true("recalculate accepted", vitals.recalculate(attributes, skills));
		expect_close("the hp maximum shrank", vitals.pool(open::PoolKind::Hp).maximum, 100.0f);
		expect_close("current hp is clamped to the smaller maximum",
					 vitals.pool(open::PoolKind::Hp).current, 70.0f);
		expect_true("endurance trained to two", (train(skills, open::BasicSkill::Endurance, 8u),
												  skills.basic(open::BasicSkill::Endurance).rank == 2u));
		expect_true("recalculate accepted", vitals.recalculate(attributes, skills));
		expect_close("stamina maximum grew with endurance",
					 vitals.pool(open::PoolKind::Stamina).maximum, 200.0f);
		expect_close("stamina current was not refilled",
					 vitals.pool(open::PoolKind::Stamina).current, 100.0f);
	}
	{
		// Break this catches: a shrinking maximum leaving current above the bar.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		expect_true("constitution base raised", attributes.set_base(open::Attribute::Constitution, 2.0f));
		open::Vitals vitals(attributes, skills);
		expect_close("hp starts at two hundred", vitals.pool(open::PoolKind::Hp).current, 200.0f);
		expect_true("left leg hit", vitals.damage(open::BodyPart::LeftLeg, 40.0f));
		expect_true("right leg hit", vitals.damage(open::BodyPart::RightLeg, 40.0f));
		expect_close("hp is one hundred and twenty", vitals.pool(open::PoolKind::Hp).current, 120.0f);
		expect_true("constitution base lowered", attributes.set_base(open::Attribute::Constitution, 1.0f));
		expect_true("recalculate accepted", vitals.recalculate(attributes, skills));
		expect_close("the hp maximum shrank", vitals.pool(open::PoolKind::Hp).maximum, 100.0f);
		expect_close("current hp is clamped to the smaller maximum",
					 vitals.pool(open::PoolKind::Hp).current, 100.0f);
		expect_close("the head maximum shrank", vitals.part(open::BodyPart::Head).maximum, 25.0f);
		expect_close("the head current is clamped too", vitals.part(open::BodyPart::Head).current, 25.0f);
		expect_close("a limb that was already below the new maximum is untouched",
					 vitals.part(open::BodyPart::LeftLeg).current, 40.0f);
		expect_true("clamping did not kill the character", vitals.alive());
	}
	{
		// Break this catches: recalculation reviving a killed character.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("head destroyed", vitals.damage(open::BodyPart::Head, 25.0f));
		expect_true("dead", !vitals.alive());
		expect_true("constitution base raised", attributes.set_base(open::Attribute::Constitution, 4.0f));
		expect_true("recalculate accepted", vitals.recalculate(attributes, skills));
		expect_close("the head maximum grew", vitals.part(open::BodyPart::Head).maximum, 100.0f);
		expect_close("the head stays at zero", vitals.part(open::BodyPart::Head).current, 0.0f);
		expect_true("still dead", !vitals.alive());
		expect_close("main hp was not revived", vitals.pool(open::PoolKind::Hp).current, 75.0f);
	}
	{
		// Break this catches: a maximum that overflows being committed anyway.
		open::Attributes attributes = basic_sheet();
		open::Skills skills;
		open::Vitals vitals(attributes, skills);
		expect_true("hp damaged", vitals.damage(open::BodyPart::Torso, 20.0f));
		expect_true("a huge constitution base is legal for attributes",
					attributes.set_base(open::Attribute::Constitution, 1.0e37f));
		expect_true("recalculate refuses an overflowing maximum", !vitals.recalculate(attributes, skills));
		expect_close("the hp maximum is unchanged", vitals.pool(open::PoolKind::Hp).maximum, 100.0f);
		expect_close("current hp is unchanged", vitals.pool(open::PoolKind::Hp).current, 80.0f);
		expect_close("the body maximum is unchanged", vitals.part(open::BodyPart::Torso).maximum, 50.0f);

		const open::Vitals created(attributes, skills);
		expect_true("a created vitals set stays finite", std::isfinite(created.pool(open::PoolKind::Hp).maximum));
		expect_true("its hp is finite", std::isfinite(created.pool(open::PoolKind::Hp).current));
		expect_true("a body part is finite", std::isfinite(created.part(open::BodyPart::LeftLeg).current));
		expect_true("the created character is alive", created.alive());
	}
	{
		// Break this catches: two characters sharing one set of bars.
		const open::Attributes attributes = basic_sheet();
		const open::Skills skills;
		open::Vitals first(attributes, skills);
		open::Vitals second(attributes, skills);
		first.damage(open::BodyPart::Head, 10.0f);
		expect_close("the other character is untouched", second.part(open::BodyPart::Head).current, 25.0f);
		expect_close("the other character's hp is untouched",
					 second.pool(open::PoolKind::Hp).current, 100.0f);
	}

	return test_summary();
}
