/*	EQEMu: Everquest Server Emulator
Copyright (C) 2001-2002 EQEMu Development Team (http://eqemulator.net)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY except by those people which sell it, which
are required to give you total support for your newly bought product;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "../common/global_define.h"
#include "../common/eq_constants.h"
#include "../common/eq_packet_structs.h"
#include "../common/rulesys.h"
#include "../common/skills.h"
#include "../common/spdat.h"
#include "../common/string_util.h"
#include "../common/data_verification.h"
#include "../common/misc_functions.h"
#include "queryserv.h"
#include "quest_parser_collection.h"
#include "string_ids.h"
#include "water_map.h"
#include "worldserver.h"
#include "zone.h"
#include "lua_parser.h"
#include "fastmath.h"
#include "mob.h"
#include "npc.h"


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <boost/concept_check.hpp>

extern QueryServ* QServ;
extern WorldServer worldserver;
extern FastMath g_Math;

#ifdef _WINDOWS
#define snprintf	_snprintf
#define strncasecmp	_strnicmp
#define strcasecmp	_stricmp
#endif

extern EntityList entity_list;
extern Zone* zone;

//SYNC WITH: tune.cpp, mob.h TuneAttackAnimation
EQ::skills::SkillType Mob::AttackAnimation(int Hand, const EQ::ItemInstance* weapon, EQ::skills::SkillType skillinuse)
{
	// Determine animation
	int type = 0;
	if (weapon && weapon->IsClassCommon()) {
		const EQ::ItemData* item = weapon->GetItem();

		Log(Logs::Detail, Logs::Attack, "Weapon skill : %i", item->ItemType);

		switch (item->ItemType) {
		case EQ::item::ItemType1HSlash: // 1H Slashing
			skillinuse = EQ::skills::Skill1HSlashing;
			type = anim1HWeapon;
			break;
		case EQ::item::ItemType2HSlash: // 2H Slashing
			skillinuse = EQ::skills::Skill2HSlashing;
			type = anim2HSlashing;
			break;
		case EQ::item::ItemType1HPiercing: // Piercing
			skillinuse = EQ::skills::Skill1HPiercing;
			type = anim1HPiercing;
			break;
		case EQ::item::ItemType1HBlunt: // 1H Blunt
			skillinuse = EQ::skills::Skill1HBlunt;
			type = anim1HWeapon;
			break;
		case EQ::item::ItemType2HBlunt: // 2H Blunt
			skillinuse = EQ::skills::Skill2HBlunt;
			type = RuleB(Combat, Classic2HBAnimation) ? anim2HWeapon : anim2HSlashing;
			break;
		case EQ::item::ItemType2HPiercing: // 2H Piercing
			if (IsClient() && CastToClient()->ClientVersion() < EQ::versions::ClientVersion::RoF2)
				skillinuse = EQ::skills::Skill1HPiercing;
			else
				skillinuse = EQ::skills::Skill2HPiercing;
			type = anim2HWeapon;
			break;
		case EQ::item::ItemTypeMartial:
			skillinuse = EQ::skills::SkillHandtoHand;
			type = animHand2Hand;
			break;
		default:
			skillinuse = EQ::skills::SkillHandtoHand;
			type = animHand2Hand;
			break;
		}// switch
	}
	else if (IsNPC()) {
		switch (skillinuse) {
		case EQ::skills::Skill1HSlashing: // 1H Slashing
			type = anim1HWeapon;
			break;
		case EQ::skills::Skill2HSlashing: // 2H Slashing
			type = anim2HSlashing;
			break;
		case EQ::skills::Skill1HPiercing: // Piercing
			type = anim1HPiercing;
			break;
		case EQ::skills::Skill1HBlunt: // 1H Blunt
			type = anim1HWeapon;
			break;
		case EQ::skills::Skill2HBlunt: // 2H Blunt
			type = anim2HSlashing; //anim2HWeapon
			break;
		case EQ::skills::Skill2HPiercing: // 2H Piercing
			type = anim2HWeapon;
			break;
		case EQ::skills::SkillHandtoHand:
			type = animHand2Hand;
			break;
		default:
			type = animHand2Hand;
			break;
		}// switch
	}
	else {
		skillinuse = EQ::skills::SkillHandtoHand;
		type = animHand2Hand;
	}

	// If we're attacking with the secondary hand, play the dual wield anim
	if (Hand == EQ::invslot::slotSecondary) {// DW anim
		type = animDualWield;

		//allow animation chance to fire to be similar to your dw chance
		if (GetDualWieldingSameDelayWeapons() == 2) {
			SetDualWieldingSameDelayWeapons(3);
		}
	}

	//If both weapons have same delay this allows a chance for DW animation
	if (GetDualWieldingSameDelayWeapons() && Hand == EQ::invslot::slotPrimary) {

		if (GetDualWieldingSameDelayWeapons() == 3 && zone->random.Roll(50)) {
			type = animDualWield;
			SetDualWieldingSameDelayWeapons(2);//Don't roll again till you do another dw attack.
		}
		SetDualWieldingSameDelayWeapons(2);//Ensures first attack is always primary.
	}

	DoAnim(type, 0, false);

	return skillinuse;
}


int Mob::GetOffenseByHand(uint16 hand)
{
	if (!IsNPC() && !IsClient())
		return 0;

	const EQ::ItemData* weapon = nullptr;

	if (hand != EQ::invslot::slotSecondary)
		hand = EQ::invslot::slotPrimary;

	if (IsNPC())
	{
		uint32 handItem = CastToNPC()->GetEquippedItemFromTextureSlot(hand == EQ::invslot::slotSecondary ? EQ::textures::weaponSecondary : EQ::textures::weaponPrimary);
		if (handItem)
			weapon = database.GetItem(handItem);
	}
	else if (IsClient())
	{
		EQ::ItemInstance* weaponInst = CastToClient()->GetInv().GetItem(hand);
		if (weaponInst && weaponInst->IsType(EQ::item::ItemClassCommon))
			weapon = weaponInst->GetItem();
	}

	if (weapon)
	{
		return GetOffense(static_cast<EQ::skills::SkillType>(GetSkillByItemType(weapon->ItemType)));
	}
	else
	{
		return GetOffense(EQ::skills::SkillHandtoHand);
	}
}


int Mob::GetToHitByHand(uint16 hand)
{
	if (!IsNPC() && !IsClient())
		return 0;

	if (IsNPC())
		return GetToHit(EQ::skills::SkillHandtoHand);

	EQ::ItemInstance* weapon = nullptr;

	if (IsClient())
	{
		if (hand == EQ::invslot::slotSecondary)
			weapon = CastToClient()->GetInv().GetItem(EQ::invslot::slotSecondary);
		else
			weapon = CastToClient()->GetInv().GetItem(EQ::invslot::slotPrimary);
	}
	if (weapon && weapon->IsType(EQ::item::ItemClassCommon))
	{
		return GetToHit((GetSkillByItemType(weapon->GetItem()->ItemType)));
	}
	else
	{
		return GetToHit(EQ::skills::SkillHandtoHand);
	}
}

// return -1 in cases that always hit
int Mob::GetToHit(EQ::skills::SkillType skill)
{
	int accuracy = 0;
	int toHit = 7 + GetSkill(EQ::skills::SkillOffense) + GetSkill(skill);

	if (IsClient())
	{
		// 184 Accuracy % aka SE_HitChance -- percentage increase
		auto hit_bonus = itembonuses.HitChanceEffect[EQ::skills::HIGHEST_SKILL + 1] +
			aabonuses.HitChanceEffect[EQ::skills::HIGHEST_SKILL + 1] +
			spellbonuses.HitChanceEffect[EQ::skills::HIGHEST_SKILL + 1] +
			itembonuses.HitChanceEffect[skill] +
			aabonuses.HitChanceEffect[skill] +
			spellbonuses.HitChanceEffect[skill];

		// taken from a client decompile (credit: demonstar)
		int drunkValue = CastToClient()->m_pp.intoxication / 2;
		if (drunkValue > 20)
		{
			int drunkReduction = 110 - drunkValue;
			if (drunkReduction > 100)
				drunkReduction = 100;
			toHit = toHit * drunkReduction / 100;
		}
		else if (GetClass() == WARRIOR && CastToClient()->IsBerserk())
		{
			toHit += 2 * GetLevel() / 5;
		}

	}
	else
	{
		accuracy = CastToNPC()->GetAccuracyRating();	// database value
		if (GetLevel() < 3)
			accuracy += 2;		// level 1 and 2 NPCs parsed a few points higher than expected
	}

	toHit += accuracy;
	return toHit;
}

// called when a mob is attacked, does the checks to see if it's a hit
// and does other mitigation checks. 'this' is the mob being attacked.
// SYNC WITH : tune.cpp, mob.h TuneCheckHitChance()
bool Mob::CheckHitChance(Mob* other, DamageHitInfo &hit)
{
#ifdef LUA_EQEMU
	bool lua_ret = false;
	bool ignoreDefault = false;
	lua_ret = LuaParser::Instance()->CheckHitChance(this, other, hit, ignoreDefault);

	if(ignoreDefault) {
		return lua_ret;
	}
#endif

	Mob *attacker = other;
	Mob *defender = this;
	Log(Logs::Detail, Logs::Attack, "CheckHitChance(%s) attacked by %s", defender->GetName(), attacker->GetName());

	if (defender->IsClient() && defender->CastToClient()->IsSitting())
		return true;

	auto avoidance = defender->GetAvoidance();
	if (avoidance == -1) // some sort of auto avoid disc
		return false;

	auto accuracy = hit.tohit;
	if (accuracy == -1)
		return true;

	int percentMod = 0;

	// Hit Chance percent modifier
		// Disciplines: Evasive, Precision, Deadeye, Trueshot, Charge
	percentMod = attacker->itembonuses.HitChanceEffect[hit.skill] +
		attacker->spellbonuses.HitChanceEffect[hit.skill] +
		attacker->aabonuses.HitChanceEffect[hit.skill] +
		attacker->itembonuses.HitChanceEffect[EQ::skills::HIGHEST_SKILL + 1] +
		attacker->spellbonuses.HitChanceEffect[EQ::skills::HIGHEST_SKILL + 1] +
		attacker->aabonuses.HitChanceEffect[EQ::skills::HIGHEST_SKILL + 1];




	// Avoidance chance percent modifier
	// Disciplines: Evasive, Precision, Voiddance, Fortitude
	percentMod -= (defender->spellbonuses.AvoidMeleeChanceEffect + defender->itembonuses.AvoidMeleeChanceEffect);

	if (percentMod != 0)
	{
		Log(Logs::Detail, Logs::Attack, "Modified chance to hit: %i%%", percentMod);

		if (percentMod > 0)
		{
			if (zone->random.Roll(percentMod))
			{
				Log(Logs::Detail, Logs::Attack, "Modified Hit");
				return true;
			}
		}
		else
		{
			if (zone->random.Roll(-percentMod))
			{
				Log(Logs::Detail, Logs::Attack, "Modified Miss");
				return false;
			}
		}
	}

	// This produces precise output.  Don't change this unless you have Sony's actual code
	double hitChance;
	hit.tohit += 10;
	avoidance += 10;

	if (hit.tohit * 1.21 > avoidance)
	{
		hitChance = 1.0 - avoidance / (hit.tohit * 1.21 * 2.0);
	}
	else
	{
		hitChance = hit.tohit * 1.21 / (avoidance * 2.0);
	}

	if (zone->random.Real(0.0, 1.0) < hitChance)
	{
		Log(Logs::Detail, Logs::Attack, "Hit;  Hit chance was %0.1f%%", hitChance * 100);
		return true;
	}

	if (IsClient() && attacker->IsNPC())
		CastToClient()->CheckIncreaseSkill(EQ::skills::SkillDefense, attacker);

	Log(Logs::Detail, Logs::Attack, "Miss;  Hit chance was %0.1f%%", hitChance * 100);
	return false;
}

bool Mob::AvoidDamage(Mob *other, DamageHitInfo &hit, bool bAvoidRiposte)
{
#ifdef LUA_EQEMU
	bool lua_ret = false;
	bool ignoreDefault = false;
	lua_ret = LuaParser::Instance()->AvoidDamage(this, other, hit, ignoreDefault);

	if (ignoreDefault) {
		return lua_ret;
	}
#endif

	/* called when a mob is attacked, does the checks to see if it's a hit
	* and does other mitigation checks. 'this' is the mob being attacked.
	*
	* special return values:
	* -1 - block
	* -2 - parry
	* -3 - riposte
	* -4 - dodge
	*
	*/

	/* Order according to current (SoF+?) dev quotes:
	* https://forums.daybreakgames.com/eq/index.php?threads/test-update-06-10-15.223510/page-2#post-3261772
	* https://forums.daybreakgames.com/eq/index.php?threads/test-update-06-10-15.223510/page-2#post-3268227
	* Riposte 50, hDEX, must have weapon/fists, doesn't work on archery/throwing
	* Block 25, hDEX, works on archery/throwing, behind block done here if back to attacker base1 is chance
	* Parry 45, hDEX, doesn't work on throwing/archery, must be facing target
	* Dodge 45, hAGI, works on archery/throwing, monks can dodge attacks from behind
	* Shield Block, rand base1
	* Staff Block, rand base1
	*    regular strike through
	*    avoiding the attack (CheckHitChance)
	* As soon as one succeeds, none of the rest are checked
	*
	* Formula (all int math)
	* (posted for parry, assume rest at the same)
	* Chance = (((SKILL + 100) + [((SKILL+100) * SPA(175).Base1) / 100]) / 45) + [(hDex / 25) - min([hDex / 25], hStrikethrough)].
	* hStrikethrough is a mob stat that was added to counter the bonuses of heroic stats
	* Number rolled against 100, if the chance is greater than 100 it happens 100% of time
	*
	* Things with 10k accuracy mods can be avoided with these skills qq
	*/
	Mob *attacker = other;
	Mob *defender = this;

	bool InFront = !attacker->BehindMob(this, attacker->GetX(), attacker->GetY());

	/*
	This special ability adds a negative modifer to the defenders riposte/block/parry/chance
	therefore reducing the defenders chance to successfully avoid the melee attack. At present
	time this is the only way to fine tune counter these mods on players. This may
	ultimately end up being more useful as fields in npc_types.
	*/

	int counter_all     = 0;
	int counter_riposte = 0;
	int counter_block   = 0;
	int counter_parry   = 0;
	int counter_dodge   = 0;

	if (attacker->GetSpecialAbility(COUNTER_AVOID_DAMAGE)) {
		counter_all     = attacker->GetSpecialAbilityParam(COUNTER_AVOID_DAMAGE, 0);
		counter_riposte = attacker->GetSpecialAbilityParam(COUNTER_AVOID_DAMAGE, 1);
		counter_block   = attacker->GetSpecialAbilityParam(COUNTER_AVOID_DAMAGE, 2);
		counter_parry   = attacker->GetSpecialAbilityParam(COUNTER_AVOID_DAMAGE, 3);
		counter_dodge   = attacker->GetSpecialAbilityParam(COUNTER_AVOID_DAMAGE, 4);
	}

	int modify_all     = 0;
	int modify_riposte = 0;
	int modify_block   = 0;
	int modify_parry   = 0;
	int modify_dodge   = 0;

	if (GetSpecialAbility(MODIFY_AVOID_DAMAGE)) {
		modify_all     = GetSpecialAbilityParam(MODIFY_AVOID_DAMAGE, 0);
		modify_riposte = GetSpecialAbilityParam(MODIFY_AVOID_DAMAGE, 1);
		modify_block   = GetSpecialAbilityParam(MODIFY_AVOID_DAMAGE, 2);
		modify_parry   = GetSpecialAbilityParam(MODIFY_AVOID_DAMAGE, 3);
		modify_dodge   = GetSpecialAbilityParam(MODIFY_AVOID_DAMAGE, 4);
	}

	// riposte -- it may seem crazy, but if the attacker has SPA 173 on them, they are immune to Ripo
	bool ImmuneRipo = false;
	if (!RuleB(Combat, UseLiveRiposteMechanics)) {
		ImmuneRipo = attacker->aabonuses.RiposteChance || attacker->spellbonuses.RiposteChance || attacker->itembonuses.RiposteChance || attacker->IsEnraged();
	}
	/*
		Live Riposte Mechanics (~Kayen updated 1/22)
		-Ripostes can not trigger another riposte. (Ie. Riposte from defender can't then trigger the attacker to riposte)
		-Ripostes can not be 'avoided', only hit or miss.
		-Attacker with SPA 173 is not immune to riposte. The defender can riposte against the attackers melee hits.

		Legacy Riposte Mechanics
		-Ripostes can trigger another riposte
		-Attacker with SPA 173 is immune to riposte
		-Attacker that is enraged is immune to riposte
	*/

	// block
	if (GetSkill(EQ::skills::SkillBlock))
	{
		if (IsClient())
			CastToClient()->CheckIncreaseSkill(EQ::skills::SkillBlock, attacker);

		// check auto discs ... I guess aa/items too :P
		if (spellbonuses.IncreaseBlockChance == 10000 || aabonuses.IncreaseBlockChance == 10000 ||
			itembonuses.IncreaseBlockChance == 10000) {
			hit.damage_done = DMG_BLOCKED;
			return true;
		}
		int chance = GetSkill(EQ::skills::SkillBlock) + 100;
		chance += (chance * (aabonuses.IncreaseBlockChance + spellbonuses.IncreaseBlockChance + itembonuses.IncreaseBlockChance)) / 100;
		chance /= 25;

		if (zone->random.Roll(chance)) {
			hit.damage_done = DMG_BLOCKED;
			return true;
		}
	}

	// parry
	if (GetSkill(EQ::skills::SkillParry) && InFront)
	{
		if (IsClient())
			CastToClient()->CheckIncreaseSkill(EQ::skills::SkillParry, attacker);

		// check auto discs ... I guess aa/items too :P
		if (spellbonuses.ParryChance == 10000 || aabonuses.ParryChance == 10000 || itembonuses.ParryChance == 10000) {
			hit.damage_done = -2;
			return true;
		}
		int chance = GetSkill(EQ::skills::SkillParry) + 100;
		chance += (chance * (aabonuses.ParryChance + spellbonuses.ParryChance + itembonuses.ParryChance)) / 100;
		chance /= 50;		// this is 45 in modern EQ.  Old EQ logs parsed to a lower parry rate, so raising this

		if (zone->random.Roll(chance)) {
			hit.damage_done = -2;
			return true;
		}
	}

	// Need to check if we have something in MainHand to actually attack with (or fists)
	if (hit.hand != EQ::invslot::slotRange && (CanThisClassRiposte() || IsEnraged()) && InFront && !ImmuneRipo && !bAvoidRiposte) {
		bool cannotRiposte = false;

		if (IsClient())
		{
			EQ::ItemInstance* weapon = nullptr;
			weapon = CastToClient()->GetInv().GetItem(EQ::invslot::slotPrimary);

			if (weapon != nullptr && !weapon->IsWeapon())
			{
				cannotRiposte = true;
			}
			else
			{
				CastToClient()->CheckIncreaseSkill(EQ::skills::SkillRiposte, attacker);
			}
		}

		// riposting ripostes is possible, but client attacks become unripable while under a rip disc
		if (attacker->IsEnraged() ||
			(attacker->IsClient() && (attacker->aabonuses.RiposteChance + attacker->spellbonuses.RiposteChance + attacker->itembonuses.RiposteChance) >= 10000)
			)
			cannotRiposte = true;

		if (!cannotRiposte)
		{
			if (IsEnraged() || spellbonuses.RiposteChance == 10000 || aabonuses.RiposteChance == 10000 || itembonuses.RiposteChance == 10000)
			{
				hit.damage_done = DMG_RIPOSTED;
				return true;
			}

			int chance = GetSkill(EQ::skills::SkillRiposte) + 100;
			chance += (chance * (aabonuses.RiposteChance + spellbonuses.RiposteChance + itembonuses.RiposteChance)) / 100;
			chance /= 55;		// this is 50 in modern EQ.  Old EQ logs parsed to a lower rate, so raising this

			if (chance > 0 && zone->random.Roll(chance)) { // could be <0 from offhand stuff
				hit.damage_done = DMG_RIPOSTED;
				return true;
			}
		}
	}

	// dodge
	if (GetSkill(EQ::skills::SkillDodge) && InFront)
	{
		if (IsClient())
			CastToClient()->CheckIncreaseSkill(EQ::skills::SkillDodge, attacker);

		// check auto discs ... I guess aa/items too :P
		if (spellbonuses.DodgeChance == 10000 || aabonuses.DodgeChance == 10000 || itembonuses.DodgeChance == 10000) {
			hit.damage_done = DMG_DODGED;
			return true;
		}
		int chance = GetSkill(EQ::skills::SkillDodge) + 100;
		chance += (chance * (aabonuses.DodgeChance + spellbonuses.DodgeChance + itembonuses.DodgeChance)) / 100;
		chance /= 45;

		if (zone->random.Roll(chance)) {
			hit.damage_done = DMG_DODGED;
			return true;
		}
	}

	return false;
}

int Mob::GetACSoftcap()
{
	// from test server Resources/ACMitigation.txt
	static int war_softcaps[] = {
		312, 314, 316, 318, 320, 322, 324, 326, 328, 330, 332, 334, 336, 338, 340, 342, 344, 346, 348, 350, 352,
		354, 356, 358, 360, 362, 364, 366, 368, 370, 372, 374, 376, 378, 380, 382, 384, 386, 388, 390, 392, 394,
		396, 398, 400, 402, 404, 406, 408, 410, 412, 414, 416, 418, 420, 422, 424, 426, 428, 430, 432, 434, 436,
		438, 440, 442, 444, 446, 448, 450, 452, 454, 456, 458, 460, 462, 464, 466, 468, 470, 472, 474, 476, 478,
		480, 482, 484, 486, 488, 490, 492, 494, 496, 498, 500, 502, 504, 506, 508, 510, 512, 514, 516, 518, 520
	};

	static int clrbrdmnk_softcaps[] = {
		274, 276, 278, 278, 280, 282, 284, 286, 288, 290, 292, 292, 294, 296, 298, 300, 302, 304, 306, 308, 308,
		310, 312, 314, 316, 318, 320, 322, 322, 324, 326, 328, 330, 332, 334, 336, 336, 338, 340, 342, 344, 346,
		348, 350, 352, 352, 354, 356, 358, 360, 362, 364, 366, 366, 368, 370, 372, 374, 376, 378, 380, 380, 382,
		384, 386, 388, 390, 392, 394, 396, 396, 398, 400, 402, 404, 406, 408, 410, 410, 412, 414, 416, 418, 420,
		422, 424, 424, 426, 428, 430, 432, 434, 436, 438, 440, 440, 442, 444, 446, 448, 450, 452, 454, 454, 456
	};

	static int palshd_softcaps[] = {
		298, 300, 302, 304, 306, 308, 310, 312, 314, 316, 318, 320, 322, 324, 326, 328, 330, 332, 334, 336, 336,
		338, 340, 342, 344, 346, 348, 350, 352, 354, 356, 358, 360, 362, 364, 366, 368, 370, 372, 374, 376, 378,
		380, 382, 384, 384, 386, 388, 390, 392, 394, 396, 398, 400, 402, 404, 406, 408, 410, 412, 414, 416, 418,
		420, 422, 424, 426, 428, 430, 432, 432, 434, 436, 438, 440, 442, 444, 446, 448, 450, 452, 454, 456, 458,
		460, 462, 464, 466, 468, 470, 472, 474, 476, 478, 480, 480, 482, 484, 486, 488, 490, 492, 494, 496, 498
	};

	static int rng_softcaps[] = {
		286, 288, 290, 292, 294, 296, 298, 298, 300, 302, 304, 306, 308, 310, 312, 314, 316, 318, 320, 322, 322,
		324, 326, 328, 330, 332, 334, 336, 338, 340, 342, 344, 344, 346, 348, 350, 352, 354, 356, 358, 360, 362,
		364, 366, 368, 368, 370, 372, 374, 376, 378, 380, 382, 384, 386, 388, 390, 390, 392, 394, 396, 398, 400,
		402, 404, 406, 408, 410, 412, 414, 414, 416, 418, 420, 422, 424, 426, 428, 430, 432, 434, 436, 436, 438,
		440, 442, 444, 446, 448, 450, 452, 454, 456, 458, 460, 460, 462, 464, 466, 468, 470, 472, 474, 476, 478
	};

	static int dru_softcaps[] = {
		254, 256, 258, 260, 262, 264, 264, 266, 268, 270, 272, 272, 274, 276, 278, 280, 282, 282, 284, 286, 288,
		290, 290, 292, 294, 296, 298, 300, 300, 302, 304, 306, 308, 308, 310, 312, 314, 316, 318, 318, 320, 322,
		324, 326, 328, 328, 330, 332, 334, 336, 336, 338, 340, 342, 344, 346, 346, 348, 350, 352, 354, 354, 356,
		358, 360, 362, 364, 364, 366, 368, 370, 372, 372, 374, 376, 378, 380, 382, 382, 384, 386, 388, 390, 390,
		392, 394, 396, 398, 400, 400, 402, 404, 406, 408, 410, 410, 412, 414, 416, 418, 418, 420, 422, 424, 426
	};

	static int rogshmbstber_softcaps[] = {
		264, 266, 268, 270, 272, 272, 274, 276, 278, 280, 282, 282, 284, 286, 288, 290, 292, 294, 294, 296, 298,
		300, 302, 304, 306, 306, 308, 310, 312, 314, 316, 316, 318, 320, 322, 324, 326, 328, 328, 330, 332, 334,
		336, 338, 340, 340, 342, 344, 346, 348, 350, 350, 352, 354, 356, 358, 360, 362, 362, 364, 366, 368, 370,
		372, 374, 374, 376, 378, 380, 382, 384, 384, 386, 388, 390, 392, 394, 396, 396, 398, 400, 402, 404, 406,
		408, 408, 410, 412, 414, 416, 418, 418, 420, 422, 424, 426, 428, 430, 430, 432, 434, 436, 438, 440, 442
	};

	static int necwizmagenc_softcaps[] = {
		248, 250, 252, 254, 256, 256, 258, 260, 262, 264, 264, 266, 268, 270, 272, 272, 274, 276, 278, 280, 280,
		282, 284, 286, 288, 288, 290, 292, 294, 296, 296, 298, 300, 302, 304, 304, 306, 308, 310, 312, 312, 314,
		316, 318, 320, 320, 322, 324, 326, 328, 328, 330, 332, 334, 336, 336, 338, 340, 342, 344, 344, 346, 348,
		350, 352, 352, 354, 356, 358, 360, 360, 362, 364, 366, 368, 368, 370, 372, 374, 376, 376, 378, 380, 382,
		384, 384, 386, 388, 390, 392, 392, 394, 396, 398, 400, 400, 402, 404, 406, 408, 408, 410, 412, 414, 416
	};

	int level = std::min(105, static_cast<int>(GetLevel())) - 1;

	switch (GetClass()) {
	case WARRIOR:
		return war_softcaps[level];
	case CLERIC:
	case BARD:
	case MONK:
		return clrbrdmnk_softcaps[level];
	case PALADIN:
	case SHADOWKNIGHT:
		return palshd_softcaps[level];
	case RANGER:
		return rng_softcaps[level];
	case DRUID:
		return dru_softcaps[level];
	case ROGUE:
	case SHAMAN:
	case BEASTLORD:
	case BERSERKER:
		return rogshmbstber_softcaps[level];
	case NECROMANCER:
	case WIZARD:
	case MAGICIAN:
	case ENCHANTER:
		return necwizmagenc_softcaps[level];
	default:
		return 350;
	}
}

double Mob::GetSoftcapReturns()
{
	// These are based on the dev post, they seem to be correct for every level
	// AKA no more hard caps
	switch (GetClass()) {
	case WARRIOR:
		return 0.35;
	case CLERIC:
	case BARD:
	case MONK:
		return 0.3;
	case PALADIN:
	case SHADOWKNIGHT:
		return 0.33;
	case RANGER:
		return 0.315;
	case DRUID:
		return 0.265;
	case ROGUE:
	case SHAMAN:
	case BEASTLORD:
	case BERSERKER:
		return 0.28;
	case NECROMANCER:
	case WIZARD:
	case MAGICIAN:
	case ENCHANTER:
		return 0.25;
	default:
		return 0.3;
	}
}

int Mob::GetClassRaceACBonus()
{
	int ac_bonus = 0;
	auto level = GetLevel();
	if (GetClass() == MONK) {
		int hardcap = 30;
		int softcap = 14;
		if (level > 99) {
			hardcap = 58;
			softcap = 35;
		}
		else if (level > 94) {
			hardcap = 57;
			softcap = 34;
		}
		else if (level > 89) {
			hardcap = 56;
			softcap = 33;
		}
		else if (level > 84) {
			hardcap = 55;
			softcap = 32;
		}
		else if (level > 79) {
			hardcap = 54;
			softcap = 31;
		}
		else if (level > 74) {
			hardcap = 53;
			softcap = 30;
		}
		else if (level > 69) {
			hardcap = 53;
			softcap = 28;
		}
		else if (level > 64) {
			hardcap = 53;
			softcap = 26;
		}
		else if (level > 63) {
			hardcap = 50;
			softcap = 24;
		}
		else if (level > 61) {
			hardcap = 47;
			softcap = 24;
		}
		else if (level > 59) {
			hardcap = 45;
			softcap = 24;
		}
		else if (level > 54) {
			hardcap = 40;
			softcap = 20;
		}
		else if (level > 50) {
			hardcap = 38;
			softcap = 18;
		}
		else if (level > 44) {
			hardcap = 36;
			softcap = 17;
		}
		else if (level > 29) {
			hardcap = 34;
			softcap = 16;
		}
		else if (level > 14) {
			hardcap = 32;
			softcap = 15;
		}
		int weight = IsClient() ? CastToClient()->CalcCurrentWeight()/10 : 0;
		if (weight < hardcap - 1) {
			double temp = level + 5;
			if (weight > softcap) {
				double redux = static_cast<double>(weight - softcap) * 6.66667;
				redux = (100.0 - std::min(100.0, redux)) * 0.01;
				temp = std::max(0.0, temp * redux);
			}
			ac_bonus = static_cast<int>((4.0 * temp) / 3.0);
		}
		else if (weight > hardcap + 1) {
			double temp = level + 5;
			double multiplier = std::min(1.0, (weight - (static_cast<double>(hardcap) - 10.0)) / 100.0);
			temp = (4.0 * temp) / 3.0;
			ac_bonus -= static_cast<int>(temp * multiplier);
		}
	}

	if (GetClass() == ROGUE) {
		int level_scaler = level - 26;
		if (GetAGI() < 80)
			ac_bonus = level_scaler / 4;
		else if (GetAGI() < 85)
			ac_bonus = (level_scaler * 2) / 4;
		else if (GetAGI() < 90)
			ac_bonus = (level_scaler * 3) / 4;
		else if (GetAGI() < 100)
			ac_bonus = (level_scaler * 4) / 4;
		else if (GetAGI() >= 100)
			ac_bonus = (level_scaler * 5) / 4;
		if (ac_bonus > 12)
			ac_bonus = 12;
	}

	if (GetClass() == BEASTLORD) {
		int level_scaler = level - 6;
		if (GetAGI() < 80)
			ac_bonus = level_scaler / 5;
		else if (GetAGI() < 85)
			ac_bonus = (level_scaler * 2) / 5;
		else if (GetAGI() < 90)
			ac_bonus = (level_scaler * 3) / 5;
		else if (GetAGI() < 100)
			ac_bonus = (level_scaler * 4) / 5;
		else if (GetAGI() >= 100)
			ac_bonus = (level_scaler * 5) / 5;
		if (ac_bonus > 16)
			ac_bonus = 16;
	}

	return ac_bonus;
}

/*	This will ignore the database AC value for NPCs under level 52 or so and calculate a value instead.
	Low level NPC mitigation estimates parsed to highly predictable and uniform values, and the AC value
	is very sensitive to erroneous entries, which means entering the wrong value in the database will
	result in super strong or weak NPCs, so it seems wiser to hardcode it.

	Most NPCs level 50+ have ~200 mit AC.  Raid bosses have more. (anywhere from 200-1200)  This uses the
	database AC value if it's higher than 200 and the default calcs to 200.

	Note that the database AC values are the computed estimates from parsed logs, so it factors in AC from
	the defense skill+agility.  If NPC data is ever leaked in the future then Sony's AC values will likely
	be lower than what the AC values in our database are because of this, and this algorithm will need to
	be altered to add in AC from defense skill and agility.
*/
int Mob::GetMitigation()
{
	if (!IsNPC() && !IsClient())
		return 0;

	int mit;

	if (IsPet())
	{
		mit = GetAC();
	}
	else
	{
		if (GetLevel() < 15)
		{
			mit = GetLevel() * 3;

			if (GetLevel() < 3)
				mit += 2;
		}
		else
		{
			if (content_service.GetCurrentExpansion() >= 5)
				mit = 200;
			else
				mit = GetLevel() * 41 / 10 - 15;
		}

		if (mit > 200)
			mit = 200;

		if (mit == 200 && GetAC() > 200)
			mit = GetAC();

		mit += itembonuses.AC + spellbonuses.AC / 4;
		if (mit < 1)
			mit = 1;
	}

	return mit;
}

int Client::GetMitigation(bool ignoreCap, int item_ac_sum, int shield_ac, int spell_ac_sum, int classnum, int level, int base_race, int carried_weight, int agi, int defense_skill_value, int combat_stability_percent, int additional_ac)
{
	int32 acSum = item_ac_sum;
	uint8 playerClass = classnum;

	// add 33% to item AC for all but NEC WIZ MAG ENC
	if (playerClass != NECROMANCER && playerClass != WIZARD && playerClass != MAGICIAN && playerClass != ENCHANTER)
	{
		acSum = 4 * acSum / 3;
	}

	// anti-twink
	if (!ignoreCap && level < 50 && acSum >(level * 6 + 25))
	{
		acSum = level * 6 + 25;
	}

	if (playerClass == MONK)
	{
		int32 hardcap, softcap;

		if (level < 15)
		{ // 1-14
			hardcap = 30;
			softcap = 14;
		}
		else if (level <= 29)
		{ // 15-29
			hardcap = 32;
			softcap = 15;
		}
		else if (level <= 44)
		{ // 30-44
			hardcap = 34;
			softcap = 16;
		}
		else if (level <= 50)
		{ // 45-50
			hardcap = 36;
			softcap = 17;
		}
		else if (level <= 54)
		{ // 51-54
			hardcap = 38;
			softcap = 18;
		}
		else if (level <= 59)
		{ // 55-59
			hardcap = 40;
			softcap = 20;
		}
		else if (level <= 61)
		{ // 60-61
			hardcap = 45;
			softcap = 24;
		}
		else if (level <= 63)
		{ // 62-63
			hardcap = 47;
			softcap = 24;
		}
		else if (level <= 64)
		{ // 64
			hardcap = 50;
			softcap = 24;
		}
		else
		{ // 65
			hardcap = 53;
			softcap = 24;
		}

		int32 weight = carried_weight;
		double acBonus = level + 5.0;

		if (weight <= softcap) // 93 bonus at level 65 when under 24 weight
		{
			acSum += static_cast<int32>(acBonus * 4.0 / 3.0);
		}
		else if (weight > hardcap + 1) // scales the penalty from -11 down to -93 at level 65 with 143 weight
		{
			double penalty = level + 5.0;
			double multiplier = (weight - (hardcap - 10)) / 100.0;
			if (multiplier > 1.0) multiplier = 1.0;
			penalty = 4.0 * penalty / 3.0;
			penalty = multiplier * penalty;

			acSum -= static_cast<int32>(penalty);
		}
		else if (weight > softcap) // scales the bonus from 93 down to 0 at level 65 with 39 weight
		{
			double reduction = (weight - softcap) * 6.66667;
			if (reduction > 100.0) reduction = 100.0;
			reduction = (100.0 - reduction) / 100.0;
			acBonus *= reduction;
			if (acBonus < 0.0) acBonus = 0.0;
			acBonus = 4.0 * acBonus / 3.0;

			acSum += static_cast<int32>(acBonus);
		}

	}
	else if (playerClass == ROGUE)
	{
		if (level >= 30 && agi > 75)
		{
			// this bonus is small, it gets maxed out at 12
			//   by level 50 with 80 agi
			//   by level 42 with 85 agi
			//   by level 38 with 90 agi
			//   by level 36 with 100 agi
			int32 levelScaler = level - 26;
			int32 acBonus = 0;

			if (agi < 80)
			{
				acBonus = levelScaler / 4;
			}
			else if (agi < 85)
			{
				acBonus = levelScaler * 2 / 4;
			}
			else if (agi < 90)
			{
				acBonus = levelScaler * 3 / 4;
			}
			else if (agi < 100)
			{
				acBonus = levelScaler * 4 / 4;
			}
			else
			{
				acBonus = levelScaler * 5 / 4;
			}

			if (acBonus > 12) acBonus = 12;

			acSum += acBonus;
		}
	}
	else if (playerClass == BEASTLORD)
	{
		if (level > 10)
		{
			// this bonus is small, it gets maxed out at 16
			//   by level 46 with 80 agi
			//   by level 33 with 85 agi
			//   by level 26 with 90 agi
			//   by level 22 with 100 agi
			int32 levelScaler = level - 6;
			int32 acBonus = 0;

			if (agi < 80)
			{
				acBonus = levelScaler / 5;
			}
			else if (agi < 85)
			{
				acBonus = levelScaler * 2 / 5;
			}
			else if (agi < 90)
			{
				acBonus = levelScaler * 3 / 5;
			}
			else if (agi < 100)
			{
				acBonus = levelScaler * 4 / 5;
			}
			else
			{
				acBonus = levelScaler * 5 / 5;
			}

			if (acBonus > 16) acBonus = 16;

			acSum += acBonus;
		}
	}

	acSum += additional_ac;

	if (acSum < 0)
		acSum = 0;

	int32 defense = defense_skill_value;
	if (defense > 0)
	{
		if (playerClass == WIZARD || playerClass == NECROMANCER || playerClass == ENCHANTER || playerClass == MAGICIAN)
		{
			acSum += defense / 2;
		}
		else
		{
			acSum += defense / 3;
		}
	}

	int spellACDivisor = 4;
	if (playerClass == WIZARD || playerClass == MAGICIAN || playerClass == NECROMANCER || playerClass == ENCHANTER)
	{
		spellACDivisor = 3;
	}
	acSum += (spell_ac_sum / spellACDivisor);

	if (agi > 70)
		acSum += agi / 20;

	if (acSum < 0)
		acSum = 0;

	int32 softcap;

	// the AC softcap values and logic were taken from Demonstar55's client decompile
	switch (playerClass)
	{
	case WARRIOR:
	{
		softcap = 430;
		break;
	}
	case PALADIN:
	case SHADOWKNIGHT:
	case CLERIC:
	case BARD:
	{
		softcap = 403;
		break;
	}
	case RANGER:
	case SHAMAN:
	{
		softcap = 375;
		break;
	}
	case MONK:
	{
		softcap = 350; // was: 315. Pre-nerf instead.
		break;
	}
	default:
	{
		softcap = 350;		// dru, rog, wiz, ench, nec, mag, bst
	}
	}

	// Combat Stability AA - this raises the softcap
	softcap += combat_stability_percent * softcap / 100;

	// shield AC is not capped
	softcap += shield_ac;

	if (!ignoreCap && acSum > softcap)
	{
		if (level < 50)
		{
			return softcap;		// it's hard < level 50
		}

		int32 overcap = acSum - softcap;
		int32 returns = 20;					// CLR, DRU, SHM, NEC, WIZ, MAG, ENC

		if (playerClass == WARRIOR)
		{
			if (level <= 61)
			{
				returns = 5;
			}
			else if (level <= 63)
			{
				returns = 4;
			}
			else
			{
				returns = 3;
			}
		}
		else if (playerClass == PALADIN || playerClass == SHADOWKNIGHT)
		{
			if (level <= 61)
			{
				returns = 6;
			}
			else if (level <= 63)
			{
				returns = 5;
			}
			else
			{
				returns = 4;
			}
		}
		else if (playerClass == BARD)
		{
			if (level <= 61)
			{
				returns = 8;
			}
			else if (level <= 63)
			{
				returns = 7;
			}
			else
			{
				returns = 6;
			}
		}
		else if (playerClass == MONK || playerClass == ROGUE)
		{
			if (level <= 61)
			{
				returns = 20;
			}
			else if (level == 62)
			{
				returns = 18;
			}
			else if (level == 63)
			{
				returns = 16;
			}
			else if (level == 64)
			{
				returns = 14;
			}
			else
			{
				returns = 12;
			}
		}
		else if (playerClass == RANGER || playerClass == BEASTLORD)
		{
			if (level <= 61)
			{
				returns = 10;
			}
			else if (level == 62)
			{
				returns = 9;
			}
			else if (level == 63)
			{
				returns = 8;
			}
			else
			{
				returns = 7;
			}
		}

		acSum = softcap + overcap / returns;
	}

	return acSum;
}

// These are fairly accurate estimates based on many parsed Live logs
// NPC defense skill and agility values are unknowable, so we estimate avoidance AC based on miss rates
int Mob::GetAvoidance()
{
	int level = GetLevel();
	int avoidance = level * 9 + 5;

	if (level <= 50 && avoidance > 400)
		avoidance = 400;
	else if (avoidance > 460)
		avoidance = 460;

	// this is how Live does it for PCs and NPCs.  AK might have (likely) been different.  Can't know how AK did it.
	// but the difference is so small nobody would notice
	avoidance += (spellbonuses.AGI + itembonuses.AGI) * 22 / 100;

	if(IsNPC())
		avoidance += CastToNPC()->GetAvoidanceRating();

	if (avoidance < 1)
		avoidance = 1;

	return avoidance;
}

// this output is precise and is based on https://forums.daybreakgames.com/eq/index.php?threads/ac-vs-acv2.210028/
int Client::GetAvoidance(int16 defense_skill_value, int16 agi, uint8 level, uint8 intoxication, int combat_agility_percent)
{
	int computedDefense = 1;
	int defenseAvoidance = 0;

	if (defense_skill_value > 0)
	{
		defenseAvoidance = defense_skill_value * 400 / 225;
	}

	// max agility bonus (called agiAvoidance here) is 53 with level > 40 and AGI 200
	// defense 252 (WAR PAL SHD MNK BRD ROG ) = 448 + 53 = 501
	// defense 240 (RNG BST) = 426 + 53 = 479
	// defense 200 (CLR DRU SHM) = 355 + 53 = 408
	// defense 145 (NEC WIZ MAG ENC) = 257 + 53 = 310

	// note: modern EQ does this here: GetAGI() > 40 ? (GetAGI() - 40) * 8000 / 36000 : 0;
	// old clients had a different calculation.  This is the precise output, based on a decompile done by Secrets
	int agiAvoidance = 0;
	if (agi < 40)
	{
		// 0-39 AGI = -25 to 0
		agiAvoidance = (25 * (agi - 40)) / 40;
	}
	else if (agi >= 60 && agi <= 74)
	{
		// 40-60 AGI = 0
		agiAvoidance = (2 * (28 - ((200 - agi) / 5))) / 3;
	}
	else if (agi >= 75)
	{
		// 75-200 AGI = 6 to 53
		// AGI over 200 provides no further benefit for this bonus

		// 36 to 53
		int bonusAdj = 80;

		if (level < 7)
		{
			// 6 to 23
			bonusAdj = 35;
		}
		else if (level < 20)
		{
			// 20 to 36
			bonusAdj = 55;
		}
		else if (level < 40)
		{
			// 30 to 46
			bonusAdj = 70;
		}

		if (agi < 200)
		{
			agiAvoidance = (2 * (bonusAdj - ((200 - agi) / 5))) / 3;
		}
		else
		{
			agiAvoidance = 2 * bonusAdj / 3;
		}
	}

	computedDefense = defenseAvoidance + agiAvoidance;

	// combat agility scaling
	computedDefense += computedDefense * combat_agility_percent / 100;

	int drunk_factor = intoxication / 2;
	if (drunk_factor > 20)
	{
		int drunk_multiplier = 110 - drunk_factor;
		if (drunk_multiplier > 100)
		{
			drunk_multiplier = 100;
		}
		computedDefense = computedDefense * drunk_multiplier / 100;
	}

	if (computedDefense < 1)
		computedDefense = 1;

	return computedDefense;
}

// the bool to ignore the Combat Agility AAs is to compare AC with the client's displayed AC.  Combat calcs
// should not ignore it
int Client::GetAvoidance(bool ignoreCombatAgility)
{
	int combat_agility_percent = ignoreCombatAgility ? 0 : aabonuses.AvoidMeleeChanceEffect;
	int computedDefense = GetAvoidance(GetSkill(EQ::skills::SkillDefense), GetAGI(), GetLevel(), m_pp.intoxication, combat_agility_percent);

	return computedDefense;
}

int Client::GetMitigation(bool ignoreCap)
{
	// shield AC is not capped, so this value is just added to the softcap
	int shield_ac = 0;
	const EQ::ItemInstance* inst = m_inv.GetItem(EQ::invslot::slotSecondary);
	{
		if (inst)
		{
			if (inst->GetItem()->ItemType == EQ::item::ItemTypeShield)
			{
				shield_ac = inst->GetItem()->AC;
			}
		}
	}
	int additional_ac = 0;

	if (IsClient())
	{
		EQ::ItemInstance* inst = CastToClient()->GetInv().GetItem(EQ::invslot::slotCharm);
		if (inst && inst->GetID() == RaceCharmIDs::CharmIksar)
		{
			additional_ac += EQ::Clamp(static_cast<int>(level), 10, 35);
		}
	}

	int carried_weight = GetWeight() / 10;

	return GetMitigation(ignoreCap, itembonuses.AC, shield_ac, spellbonuses.AC, GetClass(), GetLevel(), GetBaseRace(), carried_weight, GetAGI(), GetSkill(EQ::skills::SkillDefense), aabonuses.CombatStability, additional_ac);
}

//SYNC WITH: tune.cpp, mob.h TuneACSum
int Mob::ACSum(bool skip_caps)
{
	if (IsNPC())
	{
		return GetMitigation();
	}

	int ac_sum = GetMitigation();


	return ac_sum;
}

int Mob::GetBestMeleeSkill()
{
	int bestSkill=0;

	EQ::skills::SkillType meleeSkills[]=
	{	EQ::skills::Skill1HBlunt,
	  	EQ::skills::Skill1HSlashing,
		EQ::skills::Skill2HBlunt,
		EQ::skills::Skill2HSlashing,
		EQ::skills::SkillHandtoHand,
		EQ::skills::Skill1HPiercing,
		EQ::skills::Skill2HPiercing,
		EQ::skills::SkillCount
	};
	int i;

	for (i=0; meleeSkills[i] != EQ::skills::SkillCount; ++i) {
		int value;
		value = GetSkill(meleeSkills[i]);
		bestSkill = std::max(value, bestSkill);
	}

	return bestSkill;
}

int Mob::GetOffense(EQ::skills::SkillType skill)
{
	if (!IsNPC() && !IsClient())
		return 0;

	int offense = 0;
	int baseOffense = level * 55 / 10 - 4;
	if (baseOffense > 320)
		baseOffense = 320;

	int baseStrOffense = 0;

	if (GetLevel() > 29)
	{
		baseStrOffense = GetLevel() * 2 - 40;

		if (!IsSummonedClientPet() && zone->newzone_data.expansion >= 5) // planes
		{
			baseStrOffense += 20;
		}
	}
	else if (GetLevel() > 5)
	{
		baseStrOffense = GetLevel() / 2 + 1;
	}
	else
	{
		baseStrOffense = (GetLevel() * 5) - baseOffense;
	}

	offense = baseOffense + baseStrOffense;

	if (IsSummonedClientPet() && GetOwner())
	{
		offense = GetSkill(skill);

		if (GetOwner()->GetClass() == MAGICIAN)
			offense = offense * 125 / 100;
		else if (GetOwner()->GetClass() == NECROMANCER)
			offense = offense * 115 / 100;
		else if (GetOwner()->GetClass() == BEASTLORD)
			offense = offense * 110 / 100;
	}

	offense += (itembonuses.STR + spellbonuses.STR) * 2 / 3;
	if (!IsSummonedClientPet() && offense < baseOffense)
		offense = baseOffense;

	offense += ATK + spellbonuses.ATK;
	if (offense < 1)
		offense = 1;

	return offense;
}

// This is one half of the atk value displayed in clients
// This is accurate and based on a client decompile done by demonstar
int Client::GetOffense(EQ::skills::SkillType skill)
{
	int statBonus;

	if (skill == EQ::skills::SkillArchery || skill == EQ::skills::SkillThrowing)
	{
		statBonus = GetDEX();
	}
	else
	{
		statBonus = GetSTR();
	}

	int offense = GetSkill(skill) + spellbonuses.ATK + itembonuses.ATK + (statBonus >= 75 ? ((2 * statBonus - 150) / 3) : 0);
	if (offense < 1)
		offense = 1;

	if (GetClass() == RANGER && GetLevel() > 54)
	{
		offense = offense + GetLevel() * 4 - 216;
	}

	return offense;
}

//SYNC WITH: tune.cpp, mob.h Tuneoffense
int Mob::offense(EQ::skills::SkillType skill)
{
	if (!IsNPC() && !IsClient())
		return 0;

	if (IsNPC())
		return GetOffense(skill);
	return GetOffense(EQ::skills::SkillHandtoHand);
}

// this assumes "this" is the defender
// this returns between 0.1 to 2.0
int Mob::RollD20(double offense, double mitigation)
{
	int atkRoll = zone->random.Roll0(offense + 5);
	int defRoll = zone->random.Roll0(mitigation + 5);

	int avg = (offense + mitigation + 10) / 2;
	int index = std::max(0, (atkRoll - defRoll) + (avg / 2));
	index = (index * 20) / avg;
	index = std::max(0, index);
	index = std::min(19, index);

	return index + 1;
}

//SYNC WITH: tune.cpp, mob.h TuneMeleeMitigation
void Mob::MeleeMitigation(Mob *attacker, DamageHitInfo &hit, ExtraAttackOptions *opts)
{
#ifdef LUA_EQEMU
	bool ignoreDefault = false;
	LuaParser::Instance()->MeleeMitigation(this, attacker, hit, opts, ignoreDefault);

	if (ignoreDefault) {
		return;
	}
#endif

	if (hit.damage_done < 0 || hit.base_damage == 0)
		return;

	Mob* defender = this;
	auto mitigation = defender->GetMitigationAC();

	if (opts) {
		mitigation *= (1.0f - opts->armor_pen_percent);
		mitigation -= opts->armor_pen_flat;
	}

	auto roll = RollD20(hit.offense, mitigation);

	// +0.5 for rounding, min to 1 dmg
	hit.damage_done = std::max(static_cast<int>(roll * static_cast<double>(hit.base_damage) + 0.5), 1);

	Log(Logs::Detail, Logs::Attack, "mitigation %d vs offense %d. base %d rolled %f damage %d", mitigation, hit.offense, hit.base_damage, roll, hit.damage_done);
}

//Returns the weapon damage against the input mob
//if we cannot hit the mob with the current weapon we will get a value less than or equal to zero
//Else we know we can hit.
//GetWeaponDamage(mob*, const EQ::ItemData*) is intended to be used for mobs or any other situation where we do not have a client inventory item
//GetWeaponDamage(mob*, const EQ::ItemInstance*) is intended to be used for situations where we have a client inventory item
int64 Mob::GetWeaponDamage(Mob *against, const EQ::ItemData *weapon_item) {
	int64 dmg = 0;
	int64 banedmg = 0;

	//can't hit invulnerable stuff with weapons.
	if (against->GetInvul() || against->GetSpecialAbility(IMMUNE_MELEE)) {
		return 0;
	}

	//check to see if our weapons or fists are magical.
	if (against->GetSpecialAbility(IMMUNE_MELEE_NONMAGICAL)) {
		if (GetSpecialAbility(SPECATK_MAGICAL)) {
			dmg = 1;
		}
		//On live this occurs for ALL NPC's >= 10
		else if (IsNPC() && GetLevel() >= RuleI(Combat, NPCAttackMagicLevel)) {
			dmg = 1;
		}
		else if (weapon_item) {
			if (weapon_item->Magic) {
				if (weapon_item->Damage && (weapon_item->IsType1HWeapon() || weapon_item->IsType2HWeapon())) {
					dmg = weapon_item->Damage;
				}
				//Non weapon items, ie. boots for kick.
				else if (weapon_item->ItemType == EQ::item::ItemTypeArmor) {
					dmg = 1;
				}
				else {
					return 0;
				}
			}
			else {
				return 0;
			}
		}
		else if ((GetClass() == MONK || GetClass() == BEASTLORD) && GetLevel() >= 30) {
			dmg = GetHandToHandDamage();
		}
		else {
			return 0;
		}
	}
	else {
		if (weapon_item) {
			dmg = weapon_item->Damage;

			dmg = dmg <= 0 ? 1 : dmg;
		}
		else {
			dmg = GetHandToHandDamage();
		}
	}

	int eledmg = 0;
	if (!against->GetSpecialAbility(IMMUNE_MAGIC)) {
		if (weapon_item && weapon_item->ElemDmgAmt) {
			//we don't check resist for npcs here
			eledmg = weapon_item->ElemDmgAmt;
			dmg += eledmg;
		}
	}

	if (against->GetSpecialAbility(IMMUNE_MELEE_EXCEPT_BANE)) {
		if (weapon_item) {
			if (weapon_item->BaneDmgBody == against->GetBodyType()) {
				banedmg += weapon_item->BaneDmgAmt;
			}

			if (weapon_item->BaneDmgRace == against->GetRace()) {
				banedmg += weapon_item->BaneDmgRaceAmt;
			}
		}

		if (!banedmg) {
			if (!GetSpecialAbility(SPECATK_BANE))
				return 0;
			else
				return 1;
		}
		else
			dmg += banedmg;
	}
	else {
		if (weapon_item) {
			if (weapon_item->BaneDmgBody == against->GetBodyType()) {
				banedmg += weapon_item->BaneDmgAmt;
			}

			if (weapon_item->BaneDmgRace == against->GetRace()) {
				banedmg += weapon_item->BaneDmgRaceAmt;
			}
		}

		dmg += (banedmg + eledmg);
	}

	if (dmg <= 0) {
		return 0;
	}
	else
		return dmg;
}

int64 Mob::GetWeaponDamage(Mob *against, const EQ::ItemInstance *weapon_item, int64 *hate)
{
	int64 dmg = 0;
	int64 banedmg = 0;
	int x = 0;

	if (!against || against->GetInvul() || against->GetSpecialAbility(IMMUNE_MELEE))
		return 0;

	// check for items being illegally attained
	if (weapon_item) {
		if (!weapon_item->GetItem())
			return 0;

		if (weapon_item->GetItemRequiredLevel(true) > GetLevel())
			return 0;

		if (!weapon_item->IsEquipable(GetBaseRace(), GetClass()))
			return 0;
	}

	if (against->GetSpecialAbility(IMMUNE_MELEE_NONMAGICAL)) {
		if (weapon_item) {
			// check to see if the weapon is magic
			bool MagicWeapon = weapon_item->GetItemMagical(true) || spellbonuses.MagicWeapon || itembonuses.MagicWeapon;
			if (MagicWeapon) {
				auto rec_level = weapon_item->GetItemRecommendedLevel(true);
				if (IsClient() && GetLevel() < rec_level)
					dmg = CastToClient()->CalcRecommendedLevelBonus(
						GetLevel(), rec_level, weapon_item->GetItemWeaponDamage(true));
				else
					dmg = weapon_item->GetItemWeaponDamage(true);
				dmg = dmg <= 0 ? 1 : dmg;
			}
			else {
				return 0;
			}
		}
		else {
			bool MagicGloves = false;
			if (IsClient()) {
				const EQ::ItemInstance *gloves = CastToClient()->GetInv().GetItem(EQ::invslot::slotHands);
				if (gloves)
					MagicGloves = gloves->GetItemMagical(true);
			}

			if (GetClass() == MONK || GetClass() == BEASTLORD) {
				if (MagicGloves || GetLevel() >= 30) {
					dmg = GetHandToHandDamage();
					if (hate)
						*hate += dmg;
				}
			}
			else if (GetOwner() &&
				GetLevel() >=
				RuleI(Combat, PetAttackMagicLevel)) { // pets wouldn't actually use this but...
				dmg = 1; // it gives us an idea if we can hit
			}
			else if (MagicGloves || GetSpecialAbility(SPECATK_MAGICAL)) {
				dmg = 1;
			}
			else
				return 0;
		}
	}
	else {
		if (weapon_item) {
			if (weapon_item->GetItem()) {
				auto rec_level = weapon_item->GetItemRecommendedLevel(true);
				if (IsClient() && GetLevel() < rec_level) {
					dmg = CastToClient()->CalcRecommendedLevelBonus(
						GetLevel(), rec_level, weapon_item->GetItemWeaponDamage(true));
				}
				else {
					dmg = weapon_item->GetItemWeaponDamage(true);
				}

				dmg = dmg <= 0 ? 1 : dmg;
			}
		}
		else {
			dmg = GetHandToHandDamage();
			if (hate)
				*hate += dmg;
		}
	}

	int eledmg = 0;
	if (!against->GetSpecialAbility(IMMUNE_MAGIC)) {
		if (weapon_item && weapon_item->GetItem() && weapon_item->GetItemElementalFlag(true))
			// the client actually has the way this is done, it does not appear to check req!
			eledmg = against->ResistElementalWeaponDmg(weapon_item);
	}

	if (weapon_item && weapon_item->GetItem() &&
		(weapon_item->GetItemBaneDamageBody(true) || weapon_item->GetItemBaneDamageRace(true)))
		banedmg = against->CheckBaneDamage(weapon_item);

	if (against->GetSpecialAbility(IMMUNE_MELEE_EXCEPT_BANE)) {
		if (!banedmg) {
			if (!GetSpecialAbility(SPECATK_BANE))
				return 0;
			else
				return 1;
		}
		else {
			dmg += (banedmg + eledmg);
			if (hate)
				*hate += banedmg;
		}
	}
	else {
		dmg += (banedmg + eledmg);
		if (hate)
			*hate += banedmg;
	}

	return std::max((int64)0, dmg);
}

int64 Client::DoDamageCaps(int64 base_damage)
{
	// this is based on a client function that caps melee base_damage
	auto level = GetLevel();
	auto stop_level = RuleI(Combat, LevelToStopDamageCaps);
	if (stop_level && stop_level <= level)
		return base_damage;
	int cap = 0;
	if (level >= 125) {
		cap = 7 * level;
	}
	else if (level >= 110) {
		cap = 6 * level;
	}
	else if (level >= 90) {
		cap = 5 * level;
	}
	else if (level >= 70) {
		cap = 4 * level;
	}
	else if (level >= 40) {
		switch (GetClass()) {
		case CLERIC:
		case DRUID:
		case SHAMAN:
			cap = 80;
			break;
		case NECROMANCER:
		case WIZARD:
		case MAGICIAN:
		case ENCHANTER:
			cap = 40;
			break;
		default:
			cap = 200;
			break;
		}
	}
	else if (level >= 30) {
		switch (GetClass()) {
		case CLERIC:
		case DRUID:
		case SHAMAN:
			cap = 26;
			break;
		case NECROMANCER:
		case WIZARD:
		case MAGICIAN:
		case ENCHANTER:
			cap = 18;
			break;
		default:
			cap = 60;
			break;
		}
	}
	else if (level >= 20) {
		switch (GetClass()) {
		case CLERIC:
		case DRUID:
		case SHAMAN:
			cap = 20;
			break;
		case NECROMANCER:
		case WIZARD:
		case MAGICIAN:
		case ENCHANTER:
			cap = 12;
			break;
		default:
			cap = 30;
			break;
		}
	}
	else if (level >= 10) {
		switch (GetClass()) {
		case CLERIC:
		case DRUID:
		case SHAMAN:
			cap = 12;
			break;
		case NECROMANCER:
		case WIZARD:
		case MAGICIAN:
		case ENCHANTER:
			cap = 10;
			break;
		default:
			cap = 14;
			break;
		}
	}
	else {
		switch (GetClass()) {
		case CLERIC:
		case DRUID:
		case SHAMAN:
			cap = 9;
			break;
		case NECROMANCER:
		case WIZARD:
		case MAGICIAN:
		case ENCHANTER:
			cap = 6;
			break;
		default:
			cap = 10; // this is where the 20 damage cap comes from
			break;
		}
	}

	return std::min((int64)cap, base_damage);
}

// other is the defender, this is the attacker
//SYNC WITH: tune.cpp, mob.h TuneDoAttack
void Mob::DoAttack(Mob *other, DamageHitInfo &hit, ExtraAttackOptions *opts, bool FromRiposte)
{
	if (!other)
		return;
	LogCombat("[{}]::DoAttack vs [{}] base [{}] min [{}] offense [{}] tohit [{}] skill [{}]", GetName(),
		other->GetName(), hit.base_damage, hit.min_damage, hit.offense, hit.tohit, hit.skill);
}

// calling this with MainRange will also add the arrow damage
int64 Client::GetBaseDamage(Mob* defender, uint16 slot)
{
	if (slot != EQ::invslot::slotSecondary && slot != EQ::invslot::slotRange && slot != EQ::invslot::slotAmmo)
		slot = EQ::invslot::slotPrimary;

	int dmg = 0;

	EQ::ItemInstance* weaponInst = GetInv().GetItem(slot);
	const EQ::ItemData* weapon = nullptr;
	if (weaponInst)
		weapon = weaponInst->GetItem();

	if (weapon)
	{
		// cheaters or GMs doing stuff
		if (weapon->ReqLevel > GetLevel())
			return dmg;

		if (!weaponInst->IsEquipable(GetBaseRace(), GetClass()))
			return dmg;

		if (GetLevel() < weapon->RecLevel)
			dmg = CastToClient()->CalcRecommendedLevelBonus(GetLevel(), weapon->RecLevel, weapon->Damage);
		else
			dmg = weapon->Damage;

		if (weapon->ElemDmgAmt && !defender->GetSpecialAbility(IMMUNE_MAGIC))
		{
			int eledmg = 0;

			if (GetLevel() < weapon->RecLevel)
				eledmg = CastToClient()->CalcRecommendedLevelBonus(GetLevel(), weapon->RecLevel, weapon->ElemDmgAmt);
			else
				eledmg = weapon->ElemDmgAmt;

			if (eledmg)
			{
				eledmg = CalcEleWeaponResist(eledmg, weapon->ElemDmgType, defender);
				dmg += eledmg;
			}
		}

		if (weapon->BaneDmgBody == defender->GetBodyType() || weapon->BaneDmgRace == defender->GetRace())
		{
			if (GetLevel() < weapon->RecLevel)
				dmg += CastToClient()->CalcRecommendedLevelBonus(GetLevel(), weapon->RecLevel, weapon->BaneDmgAmt);
			else
				dmg += weapon->BaneDmgAmt;
		}

		if (slot == EQ::invslot::slotRange && GetInv().GetItem(EQ::invslot::slotAmmo))
		{
			dmg += GetBaseDamage(defender, EQ::invslot::slotAmmo);
		}
	}
	else if (slot == EQ::invslot::slotPrimary || slot == EQ::invslot::slotSecondary)
		dmg = GetHandToHandDamage();

	return dmg;
}


int64 NPC::GetBaseDamage(Mob* defender, uint16 slot)
{
	// Our database uses a min hit and max hit system, instead of Sony's DB + baseDmg * 0.1-2.0
	// This calcs a baseDamage value (which is DI*10) from min and max hits
	// baseDamage is the equivalent to weapon damage for clients
	if (slot != EQ::invslot::slotSecondary && slot != EQ::invslot::slotRange)
		slot = EQ::invslot::slotPrimary;

	int baseDamage = 1;

	if (max_dmg > min_dmg)
	{
		int di1k = (max_dmg - min_dmg) * 1000 / 19;			// multiply damage interval by 1000 to avoid using floats
		di1k = (di1k + 50) / 100 * 100;						// round DI to nearest tenth of a point
		int db = max_dmg * 1000 - di1k * 20;
		baseDamage = di1k / 100;
	}

	const EQ::ItemData* weapon = nullptr;

	if (equipment[slot] > 0)
		weapon = database.GetItem(equipment[slot]);

	if (weapon)
	{
		int weaponDmg = weapon->Damage;

		if (weapon->ElemDmgAmt)
		{
			weaponDmg += CalcEleWeaponResist(weapon->ElemDmgAmt, weapon->ElemDmgType, defender);
		}

		if (weapon->BaneDmgBody == defender->GetBodyType() || weapon->BaneDmgRace == defender->GetRace())
		{
			weaponDmg += weapon->BaneDmgAmt;
		}

		if (slot == EQ::invslot::slotRange)
		{
			weapon = database.GetItem(equipment[EQ::invslot::slotAmmo]);
			if (weapon)
			{
				if (weapon->ElemDmgAmt)
				{
					weaponDmg += CalcEleWeaponResist(weapon->ElemDmgAmt, weapon->ElemDmgType, defender);
				}
			}
		}

		if (weaponDmg > baseDamage)
			baseDamage = weaponDmg;
	}

	return baseDamage;
}

int64 Mob::CalcMeleeDamage(Mob* defender, int64& baseDamage, EQ::skills::SkillType skill)
{
	if (!defender || !baseDamage)
		return 0;

	// ranged physical damage does half that of melee
	if ((skill == EQ::skills::SkillArchery || skill == EQ::skills::SkillThrowing) && baseDamage > 1)
		baseDamage /= 2;

	int offense = GetOffense(skill);

	// mitigation roll
	int roll = RollD20(offense, defender->GetMitigation());

	if (defender->IsClient() && defender->CastToClient()->IsSitting())
		roll = 20;

	// SE_MinDamageModifier[186] for disciplines: Fellstrike, Innerflame, Duelist, Bestial Rage
	// min hit becomes 4 x weapon damage + 1 x damage bonus
	int minHit = baseDamage * GetMeleeMinDamageMod_SE(skill) / 100;

	// SE_DamageModifier[185] for disciplines: Aggressive, Ashenhand, Bestial Rage, Defensive, Duelist,
	//                                         Fellstrike, Innerflame, Silentfist, Thunderkick
	baseDamage += baseDamage * GetMeleeDamageMod_SE(skill) / 100;

	// SE_MeleeMitigation[168] for disciplines: Defensive (-50), Stonestance & Protective Spirit (-90)
	//											Aggressive (+50)
	baseDamage += baseDamage * defender->GetSpellBonuses().MeleeMitigationEffect / 100;

	if (defender->IsClient() && IsPet() && GetOwner()->IsClient()) {
		// pets do reduced damage to clients in pvp
		baseDamage /= 2;
	}

	int64 damage = (roll * baseDamage + 5) / 10;
	if (damage < minHit) damage = minHit;
	if (damage < 1)
		damage = 1;

	if (IsClient())
		CastToClient()->RollDamageMultiplier(offense, damage);

	return damage;
}

int64 Mob::CalcEleWeaponResist(int weaponDamage, int resistType, Mob* target)
{
	int resistValue = 0;

	switch (resistType)
	{
	case RESIST_FIRE:
		resistValue = target->GetFR();
		break;
	case RESIST_COLD:
		resistValue = target->GetCR();
		break;
	case RESIST_MAGIC:
		resistValue = target->GetMR();
		break;
	case RESIST_DISEASE:
		resistValue = target->GetDR();
		break;
	case RESIST_POISON:
		resistValue = target->GetPR();
		break;
	}

	if (resistValue > 200)
		return 0;

	int roll = zone->random.Int(1, 201) - resistValue;
	if (roll < 1)
		return 0;
	if (roll <= 99)
		return weaponDamage * roll / 100;
	else
		return weaponDamage;
}


int64 Client::RollDamageMultiplier(uint32 offense, int64& damage)
{
	int rollChance = 51;
	int maxExtra = 210;
	int minusFactor = 105;

	if (GetClass() == MONK && level >= 65)
	{
		rollChance = 83;
		maxExtra = 300;
		minusFactor = 50;
	}
	else if (level >= 65 || (GetClass() == MONK && level >= 63))
	{
		rollChance = 81;
		maxExtra = 295;
		minusFactor = 55;
	}
	else if (level >= 63 || (GetClass() == MONK && level >= 60))
	{
		rollChance = 79;
		maxExtra = 290;
		minusFactor = 60;
	}
	else if (level >= 60 || (GetClass() == MONK && level >= 56))
	{
		rollChance = 77;
		maxExtra = 285;
		minusFactor = 65;
	}
	else if (level >= 56)
	{
		rollChance = 72;
		maxExtra = 265;
		minusFactor = 70;
	}
	else if (level >= 51 || GetClass() == MONK)
	{
		rollChance = 65;
		maxExtra = 245;
		minusFactor = 80;
	}

	int baseBonus = (offense - minusFactor) / 2;
	if (baseBonus < 10)
		baseBonus = 10;
	if (offense < 115)		// this is not in the Sony code paste, but a great many logs confirm this
		rollChance = 0;

	if (zone->random.Roll(rollChance))
	{
		uint32 roll;

		roll = zone->random.Int(0, baseBonus) + 100;
		if (roll > maxExtra)
			roll = maxExtra;

		damage = damage * roll / 100;

		if (level >= 55 && damage > 1 && IsWarriorClass())
			damage++;

		return roll;
	}
	else
	{
		return 100;
	}
}

//note: throughout this method, setting `damage` to a negative is a way to
//stop the attack calculations
// IsFromSpell added to allow spell effects to use Attack. (Mainly for the Rampage AA right now.)
//SYNC WITH: tune.cpp, mob.h TuneClientAttack
bool Client::Attack(Mob* other, int Hand, bool bRiposte, bool IsStrikethrough, bool IsFromSpell, ExtraAttackOptions *opts)
{
	if (!other) {
		SetTarget(nullptr);
		LogError("A null Mob object was passed to Client::Attack() for evaluation!");
		return false;
	}

	if (!GetTarget())
		SetTarget(other);

	LogCombat("Attacking [{}] with hand [{}] [{}]", other ? other->GetName() : "(nullptr)", Hand, bRiposte ? "(this is a riposte)" : "");

	//SetAttackTimer();
	if (
		(IsCasting() && GetClass() != BARD && !IsFromSpell)
		|| other == nullptr
		|| ((IsClient() && CastToClient()->dead) || (other->IsClient() && other->CastToClient()->dead))
		|| (GetHP() < 0)
		|| (!IsAttackAllowed(other))
		) {
		LogCombat("Attack cancelled, invalid circumstances");
		return false; // Only bards can attack while casting
	}

	if (DivineAura() && !GetGM()) {//cant attack while invulnerable unless your a gm
		LogCombat("Attack cancelled, Divine Aura is in effect");
		MessageString(Chat::DefaultText, DIVINE_AURA_NO_ATK);	//You can't attack while invulnerable
		return false;
	}

	if (GetFeigned())
		return false; // Rogean: How can you attack while feigned? Moved up from Aggro Code.

	EQ::ItemInstance* weapon = nullptr;
	if (Hand == EQ::invslot::slotSecondary) {	// Kaiyodo - Pick weapon from the attacking hand
		weapon = GetInv().GetItem(EQ::invslot::slotSecondary);
		OffHandAtk(true);
	}
	else {
		weapon = GetInv().GetItem(EQ::invslot::slotPrimary);
		OffHandAtk(false);
	}

	if (weapon != nullptr) {
		if (!weapon->IsWeapon()) {
			LogCombat("Attack cancelled, Item [{}] ([{}]) is not a weapon", weapon->GetItem()->Name, weapon->GetID());
			return(false);
		}
		LogCombat("Attacking with weapon: [{}] ([{}])", weapon->GetItem()->Name, weapon->GetID());
	}
	else {
		LogCombat("Attacking without a weapon");
	}

	DamageHitInfo my_hit;
	// calculate attack_skill and skillinuse depending on hand and weapon
	// also send Packet to near clients
	my_hit.skill = AttackAnimation(Hand, weapon);
	LogCombat("Attacking with [{}] in slot [{}] using skill [{}]", weapon ? weapon->GetItem()->Name : "Fist", Hand, my_hit.skill);

	// Now figure out damage
	my_hit.damage_done = 1;
	uint8 mylevel = GetLevel();
	my_hit.base_damage = GetBaseDamage(other, Hand);

	// anti-twink damage caps.  Taken from decompiles
	if (mylevel < 10)
	{
		switch (GetClass())
		{
		case DRUID:
		case CLERIC:
		case SHAMAN:
			if (my_hit.base_damage > 9)
				my_hit.base_damage = 9;
			break;
		case WIZARD:
		case MAGICIAN:
		case NECROMANCER:
		case ENCHANTER:
			if (my_hit.base_damage > 6)
				my_hit.base_damage = 6;
			break;
		default:
			if (my_hit.base_damage > 10)
				my_hit.base_damage = 10;
		}
	}
	else if (mylevel < 20)
	{
		switch (GetClass())
		{
		case DRUID:
		case CLERIC:
		case SHAMAN:
			if (my_hit.base_damage > 12)
				my_hit.base_damage = 12;
			break;
		case WIZARD:
		case MAGICIAN:
		case NECROMANCER:
		case ENCHANTER:
			if (my_hit.base_damage > 10)
				my_hit.base_damage = 10;
			break;
		default:
			if (my_hit.base_damage > 14)
				my_hit.base_damage = 14;
		}
	}
	else if (mylevel < 30)
	{
		switch (GetClass())
		{
		case DRUID:
		case CLERIC:
		case SHAMAN:
			if (my_hit.base_damage > 20)
				my_hit.base_damage = 20;
			break;
		case WIZARD:
		case MAGICIAN:
		case NECROMANCER:
		case ENCHANTER:
			if (my_hit.base_damage > 12)
				my_hit.base_damage = 12;
			break;
		default:
			if (my_hit.base_damage > 30)
				my_hit.base_damage = 30;
		}
	}
	else if (mylevel < 40)
	{
		switch (GetClass())
		{
		case DRUID:
		case CLERIC:
		case SHAMAN:
			if (my_hit.base_damage > 26)
				my_hit.base_damage = 26;
			break;
		case WIZARD:
		case MAGICIAN:
		case NECROMANCER:
		case ENCHANTER:
			if (my_hit.base_damage > 18)
				my_hit.base_damage = 18;
			break;
		default:
			if (my_hit.base_damage > 60)
				my_hit.base_damage = 60;
		}
	}

	int damageBonus = 0;
	if (Hand == EQ::invslot::slotPrimary)
		damageBonus = GetDamageBonus();

	int hate = my_hit.base_damage + damageBonus;

	//if weapon damage > 0 then we know we can hit the target with this weapon
	//otherwise we cannot and we set the damage to -5 later on
	if (my_hit.damage_done > 0) {

		CheckIncreaseSkill(my_hit.skill, other, -15);
		CheckIncreaseSkill(EQ::skills::SkillOffense, other, -15);

		// ***************************************************************
		// *** Calculate the damage bonus, if applicable, for this hit ***
		// ***************************************************************

		int hit_chance_bonus = 0;
		my_hit.hand = Hand;

		if (opts) {
			my_hit.base_damage *= opts->damage_percent;
			my_hit.base_damage += opts->damage_flat;
			hate *= opts->hate_percent;
			hate += opts->hate_flat;
			hit_chance_bonus += opts->hit_chance;
		}

		my_hit.tohit = GetToHit(my_hit.skill);
		my_hit.offense = GetOffense(my_hit.skill); // we need this a few times

		other->AvoidDamage(this, my_hit, bRiposte);

		//riposte
		if (my_hit.damage_done == DMG_RIPOSTED)
		{
			DoRiposte(other);
			if (IsDead()) return false;
		}
		if (my_hit.damage_done > 0) {
			// check avoidance AC
			if (!other->CheckHitChance(this, my_hit))
			{
				my_hit.damage_done = 0;
			}

			if (my_hit.damage_done > 0)
			{
				my_hit.damage_done = damageBonus + CalcMeleeDamage(other, my_hit.base_damage, my_hit.skill);
				TryCriticalHit(other, my_hit);

				EQ::ItemInstance* inst = CastToClient()->GetInv().GetItem(EQ::invslot::slotCharm);
				if (inst && inst->GetID() == RaceCharmIDs::CharmDarkElf)
				{
					if(my_hit.damage_done)
						my_hit.damage_done = my_hit.damage_done * 1.05;
				}
			}

			if (my_hit.damage_done > 0) {
				//try a finishing blow.. if successful end the attack
				TryFinishingBlow(other, my_hit.base_damage);
				CommonOutgoingHitSuccess(other, my_hit, opts);
			}
		}
	}
	else {
		my_hit.damage_done = DMG_INVULNERABLE;
	}

	// Hate Generation is on a per swing basis, regardless of a hit, miss, or block, its always the same.
	// If we are this far, this means we are atleast making a swing.

	other->AddToHateList(this, hate);

	//Guard Assist Code
	if (RuleB(Character, PVPEnableGuardFactionAssist)) {
		if (IsClient() && other->IsClient() || (HasOwner() && GetOwner()->IsClient() && other->IsClient() )) {
			auto& mob_list = entity_list.GetCloseMobList(other);
			for (auto& e : mob_list) {
				auto mob = e.second;
				if (mob->IsNPC() && mob->CastToNPC()->IsGuard()) {
					float distance = Distance(other->CastToClient()->m_Position, mob->GetPosition());
					if ((mob->CheckLosFN(other) || mob->CheckLosFN(this)) && distance <= 70) {
						auto petorowner = GetOwnerOrSelf();
						if (other->GetReverseFactionCon(mob) <= petorowner->GetReverseFactionCon(mob)) {
							mob->AddToHateList(this);
						}
					}
				}
			}
		}
	}

	///////////////////////////////////////////////////////////
	////// Send Attack Damage
	///////////////////////////////////////////////////////////
	other->Damage(this, my_hit.damage_done, SPELL_UNKNOWN, my_hit.skill, true, -1, false, m_specialattacks);

	if (IsDead()) {
		return false;
	}

	MeleeLifeTap(my_hit.damage_done);

	CommonBreakInvisibleFromCombat();

	if (GetTarget()) {
		TriggerDefensiveProcs(other, Hand, true, my_hit.damage_done);
	}

	if (my_hit.damage_done > 0) {
		return true;
	}
	else {
		return false;
	}
}

// Our database uses a min hit and max hit system, instead of Sony's DB + baseDmg * 0.1-2.0
// This calcs a DB (which is minHit - DI) from min and max hits
int64 NPC::GetDamageBonus()
{
	if (min_dmg > max_dmg)
		return min_dmg;

	int di1k = ((max_dmg - min_dmg) * 1000) / 19;		// multiply damage interval by 1000 to avoid using floats
	di1k = (di1k + 50) / 100 * 100;						// round DI to nearest tenth of a point
	int db = max_dmg * 1000 - di1k * 20;

	return db / 1000;
}

//used by complete heal and #heal
void Mob::Heal()
{
	SetMaxHP();
	SendHPUpdate();
}

void Client::Damage(Mob* other, int64 damage, uint16 spell_id, EQ::skills::SkillType attack_skill, bool avoidable, int8 buffslot, bool iBuffTic, eSpecialAttacks special)
{
	if (dead || IsCorpse())
		return;

	if (spell_id == 0)
		spell_id = SPELL_UNKNOWN;

	// cut all PVP spell damage to 2/3
	// Blasting ourselfs is considered PvP
	//Don't do PvP mitigation if the caster is damaging himself
	//should this be applied to all damage? comments sound like some is for spell DMG
	//patch notes on PVP reductions only mention archery/throwing ... not normal dmg
	if (other && other->IsClient() && (other != this) && damage > 0) {
		int PvPMitigation = 100;
		if (attack_skill == EQ::skills::SkillArchery || attack_skill == EQ::skills::SkillThrowing)
			PvPMitigation = 80;
		else
			PvPMitigation = 67;
		damage = std::max<int64_t>((damage * PvPMitigation) / 100, 1);
	}

	if (IsClient())
	{
		EQ::ItemInstance* inst = CastToClient()->GetInv().GetItem(EQ::invslot::slotCharm);
		if (inst && inst->GetID() == RaceCharmIDs::CharmHalfElf)
		{
			damage = std::max<int64_t>((damage * 95) / 100, 1);
		}
	}

	if (!ClientFinishedLoading())
		damage = -5;

	//do a majority of the work...
	CommonDamage(other, damage, spell_id, attack_skill, avoidable, buffslot, iBuffTic, special);

	if (damage > 0) {

		if (spell_id == SPELL_UNKNOWN)
			CheckIncreaseSkill(EQ::skills::SkillDefense, other, -15);
	}
}

bool Client::Death(Mob* killerMob, int64 damage, uint16 spell, EQ::skills::SkillType attack_skill)
{
	if (!ClientFinishedLoading())
		return false;

	if (dead)
		return false;	//cant die more than once...

	if (!spell)
		spell = SPELL_UNKNOWN;

	std::string export_string = fmt::format(
		"{} {} {} {}",
		killerMob ? killerMob->GetID() : 0,
		damage,
		spell,
		static_cast<int>(attack_skill)
	);
	if (parse->EventPlayer(EVENT_DEATH, this, export_string, 0) != 0) {
		if (GetHP() < 0) {
			SetHP(0);
		}
		return false;
	}

	if (killerMob && (killerMob->IsClient() || killerMob->IsBot()) && (spell != SPELL_UNKNOWN) && damage > 0) {
		char val1[20] = { 0 };

		entity_list.MessageCloseString(
			this, /* Sender */
			false, /* Skip Sender */
			RuleI(Range, DamageMessages),
			Chat::NonMelee, /* 283 */
			HIT_NON_MELEE, /* %1 hit %2 for %3 points of non-melee damage. */
			killerMob->GetCleanName(), /* Message1 */
			GetCleanName(), /* Message2 */
			ConvertArray(damage, val1)/* Message3 */
		);
	}

	int exploss = 0;
	LogCombat("Fatal blow dealt by [{}] with [{}] damage, spell [{}], skill [{}]", killerMob ? killerMob->GetName() : "Unknown", damage, spell, attack_skill);

	// #1: Send death packet to everyone
	uint8 killed_level = GetLevel();

	SendLogoutPackets();

	/* Make self become corpse packet */
	EQApplicationPacket app2(OP_BecomeCorpse, sizeof(BecomeCorpse_Struct));
	BecomeCorpse_Struct* bc = (BecomeCorpse_Struct*)app2.pBuffer;
	bc->spawn_id = GetID();
	bc->x = GetX();
	bc->y = GetY();
	bc->z = GetZ();
	QueuePacket(&app2);

	/* Make Death Packet */
	EQApplicationPacket app(OP_Death, sizeof(Death_Struct));
	Death_Struct* d = (Death_Struct*)app.pBuffer;
	d->spawn_id = GetID();
	d->killer_id = killerMob ? killerMob->GetID() : 0;
	d->corpseid = GetID();
	d->bindzoneid = m_pp.binds[0].zone_id;
	d->spell_id = spell == SPELL_UNKNOWN ? 0xffffffff : spell;
	d->attack_skill = spell != SPELL_UNKNOWN ? 0xe7 : attack_skill;
	d->damage = damage;
	app.priority = 6;
	entity_list.QueueClients(this, &app);

	// #2: figure out things that affect the player dying and mark them dead

	InterruptSpell();

	Mob* m_pet = GetPet();
	SetPet(0);
	SetHorseId(0);
	ShieldAbilityClearVariables();
	dead = true;

	if (m_pet && m_pet->IsCharmed()) {
		m_pet->BuffFadeByEffect(SE_Charm);
	}

	if (GetMerc()) {
		GetMerc()->Suspend();
	}

	if (killerMob != nullptr)
	{
		if (killerMob->IsNPC()) {
			parse->EventNPC(EVENT_SLAY, killerMob->CastToNPC(), this, "", 0);

			mod_client_death_npc(killerMob);

			uint16 emoteid = killerMob->GetEmoteID();
			if (emoteid != 0)
				killerMob->CastToNPC()->DoNPCEmote(KILLEDPC, emoteid, killerMob);
			killerMob->TrySpellOnKill(killed_level, spell);
		}

		if (killerMob->IsClient() && (IsDueling() || killerMob->CastToClient()->IsDueling())) {
			SetDueling(false);
			SetDuelTarget(0);
			if (killerMob->IsClient() && killerMob->CastToClient()->IsDueling() && killerMob->CastToClient()->GetDuelTarget() == GetID())
			{
				//if duel opponent killed us...
				killerMob->CastToClient()->SetDueling(false);
				killerMob->CastToClient()->SetDuelTarget(0);
				entity_list.DuelMessage(killerMob, this, false);

				mod_client_death_duel(killerMob);

			}
			else {
				//otherwise, we just died, end the duel.
				Mob* who = entity_list.GetMob(GetDuelTarget());
				if (who && who->IsClient()) {
					who->CastToClient()->SetDueling(false);
					who->CastToClient()->SetDuelTarget(0);
				}
			}
		}
	}

	entity_list.RemoveFromTargets(this, true);
	hate_list.RemoveEntFromHateList(this);
	RemoveAutoXTargets();

	//remove ourself from all proximities
	ClearAllProximities();

	/*
	#3: exp loss and corpse generation
	*/

	// figure out if they should lose exp
	if (RuleB(Character, UseDeathExpLossMult)) {
		float GetNum[] = { 0.005f,0.015f,0.025f,0.035f,0.045f,0.055f,0.065f,0.075f,0.085f,0.095f,0.110f };
		int Num = RuleI(Character, DeathExpLossMultiplier);
		if ((Num < 0) || (Num > 10))
			Num = 3;
		float loss = GetNum[Num];
		exploss = (int)((float)GetEXP() * (loss)); //loose % of total XP pending rule (choose 0-10)
	}

	if (!RuleB(Character, UseDeathExpLossMult)) {
		exploss = (int)(GetLevel() * (GetLevel() / 18.0) * 12000);
	}

	if (RuleB(Zone, LevelBasedEXPMods)) {
		// Death in levels with xp_mod (such as hell levels) was resulting
		// in losing more that appropriate since the loss was the same but
		// getting it back would take way longer.  This makes the death the
		// same amount of time to recover.  Will also lose more if level is
		// granting a bonus.
		exploss *= zone->level_exp_mod[GetLevel()].ExpMod;
	}

	if ((GetLevel() < RuleI(Character, DeathExpLossLevel)) || (GetLevel() > RuleI(Character, DeathExpLossMaxLevel)) || IsBecomeNPC())
	{
		exploss = 0;
	}
	else if (killerMob)
	{
		if (killerMob->IsClient())
		{
			exploss = 0;
		}
		else if (killerMob->GetOwner() && killerMob->GetOwner()->IsClient())
		{
			exploss = 0;
		}
	}

	if (spell != SPELL_UNKNOWN)
	{
		uint32 buff_count = GetMaxTotalSlots();
		for (uint16 buffIt = 0; buffIt < buff_count; buffIt++)
		{
			if (buffs[buffIt].spellid == spell && buffs[buffIt].client)
			{
				exploss = 0;	// no exp loss for pvp dot
				break;
			}
		}
	}

	bool LeftCorpse = false;

	// now we apply the exp loss, unmem their spells, and make a corpse
	// unless they're a GM (or less than lvl 10
	if (!GetGM())
	{
		if (exploss > 0) {
			int32 newexp = GetEXP();
			if (exploss > newexp) {
				//lost more than we have... wtf..
				newexp = 1;
			}
			else {
				newexp -= exploss;
			}
			SetEXP(newexp, GetAAXP());
			//m_epp.perAA = 0;	//reset to no AA exp on death.
		}

		int32 illusion_spell_id = spellbonuses.Illusion;

		//this generates a lot of 'updates' to the client that the client does not need
		if (RuleB(Spells, BuffsFadeOnDeath)) {
			BuffFadeNonPersistDeath();
		}

		if (RuleB(Character, UnmemSpellsOnDeath)) {
			if ((ClientVersionBit() & EQ::versions::maskSoFAndLater) && RuleB(Character, RespawnFromHover))
				UnmemSpellAll(true);
			else
				UnmemSpellAll(false);
		}

		if ((RuleB(Character, LeaveCorpses) && GetLevel() >= RuleI(Character, DeathItemLossLevel)) || RuleB(Character, LeaveNakedCorpses))
		{
			// creating the corpse takes the cash/items off the player too
			auto new_corpse = new Corpse(this, exploss);

			std::string tmp;
			database.GetVariable("ServerType", tmp);
			if (tmp[0] == '1' && tmp[1] == '\0' && killerMob != nullptr && killerMob->IsClient()) {
				database.GetVariable("PvPreward", tmp);
				int reward = atoi(tmp.c_str());
				if (reward == 3) {
					database.GetVariable("PvPitem", tmp);
					int pvpitem = atoi(tmp.c_str());
					if (pvpitem>0 && pvpitem<200000)
						new_corpse->SetPlayerKillItemID(pvpitem);
				}
				else if (reward == 2)
					new_corpse->SetPlayerKillItemID(-1);
				else if (reward == 1)
					new_corpse->SetPlayerKillItemID(1);
				else
					new_corpse->SetPlayerKillItemID(0);
				if (killerMob->CastToClient()->isgrouped) {
					Group* group = entity_list.GetGroupByClient(killerMob->CastToClient());
					if (group != 0)
					{
						for (int i = 0; i<6; i++)
						{
							if (group->membername[i][0])
							{
								new_corpse->AllowPlayerLoot(group->membername[i]);
							}
						}
					}
				}
			}
			entity_list.AddCorpse(new_corpse, GetID());
			SetID(0);

			//send the become corpse packet to everybody else in the zone.
			entity_list.QueueClients(this, &app2, true);
			ApplyIllusionToCorpse(illusion_spell_id, new_corpse);
			LeftCorpse = true;
		}
	}
	else {
		BuffFadeDetrimental();
	}

	/*
	Reset AA reuse timers that need to be, live-like this is only Lay on Hands
	*/
	ResetOnDeathAlternateAdvancement();

	/*
	Reset reuse timer for classic skill based Lay on Hands (For tit I guess)
	*/
	if (GetClass() == PALADIN) // we could check if it's not expired I guess, but should be fine not to
		p_timers.Clear(&database, pTimerLayHands);

	/*
	Finally, send em home

	We change the mob variables, not pp directly, because Save() will copy
	from these and overwrite what we set in pp anyway
	*/

	if (LeftCorpse && (ClientVersionBit() & EQ::versions::maskSoFAndLater) && RuleB(Character, RespawnFromHover))
	{
		ClearDraggedCorpses();
		RespawnFromHoverTimer.Start(RuleI(Character, RespawnFromHoverTimer) * 1000);
		SendRespawnBinds();
	}
	else
	{
		if (isgrouped)
		{
			Group *g = GetGroup();
			if (g)
				g->MemberZoned(this);
		}

		Raid* r = entity_list.GetRaidByClient(this);

		if (r)
			r->MemberZoned(this);

		dead_timer.Start(5000, true);
		m_pp.zone_id = m_pp.binds[0].zone_id;
		m_pp.zoneInstance = m_pp.binds[0].instance_id;
		if (GetLevel() >= 10)
		{
			SetHP(-5);
			SetMana(0);
			SetEndurance(0);
		}
		database.MoveCharacterToZone(CharacterID(), m_pp.zone_id);
		Save();
		GoToDeath();
	}

	/* QS: PlayerLogDeaths */
	if (RuleB(QueryServ, PlayerLogDeaths)) {
		const char * killer_name = "";
		if (killerMob && killerMob->GetCleanName()) { killer_name = killerMob->GetCleanName(); }
		std::string event_desc = StringFormat("Died in zoneid:%i instid:%i by '%s', spellid:%i, damage:%i", GetZoneID(), GetInstanceID(), killer_name, spell, damage);
		QServ->PlayerLogEvent(Player_Log_Deaths, CharacterID(), event_desc);
	}

	parse->EventPlayer(EVENT_DEATH_COMPLETE, this, export_string, 0);
	return true;
}
//SYNC WITH: tune.cpp, mob.h TuneNPCAttack
bool NPC::Attack(Mob* other, int Hand, bool bRiposte, bool IsStrikethrough, bool IsFromSpell, ExtraAttackOptions *opts)
{
	if (!other) {
		SetTarget(nullptr);
		LogError("A null Mob object was passed to NPC::Attack() for evaluation!");
		return false;
	}

	if (DivineAura())
		return(false);

	if (!GetTarget())
		SetTarget(other);

	//Check that we can attack before we calc heading and face our target
	if (!IsAttackAllowed(other)) {
		if (GetOwnerID())
			SayString(NOT_LEGAL_TARGET);
		if (other) {
			if (other->IsClient())
				other->CastToClient()->RemoveXTarget(this, false);
			RemoveFromHateList(other);
			LogCombat("I am not allowed to attack [{}]", other->GetName());
		}
		return false;
	}

	FaceTarget(GetTarget());

	DamageHitInfo my_hit;
	my_hit.skill = EQ::skills::SkillHandtoHand;
	my_hit.hand = Hand;
	my_hit.damage_done = 1;
	if (Hand == EQ::invslot::slotPrimary) {
		my_hit.skill = static_cast<EQ::skills::SkillType>(GetPrimSkill());
		OffHandAtk(false);
	}
	if (Hand == EQ::invslot::slotSecondary) {
		my_hit.skill = static_cast<EQ::skills::SkillType>(GetSecSkill());
		OffHandAtk(true);
	}

	//figure out what weapon they are using, if any
	const EQ::ItemData* weapon = nullptr;
	if (Hand == EQ::invslot::slotPrimary && equipment[EQ::invslot::slotPrimary] > 0)
		weapon = database.GetItem(equipment[EQ::invslot::slotPrimary]);
	else if (equipment[EQ::invslot::slotSecondary])
		weapon = database.GetItem(equipment[EQ::invslot::slotSecondary]);

	//We dont factor much from the weapon into the attack.
	//Just the skill type so it doesn't look silly using punching animations and stuff while wielding weapons
	if (weapon) {
		LogCombat("Attacking with weapon: [{}] ([{}]) (too bad im not using it for much)", weapon->Name, weapon->ID);

		if (Hand == EQ::invslot::slotSecondary && !weapon->IsType1HWeapon()) {
			LogCombat("Attack with non-weapon cancelled");
			return false;
		}

		switch (weapon->ItemType) {
		case EQ::item::ItemType1HSlash:
			my_hit.skill = EQ::skills::Skill1HSlashing;
			break;
		case EQ::item::ItemType2HSlash:
			my_hit.skill = EQ::skills::Skill2HSlashing;
			break;
		case EQ::item::ItemType1HPiercing:
			my_hit.skill = EQ::skills::Skill1HPiercing;
			break;
		case EQ::item::ItemType2HPiercing:
			my_hit.skill = EQ::skills::Skill2HPiercing;
			break;
		case EQ::item::ItemType1HBlunt:
			my_hit.skill = EQ::skills::Skill1HBlunt;
			break;
		case EQ::item::ItemType2HBlunt:
			my_hit.skill = EQ::skills::Skill2HBlunt;
			break;
		case EQ::item::ItemTypeBow:
			my_hit.skill = EQ::skills::SkillArchery;
			break;
		case EQ::item::ItemTypeLargeThrowing:
		case EQ::item::ItemTypeSmallThrowing:
			my_hit.skill = EQ::skills::SkillThrowing;
			break;
		default:
			my_hit.skill = EQ::skills::SkillHandtoHand;
			break;
		}
	}

	//Guard Assist Code
	if (RuleB(Character, PVPEnableGuardFactionAssist)) {
		if (IsClient() && other->IsClient() || (HasOwner() && GetOwner()->IsClient() && other->IsClient())) {
			auto& mob_list = entity_list.GetCloseMobList(other);
			for (auto& e : mob_list) {
				auto mob = e.second;
				if (mob->IsNPC() && mob->CastToNPC()->IsGuard()) {
					float distance = Distance(other->GetPosition(), mob->GetPosition());
					if ((mob->CheckLosFN(other) || mob->CheckLosFN(this)) && distance <= 70) {
						if (other->GetReverseFactionCon(mob) <= GetOwner()->GetReverseFactionCon(mob)) {
							mob->AddToHateList(this);
						}
					}
				}
			}
		}
	}

	int64 weapon_damage = GetBaseDamage(other, Hand);

	int damageBonus = GetDamageBonus();
	int hate = weapon_damage / 2;
	//do attack animation regardless of whether or not we can hit below
	int16 charges = 0;
	EQ::ItemInstance weapon_inst(weapon, charges);
	my_hit.skill = AttackAnimation(Hand, &weapon_inst, my_hit.skill);

	//basically "if not immune" then do the attack
	if (weapon_damage > 0) {

		//ele and bane dmg too
		//NPCs add this differently than PCs
		//if NPCs can't inheriently hit the target we don't add bane/magic dmg which isn't exactly the same as PCs
		int eleBane = 0;
		if (weapon) {
			if (RuleB(NPC, UseBaneDamage)) {
				if (weapon->BaneDmgBody == other->GetBodyType()) {
					eleBane += weapon->BaneDmgAmt;
				}

				if (weapon->BaneDmgRace == other->GetRace()) {
					eleBane += weapon->BaneDmgRaceAmt;
				}
			}

			// I don't think NPCs use this either ....
			if (weapon->ElemDmgAmt) {
				eleBane += (weapon->ElemDmgAmt * other->ResistSpell(weapon->ElemDmgType, 0, this) / 100);
			}
		}

		if (!RuleB(NPC, UseItemBonusesForNonPets)) {
			if (!GetOwner()) {
				eleBane = 0;
			}
		}

		uint8 otherlevel = other->GetLevel();
		uint8 mylevel = GetLevel();

		otherlevel = otherlevel ? otherlevel : 1;
		mylevel = mylevel ? mylevel : 1;

		//damage = mod_npc_damage(damage, skillinuse, Hand, weapon, other);

		my_hit.base_damage = weapon_damage;
		my_hit.min_damage = 1;
		int32 hate = my_hit.base_damage / 2;

		int hit_chance_bonus = 0;

		if (opts) {
			my_hit.base_damage *= opts->damage_percent;
			my_hit.base_damage += opts->damage_flat;
			hate *= opts->hate_percent;
			hate += opts->hate_flat;
			hit_chance_bonus += opts->hit_chance;
		}

		my_hit.offense = GetOffense(my_hit.skill);
		my_hit.tohit = GetToHit(my_hit.skill);

		other->AvoidDamage(this, my_hit, bRiposte);


		if (my_hit.damage_done > 0) {
			// check avoidance AC
			if (!other->CheckHitChance(this, my_hit))
			{
				my_hit.damage_done = 0;
			}
			if (my_hit.damage_done > 0) {
				CommonOutgoingHitSuccess(other, my_hit, opts);
			}
		}

		if(my_hit.damage_done > 0)
			my_hit.damage_done = damageBonus + CalcMeleeDamage(other, my_hit.base_damage, my_hit.skill);

		other->AddToHateList(this, hate);

		LogCombat("Final damage against [{}]: [{}]", other->GetName(), my_hit.damage_done);
	}
	else {
		my_hit.damage_done = DMG_INVULNERABLE;
	}

	if (GetHP() > 0 && !other->HasDied()) {
		other->Damage(this, my_hit.damage_done, SPELL_UNKNOWN, my_hit.skill, true, -1, false, m_specialattacks); // Not avoidable client already had thier chance to Avoid
	}
	else
		return false;

	if (HasDied()) //killed by damage shield ect
		return false;

	MeleeLifeTap(my_hit.damage_done);

	CommonBreakInvisibleFromCombat();

	//I doubt this works...
	if (!GetTarget())
		return true; //We killed them

	if (!bRiposte && !other->HasDied()) {
		TryWeaponProc(nullptr, weapon, other, Hand);	//no weapon

		if (!other->HasDied())
			TrySpellProc(nullptr, weapon, other, Hand);

		if (my_hit.damage_done > 0 && HasSkillProcSuccess() && !other->HasDied())
			TrySkillProc(other, my_hit.skill, 0, true, Hand);
	}

	if (GetHP() > 0 && !other->HasDied())
		TriggerDefensiveProcs(other, Hand, true, my_hit.damage_done);

	// now check ripostes
	if (my_hit.damage_done == DMG_RIPOSTED) { // riposting
		DoRiposte(other);
	}

	if (my_hit.damage_done > 0)
		return true;

	else
		return false;
}


void NPC::Damage(Mob* other, int64 damage, uint16 spell_id, EQ::skills::SkillType attack_skill, bool avoidable, int8 buffslot, bool iBuffTic, eSpecialAttacks special) {
	if (spell_id == 0)
		spell_id = SPELL_UNKNOWN;

	//handle EVENT_ATTACK. Resets after we have not been attacked for 12 seconds
	if (attacked_timer.Check())
	{
		LogCombat("Triggering EVENT_ATTACK due to attack by [{}]", other ? other->GetName() : "nullptr");
		parse->EventNPC(EVENT_ATTACK, this, other, "", 0);
	}
	attacked_timer.Start(CombatEventTimer_expire);

	if (!IsEngaged())
		zone->AddAggroMob();

	if (GetClass() == LDON_TREASURE)
	{
		if (IsLDoNLocked() && GetLDoNLockedSkill() != LDoNTypeMechanical)
		{
			damage = -5;
		}
		else
		{
			if (IsLDoNTrapped())
			{
				MessageString(Chat::Red, LDON_ACCIDENT_SETOFF2);
				SpellFinished(GetLDoNTrapSpellID(), other, EQ::spells::CastingSlot::Item, 0, -1, spells[GetLDoNTrapSpellID()].resist_difficulty, false);
				SetLDoNTrapSpellID(0);
				SetLDoNTrapped(false);
				SetLDoNTrapDetected(false);
			}
		}
	}

	//do a majority of the work...
	CommonDamage(other, damage, spell_id, attack_skill, avoidable, buffslot, iBuffTic, special);

	if (damage > 0) {
		//see if we are gunna start fleeing
		if (!IsPet()) CheckFlee();
	}
}

bool NPC::Death(Mob* killer_mob, int64 damage, uint16 spell, EQ::skills::SkillType attack_skill)
{
	LogCombat("Fatal blow dealt by [{}] with [{}] damage, spell [{}], skill [{}]",
		((killer_mob) ? (killer_mob->GetName()) : ("[nullptr]")), damage, spell, attack_skill);

	Mob *oos = nullptr;
	if (killer_mob) {
		oos = killer_mob->GetOwnerOrSelf();
		std::string export_string = fmt::format(
			"{} {} {} {}",
			killer_mob->GetID(),
			damage,
			spell,
			static_cast<int>(attack_skill)
		);
		if (parse->EventNPC(EVENT_DEATH, this, oos, export_string, 0) != 0) {
			if (GetHP() < 0) {
				SetHP(0);
			}
			return false;
		}

		if ((killer_mob->IsClient() || killer_mob->IsBot()) && (spell != SPELL_UNKNOWN) && damage > 0) {
			char val1[20] = { 0 };

			entity_list.MessageCloseString(
				this, /* Sender */
				false, /* Skip Sender */
				RuleI(Range, DamageMessages),
				Chat::NonMelee, /* 283 */
				HIT_NON_MELEE, /* %1 hit %2 for %3 points of non-melee damage. */
				killer_mob->GetCleanName(), /* Message1 */
				GetCleanName(), /* Message2 */
				ConvertArray(damage, val1) /* Message3 */
			);
		}
	}
	else {
		std::string export_string = fmt::format(
			"{} {} {} {}",
			0,
			damage,
			spell,
			static_cast<int>(attack_skill)
		);
		if (parse->EventNPC(EVENT_DEATH, this, nullptr, export_string, 0) != 0) {
			if (GetHP() < 0) {
				SetHP(0);
			}
			return false;
		}
	}

	if (IsEngaged()) {
		zone->DelAggroMob();
		Log(Logs::Detail, Logs::Attack, "%s Mobs currently Aggro %i", __FUNCTION__, zone->MobsAggroCount());
	}

	ShieldAbilityClearVariables();

	SetHP(0);
	SetPet(0);

	if (GetSwarmOwner()) {
		Mob* owner = entity_list.GetMobID(GetSwarmOwner());
		if (owner)
			owner->SetTempPetCount(owner->GetTempPetCount() - 1);
	}

	Mob* killer = GetHateDamageTop(this);

	entity_list.RemoveFromTargets(this, p_depop);

	if (p_depop == true)
		return false;

	int32 illusion_spell_id = spellbonuses.Illusion;

	HasAISpellEffects = false;
	BuffFadeAll();
	uint8 killed_level = GetLevel();

	if (GetClass() == LDON_TREASURE) { // open chest
		auto outapp = new EQApplicationPacket(OP_Animation, sizeof(Animation_Struct));
		Animation_Struct* anim = (Animation_Struct*)outapp->pBuffer;
		anim->spawnid = GetID();
		anim->action = 0x0F;
		anim->speed = 10;
		entity_list.QueueCloseClients(this, outapp);
		safe_delete(outapp);
	}

	auto app = new EQApplicationPacket(OP_Death, sizeof(Death_Struct));
	Death_Struct* d = (Death_Struct*)app->pBuffer;
	d->spawn_id = GetID();
	d->killer_id = killer_mob ? killer_mob->GetID() : 0;
	d->bindzoneid = 0;
	d->spell_id = 0xffffffff; // Sending spell was causing extra DoT land msg
	d->attack_skill = SkillDamageTypes[attack_skill];
	d->damage = damage;
	app->priority = 6;
	entity_list.QueueClients(killer_mob, app, false);

	safe_delete(app);

	if (respawn2) {
		respawn2->DeathReset(1);
	}

	if (killer_mob && GetClass() != LDON_TREASURE)
		hate_list.AddEntToHateList(killer_mob, damage);

	Mob *give_exp = hate_list.GetDamageTopOnHateList(this);

	if (give_exp == nullptr)
		give_exp = killer;

	if (give_exp && give_exp->HasOwner()) {

		bool ownerInGroup = false;
		if ((give_exp->HasGroup() && give_exp->GetGroup()->IsGroupMember(give_exp->GetUltimateOwner()))
			|| (give_exp->IsPet() && (give_exp->GetOwner()->IsClient()
				|| (give_exp->GetOwner()->HasGroup() && give_exp->GetOwner()->GetGroup()->IsGroupMember(give_exp->GetOwner()->GetUltimateOwner())))))
			ownerInGroup = true;

		give_exp = give_exp->GetUltimateOwner();
	}

	if (give_exp && give_exp->IsTempPet() && give_exp->IsPetOwnerClient()) {
		if (give_exp->IsNPC() && give_exp->CastToNPC()->GetSwarmOwner()) {
			Mob* temp_owner = entity_list.GetMobID(give_exp->CastToNPC()->GetSwarmOwner());
			if (temp_owner)
				give_exp = temp_owner;
		}
	}

	int PlayerCount = 0; // QueryServ Player Counting
	time_t curTime = time(nullptr);
	Client *give_exp_client = nullptr;
	if (give_exp && give_exp->IsClient())
		give_exp_client = give_exp->CastToClient();

	//do faction hits even if we are a merchant, so long as a player killed us
	if (!IsCharmed() && give_exp_client && !RuleB(NPC, EnableMeritBasedFaction))
		hate_list.DoFactionHits(GetNPCFactionID());

	bool IsLdonTreasure = (GetClass() == LDON_TREASURE);

	bool awardAA = false;

	if (give_exp_client && !IsCorpse()) {
		Group *kg = entity_list.GetGroupByClient(give_exp_client);
		Raid *kr = entity_list.GetRaidByClient(give_exp_client);

		int32 finalxp = give_exp_client->GetExperienceForKill(this);
		finalxp = give_exp_client->mod_client_xp(finalxp, this);

		// handle task credit on behalf of the killer
		if (RuleB(TaskSystem, EnableTaskSystem)) {
			LogTasksDetail(
				"[NPC::Death] Triggering HandleUpdateTasksOnKill for [{}] npc [{}]",
				give_exp_client->GetCleanName(),
				GetNPCTypeID()
			);
			task_manager->HandleUpdateTasksOnKill(give_exp_client, GetNPCTypeID(), GetCleanName());
		}

		if (kr) {
			if (!IsLdonTreasure && MerchantType == 0) {
				kr->SplitExp((finalxp), this);
				if (killer_mob && (kr->IsRaidMember(killer_mob->GetName()) || kr->IsRaidMember(killer_mob->GetUltimateOwner()->GetName())))
					killer_mob->TrySpellOnKill(killed_level, spell);
			}

			/* Send the EVENT_KILLED_MERIT event for all raid members */
			for (int i = 0; i < MAX_RAID_MEMBERS; i++) {
				if (kr->members[i].member != nullptr && kr->members[i].member->IsClient()) { // If Group Member is Client
					Client* c = kr->members[i].member;
					parse->EventNPC(EVENT_KILLED_MERIT, this, c, "killed", 0);

					if (RuleB(NPC, EnableMeritBasedFaction))
						c->SetFactionLevel(c->CharacterID(), GetNPCFactionID(), c->GetBaseClass(), c->GetBaseRace(), c->GetDeity());
					mod_npc_killed_merit(kr->members[i].member);
				}

				if (!awardAA && !flag_granted.empty())
				{
					auto nameItr = m_EngagedClientNames.find(kr->members[i].membername);
					if(nameItr != m_EngagedClientNames.end() && nameItr->second.isFlagged == 0 && !nameItr->second.HasLockout(curTime))
						awardAA = true;
				}
			}

			// QueryServ Logging - Raid Kills
			if (RuleB(QueryServ, PlayerLogNPCKills)) {
				auto pack =
					new ServerPacket(ServerOP_QSPlayerLogNPCKills,
						sizeof(QSPlayerLogNPCKill_Struct) +
						(sizeof(QSPlayerLogNPCKillsPlayers_Struct) * PlayerCount));
				PlayerCount = 0;
				QSPlayerLogNPCKill_Struct* QS = (QSPlayerLogNPCKill_Struct*)pack->pBuffer;
				QS->s1.NPCID = GetNPCTypeID();
				QS->s1.ZoneID = GetZoneID();
				QS->s1.Type = 2; // Raid Fight
				for (int i = 0; i < MAX_RAID_MEMBERS; i++) {
					if (kr->members[i].member != nullptr && kr->members[i].member->IsClient()) { // If Group Member is Client
						Client *c = kr->members[i].member;
						QS->Chars[PlayerCount].char_id = c->CharacterID();
						PlayerCount++;
					}
				}
				worldserver.SendPacket(pack); // Send Packet to World
				safe_delete(pack);
			}
			// End QueryServ Logging

		}
		else if (give_exp_client->IsGrouped() && kg != nullptr) {
			if (!IsLdonTreasure && MerchantType == 0) {
				kg->SplitExp((finalxp), this);
				if (killer_mob && (kg->IsGroupMember(killer_mob->GetName()) || kg->IsGroupMember(killer_mob->GetUltimateOwner()->GetName())))
					killer_mob->TrySpellOnKill(killed_level, spell);
			}

			/* Send the EVENT_KILLED_MERIT event and update kill tasks
			* for all group members */
			for (int i = 0; i < MAX_GROUP_MEMBERS; i++) {
				if (kg->members[i] != nullptr && kg->members[i]->IsClient()) { // If Group Member is Client
					Client* c = kg->members[i]->CastToClient();
					parse->EventNPC(EVENT_KILLED_MERIT, this, c, "killed", 0);

					if (RuleB(NPC, EnableMeritBasedFaction))
						c->SetFactionLevel(c->CharacterID(), GetNPCFactionID(), c->GetBaseClass(), c->GetBaseRace(), c->GetDeity());

					mod_npc_killed_merit(c);
				}

				if (!awardAA && !flag_granted.empty())
				{
					auto nameItr = m_EngagedClientNames.find(kg->membername[i]);
					if (nameItr != m_EngagedClientNames.end() && nameItr->second.isFlagged == 0 && !nameItr->second.HasLockout(curTime))
						awardAA = true;
				}
			}

			// QueryServ Logging - Group Kills
			if (RuleB(QueryServ, PlayerLogNPCKills)) {
				auto pack =
					new ServerPacket(ServerOP_QSPlayerLogNPCKills,
						sizeof(QSPlayerLogNPCKill_Struct) +
						(sizeof(QSPlayerLogNPCKillsPlayers_Struct) * PlayerCount));
				PlayerCount = 0;
				QSPlayerLogNPCKill_Struct* QS = (QSPlayerLogNPCKill_Struct*)pack->pBuffer;
				QS->s1.NPCID = GetNPCTypeID();
				QS->s1.ZoneID = GetZoneID();
				QS->s1.Type = 1; // Group Fight
				for (int i = 0; i < MAX_GROUP_MEMBERS; i++) {
					if (kg->members[i] != nullptr && kg->members[i]->IsClient()) { // If Group Member is Client
						Client *c = kg->members[i]->CastToClient();
						QS->Chars[PlayerCount].char_id = c->CharacterID();
						PlayerCount++;
					}
				}
				worldserver.SendPacket(pack); // Send Packet to World
				safe_delete(pack);
			}
			// End QueryServ Logging
		}
		else {
			if (!IsLdonTreasure && MerchantType == 0) {
				int conlevel = give_exp->GetLevelCon(GetLevel());
				if (conlevel != CON_GRAY) {
					if (!GetOwner() || (GetOwner() && !GetOwner()->IsClient())) {
						give_exp_client->AddEXP((finalxp), conlevel);
						if (killer_mob && (killer_mob->GetID() == give_exp_client->GetID() || killer_mob->GetUltimateOwner()->GetID() == give_exp_client->GetID()))
							killer_mob->TrySpellOnKill(killed_level, spell);
					}
				}
			}

			/* Send the EVENT_KILLED_MERIT event */
			parse->EventNPC(EVENT_KILLED_MERIT, this, give_exp_client, "killed", 0);

			if (RuleB(NPC, EnableMeritBasedFaction))
				give_exp_client->SetFactionLevel(give_exp_client->CharacterID(), GetNPCFactionID(), give_exp_client->GetBaseClass(),
					give_exp_client->GetBaseRace(), give_exp_client->GetDeity());

			mod_npc_killed_merit(give_exp_client);

			if (!awardAA && !flag_granted.empty())
			{
				auto nameItr = m_EngagedClientNames.find(give_exp_client->GetCleanName());
				if (nameItr != m_EngagedClientNames.end() && nameItr->second.isFlagged == 0 && !nameItr->second.HasLockout(curTime))
					awardAA = true;
			}

			// QueryServ Logging - Solo
			if (RuleB(QueryServ, PlayerLogNPCKills)) {
				auto pack = new ServerPacket(ServerOP_QSPlayerLogNPCKills,
					sizeof(QSPlayerLogNPCKill_Struct) +
					(sizeof(QSPlayerLogNPCKillsPlayers_Struct) * 1));
				QSPlayerLogNPCKill_Struct* QS = (QSPlayerLogNPCKill_Struct*)pack->pBuffer;
				QS->s1.NPCID = GetNPCTypeID();
				QS->s1.ZoneID = GetZoneID();
				QS->s1.Type = 0; // Solo Fight
				Client *c = give_exp_client;
				QS->Chars[0].char_id = c->CharacterID();
				PlayerCount++;
				worldserver.SendPacket(pack); // Send Packet to World
				safe_delete(pack);
			}
			// End QueryServ Logging
		}
	}

	bool    allow_merchant_corpse = RuleB(Merchant, AllowCorpse);
	bool    is_merchant = (class_ == MERCHANT || class_ == ADVENTURE_MERCHANT || MerchantType != 0);

	if (!HasOwner() && !IsMerc() && !GetSwarmInfo() && (!is_merchant || allow_merchant_corpse) &&
		((killer && (killer->IsClient() || (killer->HasOwner() && killer->GetUltimateOwner()->IsClient()) ||
		(killer->IsNPC() && killer->CastToNPC()->GetSwarmInfo() && killer->CastToNPC()->GetSwarmInfo()->GetOwner() && killer->CastToNPC()->GetSwarmInfo()->GetOwner()->IsClient())))
		|| (killer_mob && IsLdonTreasure)))
	{
		if (killer != 0) {
			if (killer->GetOwner() != 0 && killer->GetOwner()->IsClient())
				killer = killer->GetOwner();
		}

		entity_list.RemoveFromAutoXTargets(this);

		const EQ::ItemData* aaTokenItem = database.GetItem(999);
		if (aaTokenItem && awardAA)
		{

			ServerLootItem_Struct qitem;
			qitem.item_id = 999;
			qitem.charges = 1;
			qitem.quest = true;
			qitem.pet = false;
			qitem.forced = false;
			qitem.aug_1 = 0;
			qitem.aug_2 = 0;
			qitem.aug_3 = 0;
			qitem.aug_4 = 0;
			qitem.aug_5 = 0;
			qitem.aug_6 = 0;

			qitem.attuned = 0;
			qitem.min_level = 0;
			qitem.max_level = 255;
			qitem.equip_slot = EQ::invslot::SLOT_INVALID;

			quest_itemlist.push_back(qitem);
		}


		const EQ::ItemData* progressionFlagToken = NPCTypedata && NPCTypedata->flag_item > 0 ? database.GetItem(NPCTypedata->flag_item) : nullptr;
		if (progressionFlagToken)
		{

			ServerLootItem_Struct qitem;
			qitem.item_id = progressionFlagToken->ID;
			qitem.charges = progressionFlagToken->MaxCharges;
			qitem.quest = true;
			qitem.pet = false;
			qitem.forced = false;
			qitem.aug_1 = 0;
			qitem.aug_2 = 0;
			qitem.aug_3 = 0;
			qitem.aug_4 = 0;
			qitem.aug_5 = 0;
			qitem.aug_6 = 0;

			qitem.attuned = 0;
			qitem.min_level = 0;
			qitem.max_level = 255;
			qitem.equip_slot = EQ::invslot::SLOT_INVALID;

			quest_itemlist.push_back(qitem);
		}

		uint16 emoteid = GetEmoteID();
		auto corpse = new Corpse(this, GetNPCTypeID(), &NPCTypedata, give_exp_client,

			level > 54 ? RuleI(NPC, MajorNPCCorpseDecayTimeMS)
			: RuleI(NPC, MinorNPCCorpseDecayTimeMS));
		entity_list.LimitRemoveNPC(this);
		entity_list.AddCorpse(corpse, GetID());

		entity_list.UnMarkNPC(GetID());
		entity_list.RemoveNPC(GetID());

		// entity_list.RemoveMobFromCloseLists(this);
		close_mobs.clear();
		SetID(0);
		ApplyIllusionToCorpse(illusion_spell_id, corpse);

		if (killer != 0 && emoteid != 0)
			corpse->CastToNPC()->DoNPCEmote(AFTERDEATH, emoteid, killer);
		if (killer != 0 && killer->IsClient()) {
			corpse->AllowPlayerLoot(killer->GetCleanName());
			if (killer->IsGrouped()) {
				Group* group = entity_list.GetGroupByClient(killer->CastToClient());
				if (group != 0) {
					for (int i = 0; i<6; i++) { // Doesnt work right, needs work
						if (group->membername[i][0]) {
							if(m_EngagedClientNames.find(group->membername[i]) != m_EngagedClientNames.end())
								corpse->AllowPlayerLoot(group->membername[i]);
						}
					}
				}
			}
			else if (killer->IsRaidGrouped()) {
				Raid* r = entity_list.GetRaidByClient(killer->CastToClient());
				if (r) {
					int i = 0;
					for (int x = 0; x < MAX_RAID_MEMBERS; x++) {
						switch (r->GetLootType()) {
						case 0:
						case 1:
							if (r->members[x].membername[0] && r->members[x].IsRaidLeader) {
								corpse->AllowPlayerLoot(r->members[x].membername);
								i++;
							}
							break;
						case 2:
							if (r->members[x].membername[0] && r->members[x].IsRaidLeader) {
								corpse->AllowPlayerLoot(r->members[x].membername);
								i++;
							}
							if (r->members[x].membername[0] && r->members[x].IsGroupLeader) {
								corpse->AllowPlayerLoot(r->members[x].membername);
								i++;
							}
							break;
						case 3:
							if (r->members[x].membername[0] && r->members[x].IsLooter) {
								corpse->AllowPlayerLoot(r->members[x].membername);
								i++;
							}
							break;
						case 4:
							if (r->members[x].membername[0]) {
								corpse->AllowPlayerLoot(r->members[x].membername);
								i++;
							}
							break;
						}
					}
				}
			}
		}
		else if (killer_mob && IsLdonTreasure) {
			auto u_owner = killer_mob->GetUltimateOwner();
			if (u_owner->IsClient())
				corpse->AllowPlayerLoot(u_owner->GetCleanName());
		}

		if (zone && zone->adv_data) {
			ServerZoneAdventureDataReply_Struct *sr = (ServerZoneAdventureDataReply_Struct*)zone->adv_data;
			if (sr->type == Adventure_Kill) {
				zone->DoAdventureCountIncrease();
			}
			else if (sr->type == Adventure_Assassinate) {
				if (sr->data_id == GetNPCTypeID()) {
					zone->DoAdventureCountIncrease();
				}
				else {
					zone->DoAdventureAssassinationCountIncrease();
				}
			}
		}
	}
	else {
		entity_list.RemoveFromXTargets(this);
	}

	// Parse quests even if we're killed by an NPC
	if (oos) {
		mod_npc_killed(oos);

		uint16 emoteid = GetEmoteID();
		if (emoteid != 0)
			DoNPCEmote(ONDEATH, emoteid, oos);
		
		if (oos->IsNPC()) {
			parse->EventNPC(EVENT_NPC_SLAY, oos->CastToNPC(), this, "", 0);
			uint16 emoteid = oos->GetEmoteID();
			if (emoteid != 0)
				oos->CastToNPC()->DoNPCEmote(KILLEDNPC, emoteid, oos);
			killer_mob->TrySpellOnKill(killed_level, spell);
		}
	}

	WipeHateList();
	p_depop = true;

	if (killer_mob && killer_mob->GetTarget() == this) //we can kill things without having them targeted
		killer_mob->SetTarget(nullptr); //via AE effects and such..

	entity_list.UpdateFindableNPCState(this, true);

	std::string export_string = fmt::format(
		"{} {} {} {}",
		killer_mob ? killer_mob->GetID() : 0,
		damage,
		spell,
		static_cast<int>(attack_skill)
	);
	parse->EventNPC(EVENT_DEATH_COMPLETE, this, oos, export_string, 0);
	combat_record.Stop();

	/* Zone controller process EVENT_DEATH_ZONE (Death events) */
	if (RuleB(Zone, UseZoneController)) {
		auto controller = entity_list.GetNPCByNPCTypeID(ZONE_CONTROLLER_NPC_ID);
		if (controller && GetNPCTypeID() != ZONE_CONTROLLER_NPC_ID) {
			export_string = fmt::format(
				"{} {} {} {} {} {:.2f} {:.2f} {:.2f} {:.2f}",
				killer_mob ? killer_mob->GetID() : 0,
				damage,
				spell,
				static_cast<int>(attack_skill),
				GetNPCTypeID(),
				GetX(),
				GetY(),
				GetZ(),
				GetHeading()
			);
			parse->EventNPC(EVENT_DEATH_ZONE, controller, nullptr, export_string, 0);
		}
	}

	return true;
}

void Mob::AddToHateList(Mob* other, int64 hate /*= 0*/, int64 damage /*= 0*/, bool iYellForHelp /*= true*/, bool bFrenzy /*= false*/, bool iBuffTic /*= false*/, uint16 spell_id, bool pet_command)
{
	if (!other)
		return;

	if (other == this)
		return;

	if (other->IsTrap())
		return;

	if (damage < 0) {
		hate = 1;
	}

	if (iYellForHelp)
		SetPrimaryAggro(true);
	else
		SetAssistAggro(true);

	bool wasengaged = IsEngaged();
	Mob* owner = other->GetOwner();
	Mob* mypet = GetPet();
	Mob* myowner = GetOwner();
	Mob* targetmob = GetTarget();
	bool on_hatelist = CheckAggro(other);

	if (other) {
		AddRampage(other);
		if (on_hatelist) { // odd reason, if you're not on the hate list, subtlety etc don't apply!
						   // Spell Casting Subtlety etc
			int64 hatemod = 100 + other->spellbonuses.hatemod + other->itembonuses.hatemod + other->aabonuses.hatemod;

			if (hatemod < 1)
				hatemod = 1;
			hate = ((hate * (hatemod)) / 100);
		}
		else {
			hate += 100; // 100 bonus initial aggro
		}
	}

	// Pet that is /pet hold on will not add to their hate list if they're not engaged
	// Pet that is /pet hold on and /pet focus on will not add others to their hate list
	// Pet that is /pet ghold on will never add to their hate list unless /pet attack or /pet qattack

	// we skip these checks if it's forced through a pet command
	if (!pet_command) {
		if (IsPet()) {
			if ((IsGHeld() || (IsHeld() && IsFocused())) && !on_hatelist) // we want them to be able to climb the hate list
				return;
			if ((IsHeld() || IsPetStop() || IsPetRegroup()) && !wasengaged) // not 100% sure on stop/regroup kind of hard to test, but regroup is like "classic hold"
				return;
		}
	}

	if (other->IsNPC() && (other->IsPet() || other->CastToNPC()->GetSwarmOwner() > 0)) {
		TryTriggerOnCastRequirement();
	}

	if (IsClient() && !IsAIControlled())
		return;

	if (IsFamiliar() || GetSpecialAbility(IMMUNE_AGGRO))
		return;

	if (GetSpecialAbility(IMMUNE_AGGRO_NPC) && other->IsNPC())
		return;

	if (GetSpecialAbility(IMMUNE_AGGRO_CLIENT) && other->IsClient())
		return;

	if (spell_id != SPELL_UNKNOWN && NoDetrimentalSpellAggro(spell_id))
		return;

	if (other == myowner)
		return;

	if (other->GetSpecialAbility(IMMUNE_AGGRO_ON))
		return;

	if (GetSpecialAbility(NPC_TUNNELVISION)) {
		int tv_mod = GetSpecialAbilityParam(NPC_TUNNELVISION, 0);

		Mob *top = GetTarget();
		if (top && top != other) {
			if (tv_mod) {
				float tv = tv_mod / 100.0f;
				hate *= tv;
			}
			else {
				hate *= RuleR(Aggro, TunnelVisionAggroMod);
			}
		}
	}
	// first add self

	// The damage on the hate list is used to award XP to the killer. This check is to prevent Killstealing.
	// e.g. Mob has 5000 hit points, Player A melees it down to 500 hp, Player B executes a headshot (10000 damage).
	// If we add 10000 damage, Player B would get the kill credit, so we only award damage credit to player B of the
	// amount of HP the mob had left.
	//
	if (damage > GetHP())
		damage = GetHP();

	if (IsNPC() && other->IsClient())
	{
		if (m_EngagedClientNames.find(other->GetCleanName()) == m_EngagedClientNames.end())
		{
			PlayerEngagementRecord record = PlayerEngagementRecord();
			record.isFlagged = other->CastToClient()->HasZoneFlag(CastToNPC()->GetFlagGranted().c_str());
			record.lockout = LootLockout();
			record.account_id = other->CastToClient()->AccountID();
			record.character_id = other->CastToClient()->CharacterID();

			auto lootLockoutItr = other->CastToClient()->loot_lockouts.find(npctype_id);
			if (lootLockoutItr != other->CastToClient()->loot_lockouts.end())
			{
				memcpy(&record.lockout, &lootLockoutItr->second, sizeof(LootLockout));
			}
			m_EngagedClientNames.emplace(other->GetCleanName(), record);
		}
	}

	if (spellbonuses.ImprovedTaunt[SBIndex::IMPROVED_TAUNT_AGGRO_MOD] && (GetLevel() < spellbonuses.ImprovedTaunt[SBIndex::IMPROVED_TAUNT_MAX_LVL])
		&& other && (buffs[spellbonuses.ImprovedTaunt[SBIndex::IMPROVED_TAUNT_BUFFSLOT]].casterid != other->GetID()))
		hate = (hate*spellbonuses.ImprovedTaunt[SBIndex::IMPROVED_TAUNT_AGGRO_MOD]) / 100;

	hate_list.AddEntToHateList(other, hate, damage, bFrenzy, !iBuffTic);

	if (other->IsClient() && !on_hatelist && !IsOnFeignMemory(other))
		other->CastToClient()->AddAutoXTarget(this);

	// if other is a merc, add the merc client to the hate list
	if (other->IsMerc()) {
		if (other->CastToMerc()->GetMercOwner() && other->CastToMerc()->GetMercOwner()->CastToClient()->GetFeigned()) {
			AddFeignMemory(other->CastToMerc()->GetMercOwner()->CastToClient());
		}
		else {
			if (!hate_list.IsEntOnHateList(other->CastToMerc()->GetMercOwner()))
				hate_list.AddEntToHateList(other->CastToMerc()->GetMercOwner(), 0, 0, false, true);
			// if mercs are reworked to include adding 'this' to owner's xtarget list, this should reflect bots code above
		}
	} //MERC

	//if I am a pet, then add pet owner if there's one
	if (owner) { // Other is a pet, add him and it
				 // EverHood 6/12/06
				 // Can't add a feigned owner to hate list
		if (owner->IsClient() && owner->CastToClient()->GetFeigned()) {
			//they avoid hate due to feign death...
		}
		else {
			// cb:2007-08-17
			// owner must get on list, but he's not actually gained any hate yet
			if (
				!owner->GetSpecialAbility(IMMUNE_AGGRO) &&
				!(GetSpecialAbility(IMMUNE_AGGRO_CLIENT) && owner->IsClient()) &&
				!(GetSpecialAbility(IMMUNE_AGGRO_NPC) && owner->IsNPC())
			) {
				if (owner->IsClient() && !CheckAggro(owner)) {
					owner->CastToClient()->AddAutoXTarget(this);
				}
				hate_list.AddEntToHateList(owner, 0, 0, false, !iBuffTic);
			}
		}
	}

	if (mypet && !mypet->IsHeld() && !mypet->IsPetStop()) { // I have a pet, add other to it
		if (
			!mypet->IsFamiliar() &&
			!mypet->GetSpecialAbility(IMMUNE_AGGRO) &&
			!(mypet->GetSpecialAbility(IMMUNE_AGGRO_CLIENT) && IsClient()) &&
			!(mypet->GetSpecialAbility(IMMUNE_AGGRO_NPC) && IsNPC())
		) {
			mypet->hate_list.AddEntToHateList(other, 0, 0, bFrenzy);
		}
	}
	else if (myowner) { // I am a pet, add other to owner if it's NPC/LD
		if (
			myowner->IsAIControlled() &&
			!myowner->GetSpecialAbility(IMMUNE_AGGRO) &&
			!(GetSpecialAbility(IMMUNE_AGGRO_CLIENT) && myowner->IsClient()) &&
			!(GetSpecialAbility(IMMUNE_AGGRO_NPC) && myowner->IsNPC())
		) {
			myowner->hate_list.AddEntToHateList(other, 0, 0, bFrenzy);
		}
	}

	//I have a swarm pet, add other to it.
	if (GetTempPetCount()) {
		entity_list.AddTempPetsToHateList(this, other, bFrenzy);
	}

	if (!wasengaged) {
		if (IsNPC() && other->IsClient() && other->CastToClient())
			parse->EventNPC(EVENT_AGGRO, CastToNPC(), other, "", 0);

		AI_Event_Engaged(other, iYellForHelp);
	}
}

// this is called from Damage() when 'this' is attacked by 'other.
// 'this' is the one being attacked
// 'other' is the attacker
// a damage shield causes damage (or healing) to whoever attacks the wearer
// a reverse ds causes damage to the wearer whenever it attack someone
// given this, a reverse ds must be checked each time the wearer is attacking
// and not when they're attacked
//a damage shield on a spell is a negative value but on an item it's a positive value so add the spell value and subtract the item value to get the end ds value
void Mob::DamageShield(Mob* attacker, bool spell_ds) {

	if (!attacker || this == attacker)
		return;

	int DS = 0;
	int rev_ds = 0;
	uint16 spellid = 0;

	if (!spell_ds)
	{
		DS = spellbonuses.DamageShield;
		rev_ds = attacker->spellbonuses.ReverseDamageShield;

		if (spellbonuses.DamageShieldSpellID != 0 && spellbonuses.DamageShieldSpellID != SPELL_UNKNOWN)
			spellid = spellbonuses.DamageShieldSpellID;
	}
	else {
		DS = spellbonuses.SpellDamageShield + itembonuses.SpellDamageShield + aabonuses.SpellDamageShield;
		rev_ds = 0;
		// This ID returns "you are burned", seemed most appropriate for spell DS
		spellid = 2166;
		/*
			Live Message - not yet used on emu
			Feedback onto you "YOUR mind burns from TARGETS NAME's feedback for %i points of non-melee damage."
			Feedback onto other "TARGETS NAME's mind burns from YOUR feedback for %i points of non-melee damage."
		*/
	}

	if (DS == 0 && rev_ds == 0)
		return;

	LogCombat("Applying Damage Shield of value [{}] to [{}]", DS, attacker->GetName());

	//invert DS... spells yield negative values for a true damage shield
	if (DS < 0) {
		if (!spell_ds) {

			DS += aabonuses.DamageShield; //Live AA - coat of thistles. (negative value)
			DS -= itembonuses.DamageShield; //+Damage Shield should only work when you already have a DS spell
			DS -= attacker->aabonuses.DS_Mitigation_Amount + attacker->itembonuses.DS_Mitigation_Amount + attacker->spellbonuses.DS_Mitigation_Amount; //Negative value to reduce
			//Do not allow flat amount reductions to reduce past 0.
			if (DS >= 0)
				return;

											//Spell data for damage shield mitigation shows a negative value for spells for clients and positive
											//value for spells that effect pets. Unclear as to why. For now will convert all positive to be consistent.
			if (attacker->IsOffHandAtk()) {
				int32 mitigation = attacker->itembonuses.DSMitigationOffHand +
					attacker->spellbonuses.DSMitigationOffHand +
					attacker->aabonuses.DSMitigationOffHand;
				DS -= DS*mitigation / 100;
			}

			int ds_mitigation = attacker->itembonuses.DSMitigation;
			// Subtract mitigations because DS_Mitigation_Percentage is a negative value when reducing total, thus final value will be positive
			ds_mitigation -= attacker->aabonuses.DS_Mitigation_Percentage + attacker->itembonuses.DS_Mitigation_Percentage + attacker->spellbonuses.DS_Mitigation_Percentage; //Negative value to reduce

			DS -= DS * ds_mitigation / 100;
		}

		attacker->Damage(this, -DS, spellid, EQ::skills::SkillAbjuration/*hackish*/, false);
		//we can assume there is a spell now
		auto outapp = new EQApplicationPacket(OP_Damage, sizeof(CombatDamage_Struct));
		CombatDamage_Struct* cds = (CombatDamage_Struct*)outapp->pBuffer;
		cds->target = attacker->GetID();
		cds->source = GetID();
		cds->type = spellbonuses.DamageShieldType;
		cds->spellid = 0x0;
		cds->damage = DS;
		entity_list.QueueCloseClients(this, outapp);
		safe_delete(outapp);
	}
	else if (DS > 0 && !spell_ds) {
		//we are healing the attacker...
		attacker->HealDamage(DS);
		//TODO: send a packet???
	}

	//Reverse DS
	//this is basically a DS, but the spell is on the attacker, not the attackee
	//if we've gotten to this point, we know we know "attacker" hit "this" (us) for damage & we aren't invulnerable
	uint16 rev_ds_spell_id = SPELL_UNKNOWN;

	if (spellbonuses.ReverseDamageShieldSpellID != 0 && spellbonuses.ReverseDamageShieldSpellID != SPELL_UNKNOWN)
		rev_ds_spell_id = spellbonuses.ReverseDamageShieldSpellID;

	if (rev_ds < 0) {
		LogCombat("Applying Reverse Damage Shield of value [{}] to [{}]", rev_ds, attacker->GetName());
		attacker->Damage(this, -rev_ds, rev_ds_spell_id, EQ::skills::SkillAbjuration/*hackish*/, false); //"this" (us) will get the hate, etc. not sure how this works on Live, but it'll works for now, and tanks will love us for this
																											//do we need to send a damage packet here also?
	}
}

uint8 Mob::GetWeaponDamageBonus(const EQ::ItemData *weapon, bool offhand)
{
	// dev quote with old and new formulas
	// https://forums.daybreakgames.com/eq/index.php?threads/test-update-09-17-15.226618/page-5#post-3326194
	//
	// We assume that the level check is done before calling this function and sinister strikes is checked before
	// calling for offhand DB
	auto level = GetLevel();
	if (!weapon)
		return 1 + ((level - 28) / 3); // how does weaponless scale?

	auto delay = weapon->Delay;
	if (weapon->IsType1HWeapon() || weapon->ItemType == EQ::item::ItemTypeMartial) {
		// we assume sinister strikes is checked before calling here
		if (!offhand) {
			if (delay <= 39)
				return 1 + ((level - 28) / 3);
			else if (delay < 43)
				return 2 + ((level - 28) / 3) + ((delay - 40) / 3);
			else if (delay < 45)
				return 3 + ((level - 28) / 3) + ((delay - 40) / 3);
			else if (delay >= 45)
				return 4 + ((level - 28) / 3) + ((delay - 40) / 3);
		}
		else {
			return 1 + ((level - 40) / 3) * (delay / 30); // YOOO shit's useless waste of AAs
		}
	}
	else {
		// 2h damage bonus
		int64 damage_bonus = 1 + (level - 28) / 3;
		if (delay <= 27)
			return damage_bonus + 1;
		// Client isn't reflecting what the dev quoted, this matches better
		if (level > 29) {
			int level_bonus = (level - 30) / 5 + 1;
			if (level > 50) {
				level_bonus++;
				int level_bonus2 = level - 50;
				if (level > 67)
					level_bonus2 += 5;
				else if (level > 59)
					level_bonus2 += 4;
				else if (level > 58)
					level_bonus2 += 3;
				else if (level > 56)
					level_bonus2 += 2;
				else if (level > 54)
					level_bonus2++;
				level_bonus += level_bonus2 * delay / 40;
			}
			damage_bonus += level_bonus;
		}
		if (delay >= 40) {
			int delay_bonus = (delay - 40) / 3 + 1;
			if (delay >= 45)
				delay_bonus += 2;
			else if (delay >= 43)
				delay_bonus++;
			damage_bonus += delay_bonus;
		}
		return damage_bonus;
	}

	return 0;
}

int Mob::GetHandToHandDamage(void)
{
	if (RuleB(Combat, UseRevampHandToHand)) {
		// everyone uses this in the revamp!
		int skill = GetSkill(EQ::skills::SkillHandtoHand);
		int epic = 0;
		if (IsClient() && CastToClient()->GetItemIDAt(12) == 10652 && GetLevel() > 46)
			epic = 280;
		if (epic > skill)
			skill = epic;
		return skill / 15 + 3;
	}

	static uint8 mnk_dmg[] = { 99,
		4, 4, 4, 4, 5, 5, 5, 5, 5, 6,           // 1-10
		6, 6, 6, 6, 7, 7, 7, 7, 7, 8,           // 11-20
		8, 8, 8, 8, 9, 9, 9, 9, 9, 10,          // 21-30
		10, 10, 10, 10, 11, 11, 11, 11, 11, 12, // 31-40
		12, 12, 12, 12, 13, 13, 13, 13, 13, 14, // 41-50
		14, 14, 14, 14, 14, 14, 14, 14, 14, 14, // 51-60
		14, 14 };                                // 61-62
	static uint8 bst_dmg[] = { 99,
		4, 4, 4, 4, 4, 5, 5, 5, 5, 5,        // 1-10
		5, 6, 6, 6, 6, 6, 6, 7, 7, 7,        // 11-20
		7, 7, 7, 8, 8, 8, 8, 8, 8, 9,        // 21-30
		9, 9, 9, 9, 9, 10, 10, 10, 10, 10,   // 31-40
		10, 11, 11, 11, 11, 11, 11, 12, 12 }; // 41-49
	if (GetClass() == MONK) {
		if (IsClient() && CastToClient()->GetItemIDAt(12) == 10652 && GetLevel() > 50)
			return 9;
		if (level > 62)
			return 15;
		return mnk_dmg[level];
	}
	else if (GetClass() == BEASTLORD) {
		if (level > 49)
			return 13;
		return bst_dmg[level];
	}
	return 2;
}

int Mob::GetHandToHandDelay(void)
{
	if (RuleB(Combat, UseRevampHandToHand)) {
		// everyone uses this in the revamp!
		int skill = GetSkill(EQ::skills::SkillHandtoHand);
		int epic = 0;
		int iksar = 0;
		if (IsClient() && CastToClient()->GetItemIDAt(12) == 10652 && GetLevel() > 46)
			epic = 280;
		else if (GetRace() == IKSAR)
			iksar = 1;
		// the delay bonus from the monk epic scales up to a skill of 280
		if (epic >= skill)
			epic = skill;
		return iksar - epic / 21 + 38;
	}

	int delay = 35;
	static uint8 mnk_hum_delay[] = { 99,
		35, 35, 35, 35, 35, 35, 35, 35, 35, 35, // 1-10
		35, 35, 35, 35, 35, 35, 35, 35, 35, 35, // 11-20
		35, 35, 35, 35, 35, 35, 35, 34, 34, 34, // 21-30
		34, 33, 33, 33, 33, 32, 32, 32, 32, 31, // 31-40
		31, 31, 31, 30, 30, 30, 30, 29, 29, 29, // 41-50
		29, 28, 28, 28, 28, 27, 27, 27, 27, 26, // 51-60
		24, 22 };                                // 61-62
	static uint8 mnk_iks_delay[] = { 99,
		35, 35, 35, 35, 35, 35, 35, 35, 35, 35, // 1-10
		35, 35, 35, 35, 35, 35, 35, 35, 35, 35, // 11-20
		35, 35, 35, 35, 35, 35, 35, 35, 35, 34, // 21-30
		34, 34, 34, 34, 34, 33, 33, 33, 33, 33, // 31-40
		33, 32, 32, 32, 32, 32, 32, 31, 31, 31, // 41-50
		31, 31, 31, 30, 30, 30, 30, 30, 30, 29, // 51-60
		25, 23 };                                // 61-62
	static uint8 bst_delay[] = { 99,
		35, 35, 35, 35, 35, 35, 35, 35, 35, 35, // 1-10
		35, 35, 35, 35, 35, 35, 35, 35, 35, 35, // 11-20
		35, 35, 35, 35, 35, 35, 35, 35, 34, 34, // 21-30
		34, 34, 34, 33, 33, 33, 33, 33, 32, 32, // 31-40
		32, 32, 32, 31, 31, 31, 31, 31, 30, 30, // 41-50
		30, 30, 30, 29, 29, 29, 29, 29, 28, 28, // 51-60
		28, 28, 28, 27, 27, 27, 27, 27, 26, 26, // 61-70
		26, 26, 26 };                            // 71-73

	if (GetClass() == MONK) {
		// Have a look to see if we have epic fists on
		if (IsClient() && CastToClient()->GetItemIDAt(12) == 10652 && GetLevel() > 50)
			return 16;
		int level = GetLevel();
		if (level > 62)
			return GetRace() == IKSAR ? 21 : 20;
		return GetRace() == IKSAR ? mnk_iks_delay[level] : mnk_hum_delay[level];
	}
	else if (GetClass() == BEASTLORD) {
		int level = GetLevel();
		if (level > 73)
			return 25;
		return bst_delay[level];
	}
	return 35;
}

int64 Mob::ReduceDamage(int64 damage)
{
	if (damage <= 0)
		return damage;

	int32 slot = -1;
	bool DisableMeleeRune = false;

	if (spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_EXISTS]) {
		slot = spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_BUFFSLOT];
		if (slot >= 0) {
			if (--buffs[slot].hit_number == 0) {

				if (!TryFadeEffect(slot))
					BuffFadeBySlot(slot, true);
			}

			if (spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_MAX_DMG_ABSORB_PER_HIT] && (damage > spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_MAX_DMG_ABSORB_PER_HIT]))
				damage -= spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_MAX_DMG_ABSORB_PER_HIT];
			else
				return DMG_RUNE;
		}
	}

	//Only mitigate if damage is above the minimium specified.
	if (spellbonuses.MeleeThresholdGuard[SBIndex::THRESHOLDGUARD_MITIGATION_PERCENT]) {
		slot = spellbonuses.MeleeThresholdGuard[SBIndex::THRESHOLDGUARD_BUFFSLOT];

		if (slot >= 0 && (damage > spellbonuses.MeleeThresholdGuard[SBIndex::THRESHOLDGUARD_MIN_DMG_TO_TRIGGER]))
		{
			DisableMeleeRune = true;
			int64 damage_to_reduce = damage * spellbonuses.MeleeThresholdGuard[SBIndex::THRESHOLDGUARD_MITIGATION_PERCENT] / 100;
			if (damage_to_reduce >= buffs[slot].melee_rune)
			{
				LogSpells("Mob::ReduceDamage SE_MeleeThresholdGuard [{}] damage negated, [{}] damage remaining, fading buff", damage_to_reduce, buffs[slot].melee_rune);
				damage -= buffs[slot].melee_rune;
				if (!TryFadeEffect(slot))
					BuffFadeBySlot(slot);
			}
			else
			{
				LogSpells("Mob::ReduceDamage SE_MeleeThresholdGuard [{}] damage negated, [{}] damage remaining", damage_to_reduce, buffs[slot].melee_rune);
				buffs[slot].melee_rune = (buffs[slot].melee_rune - damage_to_reduce);
				damage -= damage_to_reduce;
			}
		}
	}

	if (spellbonuses.MitigateMeleeRune[SBIndex::MITIGATION_RUNE_PERCENT] && !DisableMeleeRune) {
		slot = spellbonuses.MitigateMeleeRune[SBIndex::MITIGATION_RUNE_BUFFSLOT];
		if (slot >= 0)
		{
			int64 damage_to_reduce = damage * spellbonuses.MitigateMeleeRune[SBIndex::MITIGATION_RUNE_PERCENT] / 100;

			if (spellbonuses.MitigateMeleeRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT] && (damage_to_reduce > spellbonuses.MitigateMeleeRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT]))
				damage_to_reduce = spellbonuses.MitigateMeleeRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT];

			if (spellbonuses.MitigateMeleeRune[SBIndex::MITIGATION_RUNE_MAX_HP_AMT] && (damage_to_reduce >= buffs[slot].melee_rune))
			{
				LogSpells("Mob::ReduceDamage SE_MitigateMeleeDamage [{}] damage negated, [{}] damage remaining, fading buff", damage_to_reduce, buffs[slot].melee_rune);
				damage -= buffs[slot].melee_rune;
				if (!TryFadeEffect(slot))
					BuffFadeBySlot(slot);
			}
			else
			{
				LogSpells("Mob::ReduceDamage SE_MitigateMeleeDamage [{}] damage negated, [{}] damage remaining", damage_to_reduce, buffs[slot].melee_rune);

				if (spellbonuses.MitigateMeleeRune[SBIndex::MITIGATION_RUNE_MAX_HP_AMT])
					buffs[slot].melee_rune = (buffs[slot].melee_rune - damage_to_reduce);

				damage -= damage_to_reduce;
			}
		}
	}

	if (damage < 1)
		return DMG_RUNE;

	if (spellbonuses.MeleeRune[SBIndex::RUNE_AMOUNT] && spellbonuses.MeleeRune[SBIndex::RUNE_BUFFSLOT] >= 0)
		damage = RuneAbsorb(damage, SE_Rune);

	if (damage < 1)
		return DMG_RUNE;

	return(damage);
}

int64 Mob::AffectMagicalDamage(int64 damage, uint16 spell_id, const bool iBuffTic, Mob* attacker)
{
	if (damage <= 0)
		return damage;

	bool DisableSpellRune = false;
	int32 slot = -1;

	// See if we block the spell outright first
	if (!iBuffTic && spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_EXISTS]) {
		slot = spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_BUFFSLOT];
		if (slot >= 0) {
			if (--buffs[slot].hit_number == 0) {

				if (!TryFadeEffect(slot))
					BuffFadeBySlot(slot, true);
			}

			if (spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_MAX_DMG_ABSORB_PER_HIT] && (damage > spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_MAX_DMG_ABSORB_PER_HIT]))
				damage -= spellbonuses.NegateAttacks[SBIndex::NEGATE_ATK_MAX_DMG_ABSORB_PER_HIT];
			else
				return 0;
		}
	}

	// If this is a DoT, use DoT Shielding...
	if (iBuffTic) {
		int total_dotshielding = itembonuses.DoTShielding + itembonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_PERCENT] + aabonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_PERCENT];
		damage -= (damage * total_dotshielding / 100);

		if (spellbonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_PERCENT]) {
			slot = spellbonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_BUFFSLOT];
			if (slot >= 0)
			{
				int64 damage_to_reduce = damage * spellbonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_PERCENT] / 100;

				if (spellbonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT] && (damage_to_reduce > spellbonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT]))
					damage_to_reduce = spellbonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT];

				if (spellbonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_MAX_HP_AMT] && (damage_to_reduce >= buffs[slot].dot_rune))
				{
					damage -= buffs[slot].dot_rune;
					if (!TryFadeEffect(slot))
						BuffFadeBySlot(slot);
				}
				else
				{
					if (spellbonuses.MitigateDotRune[SBIndex::MITIGATION_RUNE_MAX_HP_AMT])
						buffs[slot].dot_rune = (buffs[slot].dot_rune - damage_to_reduce);

					damage -= damage_to_reduce;
				}
			}
		}
	}

	// This must be a DD then so lets apply Spell Shielding and runes.
	else
	{
		// Reduce damage by the Spell Shielding first so that the runes don't take the raw damage.
		int total_spellshielding = itembonuses.SpellShield + itembonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_PERCENT] + aabonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_PERCENT];
		damage -= (damage * total_spellshielding / 100);

		//Only mitigate if damage is above the minimium specified.
		if (spellbonuses.SpellThresholdGuard[SBIndex::THRESHOLDGUARD_MITIGATION_PERCENT]) {
			slot = spellbonuses.SpellThresholdGuard[SBIndex::THRESHOLDGUARD_BUFFSLOT];

			if (slot >= 0 && (damage > spellbonuses.MeleeThresholdGuard[SBIndex::THRESHOLDGUARD_MIN_DMG_TO_TRIGGER]))
			{
				DisableSpellRune = true;
				int64 damage_to_reduce = damage * spellbonuses.SpellThresholdGuard[SBIndex::THRESHOLDGUARD_MITIGATION_PERCENT] / 100;
				if (damage_to_reduce >= buffs[slot].magic_rune)
				{
					damage -= buffs[slot].magic_rune;
					if (!TryFadeEffect(slot))
						BuffFadeBySlot(slot);
				}
				else
				{
					buffs[slot].melee_rune = (buffs[slot].magic_rune - damage_to_reduce);
					damage -= damage_to_reduce;
				}
			}
		}

		// Do runes now.
		if (spellbonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_PERCENT] && !DisableSpellRune) {
			slot = spellbonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_BUFFSLOT];
			if (slot >= 0)
			{
				int64 damage_to_reduce = damage * spellbonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_PERCENT] / 100;

				if (spellbonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT] && (damage_to_reduce > spellbonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT]))
					damage_to_reduce = spellbonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_MAX_DMG_ABSORB_PER_HIT];

				if (spellbonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_MAX_HP_AMT] && (damage_to_reduce >= buffs[slot].magic_rune))
				{
					LogSpells("Mob::ReduceDamage SE_MitigateSpellDamage [{}] damage negated, [{}] damage remaining, fading buff", damage_to_reduce, buffs[slot].magic_rune);
					damage -= buffs[slot].magic_rune;
					if (!TryFadeEffect(slot))
						BuffFadeBySlot(slot);
				}
				else
				{
					LogSpells("Mob::ReduceDamage SE_MitigateMeleeDamage [{}] damage negated, [{}] damage remaining", damage_to_reduce, buffs[slot].magic_rune);

					if (spellbonuses.MitigateSpellRune[SBIndex::MITIGATION_RUNE_MAX_HP_AMT])
						buffs[slot].magic_rune = (buffs[slot].magic_rune - damage_to_reduce);

					damage -= damage_to_reduce;
				}
			}
		}

		if (damage < 1)
			return 0;

		//Regular runes absorb spell damage (except dots) - Confirmed on live.
		if (spellbonuses.MeleeRune[SBIndex::RUNE_AMOUNT] && spellbonuses.MeleeRune[SBIndex::RUNE_BUFFSLOT] >= 0)
			damage = RuneAbsorb(damage, SE_Rune);

		if (spellbonuses.AbsorbMagicAtt[SBIndex::RUNE_AMOUNT] && spellbonuses.AbsorbMagicAtt[SBIndex::RUNE_BUFFSLOT] >= 0)
			damage = RuneAbsorb(damage, SE_AbsorbMagicAtt);

		if (damage < 1)
			return 0;
	}
	return damage;
}

int64 Mob::ReduceAllDamage(int64 damage)
{
	if (damage <= 0)
		return damage;

	if (spellbonuses.ManaAbsorbPercentDamage) {
		int64 mana_reduced = damage * spellbonuses.ManaAbsorbPercentDamage / 100;
		if (GetMana() >= mana_reduced) {
			damage -= mana_reduced;
			SetMana(GetMana() - mana_reduced);
			TryTriggerOnCastRequirement();
		}
	}

	if (spellbonuses.EnduranceAbsorbPercentDamage[SBIndex::ENDURANCE_ABSORD_MITIGIATION]) {
		int64 damage_reduced = damage * spellbonuses.EnduranceAbsorbPercentDamage[SBIndex::ENDURANCE_ABSORD_MITIGIATION] / 10000; //If hit for 1000, at 10% then lower damage by 100;
		int32 endurance_drain = damage_reduced * spellbonuses.EnduranceAbsorbPercentDamage[SBIndex::ENDURANCE_ABSORD_DRAIN_PER_HP] / 10000; //Reduce endurance by 0.05% per HP loss
		if (endurance_drain < 1)
			endurance_drain = 1;

		if (IsClient() && CastToClient()->GetEndurance() >= endurance_drain) {
			damage -= damage_reduced;
			CastToClient()->SetEndurance(CastToClient()->GetEndurance() - endurance_drain);
			TryTriggerOnCastRequirement();
		}
	}

	CheckNumHitsRemaining(NumHit::IncomingDamage);

	return(damage);
}

bool Mob::HasProcs() const
{
	for (int i = 0; i < MAX_PROCS; i++) {
		if (PermaProcs[i].spellID != SPELL_UNKNOWN || SpellProcs[i].spellID != SPELL_UNKNOWN) {
			return true;
		}
	}

	if (IsClient()) {
		for (int i = 0; i < MAX_AA_PROCS; i += 4) {
			if (aabonuses.SpellProc[i]) {
				return true;
			}
		}
	}
	return false;
}

bool Mob::HasDefensiveProcs() const
{
	for (int i = 0; i < MAX_PROCS; i++) {
		if (DefensiveProcs[i].spellID != SPELL_UNKNOWN) {
			return true;
		}
	}

	if (IsClient()) {
		for (int i = 0; i < MAX_AA_PROCS; i += 4) {
			if (aabonuses.DefensiveProc[i]) {
				return true;
			}
		}
	}
	return false;
}

bool Mob::HasSkillProcs() const
{
	for (int i = 0; i < MAX_SKILL_PROCS; i++) {
		if (spellbonuses.SkillProc[i] || itembonuses.SkillProc[i] || aabonuses.SkillProc[i])
			return true;
	}
	return false;
}

bool Mob::HasSkillProcSuccess() const
{
	for (int i = 0; i < MAX_SKILL_PROCS; i++) {
		if (spellbonuses.SkillProcSuccess[i] || itembonuses.SkillProcSuccess[i] || aabonuses.SkillProcSuccess[i])
			return true;
	}
	return false;
}

bool Mob::HasRangedProcs() const
{
	for (int i = 0; i < MAX_PROCS; i++){
		if (RangedProcs[i].spellID != SPELL_UNKNOWN) {
			return true;
		}
	}

	if (IsClient()) {
		for (int i = 0; i < MAX_AA_PROCS; i += 4) {
			if (aabonuses.RangedProc[i]) {
				return true;
			}
		}
	}
	return false;
}

bool Client::CheckDoubleAttack()
{
	int chance = 0;
	int skill = GetSkill(EQ::skills::SkillDoubleAttack);
	//Check for bonuses that give you a double attack chance regardless of skill (ie Bestial Frenzy/Harmonious Attack AA)
	int bonusGiveDA = aabonuses.GiveDoubleAttack + spellbonuses.GiveDoubleAttack + itembonuses.GiveDoubleAttack;
	if (skill > 0)
		chance = skill + GetLevel();
	else if (!bonusGiveDA)
		return false;

	if (bonusGiveDA)
		chance += bonusGiveDA / 100.0f * 500; // convert to skill value
	int per_inc = aabonuses.DoubleAttackChance + spellbonuses.DoubleAttackChance + itembonuses.DoubleAttackChance;
	if (per_inc)
		chance += chance * per_inc / 100;

	return zone->random.Int(1, 500) <= chance;
}

// Admittedly these parses were short, but this check worked for 3 toons across multiple levels
// with varying triple attack skill (1-3% error at least)
bool Client::CheckTripleAttack()
{
	int chance = GetSkill(EQ::skills::SkillTripleAttack);
	if (chance < 1)
		return false;

	int inc = aabonuses.TripleAttackChance + spellbonuses.TripleAttackChance + itembonuses.TripleAttackChance;
	chance = static_cast<int>(chance * (1 + inc / 100.0f));
	chance = (chance * 100) / (chance + 800);

	return zone->random.Int(1, 100) <= chance;
}

bool Client::CheckDoubleRangedAttack() {
	int32 chance = spellbonuses.DoubleRangedAttack + itembonuses.DoubleRangedAttack + aabonuses.DoubleRangedAttack;

	if (chance && zone->random.Roll(chance))
		return true;

	return false;
}

bool Mob::CheckDoubleAttack()
{
	// Not 100% certain pets follow this or if it's just from pets not always
	// having the same skills as most mobs
	int chance = GetSkill(EQ::skills::SkillDoubleAttack);
	if (GetLevel() > 35)
		chance += GetLevel();

	int per_inc = aabonuses.DoubleAttackChance + spellbonuses.DoubleAttackChance + itembonuses.DoubleAttackChance;
	if (per_inc)
		chance += chance * per_inc / 100;

	return zone->random.Int(1, 500) <= chance;
}

void Mob::CommonDamage(Mob* attacker, int64 &damage, const uint16 spell_id, const EQ::skills::SkillType skill_used, bool &avoidable, const int8 buffslot, const bool iBuffTic, eSpecialAttacks special) {
	// This method is called with skill_used=ABJURE for Damage Shield damage.
	bool FromDamageShield = (skill_used == EQ::skills::SkillAbjuration);
	bool ignore_invul = false;
	if (IsValidSpell(spell_id))
		ignore_invul = spell_id == SPELL_CAZIC_TOUCH || spells[spell_id].cast_not_standing;

	if (!ignore_invul && (GetInvul() || DivineAura())) {
		LogCombat("Avoiding [{}] damage due to invulnerability", damage);
		damage = DMG_INVULNERABLE;
	}

	// this should actually happen MUCH sooner, need to investigate though -- good enough for now
	if ((skill_used == EQ::skills::SkillArchery || skill_used == EQ::skills::SkillThrowing) && GetSpecialAbility(IMMUNE_RANGED_ATTACKS)) {
		LogCombat("Avoiding [{}] damage due to IMMUNE_RANGED_ATTACKS", damage);
		damage = DMG_INVULNERABLE;
	}

	if (spell_id != SPELL_UNKNOWN || attacker == nullptr)
		avoidable = false;

	// only apply DS if physical damage (no spell damage)
	// damage shield calls this function with spell_id set, so its unavoidable
	if (attacker && damage > 0 && spell_id == SPELL_UNKNOWN && skill_used != EQ::skills::SkillArchery && skill_used != EQ::skills::SkillThrowing) {
		DamageShield(attacker);
	}

	if (spell_id == SPELL_UNKNOWN && skill_used) {
		CheckNumHitsRemaining(NumHit::IncomingHitAttempts);

		if (attacker)
			attacker->CheckNumHitsRemaining(NumHit::OutgoingHitAttempts);
	}

	if (attacker) {
		if (attacker->IsClient()) {
			if (!RuleB(Combat, EXPFromDmgShield)) {
				// Damage shield damage shouldn't count towards who gets EXP
				if (!attacker->CastToClient()->GetFeigned() && !FromDamageShield)
					AddToHateList(attacker, 0, damage, true, false, iBuffTic, spell_id);
			}
			else {
				if (!attacker->CastToClient()->GetFeigned())
					AddToHateList(attacker, 0, damage, true, false, iBuffTic, spell_id);
			}
		}
		else
			AddToHateList(attacker, 0, damage, true, false, iBuffTic, spell_id);
	}

	if (damage > 0) {
		//if there is some damage being done and theres an attacker involved
		if (attacker) {
			// if spell is lifetap add hp to the caster
			if (spell_id != SPELL_UNKNOWN && IsLifetapSpell(spell_id)) {
				int64 healed = damage;

				healed = RuleB(Spells, CompoundLifetapHeals) ? attacker->GetActSpellHealing(spell_id, healed) : healed;
				LogCombat("Applying lifetap heal of [{}] to [{}]", healed, attacker->GetName());
				attacker->HealDamage(healed);

				//we used to do a message to the client, but its gone now.
				// emote goes with every one ... even npcs
				entity_list.MessageClose(this, false, RuleI(Range, SpellMessages), Chat::Emote, "%s beams a smile at %s", attacker->GetCleanName(), GetCleanName());
			}

			// If a client pet is damaged while sitting, stand, fix sit button,
			// and remove sitting regen.  Removes bug where client clicks sit
			// during battle and gains pet hp-regen and bugs the sit button.
			if (IsPet()) {
				Mob *owner = GetOwner();
				if (owner && owner->IsClient()) {
					if (GetPetOrder() == SPO_Sit) {
						SetPetOrder(SPO_Follow);
					}
					// fix GUI sit button to be unpressed and stop sitting regen
					owner->CastToClient()->SetPetCommandState(PET_BUTTON_SIT, 0);
					SetAppearance(eaStanding);
				}
			}

		}	//end `if there is some damage being done and theres anattacker person involved`

		Mob *pet = GetPet();
		// pets that have GHold will never automatically add NPCs
		// pets that have Hold and no Focus will add NPCs if they're engaged
		// pets that have Hold and Focus will not add NPCs
		if (
			pet &&
			!pet->IsFamiliar() &&
			!pet->GetSpecialAbility(IMMUNE_AGGRO) &&
			!pet->IsEngaged() &&
			attacker &&
			!(pet->GetSpecialAbility(IMMUNE_AGGRO_CLIENT) && attacker->IsClient()) &&
			!(pet->GetSpecialAbility(IMMUNE_AGGRO_NPC) && attacker->IsNPC()) &&
			attacker != this &&
			!attacker->IsCorpse() &&
			!pet->IsGHeld() &&
			!attacker->IsTrap()
		) {
			if (!pet->IsHeld()) {
				LogAggro("Sending pet [{}] into battle due to attack", pet->GetName());
				if (IsClient()) {
					// if pet was sitting his new mode is follow
					// following after the battle (live verified)
					if (pet->GetPetOrder() == SPO_Sit) {
						pet->SetPetOrder(SPO_Follow);
					}

					// fix GUI sit button to be unpressed and stop sitting regen
					CastToClient()->SetPetCommandState(PET_BUTTON_SIT, 0);
					pet->SetAppearance(eaStanding);
				}

				pet->AddToHateList(attacker, 1, 0, true, false, false, spell_id);
				pet->SetTarget(attacker);
				MessageString(Chat::NPCQuestSay, PET_ATTACKING, pet->GetCleanName(), attacker->GetCleanName());
			}
		}

		if (GetTempPetCount()) {
			entity_list.AddTempPetsToHateListOnOwnerDamage(this, attacker, spell_id);
		}

		//see if any runes want to reduce this damage
		if (spell_id == SPELL_UNKNOWN) {
			damage = ReduceDamage(damage);
			LogCombat("Melee Damage reduced to [{}]", damage);
			damage = ReduceAllDamage(damage);
			TryTriggerThreshHold(damage, SE_TriggerMeleeThreshold, attacker);

			if (skill_used)
				CheckNumHitsRemaining(NumHit::IncomingHitSuccess);

		}
		else {
			int64 origdmg = damage;
			damage = AffectMagicalDamage(damage, spell_id, iBuffTic, attacker);
			if (origdmg != damage && attacker && attacker->IsClient()) {
				if (attacker->CastToClient()->GetFilter(FilterDamageShields) != FilterHide)
					attacker->Message(Chat::Yellow, "The Spellshield absorbed %d of %d points of damage", origdmg - damage, origdmg);
			}
			if (damage == 0 && attacker && origdmg != damage && IsClient()) {
				//Kayen: Probably need to add a filter for this - Not sure if this msg is correct but there should be a message for spell negate/runes.
				Message(263, "%s tries to cast on YOU, but YOUR magical skin absorbs the spell.", attacker->GetCleanName());
			}
			damage = ReduceAllDamage(damage);
			TryTriggerThreshHold(damage, SE_TriggerSpellThreshold, attacker);
		}

		if (IsClient() && CastToClient()->sneaking) {
			CastToClient()->sneaking = false;
			SendAppearancePacket(AT_Sneak, 0);
		}
		if (attacker && attacker->IsClient() && attacker->CastToClient()->sneaking) {
			attacker->CastToClient()->sneaking = false;
			attacker->SendAppearancePacket(AT_Sneak, 0);
		}

		//final damage has been determined.
		SetHP(int64(GetHP() - damage));

		if (HasDied()) {
			bool IsSaved = false;

			if (TryDivineSave())
				IsSaved = true;

			if (!IsSaved && !TrySpellOnDeath()) {
				SetHP(-500);

				if (Death(attacker, damage, spell_id, skill_used)) {
					return;
				}
			}
		}
		else {
			if (GetHPRatio() < 16)
				TryDeathSave();
		}

		TryTriggerOnCastRequirement();

		//fade mez if we are mezzed
		if (IsMezzed() && attacker) {
			LogCombat("Breaking mez due to attack");
			entity_list.MessageCloseString(
				this, /* Sender */
				true,  /* Skip Sender */
				RuleI(Range, SpellMessages),
				Chat::SpellWornOff, /* 284 */
				HAS_BEEN_AWAKENED, // %1 has been awakened by %2.
				GetCleanName(), /* Message1 */
				attacker->GetCleanName() /* Message2 */
			);
			BuffFadeByEffect(SE_Mez);
		}

		if (spell_id != SPELL_UNKNOWN && !iBuffTic) {
			//see if root will break
			if (IsRooted() && !FromDamageShield)  // neotoyko: only spells cancel root
				TryRootFadeByDamage(buffslot, attacker);
		}
		else if (spell_id == SPELL_UNKNOWN)
		{
			//increment chances of interrupting
			if (IsCasting()) { //shouldnt interrupt on regular spell damage
				attacked_count++;
				LogCombat("Melee attack while casting. Attack count [{}]", attacked_count);
			}
		}

		//send an HP update if we are hurt
		if (GetHP() < GetMaxHP())
			SendHPUpdate(); // the OP_Damage actually updates the client in these cases, so we skip the HP update for them
	}	//end `if damage was done`

		//send damage packet...
	if (!iBuffTic) { //buff ticks do not send damage, instead they just call SendHPUpdate(), which is done above
		auto outapp = new EQApplicationPacket(OP_Damage, sizeof(CombatDamage_Struct));
		CombatDamage_Struct* a = (CombatDamage_Struct*)outapp->pBuffer;
		a->target = GetID();
		if (attacker == nullptr)
			a->source = 0;
		else if (attacker->IsClient() && attacker->CastToClient()->GMHideMe())
			a->source = 0;
		else
			a->source = attacker->GetID();
		a->type = SkillDamageTypes[skill_used]; // was 0x1c
		a->damage = damage;
		a->spellid = spell_id;
		if (special == eSpecialAttacks::AERampage)
			a->special = 1;
		else if (special == eSpecialAttacks::Rampage)
			a->special = 2;
		else
			a->special = 0;
		a->hit_heading = attacker ? attacker->GetHeading() : 0.0f;
		if (RuleB(Combat, MeleePush) && damage > 0 && !IsRooted() &&
			(IsClient() || zone->random.Roll(RuleI(Combat, MeleePushChance)))) {
			a->force = EQ::skills::GetSkillMeleePushForce(skill_used);
			if (IsNPC()) {
				if (attacker->IsNPC())
					a->force = 0.0f; // 2013 change that disabled NPC vs NPC push
				else
					a->force *= 0.10f; // force against NPCs is divided by 10 I guess? ex bash is 0.3, parsed 0.03 against an NPC
				if (ForcedMovement == 0 && a->force != 0.0f && position_update_melee_push_timer.Check()) {
					m_Delta.x += a->force * g_Math.FastSin(a->hit_heading);
					m_Delta.y += a->force * g_Math.FastCos(a->hit_heading);
					ForcedMovement = 3;
				}
			}
		}

		//Note: if players can become pets, they will not receive damage messages of their own
		//this was done to simplify the code here (since we can only effectively skip one mob on queue)
		eqFilterType filter;
		Mob *skip = attacker;
		if (attacker && attacker->GetOwnerID()) {
			//attacker is a pet, let pet owners see their pet's damage
			Mob* owner = attacker->GetOwner();
			if (owner && owner->IsClient()) {
				if (((spell_id != SPELL_UNKNOWN) || (FromDamageShield)) && damage>0) {
					//special crap for spell damage, looks hackish to me
					char val1[20] = { 0 };
					owner->MessageString(Chat::NonMelee, OTHER_HIT_NONMELEE, GetCleanName(), ConvertArray(damage, val1));
				}
				else {
					if (damage > 0) {
						if (spell_id != SPELL_UNKNOWN)
							filter = iBuffTic ? FilterDOT : FilterSpellDamage;
						else
							filter = FilterPetHits;
					}
					else if (damage == -5)
						filter = FilterNone;	//cant filter invulnerable
					else
						filter = FilterPetMisses;

					if (!FromDamageShield)
						owner->CastToClient()->QueuePacket(outapp, true, CLIENT_CONNECTED, filter);
				}
			}
			skip = owner;
		}
		else {
			//attacker is not a pet, send to the attacker
			//if the attacker is a client, try them with the correct filter
			if (attacker && (attacker->IsClient() || attacker->IsBot())) {
				if ((spell_id != SPELL_UNKNOWN || FromDamageShield) && damage > 0) {
					//special crap for spell damage, looks hackish to me
					char val1[20] = { 0 };
					if (FromDamageShield) {
						if (attacker->CastToClient()->GetFilter(FilterDamageShields) != FilterHide)
							attacker->MessageString(Chat::DamageShield, OTHER_HIT_NONMELEE, GetCleanName(), ConvertArray(damage, val1));
					}
					else {
						entity_list.MessageCloseString(
							this, /* Sender */
							true, /* Skip Sender */
							RuleI(Range, SpellMessages),
							Chat::NonMelee, /* 283 */
							HIT_NON_MELEE, /* %1 hit %2 for %3 points of non-melee damage. */
							attacker->GetCleanName(), /* Message1 */
							GetCleanName(), /* Message2 */
							ConvertArray(damage, val1) /* Message3 */
						);
					}
				}
				// Only try to queue these packets to a client
				else if (attacker && (attacker->IsClient())) {
					if (damage > 0) {
						if (spell_id != SPELL_UNKNOWN)
							filter = iBuffTic ? FilterDOT : FilterSpellDamage;
						else
							filter = FilterNone;	//cant filter our own hits
					}
					else if (damage == -5)
						filter = FilterNone;	//cant filter invulnerable
					else
						filter = FilterMyMisses;

					attacker->CastToClient()->QueuePacket(outapp, true, CLIENT_CONNECTED, filter);
				}
			}
			skip = attacker;
		}

		//send damage to all clients around except the specified skip mob (attacker or the attacker's owner) and ourself
		if (damage > 0) {
			if (spell_id != SPELL_UNKNOWN)
				filter = iBuffTic ? FilterDOT : FilterSpellDamage;
			else
				filter = FilterOthersHit;
		}
		else if (damage == -5)
			filter = FilterNone;	//cant filter invulnerable
		else
			filter = FilterOthersMiss;
		//make attacker (the attacker) send the packet so we can skip them and the owner
		//this call will send the packet to `this` as well (using the wrong filter) (will not happen until PC charm works)
		// If this is Damage Shield damage, the correct OP_Damage packets will be sent from Mob::DamageShield, so
		// we don't send them here.
		if (!FromDamageShield) {

			// Determine message range based on spell/other-damage
			int range;
			if (IsValidSpell(spell_id)) {
				range = RuleI(Range, SpellMessages);
			}
			else {
				range = RuleI(Range, DamageMessages);
			}

			// If an "innate" spell, change to spell type to
			// produce a spell message.  Send to everyone.
			// This fixes issues with npc-procs like 1002 and 918 which
			// need to spit out extra spell color.
			if (IsValidSpell(spell_id) && skill_used == EQ::skills::SkillTigerClaw) {
				a->type = DamageTypeSpell;
				entity_list.QueueCloseClients(
					this, /* Sender */
					outapp, /* packet */
					false, /* Skip Sender */
					range, /* distance packet travels at the speed of sound */
					0, /* don't skip anyone on spell */
					true, /* Packet ACK */
					filter /* eqFilterType filter */
					);
			}
			else {
				//I dont think any filters apply to damage affecting us
				if (IsClient()) {
					CastToClient()->QueuePacket(outapp);
				}

				// Otherwise, send normal spell or melee message to observers.
				entity_list.QueueCloseClients(
					this, /* Sender */
					outapp, /* packet */
					true, /* Skip Sender */
					range, /* distance packet travels at the speed of sound */
					(IsValidSpell(spell_id) && skill_used != EQ::skills::SkillTigerClaw) ? 0 : skip,
					true, /* Packet ACK */
					filter /* eqFilterType filter */
					);
			}
		}

		safe_delete(outapp);
	}
	else {
		//else, it is a buff tic...
		// So we can see our dot dmg like live shows it.
		if (spell_id != SPELL_UNKNOWN && damage > 0 && attacker && attacker != this && attacker->IsClient()) {
			//might filter on (attack_skill>200 && attack_skill<250), but I dont think we need it
			attacker->FilteredMessageString(attacker, Chat::DotDamage, FilterDOT,
				YOUR_HIT_DOT, GetCleanName(), itoa(damage), spells[spell_id].name);

			/* older clients don't have the below String ID, but it will be filtered */
			entity_list.FilteredMessageCloseString(
				attacker, /* Sender */
				true, /* Skip Sender */
				RuleI(Range, SpellMessages),
				Chat::DotDamage, /* Type: 325 */
				FilterDOT, /* FilterType: 19 */
				OTHER_HIT_DOT,  /* MessageFormat: %1 has taken %2 damage from %3 by %4. */
				GetCleanName(), /* Message1 */
				itoa(damage), /* Message2 */
				attacker->GetCleanName(), /* Message3 */
				spells[spell_id].name /* Message4 */
			);
		}
	} //end packet sending

}

void Mob::HealDamage(uint64 amount, Mob *caster, uint16 spell_id)
{
	int64 maxhp = GetMaxHP();
	int64 curhp = GetHP();
	uint64 acthealed = 0;

	if (amount > (maxhp - curhp))
		acthealed = (maxhp - curhp);
	else
		acthealed = amount;

	if (acthealed > 100) {
		if (caster) {
			if (IsBuffSpell(spell_id)) { // hots
										 // message to caster
				if (caster->IsClient() && caster == this) {
					if (caster->CastToClient()->ClientVersionBit() & EQ::versions::maskSoFAndLater)
						FilteredMessageString(caster, Chat::NonMelee, FilterHealOverTime,
							HOT_HEAL_SELF, itoa(acthealed), spells[spell_id].name);
					else
						FilteredMessageString(caster, Chat::NonMelee, FilterHealOverTime,
							YOU_HEALED, GetCleanName(), itoa(acthealed));
				}
				else if (caster->IsClient() && caster != this) {
					if (caster->CastToClient()->ClientVersionBit() & EQ::versions::maskSoFAndLater)
						caster->FilteredMessageString(caster, Chat::NonMelee, FilterHealOverTime,
							HOT_HEAL_OTHER, GetCleanName(), itoa(acthealed),
							spells[spell_id].name);
					else
						caster->FilteredMessageString(caster, Chat::NonMelee, FilterHealOverTime,
							YOU_HEAL, GetCleanName(), itoa(acthealed));
				}
				// message to target
				if (IsClient() && caster != this) {
					if (CastToClient()->ClientVersionBit() & EQ::versions::maskSoFAndLater)
						FilteredMessageString(this, Chat::NonMelee, FilterHealOverTime,
							HOT_HEALED_OTHER, caster->GetCleanName(),
							itoa(acthealed), spells[spell_id].name);
					else
						FilteredMessageString(this, Chat::NonMelee, FilterHealOverTime,
							YOU_HEALED, caster->GetCleanName(), itoa(acthealed));
				}
			}
			else { // normal heals
				FilteredMessageString(caster, Chat::NonMelee, FilterSpellDamage,
					YOU_HEALED, caster->GetCleanName(), itoa(acthealed));
				if (caster != this)
					caster->FilteredMessageString(caster, Chat::NonMelee, FilterSpellDamage,
						YOU_HEAL, GetCleanName(), itoa(acthealed));
			}
		}
		else {
			Message(Chat::NonMelee, "You have been healed for %d points of damage.", acthealed);
		}
	}

	if (curhp < maxhp) {
		if ((curhp + amount) > maxhp)
			curhp = maxhp;
		else
			curhp += amount;
		SetHP(curhp);

		SendHPUpdate();
	}
}

//proc chance includes proc bonus
float Mob::GetProcChances(float ProcBonus, uint16 hand)
{
	int mydex = GetDEX();
	float ProcChance = 0.0f;

	uint32 weapon_speed = GetWeaponSpeedbyHand(hand);

	if (RuleB(Combat, AdjustProcPerMinute)) {
		ProcChance = (static_cast<float>(weapon_speed) *
			RuleR(Combat, AvgProcsPerMinute) / 60000.0f); // compensate for weapon_speed being in ms
		ProcBonus += static_cast<float>(mydex) * RuleR(Combat, ProcPerMinDexContrib);
		ProcChance += ProcChance * ProcBonus / 100.0f;
	}
	else {
		ProcChance = RuleR(Combat, BaseProcChance) +
			static_cast<float>(mydex) / RuleR(Combat, ProcDexDivideBy);
		ProcChance += ProcChance * ProcBonus / 100.0f;
	}

	LogCombat("Proc chance [{}] ([{}] from bonuses)", ProcChance, ProcBonus);
	return ProcChance;
}

float Mob::GetDefensiveProcChances(float &ProcBonus, float &ProcChance, uint16 hand, Mob* on) {

	if (!on)
		return ProcChance;

	int myagi = on->GetAGI();
	ProcBonus = 0;
	ProcChance = 0;

	uint32 weapon_speed = GetWeaponSpeedbyHand(hand);

	ProcChance = (static_cast<float>(weapon_speed) * RuleR(Combat, AvgDefProcsPerMinute) / 60000.0f); // compensate for weapon_speed being in ms
	ProcBonus += static_cast<float>(myagi) * RuleR(Combat, DefProcPerMinAgiContrib) / 100.0f;
	ProcChance = ProcChance + (ProcChance * ProcBonus);

	LogCombat("Defensive Proc chance [{}] ([{}] from bonuses)", ProcChance, ProcBonus);
	return ProcChance;
}

// argument 'weapon' not used
void Mob::TryDefensiveProc(Mob *on, uint16 hand) {

	if (!on) {
		SetTarget(nullptr);
		LogError("A null Mob object was passed to Mob::TryDefensiveProc for evaluation!");
		return;
	}

	if (!HasDefensiveProcs()) {
		return;
	}

	if (!on->HasDied() && on->GetHP() > 0) {

		float ProcChance, ProcBonus;
		on->GetDefensiveProcChances(ProcBonus, ProcChance, hand, this);

		if (hand == EQ::invslot::slotSecondary) {
			ProcChance /= 2;
		}

		int level_penalty = 0;
		int level_diff = GetLevel() - on->GetLevel();
		if (level_diff > 6) {//10% penalty per level if > 6 levels over target.
			level_penalty = (level_diff - 6) * 10;
		}

		ProcChance -= ProcChance*level_penalty / 100;

		if (ProcChance < 0) {
			return;
		}

		//Spell Procs and Quest added procs
		for (int i = 0; i < MAX_PROCS; i++) {
			if (IsValidSpell(DefensiveProcs[i].spellID)) {
				if (!IsProcLimitTimerActive(DefensiveProcs[i].base_spellID, DefensiveProcs[i].proc_reuse_time, ProcType::DEFENSIVE_PROC)) {
					float chance = ProcChance * (static_cast<float>(DefensiveProcs[i].chance) / 100.0f);
					if (zone->random.Roll(chance)) {
						ExecWeaponProc(nullptr, DefensiveProcs[i].spellID, on);
						CheckNumHitsRemaining(NumHit::DefensiveSpellProcs, 0, DefensiveProcs[i].base_spellID);
						SetProcLimitTimer(DefensiveProcs[i].base_spellID, DefensiveProcs[i].proc_reuse_time, ProcType::DEFENSIVE_PROC);
					}
				}
			}
		}

		//AA Procs
		if (IsClient()){
			for (int i = 0; i < MAX_AA_PROCS; i += 4) {
				int32 aa_rank_id = aabonuses.DefensiveProc[i + +SBIndex::COMBAT_PROC_ORIGIN_ID];
				int32 aa_spell_id = aabonuses.DefensiveProc[i + SBIndex::COMBAT_PROC_SPELL_ID];
				int32 aa_proc_chance = 100 + aabonuses.DefensiveProc[i + SBIndex::COMBAT_PROC_RATE_MOD];
				uint32 aa_proc_reuse_timer = aabonuses.DefensiveProc[i + SBIndex::COMBAT_PROC_REUSE_TIMER];

				if (aa_rank_id) {
					if (!IsProcLimitTimerActive(-aa_rank_id, aa_proc_reuse_timer, ProcType::DEFENSIVE_PROC)) {
						float chance = ProcChance * (static_cast<float>(aa_proc_chance) / 100.0f);
						if (zone->random.Roll(chance) && IsValidSpell(aa_spell_id)) {
							ExecWeaponProc(nullptr, aa_spell_id, on);
							SetProcLimitTimer(-aa_rank_id, aa_proc_reuse_timer, ProcType::DEFENSIVE_PROC);
						}
					}
				}
			}
		}
	}
}

void Mob::TryCombatProcs(const EQ::ItemInstance* weapon_g, Mob *on, uint16 hand, const EQ::ItemData* weapon_data) {

	if (!on) {
		SetTarget(nullptr);
		LogError("A null Mob object was passed to Mob::TryWeaponProc for evaluation!");
		return;
	}

	if (!IsAttackAllowed(on)) {
		LogCombat("Preventing procing off of unattackable things");
		return;
	}

	if (DivineAura()) {
		LogCombat("Procs cancelled, Divine Aura is in effect");
		return;
	}

	//used for special case when checking last ammo item on projectile hit.
	if (!weapon_g && weapon_data) {
		TryWeaponProc(nullptr, weapon_data, on, hand);
		TrySpellProc(nullptr, weapon_data, on, hand);
		return;
	}

	if (!weapon_g) {
		TrySpellProc(nullptr, (const EQ::ItemData*)nullptr, on);
		return;
	}

	if (!weapon_g->IsClassCommon()) {
		TrySpellProc(nullptr, (const EQ::ItemData*)nullptr, on);
		return;
	}

	// Innate + aug procs from weapons
	// TODO: powersource procs -- powersource procs are on invis augs, so shouldn't need anything extra
	TryWeaponProc(weapon_g, weapon_g->GetItem(), on, hand);
	// Procs from Buffs and AA both melee and range
	TrySpellProc(weapon_g, weapon_g->GetItem(), on, hand);

	return;
}

void Mob::TryWeaponProc(const EQ::ItemInstance *inst, const EQ::ItemData *weapon, Mob *on, uint16 hand)
{
	if (!on) {
		return;
	}
	if (!weapon)
		return;
	uint16 skillinuse = 28;
	int ourlevel = GetLevel();
	float ProcBonus = static_cast<float>(aabonuses.ProcChanceSPA +
		spellbonuses.ProcChanceSPA + itembonuses.ProcChanceSPA);
	ProcBonus += static_cast<float>(itembonuses.ProcChance) / 10.0f; // Combat Effects
	float ProcChance = GetProcChances(ProcBonus, hand);

	if (hand == EQ::invslot::slotSecondary)
		ProcChance /= 2;

	// Try innate proc on weapon
	// We can proc once here, either weapon or one aug
	bool proced = false; // silly bool to prevent augs from going if weapon does

	if (weapon->Proc.Type == EQ::item::ItemEffectCombatProc && IsValidSpell(weapon->Proc.Effect)) {
		float WPC = ProcChance * (100.0f + // Proc chance for this weapon
			static_cast<float>(weapon->ProcRate)) / 100.0f;
		if (zone->random.Roll(WPC)) {	// 255 dex = 0.084 chance of proc. No idea what this number should be really.
			if (weapon->Proc.Level2 > ourlevel) {
				LogCombat("Tried to proc ([{}]), but our level ([{}]) is lower than required ([{}])",
					weapon->Name, ourlevel, weapon->Proc.Level2);
				if (IsPet()) {
					Mob *own = GetOwner();
					if (own)
						own->MessageString(Chat::Red, PROC_PETTOOLOW);
				}
				else {
					MessageString(Chat::Red, PROC_TOOLOW);
				}
			}
			else {
				LogCombat("Attacking weapon ([{}]) successfully procing spell [{}] ([{}] percent chance)", weapon->Name, weapon->Proc.Effect, WPC * 100);
				ExecWeaponProc(inst, weapon->Proc.Effect, on);
				proced = true;
			}
		}
	}
	//If OneProcPerWeapon is not enabled, we reset the try for that weapon regardless of if we procced or not.
	//This is for some servers that may want to have as many procs triggering from weapons as possible in a single round.
	if (!RuleB(Combat, OneProcPerWeapon))
		proced = false;

	if (!proced && inst) {
		for (int r = EQ::invaug::SOCKET_BEGIN; r <= EQ::invaug::SOCKET_END; r++) {
			const EQ::ItemInstance *aug_i = inst->GetAugment(r);
			if (!aug_i) // no aug, try next slot!
				continue;
			const EQ::ItemData *aug = aug_i->GetItem();
			if (!aug)
				continue;

			if (aug->Proc.Type == EQ::item::ItemEffectCombatProc && IsValidSpell(aug->Proc.Effect)) {
				float APC = ProcChance * (100.0f + // Proc chance for this aug
					static_cast<float>(aug->ProcRate)) / 100.0f;
				if (zone->random.Roll(APC)) {
					if (aug->Proc.Level2 > ourlevel) {
						if (IsPet()) {
							Mob *own = GetOwner();
							if (own)
								own->MessageString(Chat::Red, PROC_PETTOOLOW);
						}
						else {
							MessageString(Chat::Red, PROC_TOOLOW);
						}
					}
					else {
						ExecWeaponProc(aug_i, aug->Proc.Effect, on);
						if (RuleB(Combat, OneProcPerWeapon))
							break;
					}
				}
			}
		}
	}
	// TODO: Powersource procs -- powersource procs are from augs so shouldn't need anything extra

	return;
}

void Mob::TrySpellProc(const EQ::ItemInstance *inst, const EQ::ItemData *weapon, Mob *on, uint16 hand)
{
	if (!on) {
		return;
	}

	float ProcBonus = static_cast<float>(spellbonuses.SpellProcChance +
		itembonuses.SpellProcChance + aabonuses.SpellProcChance);
	float ProcChance = 0.0f;
	ProcChance = GetProcChances(ProcBonus, hand);

	bool passed_skill_limit_check = true;
	EQ::skills::SkillType skillinuse = EQ::skills::SkillHandtoHand;

	if (weapon){
		skillinuse = GetSkillByItemType(weapon->ItemType);
	}

	if (hand == EQ::invslot::slotSecondary) {
		ProcChance /= 2;
	}

	bool rangedattk = false;
	if (weapon && hand == EQ::invslot::slotRange) {
		if (weapon->ItemType == EQ::item::ItemTypeArrow ||
			weapon->ItemType == EQ::item::ItemTypeLargeThrowing ||
			weapon->ItemType == EQ::item::ItemTypeSmallThrowing ||
			weapon->ItemType == EQ::item::ItemTypeBow) {
			rangedattk = true;
		}
	}

	if (!weapon && hand == EQ::invslot::slotRange && GetSpecialAbility(SPECATK_RANGED_ATK)) {
		rangedattk = true;
	}

	int16 poison_slot=-1;

	for (uint32 i = 0; i < MAX_PROCS; i++) {
		if (IsPet() && hand != EQ::invslot::slotPrimary) //Pets can only proc spell procs from their primay hand (ie; beastlord pets)
			continue; // If pets ever can proc from off hand, this will need to change

		if (SpellProcs[i].base_spellID == POISON_PROC &&
			(!weapon || weapon->ItemType != EQ::item::ItemType1HPiercing)) {
			continue; // Old school poison will only proc with 1HP equipped.
		}

		// Not ranged
		if (!rangedattk) {
			// Perma procs (Not used for AA, they are handled below)
			if (PermaProcs[i].spellID != SPELL_UNKNOWN) {
				if (zone->random.Roll(PermaProcs[i].chance)) { // TODO: Do these get spell bonus?
					LogCombat("Permanent proc [{}] procing spell [{}] ([{}] percent chance)", i, PermaProcs[i].spellID, PermaProcs[i].chance);
					ExecWeaponProc(nullptr, PermaProcs[i].spellID, on);
				}
				else {
					LogCombat("Permanent proc [{}] failed to proc [{}] ([{}] percent chance)", i, PermaProcs[i].spellID, PermaProcs[i].chance);
				}
			}

			// Spell procs (buffs)
			if (SpellProcs[i].spellID != SPELL_UNKNOWN) {
				if (SpellProcs[i].base_spellID == POISON_PROC) {
					poison_slot=i;
					continue; // Process the poison proc last per @mackal
				}

				passed_skill_limit_check = PassLimitToSkill(skillinuse, SpellProcs[i].base_spellID, ProcType::MELEE_PROC);

				if (passed_skill_limit_check && !IsProcLimitTimerActive(SpellProcs[i].base_spellID, SpellProcs[i].proc_reuse_time, ProcType::MELEE_PROC)) {
					float chance = ProcChance * (static_cast<float>(SpellProcs[i].chance) / 100.0f);
					if (zone->random.Roll(chance)) {
						LogCombat("Spell proc [{}] procing spell [{}] ([{}] percent chance)", i, SpellProcs[i].spellID, chance);
						SendBeginCast(SpellProcs[i].spellID, 0);
						ExecWeaponProc(nullptr, SpellProcs[i].spellID, on, SpellProcs[i].level_override);
						SetProcLimitTimer(SpellProcs[i].base_spellID, SpellProcs[i].proc_reuse_time, ProcType::MELEE_PROC);
						CheckNumHitsRemaining(NumHit::OffensiveSpellProcs, 0, SpellProcs[i].base_spellID);
					}
					else {
						LogCombat("Spell proc [{}] failed to proc [{}] ([{}] percent chance)", i, SpellProcs[i].spellID, chance);
					}
				}
			}
		}
		else if (rangedattk) { // ranged only
							   // ranged spell procs (buffs)
			if (RangedProcs[i].spellID != SPELL_UNKNOWN) {

				passed_skill_limit_check = PassLimitToSkill(skillinuse, RangedProcs[i].base_spellID, ProcType::RANGED_PROC);

				if (passed_skill_limit_check && !IsProcLimitTimerActive(RangedProcs[i].base_spellID, RangedProcs[i].proc_reuse_time, ProcType::RANGED_PROC)) {
					float chance = ProcChance * (static_cast<float>(RangedProcs[i].chance) / 100.0f);
					if (zone->random.Roll(chance)) {
						LogCombat("Ranged proc [{}] procing spell [{}] ([{}] percent chance)", i, RangedProcs[i].spellID, chance);
						ExecWeaponProc(nullptr, RangedProcs[i].spellID, on);
						CheckNumHitsRemaining(NumHit::OffensiveSpellProcs, 0, RangedProcs[i].base_spellID);
						SetProcLimitTimer(RangedProcs[i].base_spellID, RangedProcs[i].proc_reuse_time, ProcType::RANGED_PROC);
					}
					else {
						LogCombat("Ranged proc [{}] failed to proc [{}] ([{}] percent chance)", i, RangedProcs[i].spellID, chance);
					}
				}
			}
		}
	}

	//AA Melee and Ranged Procs
	if (IsClient()) {
		for (int i = 0; i < MAX_AA_PROCS; i += 4) {

			int32 aa_rank_id = 0;
			int32 aa_spell_id = SPELL_UNKNOWN;
			int32 aa_proc_chance = 100;
			uint32 aa_proc_reuse_timer = 0;
			int proc_type = 0; //used to deterimne which timer array is used.

			if (!rangedattk) {

				aa_rank_id = aabonuses.SpellProc[i + SBIndex::COMBAT_PROC_ORIGIN_ID];
				aa_spell_id = aabonuses.SpellProc[i + SBIndex::COMBAT_PROC_SPELL_ID];
				aa_proc_chance += aabonuses.SpellProc[i + SBIndex::COMBAT_PROC_RATE_MOD];
				aa_proc_reuse_timer = aabonuses.SpellProc[i + SBIndex::COMBAT_PROC_RATE_MOD];
				proc_type = ProcType::MELEE_PROC;
			}
			else {
				aa_rank_id = aabonuses.RangedProc[i + SBIndex::COMBAT_PROC_ORIGIN_ID];
				aa_spell_id = aabonuses.RangedProc[i + SBIndex::COMBAT_PROC_SPELL_ID];
				aa_proc_chance += aabonuses.RangedProc[i + SBIndex::COMBAT_PROC_RATE_MOD];
				aa_proc_reuse_timer = aabonuses.RangedProc[i + SBIndex::COMBAT_PROC_RATE_MOD];
				proc_type = ProcType::RANGED_PROC;
			}

			if (aa_rank_id) {

				passed_skill_limit_check = PassLimitToSkill(skillinuse, 0, proc_type, aa_rank_id);

				if (passed_skill_limit_check && !IsProcLimitTimerActive(-aa_rank_id, aa_proc_reuse_timer, proc_type)) {
					float chance = ProcChance * (static_cast<float>(aa_proc_chance) / 100.0f);
					if (zone->random.Roll(chance) && IsValidSpell(aa_spell_id)) {
						LogCombat("AA proc [{}] procing spell [{}] ([{}] percent chance)", aa_rank_id, aa_spell_id, chance);
						ExecWeaponProc(nullptr, aa_spell_id, on);
						SetProcLimitTimer(-aa_rank_id, aa_proc_reuse_timer, proc_type);
					}
					else {
						LogCombat("AA proc [{}] failed to proc [{}] ([{}] percent chance)", aa_rank_id, aa_spell_id, chance);
					}
				}
			}
		}
	}

	if (poison_slot > -1) {
		bool one_shot = !RuleB(Combat, UseExtendedPoisonProcs);
		float chance = (one_shot) ? 100.0f : ProcChance * (static_cast<float>(SpellProcs[poison_slot].chance) / 100.0f);
		uint16 spell_id = SpellProcs[poison_slot].spellID;

		if (zone->random.Roll(chance)) {
			LogCombat("Poison proc [{}] procing spell [{}] ([{}] percent chance)", poison_slot, spell_id, chance);
			SendBeginCast(spell_id, 0);
			ExecWeaponProc(nullptr, spell_id, on, SpellProcs[poison_slot].level_override);
			if (one_shot) {
				RemoveProcFromWeapon(spell_id);
			}
		}
	}

	TryCastOnSkillUse(on, skillinuse);

	if (HasSkillProcs() && hand != EQ::invslot::slotRange) { //We check ranged skill procs within the attack functions.
		TrySkillProc(on, skillinuse, 0, false, hand);
	}

	if (HasSkillProcSuccess() && hand != EQ::invslot::slotRange) { //We check ranged skill procs within the attack functions.
		TrySkillProc(on, skillinuse, 0, true, hand);
	}
	return;
}

void Mob::TryPetCriticalHit(Mob *defender, DamageHitInfo &hit)
{
	if (hit.damage_done < 1)
		return;

	// Allows pets to perform critical hits.
	// Each rank adds an additional 1% chance for any melee hit (primary, secondary, kick, bash, etc) to critical,
	// dealing up to 63% more damage. http://www.magecompendium.com/aa-short-library.html
	// appears to be 70% damage, unsure if changed or just bad info before

	Mob *owner = nullptr;
	int critChance = 0;
	critChance += RuleI(Combat, PetBaseCritChance); // 0 by default
	int critMod = 170;

	if (IsPet())
		owner = GetOwner();
	else if ((IsNPC() && CastToNPC()->GetSwarmOwner()))
		owner = entity_list.GetMobID(CastToNPC()->GetSwarmOwner());
	else
		return;

	if (!owner)
		return;

	int CritPetChance =
		owner->aabonuses.PetCriticalHit + owner->itembonuses.PetCriticalHit + owner->spellbonuses.PetCriticalHit;

	if (CritPetChance || critChance)
		// For pets use PetCriticalHit for base chance, pets do not innately critical with without it
		critChance += CritPetChance;

	if (critChance > 0) {
		if (zone->random.Roll(critChance)) {
			critMod += GetCritDmgMod(hit.skill, owner);
			hit.damage_done += 5;
			hit.damage_done = (hit.damage_done * critMod) / 100;

			entity_list.FilteredMessageCloseString(
				this, /* Sender */
				false,  /* Skip Sender */
				RuleI(Range, CriticalDamage),
				Chat::MeleeCrit, /* Type: 301 */
				FilterMeleeCrits, /* FilterType: 12 */
				CRITICAL_HIT, /* MessageFormat: %1 scores a critical hit! (%2) */
				GetCleanName(), /* Message1 */
				itoa(hit.damage_done) /* Message2 */
			);

		}
	}
}

void Mob::TryCriticalHit(Mob *defender, DamageHitInfo &hit, ExtraAttackOptions *opts)
{
#ifdef LUA_EQEMU
	bool ignoreDefault = false;
	LuaParser::Instance()->TryCriticalHit(this, defender, hit, opts, ignoreDefault);

	if (ignoreDefault) {
		return;
	}
#endif

	if (hit.damage_done < 1 || !defender)
		return;

	// decided to branch this into it's own function since it's going to be duplicating a lot of the
	// code in here, but could lead to some confusion otherwise
	if ((IsPet() && GetOwner()->IsClient()) || (IsNPC() && CastToNPC()->GetSwarmOwner())) {
		TryPetCriticalHit(defender, hit);
		return;
	}
	
	if (IsNPC() && !RuleB(Combat, NPCCanCrit))
		return;

	// 1: Try Slay Undead
	if (defender->GetBodyType() == BT_Undead || defender->GetBodyType() == BT_SummonedUndead ||
		defender->GetBodyType() == BT_Vampire) {
		int SlayRateBonus = aabonuses.SlayUndead[SBIndex::SLAYUNDEAD_RATE_MOD] + itembonuses.SlayUndead[SBIndex::SLAYUNDEAD_RATE_MOD] + spellbonuses.SlayUndead[SBIndex::SLAYUNDEAD_RATE_MOD];
		if (SlayRateBonus) {
			float slayChance = static_cast<float>(SlayRateBonus) / 10000.0f;
			if (zone->random.Roll(slayChance)) {
				int SlayDmgBonus = std::max(
				{aabonuses.SlayUndead[SBIndex::SLAYUNDEAD_DMG_MOD], itembonuses.SlayUndead[SBIndex::SLAYUNDEAD_DMG_MOD], spellbonuses.SlayUndead[SBIndex::SLAYUNDEAD_DMG_MOD] });
				hit.damage_done = std::max(hit.damage_done, hit.base_damage) + 5;
				hit.damage_done = (hit.damage_done * SlayDmgBonus) / 100;

				/* Female */
				if (GetGender() == 1) {
					entity_list.FilteredMessageCloseString(
						this, /* Sender */
						false, /* Skip Sender */
						RuleI(Range, CriticalDamage),
						Chat::MeleeCrit, /* Type: 301 */
						FilterMeleeCrits, /* FilterType: 12 */
						FEMALE_SLAYUNDEAD, /* MessageFormat: %1's holy blade cleanses her target!(%2) */
						GetCleanName(), /* Message1 */
						itoa(hit.damage_done) /* Message2 */
					);
				}
				/* Males and Neuter */
				else {
					entity_list.FilteredMessageCloseString(
						this, /* Sender */
						false, /* Skip Sender */
						RuleI(Range, CriticalDamage),
						Chat::MeleeCrit, /* Type: 301 */
						FilterMeleeCrits, /* FilterType: 12 */
						MALE_SLAYUNDEAD, /* MessageFormat: %1's holy blade cleanses his target!(%2)  */
						GetCleanName(), /* Message1 */
						itoa(hit.damage_done) /* Message2 */
					);
				}
				return;
			}
		}
	}

	// 2: Try Melee Critical
	// a lot of good info: http://giline.versus.jp/shiden/damage_e.htm, http://giline.versus.jp/shiden/su.htm

	// We either require an innate crit chance or some SPA 169 to crit
	bool innate_crit = false;
	int crit_chance = GetCriticalChanceBonus(hit.skill);
	if ((GetClass() == WARRIOR || GetClass() == BERSERKER) && GetLevel() >= 12)
		innate_crit = true;
	else if (GetClass() == RANGER && GetLevel() >= 12 && hit.skill == EQ::skills::SkillArchery)
		innate_crit = true;
	else if (GetClass() == ROGUE && GetLevel() >= 12 && hit.skill == EQ::skills::SkillThrowing)
		innate_crit = true;

	// we have a chance to crit!
	if (innate_crit || crit_chance) {
		int difficulty = 0;
		if (hit.skill == EQ::skills::SkillArchery)
			difficulty = RuleI(Combat, ArcheryCritDifficulty);
		else if (hit.skill == EQ::skills::SkillThrowing)
			difficulty = RuleI(Combat, ThrowingCritDifficulty);
		else
			difficulty = RuleI(Combat, MeleeCritDifficulty);
		int roll = zone->random.Int(1, difficulty);

		int dex_bonus = GetDEX();
		if (dex_bonus > 255)
			dex_bonus = 255 + ((dex_bonus - 255) / 5);
		dex_bonus += 45; // chances did not match live without a small boost

						 // so if we have an innate crit we have a better chance, except for ber throwing
		if (!innate_crit || (GetClass() == BERSERKER && hit.skill == EQ::skills::SkillThrowing))
			dex_bonus = dex_bonus * 3 / 5;

		if (crit_chance)
			dex_bonus += dex_bonus * crit_chance / 100;

		// check if we crited
		if (roll < dex_bonus) {
			// step 1: check for finishing blow
			if (TryFinishingBlow(defender, hit.damage_done))
				return;

			// step 2: calculate damage
			hit.damage_done = std::max(hit.damage_done, hit.base_damage) + 5;
			int og_damage = hit.damage_done;
			int crit_mod = 170 + GetCritDmgMod(hit.skill);
			if (crit_mod < 100) {
				crit_mod = 100;
			}

			hit.damage_done = hit.damage_done * crit_mod / 100;
			LogCombat("Crit success roll [{}] dex chance [{}] og dmg [{}] crit_mod [{}] new dmg [{}]", roll, dex_bonus, og_damage, crit_mod, hit.damage_done);

			// step 3: check deadly strike
			if (GetClass() == ROGUE && hit.skill == EQ::skills::SkillThrowing) {
				if (BehindMob(defender, GetX(), GetY())) {
					int chance = GetLevel() * 12;
					if (zone->random.Int(1, 1000) < chance) {
						// step 3a: check assassinate
						int assdmg = TryAssassinate(defender, hit.skill); // I don't think this is right
						if (assdmg) {
							hit.damage_done = assdmg;
							return;
						}
						hit.damage_done = hit.damage_done * 200 / 100;

						entity_list.FilteredMessageCloseString(
							this, /* Sender */
							false, /* Skip Sender */
							RuleI(Range, CriticalDamage),
							Chat::MeleeCrit, /* Type: 301 */
							FilterMeleeCrits, /* FilterType: 12 */
							DEADLY_STRIKE, /* MessageFormat: %1 scores a Deadly Strike!(%2) */
							GetCleanName(), /* Message1 */
							itoa(hit.damage_done) /* Message2 */
						);
						return;
					}
				}
			}

			// step 4: check crips
			// this SPA was reused on live ...
			bool berserk = spellbonuses.BerserkSPA || itembonuses.BerserkSPA || aabonuses.BerserkSPA;
			if (!berserk) {
				if (zone->random.Roll(GetCrippBlowChance())) {
					berserk = true;
				} // TODO: Holyforge is suppose to have an innate extra undead chance? 1/5 which matches the SPA crip though ...
			}

			if (IsBerserk() || berserk) {
				hit.damage_done += og_damage * 119 / 100;
				LogCombat("Crip damage [{}]", hit.damage_done);

				entity_list.FilteredMessageCloseString(
					this, /* Sender */
					false, /* Skip Sender */
					RuleI(Range, CriticalDamage),
					Chat::MeleeCrit, /* Type: 301 */
					FilterMeleeCrits, /* FilterType: 12 */
					CRIPPLING_BLOW, /* MessageFormat: %1 lands a Crippling Blow!(%2) */
					GetCleanName(), /* Message1 */
					itoa(hit.damage_done) /* Message2 */
				);

				// Crippling blows also have a chance to stun
				// Kayen: Crippling Blow would cause a chance to interrupt for npcs < 55, with a
				// staggers message.
				if (defender->GetLevel() <= 55 && !defender->GetSpecialAbility(UNSTUNABLE)) {
					defender->Emote("staggers.");
					defender->Stun(2000);
				}
				return;
			}

			/* Normal Critical hit message */
			entity_list.FilteredMessageCloseString(
				this, /* Sender */
				false, /* Skip Sender */
				RuleI(Range, CriticalDamage),
				Chat::MeleeCrit, /* Type: 301 */
				FilterMeleeCrits, /* FilterType: 12 */
				CRITICAL_HIT, /* MessageFormat: %1 scores a critical hit! (%2) */
				GetCleanName(), /* Message1 */
				itoa(hit.damage_done) /* Message2 */
			);
		}
	}
}

bool Mob::TryFinishingBlow(Mob *defender, int64 &damage)
{
	float hp_limit = 10.0f;

	auto fb_hp_limit = std::max(
		{
			aabonuses.FinishingBlowLvl[SBIndex::FINISHING_BLOW_LEVEL_HP_RATIO],
			spellbonuses.FinishingBlowLvl[SBIndex::FINISHING_BLOW_LEVEL_HP_RATIO],
			itembonuses.FinishingBlowLvl[SBIndex::FINISHING_BLOW_LEVEL_HP_RATIO]
		}
	);

	if (fb_hp_limit) {
		hp_limit = fb_hp_limit/10.0f;
	}
	if (defender && !defender->IsClient() && defender->GetHPRatio() < hp_limit) {

		uint32 FB_Dmg =
				   aabonuses.FinishingBlow[SBIndex::FINISHING_EFFECT_DMG] + spellbonuses.FinishingBlow[SBIndex::FINISHING_EFFECT_DMG] + itembonuses.FinishingBlow[SBIndex::FINISHING_EFFECT_DMG];

		uint32 FB_Level = 0;
		FB_Level = aabonuses.FinishingBlowLvl[SBIndex::FINISHING_EFFECT_LEVEL_MAX];
		if (FB_Level < spellbonuses.FinishingBlowLvl[SBIndex::FINISHING_EFFECT_LEVEL_MAX])
			FB_Level = spellbonuses.FinishingBlowLvl[SBIndex::FINISHING_EFFECT_LEVEL_MAX];
		else if (FB_Level < itembonuses.FinishingBlowLvl[SBIndex::FINISHING_EFFECT_LEVEL_MAX])
			FB_Level = itembonuses.FinishingBlowLvl[SBIndex::FINISHING_EFFECT_LEVEL_MAX];

		// modern AA description says rank 1 (500) is 50% chance
		int ProcChance =
				aabonuses.FinishingBlow[SBIndex::FINISHING_EFFECT_PROC_CHANCE] + spellbonuses.FinishingBlow[SBIndex::FINISHING_EFFECT_PROC_CHANCE] + spellbonuses.FinishingBlow[SBIndex::FINISHING_EFFECT_PROC_CHANCE];

		if (FB_Level && FB_Dmg && (defender->GetLevel() <= FB_Level) &&
			(ProcChance >= zone->random.Int(1, 1000))) {

			/* Finishing Blow Critical Message */
			entity_list.FilteredMessageCloseString(
				this, /* Sender */
				false, /* Skip Sender */
				RuleI(Range, CriticalDamage),
				Chat::MeleeCrit, /* Type: 301 */
				FilterMeleeCrits, /* FilterType: 12 */
				FINISHING_BLOW, /* MessageFormat: %1 scores a Finishing Blow!!) */
				GetCleanName() /* Message1 */
			);

			damage = FB_Dmg;
			return true;
		}
	}
	return false;
}

void Mob::DoRiposte(Mob *defender)
{
	LogCombat("Preforming a riposte");

	if (!defender)
		return;

	// so ahhh the angle you can riposte is larger than the angle you can hit :P
	if (!defender->IsFacingMob(this)) {
		defender->MessageString(Chat::TooFarAway, CANT_SEE_TARGET);
		return;
	}

	defender->Attack(this, EQ::invslot::slotPrimary, true);

	if (HasDied())
		return;

	// this effect isn't used on live? See no AAs or spells
	int32 DoubleRipChance = defender->aabonuses.DoubleRiposte + defender->spellbonuses.DoubleRiposte +
		defender->itembonuses.DoubleRiposte;

	if (DoubleRipChance && zone->random.Roll(DoubleRipChance)) {
		LogCombat("Preforming a double riposted from SE_DoubleRiposte ([{}] percent chance)", DoubleRipChance);
		defender->Attack(this, EQ::invslot::slotPrimary, true);
		if (HasDied())
			return;
	}

	DoubleRipChance = defender->aabonuses.GiveDoubleRiposte[SBIndex::DOUBLE_RIPOSTE_CHANCE] + defender->spellbonuses.GiveDoubleRiposte[SBIndex::DOUBLE_RIPOSTE_CHANCE] +
					  defender->itembonuses.GiveDoubleRiposte[SBIndex::DOUBLE_RIPOSTE_CHANCE];

	// Live AA - Double Riposte
	if (DoubleRipChance && zone->random.Roll(DoubleRipChance)) {
		LogCombat("Preforming a double riposted from SE_GiveDoubleRiposte base1 == 0 ([{}] percent chance)", DoubleRipChance);
		defender->Attack(this, EQ::invslot::slotPrimary, true);
		if (HasDied())
			return;
	}

	// Double Riposte effect, allows for a chance to do RIPOSTE with a skill specific special attack (ie Return Kick).
	// Coded narrowly: Limit to one per client. Limit AA only. [1 = Skill Attack Chance, 2 = Skill]

	DoubleRipChance = defender->aabonuses.GiveDoubleRiposte[SBIndex::DOUBLE_RIPOSTE_SKILL_ATK_CHANCE];

	if (DoubleRipChance && zone->random.Roll(DoubleRipChance)) {
		LogCombat("Preforming a return SPECIAL ATTACK ([{}] percent chance)", DoubleRipChance);

		if (defender->GetClass() == MONK)
			defender->MonkSpecialAttack(this, defender->aabonuses.GiveDoubleRiposte[SBIndex::DOUBLE_RIPOSTE_SKILL]);
		else if (defender->IsClient()) // so yeah, even if you don't have the skill you can still do the attack :P (and we don't crash anymore)
			defender->CastToClient()->DoClassAttacks(this, defender->aabonuses.GiveDoubleRiposte[SBIndex::DOUBLE_RIPOSTE_SKILL], true);
	}
}

void Mob::ApplyMeleeDamageMods(uint16 skill, int64 &damage, Mob *defender, ExtraAttackOptions *opts)
{
	int64 dmgbonusmod = 0;

	dmgbonusmod += GetMeleeDamageMod_SE(skill);
	dmgbonusmod += GetMeleeDmgPositionMod(defender);
	if (opts)
		dmgbonusmod += opts->melee_damage_bonus_flat;

	if (defender) {
		if (defender->IsClient() && defender->GetClass() == WARRIOR)
			dmgbonusmod -= 5;
		// 168 defensive
		dmgbonusmod += (defender->spellbonuses.MeleeMitigationEffect +
		                defender->itembonuses.MeleeMitigationEffect +
		                defender->aabonuses.MeleeMitigationEffect);
	}

	damage += damage * dmgbonusmod / 100;
}

bool Mob::HasDied() {
	bool Result = false;
	int64 hp_below = 0;

	hp_below = (GetDelayDeath() * -1);

	if ((GetHP()) <= (hp_below))
		Result = true;

	return Result;
}

const DamageTable &Mob::GetDamageTable() const
{
	static const DamageTable dmg_table[] = {
		{ 210, 49, 105 }, // 1-50
		{ 245, 35,  80 }, // 51
		{ 245, 35,  80 }, // 52
		{ 245, 35,  80 }, // 53
		{ 245, 35,  80 }, // 54
		{ 245, 35,  80 }, // 55
		{ 265, 28,  70 }, // 56
		{ 265, 28,  70 }, // 57
		{ 265, 28,  70 }, // 58
		{ 265, 28,  70 }, // 59
		{ 285, 23,  65 }, // 60
		{ 285, 23,  65 }, // 61
		{ 285, 23,  65 }, // 62
		{ 290, 21,  60 }, // 63
		{ 290, 21,  60 }, // 64
		{ 295, 19,  55 }, // 65
		{ 295, 19,  55 }, // 66
		{ 300, 19,  55 }, // 67
		{ 300, 19,  55 }, // 68
		{ 300, 19,  55 }, // 69
		{ 305, 19,  55 }, // 70
		{ 305, 19,  55 }, // 71
		{ 310, 17,  50 }, // 72
		{ 310, 17,  50 }, // 73
		{ 310, 17,  50 }, // 74
		{ 315, 17,  50 }, // 75
		{ 315, 17,  50 }, // 76
		{ 325, 17,  45 }, // 77
		{ 325, 17,  45 }, // 78
		{ 325, 17,  45 }, // 79
		{ 335, 17,  45 }, // 80
		{ 335, 17,  45 }, // 81
		{ 345, 17,  45 }, // 82
		{ 345, 17,  45 }, // 83
		{ 345, 17,  45 }, // 84
		{ 355, 17,  45 }, // 85
		{ 355, 17,  45 }, // 86
		{ 365, 17,  45 }, // 87
		{ 365, 17,  45 }, // 88
		{ 365, 17,  45 }, // 89
		{ 375, 17,  45 }, // 90
		{ 375, 17,  45 }, // 91
		{ 380, 17,  45 }, // 92
		{ 380, 17,  45 }, // 93
		{ 380, 17,  45 }, // 94
		{ 385, 17,  45 }, // 95
		{ 385, 17,  45 }, // 96
		{ 390, 17,  45 }, // 97
		{ 390, 17,  45 }, // 98
		{ 390, 17,  45 }, // 99
		{ 395, 17,  45 }, // 100
		{ 395, 17,  45 }, // 101
		{ 400, 17,  45 }, // 102
		{ 400, 17,  45 }, // 103
		{ 400, 17,  45 }, // 104
		{ 405, 17,  45 }  // 105
	};

	static const DamageTable mnk_table[] = {
		{ 220, 45, 100 }, // 1-50
		{ 245, 35,  80 }, // 51
		{ 245, 35,  80 }, // 52
		{ 245, 35,  80 }, // 53
		{ 245, 35,  80 }, // 54
		{ 245, 35,  80 }, // 55
		{ 285, 23,  65 }, // 56
		{ 285, 23,  65 }, // 57
		{ 285, 23,  65 }, // 58
		{ 285, 23,  65 }, // 59
		{ 290, 21,  60 }, // 60
		{ 290, 21,  60 }, // 61
		{ 290, 21,  60 }, // 62
		{ 295, 19,  55 }, // 63
		{ 295, 19,  55 }, // 64
		{ 300, 17,  50 }, // 65
		{ 300, 17,  50 }, // 66
		{ 310, 17,  50 }, // 67
		{ 310, 17,  50 }, // 68
		{ 310, 17,  50 }, // 69
		{ 320, 17,  50 }, // 70
		{ 320, 17,  50 }, // 71
		{ 325, 15,  45 }, // 72
		{ 325, 15,  45 }, // 73
		{ 325, 15,  45 }, // 74
		{ 330, 15,  45 }, // 75
		{ 330, 15,  45 }, // 76
		{ 335, 15,  40 }, // 77
		{ 335, 15,  40 }, // 78
		{ 335, 15,  40 }, // 79
		{ 345, 15,  40 }, // 80
		{ 345, 15,  40 }, // 81
		{ 355, 15,  40 }, // 82
		{ 355, 15,  40 }, // 83
		{ 355, 15,  40 }, // 84
		{ 365, 15,  40 }, // 85
		{ 365, 15,  40 }, // 86
		{ 375, 15,  40 }, // 87
		{ 375, 15,  40 }, // 88
		{ 375, 15,  40 }, // 89
		{ 385, 15,  40 }, // 90
		{ 385, 15,  40 }, // 91
		{ 390, 15,  40 }, // 92
		{ 390, 15,  40 }, // 93
		{ 390, 15,  40 }, // 94
		{ 395, 15,  40 }, // 95
		{ 395, 15,  40 }, // 96
		{ 400, 15,  40 }, // 97
		{ 400, 15,  40 }, // 98
		{ 400, 15,  40 }, // 99
		{ 405, 15,  40 }, // 100
		{ 405, 15,  40 }, // 101
		{ 410, 15,  40 }, // 102
		{ 410, 15,  40 }, // 103
		{ 410, 15,  40 }, // 104
		{ 415, 15,  40 }, // 105
	};

	bool monk = GetClass() == MONK;
	bool melee = IsWarriorClass();
	// tables caped at 105 for now -- future proofed for a while at least :P
	int level = std::min(static_cast<int>(GetLevel()), 105);

	if (!melee || (!monk && level < 51))
		return dmg_table[0];

	if (monk && level < 51)
		return mnk_table[0];

	auto &which = monk ? mnk_table : dmg_table;
	return which[level - 50];
}

int64 Client::GetDamageBonus()
{
	if (GetLevel() < 28 || !IsWarriorClass())
		return 0;

	int delay = 1;
	int64 bonus = 1 + (GetLevel() - 28) / 3;

	EQ::ItemInstance* weaponInst = GetInv().GetItem(EQ::invslot::slotPrimary);
	const EQ::ItemData* weapon = nullptr;
	if (weaponInst)
		weapon = weaponInst->GetItem();

	if (!weapon)
		delay = GetHandToHandDelay();
	else
		delay = weapon->Delay;

	if (weapon && (weapon->ItemType == EQ::item::ItemType2HSlash || weapon->ItemType == EQ::item::ItemType2HBlunt || weapon->ItemType == EQ::item::ItemType2HPiercing))
	{
		if (delay <= 27)
			return bonus + 1;

		if (level > 29)
		{
			int level_bonus = (level - 30) / 5 + 1;
			if (level > 50)
			{
				level_bonus++;
				int level_bonus2 = level - 50;
				if (level > 67)
					level_bonus2 += 5;
				else if (level > 59)
					level_bonus2 += 4;
				else if (level > 58)
					level_bonus2 += 3;
				else if (level > 56)
					level_bonus2 += 2;
				else if (level > 54)
					level_bonus2++;
				level_bonus += level_bonus2 * delay / 40;
			}
			bonus += level_bonus;
		}
		if (delay >= 40)
		{
			int delay_bonus = (delay - 40) / 3 + 1;
			if (delay >= 45)
				delay_bonus += 2;
			else if (delay >= 43)
				delay_bonus++;
			bonus += delay_bonus;
		}
		return bonus;
	}
	return bonus;
}

void Mob::ApplyDamageTable(DamageHitInfo &hit)
{
#ifdef LUA_EQEMU
	bool ignoreDefault = false;
	LuaParser::Instance()->ApplyDamageTable(this, hit, ignoreDefault);

	if (ignoreDefault) {
		return;
	}
#endif

	// someone may want to add this to custom servers, can remove this if that's the case
	if (!IsClient()
		)
		return;
	// this was parsed, but we do see the min of 10 and the normal minus factor is 105, so makes sense
	if (hit.offense < 115)
		return;

	// things that come out to 1 dmg seem to skip this (ex non-bash slam classes)
	if (hit.damage_done < 2)
		return;

	auto &damage_table = GetDamageTable();

	if (zone->random.Roll(damage_table.chance))
		return;

	int basebonus = hit.offense - damage_table.minusfactor;
	basebonus = std::max(10, basebonus / 2);
	int extrapercent = zone->random.Roll0(basebonus);
	int percent = std::min(100 + extrapercent, damage_table.max_extra);
	hit.damage_done = (hit.damage_done * percent) / 100;

	if (IsWarriorClass() && GetLevel() > 54)
		hit.damage_done++;
	Log(Logs::Detail, Logs::Attack, "Damage table applied %d (max %d)", percent, damage_table.max_extra);
}

void Mob::TrySkillProc(Mob *on, EQ::skills::SkillType skill, uint16 ReuseTime, bool Success, uint16 hand, bool IsDefensive)
{
	if (!on) {
		SetTarget(nullptr);
		LogError("A null Mob object was passed to Mob::TrySkillProc for evaluation!");
		return;
	}

	if (on->HasDied()) {
		return;
	}

	if (!spellbonuses.LimitToSkill[skill] && !itembonuses.LimitToSkill[skill] && !aabonuses.LimitToSkill[skill]) {
		return;
	}

	/*
		Allow one proc from each (Spell/Item/AA)
		Kayen: Due to limited avialability of effects on live it is too difficult
		to confirm how they stack at this time, will adjust formula when more data is avialablle to test.
	*/
	bool CanProc = true;

	uint16 base_spell_id = 0;
	uint16 proc_spell_id = 0;
	float ProcMod = 0;
	float chance = 0;

	if (IsDefensive) {
		chance = on->GetSkillProcChances(ReuseTime, hand);
	}
	else {
		chance = GetSkillProcChances(ReuseTime, hand);
	}

	if (spellbonuses.LimitToSkill[skill]) {

		for (int i = 0; i < MAX_SKILL_PROCS; i++) {
			if (CanProc &&
				((!Success && spellbonuses.SkillProc[i] && IsValidSpell(spellbonuses.SkillProc[i]))
					|| (Success && spellbonuses.SkillProcSuccess[i] && IsValidSpell(spellbonuses.SkillProcSuccess[i])))) {

				if (Success) {
					base_spell_id = spellbonuses.SkillProcSuccess[i];
				}
				else {
					base_spell_id = spellbonuses.SkillProc[i];
				}

				proc_spell_id = 0;
				ProcMod = 0;

				for (int i = 0; i < EFFECT_COUNT; i++) {

					if (spells[base_spell_id].effect_id[i] == SE_SkillProcAttempt || spells[base_spell_id].effect_id[i] == SE_SkillProcSuccess) {
						proc_spell_id = spells[base_spell_id].base_value[i];
						ProcMod = static_cast<float>(spells[base_spell_id].limit_value[i]);
					}

					else if (spells[base_spell_id].effect_id[i] == SE_LimitToSkill && spells[base_spell_id].base_value[i] <= EQ::skills::HIGHEST_SKILL) {
						if (CanProc && spells[base_spell_id].base_value[i] == skill && IsValidSpell(proc_spell_id)) {
							float final_chance = chance * (ProcMod / 100.0f);
							if (zone->random.Roll(final_chance)) {
								ExecWeaponProc(nullptr, proc_spell_id, on);
								CheckNumHitsRemaining(NumHit::OffensiveSpellProcs, 0, base_spell_id);
								CanProc = false;
								break;
							}
						}
					}
					else {
						//Reset and check for proc in sequence
						proc_spell_id = 0;
						ProcMod = 0;
					}
				}
			}
		}
	}

	if (itembonuses.LimitToSkill[skill]) {
		CanProc = true;
		for (int i = 0; i < MAX_SKILL_PROCS; i++) {
			if (CanProc &&
				((!Success && itembonuses.SkillProc[i] && IsValidSpell(itembonuses.SkillProc[i]))
					|| (Success && itembonuses.SkillProcSuccess[i] && IsValidSpell(itembonuses.SkillProcSuccess[i])))) {

				if (Success) {
					base_spell_id = itembonuses.SkillProcSuccess[i];
				}
				else {
					base_spell_id = itembonuses.SkillProc[i];
				}

				proc_spell_id = 0;
				ProcMod = 0;

				for (int i = 0; i < EFFECT_COUNT; i++) {
					if (spells[base_spell_id].effect_id[i] == SE_SkillProcAttempt || spells[base_spell_id].effect_id[i] == SE_SkillProcSuccess) {
						proc_spell_id = spells[base_spell_id].base_value[i];
						ProcMod = static_cast<float>(spells[base_spell_id].limit_value[i]);
					}

					else if (spells[base_spell_id].effect_id[i] == SE_LimitToSkill && spells[base_spell_id].base_value[i] <= EQ::skills::HIGHEST_SKILL) {

						if (CanProc && spells[base_spell_id].base_value[i] == skill && IsValidSpell(proc_spell_id)) {
							float final_chance = chance * (ProcMod / 100.0f);
							if (zone->random.Roll(final_chance)) {
								ExecWeaponProc(nullptr, proc_spell_id, on);
								CanProc = false;
								break;
							}
						}
					}
					else {
						proc_spell_id = 0;
						ProcMod = 0;
					}
				}
			}
		}
	}

	if (IsClient() && aabonuses.LimitToSkill[skill]) {

		CanProc = true;
		uint32 effect_id = 0;
		int32 base_value = 0;
		int32 limit_value = 0;
		uint32 slot = 0;

		for (int i = 0; i < MAX_SKILL_PROCS; i++) {
			if (CanProc &&
				((!Success && aabonuses.SkillProc[i])
					|| (Success && aabonuses.SkillProcSuccess[i]))) {
				int aaid = 0;

				if (Success)
					base_spell_id = aabonuses.SkillProcSuccess[i];
				else
					base_spell_id = aabonuses.SkillProc[i];

				proc_spell_id = 0;
				ProcMod = 0;

				for (auto &rank_info : aa_ranks) {
					auto ability_rank = zone->GetAlternateAdvancementAbilityAndRank(rank_info.first, rank_info.second.first);
					auto ability = ability_rank.first;
					auto rank = ability_rank.second;

					if (!ability) {
						continue;
					}

					for (auto &effect : rank->effects) {
						effect_id = effect.effect_id;
						base_value = effect.base_value;
						limit_value = effect.limit_value;
						slot = effect.slot;

						if (effect_id == SE_SkillProcAttempt || effect_id == SE_SkillProcSuccess) {
							proc_spell_id = base_value;
							ProcMod = static_cast<float>(limit_value);
						}
						else if (effect_id == SE_LimitToSkill && base_value <= EQ::skills::HIGHEST_SKILL) {

							if (CanProc && base_value == skill && IsValidSpell(proc_spell_id)) {
								float final_chance = chance * (ProcMod / 100.0f);

								if (zone->random.Roll(final_chance)) {
									ExecWeaponProc(nullptr, proc_spell_id, on);
									CanProc = false;
									break;
								}
							}
						}
						else {
							proc_spell_id = 0;
							ProcMod = 0;
						}
					}
				}
			}
		}
	}
}

float Mob::GetSkillProcChances(uint16 ReuseTime, uint16 hand) {

	uint32 weapon_speed;
	float ProcChance = 0;

	if (!ReuseTime && hand) {
		weapon_speed = GetWeaponSpeedbyHand(hand);
		ProcChance = static_cast<float>(weapon_speed) * (RuleR(Combat, AvgProcsPerMinute) / 60000.0f);
		if (hand == EQ::invslot::slotSecondary) {
			ProcChance /= 2;
		}
	}

	else {
		ProcChance = static_cast<float>(ReuseTime) * (RuleR(Combat, AvgProcsPerMinute) / 60000.0f);
	}

	return ProcChance;
}

void Mob::TryCastOnSkillUse(Mob *on, EQ::skills::SkillType skill) {

	if (!spellbonuses.HasSkillAttackProc[skill] && !itembonuses.HasSkillAttackProc[skill] && !aabonuses.HasSkillAttackProc[skill]) {
		return;
	}

	if (!on) {
		SetTarget(nullptr);
		LogError("A null Mob object was passed to Mob::TryCastOnSkillUse for evaluation!");
		return;
	}

	if (on->HasDied()) {
		return;
	}

	if (spellbonuses.HasSkillAttackProc[skill]) {
		for (int i = 0; i < MAX_CAST_ON_SKILL_USE; i += 3) {
			if (spellbonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID] && skill == spellbonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SKILL]) {
				if (IsValidSpell(spellbonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID]) && zone->random.Int(1, 1000) <= spellbonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_CHANCE]) {
					SpellFinished(spellbonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID], on, EQ::spells::CastingSlot::Item, 0, -1, spells[spellbonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID]].resist_difficulty);
				}
			}
		}
	}

	if (itembonuses.HasSkillAttackProc[skill]) {
		for (int i = 0; i < MAX_CAST_ON_SKILL_USE; i += 3) {
			if (itembonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID] && skill == itembonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SKILL]) {
				if (IsValidSpell(itembonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID]) && zone->random.Int(1, 1000) <= spellbonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_CHANCE]) {
					SpellFinished(itembonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID], on, EQ::spells::CastingSlot::Item, 0, -1, spells[itembonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID]].resist_difficulty);
				}
			}
		}
	}

	if (aabonuses.HasSkillAttackProc[skill]) {
		for (int i = 0; i < MAX_CAST_ON_SKILL_USE; i += 3) {
			if (IsValidSpell(aabonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID]) && aabonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID] && skill == aabonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SKILL]) {
				if (zone->random.Int(1, 1000) <= aabonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_CHANCE]) {
					SpellFinished(aabonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID], on, EQ::spells::CastingSlot::Item, 0, -1, spells[aabonuses.SkillAttackProc[i + SBIndex::SKILLATK_PROC_SPELL_ID]].resist_difficulty);
				}
			}
		}
	}
}

bool Mob::TryRootFadeByDamage(int buffslot, Mob* attacker) {

	/*Dev Quote 2010: http://forums.station.sony.com/eq/posts/list.m?topic_id=161443
	The Viscid Roots AA does the following: Reduces the chance for root to break by X percent.
	There is no distinction of any kind between the caster inflicted damage, or anyone
	else's damage. There is also no distinction between Direct and DOT damage in the root code.

	General Mechanics
	- Check buffslot to make sure damage from a root does not cancel the root
	- If multiple roots on target, always and only checks first root slot and if broken only removes that slots root.
	- Only roots on determental spells can be broken by damage.
	- Root break chance values obtained from live parses.
	*/

	if (!attacker || !spellbonuses.Root[SBIndex::ROOT_EXISTS] || spellbonuses.Root[SBIndex::ROOT_BUFFSLOT] < 0) {
		return false;
	}

	if (IsDetrimentalSpell(buffs[spellbonuses.Root[SBIndex::ROOT_BUFFSLOT]].spellid) && spellbonuses.Root[SBIndex::ROOT_BUFFSLOT] != buffslot) {

		int BreakChance = RuleI(Spells, RootBreakFromSpells);
		BreakChance -= BreakChance * buffs[spellbonuses.Root[SBIndex::ROOT_BUFFSLOT]].RootBreakChance / 100;
		int level_diff = attacker->GetLevel() - GetLevel();

		//Use baseline if level difference <= 1 (ie. If target is (1) level less than you, or equal or greater level)

		if (level_diff == 2) {
			BreakChance = (BreakChance * 80) / 100; //Decrease by 20%;
		}
		else if (level_diff >= 3 && level_diff <= 20) {
			BreakChance = (BreakChance * 60) / 100; //Decrease by 40%;
		}
		else if (level_diff > 21) {
			BreakChance = (BreakChance * 20) / 100; //Decrease by 80%;
		}

		if (BreakChance < 1) {
			BreakChance = 1;
		}

		if (zone->random.Roll(BreakChance)) {

			if (!TryFadeEffect(spellbonuses.Root[SBIndex::ROOT_BUFFSLOT])) {
				BuffFadeBySlot(spellbonuses.Root[SBIndex::ROOT_BUFFSLOT]);
				LogCombat("Spell broke root! BreakChance percent chance");
				return true;
			}
		}
	}

	LogCombat("Spell did not break root. BreakChance percent chance");
	return false;
}

int32 Mob::RuneAbsorb(int64 damage, uint16 type)
{
	uint32 buff_max = GetMaxTotalSlots();
	if (type == SE_Rune) {
		for (uint32 slot = 0; slot < buff_max; slot++) {
			if (slot == spellbonuses.MeleeRune[SBIndex::RUNE_BUFFSLOT] && spellbonuses.MeleeRune[SBIndex::RUNE_AMOUNT] && buffs[slot].melee_rune && IsValidSpell(buffs[slot].spellid)) {
				int melee_rune_left = buffs[slot].melee_rune;

				if (melee_rune_left > damage)
				{
					melee_rune_left -= damage;
					buffs[slot].melee_rune = melee_rune_left;
					return -6;
				}

				else
				{
					if (melee_rune_left > 0)
						damage -= melee_rune_left;

					if (!TryFadeEffect(slot))
						BuffFadeBySlot(slot);
				}
			}
		}
	}

	else {
		for (uint32 slot = 0; slot < buff_max; slot++) {
			if (slot == spellbonuses.AbsorbMagicAtt[SBIndex::RUNE_BUFFSLOT] && spellbonuses.AbsorbMagicAtt[SBIndex::RUNE_AMOUNT] && buffs[slot].magic_rune && IsValidSpell(buffs[slot].spellid)) {
				int magic_rune_left = buffs[slot].magic_rune;
				if (magic_rune_left > damage)
				{
					magic_rune_left -= damage;
					buffs[slot].magic_rune = magic_rune_left;
					return 0;
				}

				else
				{
					if (magic_rune_left > 0)
						damage -= magic_rune_left;

					if (!TryFadeEffect(slot))
						BuffFadeBySlot(slot);
				}
			}
		}
	}
	return damage;
}

//SYNC WITH: tune.cpp, mob.h TuneCommonOutgoingHitSucces
void Mob::CommonOutgoingHitSuccess(Mob* defender, DamageHitInfo &hit, ExtraAttackOptions *opts)
{
	if (!defender)
		return;

#ifdef LUA_EQEMU
	bool ignoreDefault = false;
	LuaParser::Instance()->CommonOutgoingHitSuccess(this, defender, hit, opts, ignoreDefault);

	if (ignoreDefault) {
		return;
	}
#endif

	if (hit.damage_done < 1)
		hit.damage_done = 1;

	if (hit.skill == EQ::skills::SkillArchery) {
		int bonus = aabonuses.ArcheryDamageModifier + itembonuses.ArcheryDamageModifier + spellbonuses.ArcheryDamageModifier;
		hit.damage_done += hit.damage_done * bonus / 100;
		int headshot = TryHeadShot(defender, hit.skill);
		if (headshot > 0) {
			hit.damage_done = headshot;
		}
		else if (GetClass() == RANGER && GetLevel() > 50) { // no double dmg on headshot
			if ((defender->IsNPC() && !defender->IsMoving() && !defender->IsRooted()) || !RuleB(Combat, ArcheryBonusRequiresStationary)) {
				hit.damage_done *= 2;
				MessageString(Chat::MeleeCrit, BOW_DOUBLE_DAMAGE);
			}
		}
	}

	// this has some weird ordering
	// Seems the crit message is generated before some of them :P
	if (IsClient()) {
		int extra = 0;
		switch (hit.skill) {
		case EQ::skills::SkillThrowing:
		case EQ::skills::SkillArchery:
			extra = CastToClient()->GetHeroicDEX() / 10;
			break;
		default:
			extra = CastToClient()->GetHeroicSTR() / 10;
			break;
		}
		hit.damage_done += extra;
	}

	// this appears where they do special attack dmg mods
	int spec_mod = 0;
	if (IsSpecialAttack(eSpecialAttacks::Rampage)) {
		int mod = GetSpecialAbilityParam(SPECATK_RAMPAGE, 2);
		if (mod > 0)
			spec_mod = mod;
		if ((IsPet() || IsTempPet()) && IsPetOwnerClient()) {
			//SE_PC_Pet_Rampage SPA 464 on pet, damage modifier
			int spell_mod = spellbonuses.PC_Pet_Rampage[SBIndex::PET_RAMPAGE_DMG_MOD] + itembonuses.PC_Pet_Rampage[SBIndex::PET_RAMPAGE_DMG_MOD] + aabonuses.PC_Pet_Rampage[SBIndex::PET_RAMPAGE_DMG_MOD];
			if (spell_mod > spec_mod)
				spec_mod = spell_mod;
		}
	}
	else if (IsSpecialAttack(eSpecialAttacks::AERampage)) {
		int mod = GetSpecialAbilityParam(SPECATK_AREA_RAMPAGE, 2);
		if (mod > 0)
			spec_mod = mod;
		if ((IsPet() || IsTempPet()) && IsPetOwnerClient()) {
			//SE_PC_Pet_AE_Rampage SPA 465 on pet, damage modifier
			int spell_mod = spellbonuses.PC_Pet_AE_Rampage[SBIndex::PET_RAMPAGE_DMG_MOD] + itembonuses.PC_Pet_AE_Rampage[SBIndex::PET_RAMPAGE_DMG_MOD] + aabonuses.PC_Pet_AE_Rampage[SBIndex::PET_RAMPAGE_DMG_MOD];
			if (spell_mod > spec_mod)
				spec_mod = spell_mod;
		}
	}
	if (spec_mod > 0)
		hit.damage_done = (hit.damage_done * spec_mod) / 100;

	int pct_damage_reduction = defender->GetSkillDmgTaken(hit.skill, opts) + defender->GetPositionalDmgTaken(this);

	hit.damage_done += (hit.damage_done * pct_damage_reduction / 100) + defender->GetPositionalDmgTakenAmt(this);

	if (defender->GetShielderID()) {
		DoShieldDamageOnShielder(defender, hit.damage_done, hit.skill);
		hit.damage_done -= hit.damage_done * defender->GetShieldTargetMitigation() / 100; //Default shielded takes 50 pct damage
	}

	CheckNumHitsRemaining(NumHit::OutgoingHitSuccess);
}

void Mob::DoShieldDamageOnShielder(Mob *shield_target, int64 hit_damage_done, EQ::skills::SkillType skillInUse)
{
	if (!shield_target) {
		return;
	}

	Mob *shielder = entity_list.GetMob(shield_target->GetShielderID());
	if (!shielder) {
		shield_target->SetShielderID(0);
		shield_target->SetShieldTargetMitigation(0);
		return;
	}

	if (shield_target->CalculateDistance(shielder->GetX(), shielder->GetY(), shielder->GetZ()) > static_cast<float>(shielder->GetMaxShielderDistance())) {
		shielder->SetShieldTargetID(0);
		shielder->SetShielderMitigation(0);
		shielder->SetShielderMaxDistance(0);
		shielder->shield_timer.Disable();
		shield_target->SetShielderID(0);
		shield_target->SetShieldTargetMitigation(0);
		return; //Too far away, no message is given though.
	}

	int mitigation = shielder->GetShielderMitigation(); //Default shielder mitigates 25 pct of damage taken, this can be increased up to max 50 by equipping a shield item
	if (shielder->IsClient() && shielder->HasShieldEquiped()) {
		EQ::ItemInstance* inst = shielder->CastToClient()->GetInv().GetItem(EQ::invslot::slotSecondary);
		if (inst) {
			const EQ::ItemData* shield = inst->GetItem();
			if (shield && shield->ItemType == EQ::item::ItemTypeShield) {
				mitigation += shield->AC * 50 / 100; //1% increase per 2 AC
				mitigation = std::min(50, mitigation);//50 pct max mitigation bonus from /shield
			}
		}
	}

	hit_damage_done -= hit_damage_done * mitigation / 100;
	shielder->Damage(this, hit_damage_done, SPELL_UNKNOWN, skillInUse, true, -1, false, m_specialattacks);
	shielder->CheckNumHitsRemaining(NumHit::OutgoingHitSuccess);
}

void Mob::CommonBreakInvisibleFromCombat()
{
	//break invis when you attack
	BreakInvisibleSpells();
	CancelSneakHide();

	if (spellbonuses.NegateIfCombat) {
		BuffFadeByEffect(SE_NegateIfCombat);
	}

	hidden = false;
	improved_hidden = false;
}

/* Dev quotes:
* Old formula
*	 Final delay = (Original Delay / (haste mod *.01f)) + ((Hundred Hands / 100) * Original Delay)
* New formula
*	 Final delay = (Original Delay / (haste mod *.01f)) + ((Hundred Hands / 1000) * (Original Delay / (haste mod *.01f))
* Base Delay	  20			  25			  30			  37
* Haste		   2.25			2.25			2.25			2.25
* HHE (old)	  -17			 -17			 -17			 -17
* Final Delay	 5.488888889	 6.861111111	 8.233333333	 10.15444444
*
* Base Delay	  20			  25			  30			  37
* Haste		   2.25			2.25			2.25			2.25
* HHE (new)	  -383			-383			-383			-383
* Final Delay	 5.484444444	 6.855555556	 8.226666667	 10.14622222
*
* Difference	 -0.004444444   -0.005555556   -0.006666667   -0.008222222
*
* These times are in 10th of a second
*/

void Mob::SetAttackTimer()
{
	attack_timer.SetAtTrigger(4000, true);
}

void Client::SetAttackTimer()
{
	float haste_mod = GetHaste() * 0.01f;
	int primary_speed = 0;
	int secondary_speed = 0;

	//default value for attack timer in case they have
	//an invalid weapon equipped:
	attack_timer.SetAtTrigger(4000, true);

	Timer *TimerToUse = nullptr;

	for (int i = EQ::invslot::slotRange; i <= EQ::invslot::slotSecondary; i++) {
		//pick a timer
		if (i == EQ::invslot::slotPrimary)
			TimerToUse = &attack_timer;
		else if (i == EQ::invslot::slotRange)
			TimerToUse = &ranged_timer;
		else if (i == EQ::invslot::slotSecondary)
			TimerToUse = &attack_dw_timer;
		else	//invalid slot (hands will always hit this)
			continue;

		const EQ::ItemData *ItemToUse = nullptr;

		//find our item
		EQ::ItemInstance *ci = GetInv().GetItem(i);
		if (ci)
			ItemToUse = ci->GetItem();

		//special offhand stuff
		if (i == EQ::invslot::slotSecondary) {
			//if we cant dual wield, skip it
			if (!CanThisClassDualWield() || HasTwoHanderEquipped()) {
				attack_dw_timer.Disable();
				continue;
			}
		}

		//see if we have a valid weapon
		if (ItemToUse != nullptr) {
			//check type and damage/delay
			if (!ItemToUse->IsClassCommon()
				|| ItemToUse->Damage == 0
				|| ItemToUse->Delay == 0) {
				//no weapon
				ItemToUse = nullptr;
			}
			// Check to see if skill is valid
			else if ((ItemToUse->ItemType > EQ::item::ItemTypeLargeThrowing) &&
				(ItemToUse->ItemType != EQ::item::ItemTypeMartial) &&
				(ItemToUse->ItemType != EQ::item::ItemType2HPiercing)) {
				//no weapon
				ItemToUse = nullptr;
			}
		}

		int hhe = itembonuses.HundredHands + spellbonuses.HundredHands;
		int speed = 0;
		int delay = 3500;

		//if we have no weapon..
		if (ItemToUse == nullptr)
			delay = 100 * GetHandToHandDelay();
		else
			//we have a weapon, use its delay
			delay = 100 * ItemToUse->Delay;

		speed = delay / haste_mod;

		if (ItemToUse && ItemToUse->ItemType == EQ::item::ItemTypeBow) {
			// Live actually had a bug here where they would return the non-modified attack speed
			// rather than the cap ...
			speed = std::max(speed - GetQuiverHaste(speed), RuleI(Combat, QuiverHasteCap));
		}
		else {
			if (RuleB(Spells, Jun182014HundredHandsRevamp))
				speed = static_cast<int>(speed + ((hhe / 1000.0f) * speed));
			else
				speed = static_cast<int>(speed + ((hhe / 100.0f) * delay));
		}
		TimerToUse->SetAtTrigger(std::max(RuleI(Combat, MinHastedDelay), speed), true, true);

		if (i == EQ::invslot::slotPrimary) {
			primary_speed = speed;
		}
		else if (i == EQ::invslot::slotSecondary) {
			secondary_speed = speed;
		}
	}

	//To allow for duel wield animation to display correctly if both weapons have same delay
	if (primary_speed == secondary_speed) {
		SetDualWieldingSameDelayWeapons(1);
	}
	else {
		SetDualWieldingSameDelayWeapons(0);
	}
}

void NPC::SetAttackTimer()
{
	float haste_mod = GetHaste() * 0.01f;

	//default value for attack timer in case they have
	//an invalid weapon equipped:
	attack_timer.SetAtTrigger(4000, true);

	Timer *TimerToUse = nullptr;
	int hhe = itembonuses.HundredHands + spellbonuses.HundredHands;

	// Technically NPCs should do some logic for weapons, but the effect is minimal
	// What they do is take the lower of their set delay and the weapon's
	// ex. Mob's delay set to 20, weapon set to 19, delay 19
	// Mob's delay set to 20, weapon set to 21, delay 20
	int speed = 0;
	if (RuleB(Spells, Jun182014HundredHandsRevamp))
		speed = static_cast<int>((attack_delay / haste_mod) + ((hhe / 1000.0f) * (attack_delay / haste_mod)));
	else
		speed = static_cast<int>((attack_delay / haste_mod) + ((hhe / 100.0f) * attack_delay));

	for (int i = EQ::invslot::slotRange; i <= EQ::invslot::slotSecondary; i++) {
		//pick a timer
		if (i == EQ::invslot::slotPrimary)
			TimerToUse = &attack_timer;
		else if (i == EQ::invslot::slotRange)
			TimerToUse = &ranged_timer;
		else if (i == EQ::invslot::slotSecondary)
			TimerToUse = &attack_dw_timer;
		else	//invalid slot (hands will always hit this)
			continue;

		//special offhand stuff
		if (i == EQ::invslot::slotSecondary) {
			// SPECATK_QUAD is uncheesable
			if (!CanThisClassDualWield() || (HasTwoHanderEquipped() && !GetSpecialAbility(SPECATK_QUAD))) {
				attack_dw_timer.Disable();
				continue;
			}
		}

		TimerToUse->SetAtTrigger(std::max(RuleI(Combat, MinHastedDelay), speed), true, true);
	}
}

void Client::DoAttackRounds(Mob *target, int hand, bool IsFromSpell)
{
	if (!target)
		return;

	Attack(target, hand, false, false, IsFromSpell);

	bool candouble = CanThisClassDoubleAttack();
	// extra off hand non-sense, can only double with skill of 150 or above
	// or you have any amount of GiveDoubleAttack
	if (candouble && hand == EQ::invslot::slotSecondary)
		candouble =
		    GetSkill(EQ::skills::SkillDoubleAttack) > 149 ||
		    (aabonuses.GiveDoubleAttack + spellbonuses.GiveDoubleAttack + itembonuses.GiveDoubleAttack) > 0;

	if (candouble) {
		CheckIncreaseSkill(EQ::skills::SkillDoubleAttack, target, -10);
		if (CheckDoubleAttack()) {
			Attack(target, hand, false, false, IsFromSpell);

			if (hand == EQ::invslot::slotPrimary) {

				if (HasTwoHanderEquipped()) {
					auto extraattackchance = aabonuses.ExtraAttackChance[SBIndex::EXTRA_ATTACK_CHANCE] + spellbonuses.ExtraAttackChance[SBIndex::EXTRA_ATTACK_CHANCE] +
											 itembonuses.ExtraAttackChance[SBIndex::EXTRA_ATTACK_CHANCE];
					if (extraattackchance && zone->random.Roll(extraattackchance)) {
						auto extraattackamt = std::max({aabonuses.ExtraAttackChance[SBIndex::EXTRA_ATTACK_NUM_ATKS], spellbonuses.ExtraAttackChance[SBIndex::EXTRA_ATTACK_NUM_ATKS], itembonuses.ExtraAttackChance[SBIndex::EXTRA_ATTACK_NUM_ATKS] });
						for (int i = 0; i < extraattackamt; i++) {
							Attack(target, hand, false, false, IsFromSpell);
						}
					}
				}
				else {
					auto extraattackchance_primary = aabonuses.ExtraAttackChancePrimary[SBIndex::EXTRA_ATTACK_CHANCE] + spellbonuses.ExtraAttackChancePrimary[SBIndex::EXTRA_ATTACK_CHANCE] +
													 itembonuses.ExtraAttackChancePrimary[SBIndex::EXTRA_ATTACK_CHANCE];
					if (extraattackchance_primary && zone->random.Roll(extraattackchance_primary)) {
						auto extraattackamt_primary = std::max({aabonuses.ExtraAttackChancePrimary[SBIndex::EXTRA_ATTACK_NUM_ATKS], spellbonuses.ExtraAttackChancePrimary[SBIndex::EXTRA_ATTACK_NUM_ATKS], itembonuses.ExtraAttackChancePrimary[SBIndex::EXTRA_ATTACK_NUM_ATKS] });
						for (int i = 0; i < extraattackamt_primary; i++) {
							Attack(target, hand, false, false, IsFromSpell);
						}
					}
				}
			}

			if (hand == EQ::invslot::slotSecondary) {
				auto extraattackchance_secondary = aabonuses.ExtraAttackChanceSecondary[SBIndex::EXTRA_ATTACK_CHANCE] + spellbonuses.ExtraAttackChanceSecondary[SBIndex::EXTRA_ATTACK_CHANCE] +
												   itembonuses.ExtraAttackChanceSecondary[SBIndex::EXTRA_ATTACK_CHANCE];
				if (extraattackchance_secondary && zone->random.Roll(extraattackchance_secondary)) {
					auto extraattackamt_secondary = std::max({aabonuses.ExtraAttackChanceSecondary[SBIndex::EXTRA_ATTACK_NUM_ATKS], spellbonuses.ExtraAttackChanceSecondary[SBIndex::EXTRA_ATTACK_NUM_ATKS], itembonuses.ExtraAttackChanceSecondary[SBIndex::EXTRA_ATTACK_NUM_ATKS] });
					for (int i = 0; i < extraattackamt_secondary; i++) {
						Attack(target, hand, false, false, IsFromSpell);
					}
				}
			}

			// you can only triple from the main hand
			if (hand == EQ::invslot::slotPrimary && CanThisClassTripleAttack()) {
				CheckIncreaseSkill(EQ::skills::SkillTripleAttack, target, -10);
				if (CheckTripleAttack()) {
					Attack(target, hand, false, false, IsFromSpell);
					auto flurrychance = aabonuses.FlurryChance + spellbonuses.FlurryChance +
							    itembonuses.FlurryChance;
					if (flurrychance && zone->random.Roll(flurrychance)) {
						Attack(target, hand, false, false, IsFromSpell);
						if (zone->random.Roll(flurrychance))
							Attack(target, hand, false, false, IsFromSpell);
						MessageString(Chat::NPCFlurry, YOU_FLURRY);
					}
				}
			}
		}
	}
}

bool Mob::CheckDualWield()
{
	// Pets /might/ follow a slightly different progression
	// although it could all be from pets having different skills than most mobs
	int chance = GetSkill(EQ::skills::SkillDualWield);
	if (GetLevel() > 35)
		chance += GetLevel();

	chance += aabonuses.Ambidexterity + spellbonuses.Ambidexterity + itembonuses.Ambidexterity;
	int per_inc = spellbonuses.DualWieldChance + aabonuses.DualWieldChance + itembonuses.DualWieldChance;
	if (per_inc)
		chance += chance * per_inc / 100;

	return zone->random.Int(1, 375) <= chance;
}

bool Client::CheckDualWield()
{
	int chance = GetSkill(EQ::skills::SkillDualWield) + GetLevel();

	chance += aabonuses.Ambidexterity + spellbonuses.Ambidexterity + itembonuses.Ambidexterity;
	int per_inc = spellbonuses.DualWieldChance + aabonuses.DualWieldChance + itembonuses.DualWieldChance;
	if (per_inc)
		chance += chance * per_inc / 100;

	return zone->random.Int(1, 375) <= chance;
}

void Mob::DoMainHandAttackRounds(Mob *target, ExtraAttackOptions *opts)
{
	if (!target)
		return;

	if (RuleB(Combat, UseLiveCombatRounds)) {
		// A "quad" on live really is just a successful dual wield where both double attack
		// The mobs that could triple lost the ability to when the triple attack skill was added in
		Attack(target, EQ::invslot::slotPrimary, false, false, false, opts);
		if (CanThisClassDoubleAttack() && CheckDoubleAttack()) {
			Attack(target, EQ::invslot::slotPrimary, false, false, false, opts);
			if ((IsPet() || IsTempPet()) && IsPetOwnerClient()) {
				int chance = spellbonuses.PC_Pet_Flurry + itembonuses.PC_Pet_Flurry + aabonuses.PC_Pet_Flurry;
				if (chance && zone->random.Roll(chance))
					Flurry(nullptr);
			}
		}
		return;
	}

	if (IsNPC()) {
		int16 n_atk = CastToNPC()->GetNumberOfAttacks();
		if (n_atk <= 1) {
			Attack(target, EQ::invslot::slotPrimary, false, false, false, opts);
		}
		else {
			for (int i = 0; i < n_atk; ++i) {
				Attack(target, EQ::invslot::slotPrimary, false, false, false, opts);
			}
		}
	}
	else {
		Attack(target, EQ::invslot::slotPrimary, false, false, false, opts);
	}

	// we use this random value in three comparisons with different
	// thresholds, and if its truely random, then this should work
	// out reasonably and will save us compute resources.
	int32 RandRoll = zone->random.Int(0, 99);
	if ((CanThisClassDoubleAttack() || GetSpecialAbility(SPECATK_TRIPLE) || GetSpecialAbility(SPECATK_QUAD))
		// check double attack, this is NOT the same rules that clients use...
		&&
		RandRoll < (GetLevel() + NPCDualAttackModifier)) {
		Attack(target, EQ::invslot::slotPrimary, false, false, false, opts);
		// lets see if we can do a triple attack with the main hand
		// pets are excluded from triple and quads...
		if ((GetSpecialAbility(SPECATK_TRIPLE) || GetSpecialAbility(SPECATK_QUAD)) && !IsPet() &&
			RandRoll < (GetLevel() + NPCTripleAttackModifier)) {
			Attack(target, EQ::invslot::slotPrimary, false, false, false, opts);
			// now lets check the quad attack
			if (GetSpecialAbility(SPECATK_QUAD) && RandRoll < (GetLevel() + NPCQuadAttackModifier)) {
				Attack(target, EQ::invslot::slotPrimary, false, false, false, opts);
			}
		}
	}
}

void Mob::DoOffHandAttackRounds(Mob *target, ExtraAttackOptions *opts)
{
	if (!target)
		return;
	// Mobs will only dual wield w/ the flag or have a secondary weapon
	// For now, SPECATK_QUAD means innate DW when Combat:UseLiveCombatRounds is true
	if ((GetSpecialAbility(SPECATK_INNATE_DW) ||
		(RuleB(Combat, UseLiveCombatRounds) && GetSpecialAbility(SPECATK_QUAD))) ||
		GetEquippedItemFromTextureSlot(EQ::textures::weaponSecondary) != 0) {
		if (CheckDualWield()) {
			Attack(target, EQ::invslot::slotSecondary, false, false, false, opts);
			if (CanThisClassDoubleAttack() && GetLevel() > 35 && CheckDoubleAttack()) {
				Attack(target, EQ::invslot::slotSecondary, false, false, false, opts);

				if ((IsPet() || IsTempPet()) && IsPetOwnerClient()) {
					int chance = spellbonuses.PC_Pet_Flurry + itembonuses.PC_Pet_Flurry + aabonuses.PC_Pet_Flurry;
					if (chance && zone->random.Roll(chance))
						Flurry(nullptr);
				}
			}
		}
	}
}


int Mob::GetPetAvoidanceBonusFromOwner()
{
	Mob *owner = nullptr;
	if (IsPet())
		owner = GetOwner();
	else if (IsNPC() && CastToNPC()->GetSwarmOwner())
		owner = entity_list.GetMobID(CastToNPC()->GetSwarmOwner());

	if (owner)
		return owner->aabonuses.PetAvoidance + owner->spellbonuses.PetAvoidance + owner->itembonuses.PetAvoidance;

	return 0;
}
int Mob::GetPetACBonusFromOwner()
{
	Mob *owner = nullptr;
	if (IsPet())
		owner = GetOwner();
	else if (IsNPC() && CastToNPC()->GetSwarmOwner())
		owner = entity_list.GetMobID(CastToNPC()->GetSwarmOwner());

	if (owner)
		return owner->aabonuses.PetMeleeMitigation + owner->spellbonuses.PetMeleeMitigation + owner->itembonuses.PetMeleeMitigation;

	return 0;
}
int Mob::GetPetATKBonusFromOwner()
{
	Mob *owner = nullptr;
	if (IsPet())
		owner = GetOwner();
	else if (IsNPC() && CastToNPC()->GetSwarmOwner())
		owner = entity_list.GetMobID(CastToNPC()->GetSwarmOwner());

	if (owner)
		return owner->aabonuses.Pet_Add_Atk + owner->spellbonuses.Pet_Add_Atk + owner->itembonuses.Pet_Add_Atk;

	return 0;
}


bool Mob::GetWasSpawnedInWater() const {
	return spawned_in_water;
}

void Mob::SetSpawnedInWater(bool spawned_in_water) {
	Mob::spawned_in_water = spawned_in_water;
}

int64 Mob::GetHPRegen() const
{
	return hp_regen;
}

int64 Mob::GetManaRegen() const
{
	return mana_regen;
}
