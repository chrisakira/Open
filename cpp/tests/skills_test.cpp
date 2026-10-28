#include "open/skills.hpp"

#include <optional>

#include "test_support.hpp"

// Task 3 tests. Break these catches: a rank that starts in the wrong place, an
// XP curve entry that is off by a factor, a rank-up that keeps its residual
// XP, a specialization that trains the parent instead of itself, a second
// specialization for one basic, or a failed use that still grants progress.

namespace {

constexpr unsigned kBasicCap = 100u;
constexpr unsigned kAdvancedCap = 200u;

// 8n for n = 1..99.
constexpr unsigned kBasicCapSuccesses = 39600u;
// 8*max(n,1) for n = 0..99 plus 40n for n = 100..199.
constexpr unsigned kAdvancedCapSuccesses = 637608u;

void train_basic(open::Skills &skills, open::BasicSkill basic, unsigned times) {
	for (unsigned i = 0; i < times; ++i) {
		expect_true("basic use recorded", skills.record_use(basic, std::nullopt, true));
	}
}

void train_advanced(open::Skills &skills, open::BasicSkill basic, open::AdvancedSkill option,
					unsigned times) {
	for (unsigned i = 0; i < times; ++i) {
		expect_true("advanced use recorded", skills.record_use(basic, option, true));
	}
}

}  // namespace

int main() {
	{
		// Break this catches: a basic that starts at 0, or shares one counter.
		const open::Skills skills;
		for (int i = 0; i < static_cast<int>(open::BasicSkill::Count); ++i) {
			const open::BasicSkill basic = static_cast<open::BasicSkill>(i);
			expect_true("every basic skill starts at rank one", skills.basic(basic).rank == 1u);
			expect_true("every basic skill starts with no residual xp", skills.basic(basic).xp == 0u);
			expect_true("no specialization is chosen yet", !skills.chosen(basic).has_value());
		}
		for (int i = 0; i < static_cast<int>(open::AdvancedSkill::Count); ++i) {
			const open::AdvancedSkill option = static_cast<open::AdvancedSkill>(i);
			expect_true("an unchosen option has no track", !skills.advanced(option).has_value());
		}
	}
	{
		// Break this catches: an off-by-one in the first rank cost, or residual XP
		// surviving a rank-up.
		open::Skills skills;
		for (int i = 0; i < 7; ++i) {
			skills.record_use(open::BasicSkill::Endurance, std::nullopt, true);
		}
		expect_true("seven uses have not raised endurance",
					skills.basic(open::BasicSkill::Endurance).rank == 1u);
		expect_true("seven uses leave seven residual xp",
					skills.basic(open::BasicSkill::Endurance).xp == 7u);
		skills.record_use(open::BasicSkill::Endurance, std::nullopt, true);
		expect_true("eight uses raise endurance once",
					skills.basic(open::BasicSkill::Endurance).rank == 2u);
		expect_true("rank-up consumes residual xp", skills.basic(open::BasicSkill::Endurance).xp == 0u);
	}
	{
		// Break this catches: other basics training along with endurance.
		const open::Skills skills;
		expect_true("one handed still one", skills.basic(open::BasicSkill::OneHandedWeapons).rank == 1u);
		expect_true("stealth still one", skills.basic(open::BasicSkill::Stealth).rank == 1u);
	}
	{
		// Break this catches: an XP curve entry drifting.
		expect_true("basic 1->2 costs eight", open::basic_xp_to_next(1u) == 8u);
		expect_true("basic 2->3 costs sixteen", open::basic_xp_to_next(2u) == 16u);
		expect_true("basic 50->51 costs 400", open::basic_xp_to_next(50u) == 400u);
		expect_true("basic 99->100 costs 792", open::basic_xp_to_next(99u) == 792u);
		expect_true("basic cap has nothing left to buy", open::basic_xp_to_next(kBasicCap) == 0u);

		expect_true("advanced 0->1 costs eight", open::advanced_xp_to_next(0u) == 8u);
		expect_true("advanced 1->2 costs eight", open::advanced_xp_to_next(1u) == 8u);
		expect_true("advanced 50->51 costs 400", open::advanced_xp_to_next(50u) == 400u);
		expect_true("advanced 99->100 costs 792", open::advanced_xp_to_next(99u) == 792u);
		expect_true("advanced 100->101 costs 4000", open::advanced_xp_to_next(100u) == 4000u);
		expect_true("advanced 199->200 costs 7960", open::advanced_xp_to_next(199u) == 7960u);
		expect_true("advanced cap has nothing left to buy", open::advanced_xp_to_next(kAdvancedCap) == 0u);
	}
	{
		// Break this catches: a rank range that lets a caller index or loop past
		// the cap.
		expect_throws_invalid("basic cost rejects rank zero", [] { open::basic_xp_to_next(0u); });
		expect_throws_invalid("basic cost rejects rank above the cap",
							  [] { open::basic_xp_to_next(kBasicCap + 1u); });
		expect_throws_invalid("advanced cost rejects a rank above the cap",
							  [] { open::advanced_xp_to_next(kAdvancedCap + 1u); });
	}
	{
		// Break this catches: a curve that is cheaper or dearer than the locked
		// total.
		open::Skills skills;
		train_basic(skills, open::BasicSkill::Endurance, kBasicCapSuccesses);
		expect_true("the locked basic total reaches the cap",
					skills.basic(open::BasicSkill::Endurance).rank == kBasicCap);
		expect_true("the cap leaves no residual xp", skills.basic(open::BasicSkill::Endurance).xp == 0u);
		expect_true("one basic can be capped while another stays fresh",
					skills.basic(open::BasicSkill::Stealth).rank == 1u);
	}
	{
		// Break this catches: XP piling up past the cap.
		open::Skills skills;
		train_basic(skills, open::BasicSkill::Endurance, kBasicCapSuccesses + 25u);
		expect_true("capped rank does not grow", skills.basic(open::BasicSkill::Endurance).rank == kBasicCap);
		expect_true("capped uses discard xp", skills.basic(open::BasicSkill::Endurance).xp == 0u);
		expect_true("a capped use is still a valid no-op",
					skills.record_use(open::BasicSkill::Endurance, std::nullopt, true));
	}
	{
		// Break this catches: advancing before the parent is frozen at 100.
		open::Skills skills;
		expect_true("advancing a fresh parent fails", !skills.advance(open::AdvancedSkill::Katana));
		train_basic(skills, open::BasicSkill::OneHandedWeapons, kBasicCapSuccesses - 1u);
		expect_true("the parent is not capped yet",
					skills.basic(open::BasicSkill::OneHandedWeapons).rank == kBasicCap - 1u);
		expect_true("advancing one use too early fails", !skills.advance(open::AdvancedSkill::Katana));
		expect_true("no choice was recorded by the failed advance",
					!skills.chosen(open::BasicSkill::OneHandedWeapons).has_value());
		expect_true("one more use caps the parent",
					skills.record_use(open::BasicSkill::OneHandedWeapons, std::nullopt, true));
		expect_true("the parent is now capped",
					skills.basic(open::BasicSkill::OneHandedWeapons).rank == kBasicCap);
		expect_true("advancing at the cap succeeds", skills.advance(open::AdvancedSkill::Katana));
		expect_true("the chosen option is remembered",
					skills.chosen(open::BasicSkill::OneHandedWeapons) ==
						std::optional<open::AdvancedSkill>(open::AdvancedSkill::Katana));
	}
	{
		// Break this catches: a specialization starting at 1 (or at the parent's
		// 100), and a sibling job that follows the specialization instead of the
		// frozen basic.
		open::Skills skills;
		train_basic(skills, open::BasicSkill::OneHandedWeapons, kBasicCapSuccesses);
		expect_true("katana chosen", skills.advance(open::AdvancedSkill::Katana));
		const std::optional<open::Training> katana = skills.advanced(open::AdvancedSkill::Katana);
		expect_true("the chosen option now has a track", katana.has_value());
		expect_true("the specialization starts at rank zero", katana->rank == 0u);
		expect_true("the specialization starts with no residual xp", katana->xp == 0u);
		expect_true("a sibling option has no track of its own",
					!skills.advanced(open::AdvancedSkill::Axe).has_value());

		expect_true("resolve follows the specialization",
					skills.resolve(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana).rank == 0u);
		expect_true("resolve falls back for a sibling job",
					skills.resolve(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Axe).rank == kBasicCap);
		expect_true("resolve falls back with no option",
					skills.resolve(open::BasicSkill::OneHandedWeapons, std::nullopt).rank == kBasicCap);

		expect_true("the frozen basic keeps its counter",
					skills.record_use(open::BasicSkill::OneHandedWeapons, std::nullopt, true));
		expect_true("the frozen basic does not grow",
					skills.basic(open::BasicSkill::OneHandedWeapons).rank == kBasicCap);
		expect_true("the frozen basic takes no residual xp",
					skills.basic(open::BasicSkill::OneHandedWeapons).xp == 0u);

		train_advanced(skills, open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana, 8u);
		expect_true("the specialization grows",
					skills.advanced(open::AdvancedSkill::Katana)->rank == 1u);
		expect_true("a sibling job still uses the frozen basic",
					skills.resolve(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Axe).rank == kBasicCap);
		expect_true("an untrained sibling stays at zero",
					!skills.advanced(open::AdvancedSkill::Sword).has_value());
	}
	{
		// Break this catches: a second specialization for one basic.
		open::Skills skills;
		train_basic(skills, open::BasicSkill::OneHandedWeapons, kBasicCapSuccesses);
		expect_true("katana chosen first", skills.advance(open::AdvancedSkill::Katana));
		expect_true("a second one-handed specialization is refused",
					!skills.advance(open::AdvancedSkill::Sword));
		expect_true("the first choice survives the refusal",
					skills.chosen(open::BasicSkill::OneHandedWeapons) ==
						std::optional<open::AdvancedSkill>(open::AdvancedSkill::Katana));
		expect_true("a sibling option is a valid fallback use",
					skills.record_use(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Sword, true));
		expect_true("the chosen track is unchanged by a fallback use",
					skills.advanced(open::AdvancedSkill::Katana)->rank == 0u);
		expect_true("the fallback use cannot grow the frozen basic",
					skills.basic(open::BasicSkill::OneHandedWeapons).rank == kBasicCap);
		expect_true("the fallback use leaves no residual xp on the frozen basic",
					skills.basic(open::BasicSkill::OneHandedWeapons).xp == 0u);
	}
	{
		// Break this catches: one global specialization lock, and the element tax.
		open::Skills skills;
		train_basic(skills, open::BasicSkill::OneHandedWeapons, kBasicCapSuccesses);
		train_basic(skills, open::BasicSkill::ElementalMagic, kBasicCapSuccesses);
		expect_true("one-handed specializes", skills.advance(open::AdvancedSkill::Katana));
		expect_true("a separate basic also specializes", skills.advance(open::AdvancedSkill::Fire));
		expect_true("fire excludes the other elements", !skills.advance(open::AdvancedSkill::Ice));
		expect_true("fire remains the element choice",
					skills.chosen(open::BasicSkill::ElementalMagic) ==
						std::optional<open::AdvancedSkill>(open::AdvancedSkill::Fire));
		expect_true("ice has no track", !skills.advanced(open::AdvancedSkill::Ice).has_value());
	}
	{
		// Break this catches: a basic that gained options the spec does not have.
		const open::Skills skills;
		expect_true("endurance has no chosen option",
					!skills.chosen(open::BasicSkill::Endurance).has_value());
		expect_true("lockpicking has no chosen option",
					!skills.chosen(open::BasicSkill::Lockpicking).has_value());
		for (int i = 0; i < static_cast<int>(open::AdvancedSkill::Count); ++i) {
			const open::AdvancedSkill option = static_cast<open::AdvancedSkill>(i);
			expect_true("no option belongs to endurance or lockpicking",
						open::advanced_rule(option).parent != open::BasicSkill::Endurance &&
							open::advanced_rule(option).parent != open::BasicSkill::Lockpicking);
		}
	}
	{
		// Break this catches: the advanced curve not resetting at rank 100, or a
		// track that outruns its cap. Both sides of rank 100 are pinned.
		open::Skills skills;
		train_basic(skills, open::BasicSkill::OneHandedWeapons, kBasicCapSuccesses);
		expect_true("katana chosen", skills.advance(open::AdvancedSkill::Katana));
		train_advanced(skills, open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana, 39607u);
		expect_true("just short of rank 100 the cheap curve is still running",
					skills.advanced(open::AdvancedSkill::Katana)->rank == 99u);
		expect_true("just short of rank 100 the residual is one below the cost",
					skills.advanced(open::AdvancedSkill::Katana)->xp == 791u);
		train_advanced(skills, open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana, 1u);
		expect_true("rank 100 is reached",
					skills.advanced(open::AdvancedSkill::Katana)->rank == 100u);
		expect_true("rank 100 leaves no residual", skills.advanced(open::AdvancedSkill::Katana)->xp == 0u);
		train_advanced(skills, open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana, 3999u);
		expect_true("the expensive side has not paid for a point yet",
					skills.advanced(open::AdvancedSkill::Katana)->rank == 100u);
		expect_true("the expensive side banked its residual",
					skills.advanced(open::AdvancedSkill::Katana)->xp == 3999u);
		train_advanced(skills, open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana, 1u);
		expect_true("rank 101 costs 4000", skills.advanced(open::AdvancedSkill::Katana)->rank == 101u);
		train_advanced(skills, open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana,
					   kAdvancedCapSuccesses - 39608u - 4000u);
		expect_true("the locked advanced total reaches the cap",
					skills.advanced(open::AdvancedSkill::Katana)->rank == kAdvancedCap);
		expect_true("the advanced cap leaves no residual xp",
					skills.advanced(open::AdvancedSkill::Katana)->xp == 0u);
		expect_true("a capped advanced use is a valid no-op",
					skills.record_use(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Katana, true));
		expect_true("the advanced cap does not grow",
					skills.advanced(open::AdvancedSkill::Katana)->rank == kAdvancedCap);
	}
	{
		// Break this catches: a failed or cancelled action granting progress.
		open::Skills skills;
		for (unsigned i = 0; i < 40u; ++i) {
			expect_true("a failed use is accepted but does nothing",
						skills.record_use(open::BasicSkill::Smithing, std::nullopt, false));
		}
		expect_true("failed uses grant no rank", skills.basic(open::BasicSkill::Smithing).rank == 1u);
		expect_true("failed uses grant no xp", skills.basic(open::BasicSkill::Smithing).xp == 0u);

		train_basic(skills, open::BasicSkill::Smithing, 7u);
		expect_true("seven successes stick", skills.basic(open::BasicSkill::Smithing).xp == 7u);
		skills.record_use(open::BasicSkill::Smithing, std::nullopt, false);
		expect_true("a failure after progress does not remove it",
					skills.basic(open::BasicSkill::Smithing).xp == 7u);
		skills.record_use(open::BasicSkill::Smithing, std::nullopt, false);
		expect_true("a failure after progress does not add to it",
					skills.basic(open::BasicSkill::Smithing).xp == 7u);

		train_basic(skills, open::BasicSkill::Smithing, kBasicCapSuccesses);
		expect_true("smithing specialized", skills.advance(open::AdvancedSkill::Weaponsmith));
		train_advanced(skills, open::BasicSkill::Smithing, open::AdvancedSkill::Weaponsmith, 7u);
		expect_true("advanced progress exists before a failure",
					skills.advanced(open::AdvancedSkill::Weaponsmith)->xp == 7u);
		expect_true("a failed advanced use is accepted",
					skills.record_use(open::BasicSkill::Smithing, open::AdvancedSkill::Weaponsmith, false));
		expect_true("a failed advanced use keeps progress",
					skills.advanced(open::AdvancedSkill::Weaponsmith)->xp == 7u);
		expect_true("a failed advanced use grants nothing",
					skills.advanced(open::AdvancedSkill::Weaponsmith)->rank == 0u);
	}
	{
		// Break this catches: an option from another parent being treated as a
		// fallback instead of a caller error.
		open::Skills skills;
		expect_true("an option of another parent is refused by record_use",
					!skills.record_use(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Bow, true));
		expect_true("the refusal left the basic untouched",
					skills.basic(open::BasicSkill::OneHandedWeapons).rank == 1u);
		expect_true("the refusal left the other basic untouched",
					skills.basic(open::BasicSkill::Archery).rank == 1u);
		expect_throws_invalid("resolve rejects an option of another parent", [&skills] {
			skills.resolve(open::BasicSkill::OneHandedWeapons, open::AdvancedSkill::Bow);
		});
		expect_throws_invalid("resolve rejects a bad option id", [&skills] {
			skills.resolve(open::BasicSkill::OneHandedWeapons, static_cast<open::AdvancedSkill>(72));
		});
		expect_true("advance rejects a bad option id", !skills.advance(static_cast<open::AdvancedSkill>(72)));
		expect_true("advance rejects a negative option id",
					!skills.advance(static_cast<open::AdvancedSkill>(-1)));
		expect_true("record_use rejects a bad basic id",
					!skills.record_use(static_cast<open::BasicSkill>(35), std::nullopt, true));
		expect_true("record_use rejects a negative basic id",
					!skills.record_use(static_cast<open::BasicSkill>(-1), std::nullopt, false));
		expect_throws_invalid("basic rejects a bad id",
							  [&skills] { skills.basic(static_cast<open::BasicSkill>(35)); });
		expect_throws_invalid("chosen rejects a bad id",
							  [&skills] { skills.chosen(static_cast<open::BasicSkill>(-1)); });
		expect_throws_invalid("advanced rejects a bad id",
							  [&skills] { skills.advanced(static_cast<open::AdvancedSkill>(72)); });
		expect_throws_invalid("advanced rejects a negative id",
							  [&skills] { skills.advanced(static_cast<open::AdvancedSkill>(-1)); });
	}
	{
		// Break this catches: characters sharing one skill ledger.
		open::Skills first;
		const open::Skills second;
		train_basic(first, open::BasicSkill::Endurance, 20u);
		expect_true("another character is unaffected", second.basic(open::BasicSkill::Endurance).rank == 1u);
		expect_true("another character has no residual xp", second.basic(open::BasicSkill::Endurance).xp == 0u);
	}

	{
		// Break this catches: a title unlocking at the wrong count, decaying, or
		// keeping a counter that runs past its threshold.
		for (int t = 0; t < static_cast<int>(open::Title::Count); ++t) {
			const open::Title title = static_cast<open::Title>(t);
			const unsigned threshold = open::title_rule(title).threshold;
			open::Skills skills;
			expect_true("no title at zero", !skills.has_title(title));
			expect_true("no count at zero", skills.title_count(title) == 0u);
			for (unsigned i = 0; i + 1u < threshold; ++i) {
				expect_true("title progress accepted", skills.record_title_progress(title, true));
			}
			expect_true("title is absent one short of its threshold", !skills.has_title(title));
			expect_true("counter sits one short", skills.title_count(title) == threshold - 1u);
			expect_true("the final achievement is accepted", skills.record_title_progress(title, true));
			expect_true("title is present at its threshold", skills.has_title(title));
			expect_true("counter reached its threshold", skills.title_count(title) == threshold);
			for (unsigned i = 0; i < 3u; ++i) {
				expect_true("extra progress is accepted", skills.record_title_progress(title, true));
			}
			expect_true("an unlocked title never drops", skills.has_title(title));
			expect_true("the counter saturates at the threshold", skills.title_count(title) == threshold);
		}
	}
	{
		// Break this catches: a failed achievement counting toward a title.
		open::Skills skills;
		for (unsigned i = 0; i < 400u; ++i) {
			expect_true("a failed achievement is accepted",
						skills.record_title_progress(open::Title::Duelist, false));
		}
		expect_true("failed achievements grant no count", skills.title_count(open::Title::Duelist) == 0u);
		expect_true("failed achievements grant no title", !skills.has_title(open::Title::Duelist));
		expect_true("title progress grants no skill xp",
					skills.basic(open::BasicSkill::OneHandedWeapons).rank == 1u);
		expect_true("title progress leaves no skill xp residual",
					skills.basic(open::BasicSkill::OneHandedWeapons).xp == 0u);
	}
	{
		// Break this catches: a title bonus applied to the wrong skill, or twice.
		for (int t = 0; t < static_cast<int>(open::Title::Count); ++t) {
			const open::Title title = static_cast<open::Title>(t);
			const open::TitleRule &rule = open::title_rule(title);
			open::Skills skills;
			for (unsigned i = 0; i < rule.threshold; ++i) {
				skills.record_title_progress(title, true);
			}
			expect_true("the title is held", skills.has_title(title));
			for (int b = 0; b < static_cast<int>(open::BasicSkill::Count); ++b) {
				const open::BasicSkill basic = static_cast<open::BasicSkill>(b);
				const bool named = rule.skill.has_value() && rule.skill.value() == basic;
				if (named) {
					expect_close("a held title multiplies the skill it names",
								 skills.title_multiplier(basic), 1.1f);
				} else {
					expect_true("a title leaves every other skill alone",
								skills.title_multiplier(basic) == 1.0f);
				}
			}
		}
	}
	{
		// Break this catches: Dragonslayer becoming a second skill bonus, or a
		// specialist bonus that is not gated on the target.
		open::Skills skills;
		expect_true("an unearned dragonslayer does not boost the target",
					skills.target_multiplier(true) == 1.0f);
		expect_true("an unearned dragonslayer does not boost a normal target",
					skills.target_multiplier(false) == 1.0f);
		for (unsigned i = 0; i < open::title_rule(open::Title::Dragonslayer).threshold; ++i) {
			expect_true("dragon kill recorded",
						skills.record_title_progress(open::Title::Dragonslayer, true));
		}
		expect_true("dragonslayer is held", skills.has_title(open::Title::Dragonslayer));
		expect_close("dragonslayer boosts its target", skills.target_multiplier(true), 1.25f);
		expect_true("dragonslayer leaves other targets alone", skills.target_multiplier(false) == 1.0f);
		for (int b = 0; b < static_cast<int>(open::BasicSkill::Count); ++b) {
			expect_true("dragonslayer is not a skill-wide bonus",
						skills.title_multiplier(static_cast<open::BasicSkill>(b)) == 1.0f);
		}
	}
	{
		// Break this catches: several held titles not all applying through the
		// same query, and a dragon kill double-reporting.
		open::Skills skills;
		for (unsigned i = 0; i < open::title_rule(open::Title::Duelist).threshold; ++i) {
			skills.record_title_progress(open::Title::Duelist, true);
		}
		for (unsigned i = 0; i < open::title_rule(open::Title::Dragonslayer).threshold; ++i) {
			skills.record_title_progress(open::Title::Dragonslayer, true);
		}
		expect_close("the one-handed title still applies alongside dragonslayer",
					 skills.title_multiplier(open::BasicSkill::OneHandedWeapons), 1.1f);
		expect_close("and the target bonus is separate", skills.target_multiplier(true), 1.25f);
		expect_true("the two reports did not train anything else",
					skills.basic(open::BasicSkill::OneHandedWeapons).rank == 1u);
	}
	{
		// Break this catches: a bad title id reaching the counter array.
		open::Skills skills;
		expect_true("a bad title id is refused",
					!skills.record_title_progress(static_cast<open::Title>(14), true));
		expect_true("a negative title id is refused",
					!skills.record_title_progress(static_cast<open::Title>(-1), true));
		expect_throws_invalid("title_count rejects a bad id",
							  [&skills] { skills.title_count(static_cast<open::Title>(14)); });
		expect_throws_invalid("has_title rejects a bad id",
							  [&skills] { skills.has_title(static_cast<open::Title>(-1)); });
		for (int t = 0; t < static_cast<int>(open::Title::Count); ++t) {
			expect_true("a refused id mutated nothing",
						skills.title_count(static_cast<open::Title>(t)) == 0u);
		}
		expect_true("a refused title is not held", !skills.has_title(open::Title::Duelist));
		expect_true("a refused title does not boost the target", skills.target_multiplier(true) == 1.0f);
	}

	return test_summary();
}
