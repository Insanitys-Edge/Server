/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2016 EQEMu Development Team (http://eqemulator.net)

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
#include "../common/eqemu_logsys.h"

#include "../common/rulesys.h"
#include "../common/spdat.h"

#include "../common/data_verification.h"

#include "client.h"
#include "mob.h"

#include <algorithm>


int32 Client::GetMaxStat() const
{
	if ((RuleI(Character, StatCap)) > 0) {
		return (RuleI(Character, StatCap));
	}
	int level = GetLevel();
	int32 base = 0;
	if (level < 61) {
		base = 255;
	}
	else if (ClientVersion() >= EQ::versions::ClientVersion::SoF) {
		base = 255 + 5 * (level - 60);
	}
	else if (level < 71) {
		base = 255 + 5 * (level - 60);
	}
	else {
		base = 330;
	}
	return (base);
}

int32 Client::GetMaxResist() const
{
	int level = GetLevel();
	int32 base = 500;
	if (level > 65) {
		base += ((level - 65) * 5);
	}
	return base;
}

int32 Client::GetMaxSTR() const
{
	return GetMaxStat()
	       + itembonuses.STRCapMod
	       + spellbonuses.STRCapMod
	       + aabonuses.STRCapMod;
}
int32 Client::GetMaxSTA() const
{
	return GetMaxStat()
	       + itembonuses.STACapMod
	       + spellbonuses.STACapMod
	       + aabonuses.STACapMod;
}
int32 Client::GetMaxDEX() const
{
	return GetMaxStat()
	       + itembonuses.DEXCapMod
	       + spellbonuses.DEXCapMod
	       + aabonuses.DEXCapMod;
}
int32 Client::GetMaxAGI() const
{
	return GetMaxStat()
	       + itembonuses.AGICapMod
	       + spellbonuses.AGICapMod
	       + aabonuses.AGICapMod;
}
int32 Client::GetMaxINT() const
{
	return GetMaxStat()
	       + itembonuses.INTCapMod
	       + spellbonuses.INTCapMod
	       + aabonuses.INTCapMod;
}
int32 Client::GetMaxWIS() const
{
	return GetMaxStat()
	       + itembonuses.WISCapMod
	       + spellbonuses.WISCapMod
	       + aabonuses.WISCapMod;
}
int32 Client::GetMaxCHA() const
{
	return GetMaxStat()
	       + itembonuses.CHACapMod
	       + spellbonuses.CHACapMod
	       + aabonuses.CHACapMod;
}
int32 Client::GetMaxMR() const
{
	return GetMaxResist()
	       + itembonuses.MRCapMod
	       + spellbonuses.MRCapMod
	       + aabonuses.MRCapMod;
}
int32 Client::GetMaxPR() const
{
	return GetMaxResist()
	       + itembonuses.PRCapMod
	       + spellbonuses.PRCapMod
	       + aabonuses.PRCapMod;
}
int32 Client::GetMaxDR() const
{
	return GetMaxResist()
	       + itembonuses.DRCapMod
	       + spellbonuses.DRCapMod
	       + aabonuses.DRCapMod;
}
int32 Client::GetMaxCR() const
{
	return GetMaxResist()
	       + itembonuses.CRCapMod
	       + spellbonuses.CRCapMod
	       + aabonuses.CRCapMod;
}
int32 Client::GetMaxFR() const
{
	return GetMaxResist()
	       + itembonuses.FRCapMod
	       + spellbonuses.FRCapMod
	       + aabonuses.FRCapMod;
}
int32 Client::GetMaxCorrup() const
{
	return GetMaxResist()
	       + itembonuses.CorrupCapMod
	       + spellbonuses.CorrupCapMod
	       + aabonuses.CorrupCapMod;
}

int64 Client::LevelRegen(int level, bool is_sitting, bool is_resting, bool is_feigned, bool is_famished, bool has_racial_regen_bonus, bool has_frog_racial_regen_bonus)
{
	// base regen is 1
	int hp_regen_amount = 1;

	// sitting adds 1
	if (is_sitting)
	{
		hp_regen_amount += 1;
	}

	// feigning at 51+ adds 1 as if sitting
	if (level > 50 && is_feigned)
	{
		hp_regen_amount += 1;
	}

	// being either hungry or thirsty negates the passive regen from standing/sitting/feigning but can still benefit from level bonuses and resting
	if (is_famished)	// the client checks to see if either food or water is 0 in pp, not for the advanced sickness that happens later
	{
		hp_regen_amount = 0;
	}

	// additional point of regen is gained at levels 51, 56, 60, 61, 63 and 65
	if (level >= 51)
	{
		hp_regen_amount += 1;
	}
	if (level >= 56)
	{
		hp_regen_amount += 1;
	}
	if (level >= 60)
	{
		hp_regen_amount += 1;
	}
	if (level >= 61)
	{
		hp_regen_amount += 1;
	}
	if (level >= 63)
	{
		hp_regen_amount += 1;
	}
	if (level >= 65)
	{
		hp_regen_amount += 1;
	}

	// resting begins after sitting for 1 minute.
	// 1 additional point of regen is gained at levels 20 and 50
	if (is_sitting && is_resting)
	{
		if (level >= 20)
		{
			hp_regen_amount += 1;
		}
		if (level >= 50)
		{
			hp_regen_amount += 1;
		}
	}

	// racial trait adds to then doubles regen bonuses
	if (has_racial_regen_bonus)
	{
		if (level >= 51)
		{
			hp_regen_amount += 1;
		}
		if (level >= 56)
		{
			hp_regen_amount += 1;
		}

		hp_regen_amount *= std::ceil(has_frog_racial_regen_bonus ? 1.5 : 2.0);
	}

	if (IsBerserk())
	{
		if (GetHPRatio() < 15.0f)
			hp_regen_amount *= 4.0;
		else if (GetHPRatio() < 20.0f)
			hp_regen_amount *= 3.5;
		else if (GetHPRatio() < 25.0f)
			hp_regen_amount *= 3.0;
		else if (GetHPRatio() < 30.0f)
			hp_regen_amount *= 2.5;
		else if (GetHPRatio() < 35.0f)
			hp_regen_amount *= 2.0;
		else if (GetHPRatio() < 45.0f)
			hp_regen_amount *= 1.5;
	}

	return hp_regen_amount;
}

int64 Client::CalcHPRegen(bool bCombat)
{
	bool is_sitting = true;
	bool has_racial_regen_bonus = false;
	bool has_frog_racial_regen_bonus = false;
	bool is_feigned = GetClass() == MONK && GetFeigned();	// only monks get this bonus

	if (IsClient())
	{
		EQ::ItemInstance* inst = CastToClient()->GetInv().GetItem(EQ::invslot::slotCharm);
		if (inst && inst->GetID() == RaceCharmIDs::CharmTroll ||
			inst && inst->GetID() == RaceCharmIDs::CharmIksar)
		{
			has_racial_regen_bonus = true;
		}

		if (inst && inst->GetID() == RaceCharmIDs::CharmFroglok)
		{
			has_racial_regen_bonus = true;
			has_frog_racial_regen_bonus = true;
		}
	}

	// naked regen
	int32 hp_regen_amount = LevelRegen(GetLevel(), is_sitting, true, is_feigned, IsStarved(), has_racial_regen_bonus, has_frog_racial_regen_bonus);

	// add AA regen - this is here because of the check below needing to negate it so we can bleed out in sync with the client
	hp_regen_amount += aabonuses.HPRegen;

	// we're almost dead, our regeneration won't save us now but a heal could
	if (GetHP() <= 0)
	{
		if (hp_regen_amount <= 0 || GetHP() < -5)
		{
			// bleed to death slowly
			hp_regen_amount = -1;
		}
	}

	// add spell and item regen
	hp_regen_amount += itembonuses.HPRegen + spellbonuses.HPRegen;

	// special case, if we're unconscious and our hp isn't changing, make it -1 so the character doesn't end up stuck in that state
	// this only applies if the character ends up with between -5 and 0 hp, then once they reach -6 they will hit the normal bleeding logic
	if (GetHP() <= 0 && hp_regen_amount == 0)
	{
		hp_regen_amount = -1;
	}

	if (GetBaseRace() == DRAKKIN)
	{
		return -3;
	}

	return hp_regen_amount;
}

int64 Client::CalcHPRegenCap()
{
	int64 cap = RuleI(Character, ItemHealthRegenCap);
	if (GetLevel() > 60) {
		cap = std::max(cap, static_cast<int64>(GetLevel() - 30)); // if the rule is set greater than normal I guess
	}

	if (GetLevel() > 65) {
		cap += GetLevel() - 65;
	}

	cap += aabonuses.ItemHPRegenCap + spellbonuses.ItemHPRegenCap + itembonuses.ItemHPRegenCap;

	return (cap * RuleI(Character, HPRegenMultiplier) / 100);
}

int64 Client::CalcMaxHP()
{
	float nd = 10000;
	max_hp = (CalcBaseHP() + itembonuses.HP);
	//The AA desc clearly says it only applies to base hp..
	//but the actual effect sent on live causes the client
	//to apply it to (basehp + itemhp).. I will oblige to the client's whims over
	//the aa description
	nd += aabonuses.MaxHP;	//Natural Durability, Physical Enhancement, Planar Durability
	max_hp = (float)max_hp * (float)nd / (float)10000; //this is to fix the HP-above-495k issue
	max_hp += spellbonuses.HP + aabonuses.HP;
	max_hp += GroupLeadershipAAHealthEnhancement();
	max_hp += max_hp * ((spellbonuses.MaxHPChange + itembonuses.MaxHPChange) / 10000.0f);
	if (current_hp > max_hp) {
		current_hp = max_hp;
	}
	int64 hp_perc_cap = spellbonuses.HPPercCap[SBIndex::RESOURCE_PERCENT_CAP];
	if (hp_perc_cap) {
		int64 curHP_cap = (max_hp * hp_perc_cap) / 100;
		if (current_hp > curHP_cap || (spellbonuses.HPPercCap[SBIndex::RESOURCE_AMOUNT_CAP] && current_hp > spellbonuses.HPPercCap[SBIndex::RESOURCE_AMOUNT_CAP])) {
			current_hp = curHP_cap;
		}
	}

	return max_hp;
}

uint32 Mob::GetClassLevelFactor()
{
	uint32 multiplier = 0;
	uint8  mlevel     = GetLevel();
	switch (GetClass()) {
		case WARRIOR: {
			if (mlevel < 20) {
				multiplier = 220;
			}
			else if (mlevel < 30) {
				multiplier = 230;
			}
			else if (mlevel < 40) {
				multiplier = 250;
			}
			else if (mlevel < 53) {
				multiplier = 270;
			}
			else if (mlevel < 57) {
				multiplier = 280;
			}
			else if (mlevel < 60) {
				multiplier = 290;
			}
			else if (mlevel < 70) {
				multiplier = 300;
			}
			else {
				multiplier = 311;
			}
			break;
		}
		case DRUID:
		case CLERIC:
		case SHAMAN: {
			if (mlevel < 70) {
				multiplier = 150;
			}
			else {
				multiplier = 157;
			}
			break;
		}
		case BERSERKER:
		case PALADIN:
		case SHADOWKNIGHT: {
			if (mlevel < 35) {
				multiplier = 210;
			}
			else if (mlevel < 45) {
				multiplier = 220;
			}
			else if (mlevel < 51) {
				multiplier = 230;
			}
			else if (mlevel < 56) {
				multiplier = 240;
			}
			else if (mlevel < 60) {
				multiplier = 250;
			}
			else if (mlevel < 68) {
				multiplier = 260;
			}
			else {
				multiplier = 270;
			}
			break;
		}
		case MONK:
		case BARD:
		case ROGUE:
		case BEASTLORD: {
			if (mlevel < 51) {
				multiplier = 180;
			}
			else if (mlevel < 58) {
				multiplier = 190;
			}
			else if (mlevel < 70) {
				multiplier = 200;
			}
			else {
				multiplier = 210;
			}
			break;
		}
		case RANGER: {
			if (mlevel < 58) {
				multiplier = 200;
			}
			else if (mlevel < 70) {
				multiplier = 210;
			}
			else {
				multiplier = 220;
			}
			break;
		}
		case MAGICIAN:
		case WIZARD:
		case NECROMANCER:
		case ENCHANTER: {
			if (mlevel < 70) {
				multiplier = 120;
			}
			else {
				multiplier = 127;
			}
			break;
		}
		default: {
			if (mlevel < 35) {
				multiplier = 210;
			}
			else if (mlevel < 45) {
				multiplier = 220;
			}
			else if (mlevel < 51) {
				multiplier = 230;
			}
			else if (mlevel < 56) {
				multiplier = 240;
			}
			else if (mlevel < 60) {
				multiplier = 250;
			}
			else {
				multiplier = 260;
			}
			break;
		}
	}

	return multiplier;
}

int64 Client::CalcBaseHP()
{

	if (ClientVersion() >= EQ::versions::ClientVersion::SoF && RuleB(Character, SoDClientUseSoDHPManaEnd)) {
		int stats = GetSTA();
		if (stats > 255) {
			stats = (stats - 255) / 2;
			stats += 255;
		}
		base_hp = 5;
		auto base_data = database.GetBaseData(GetLevel(), GetClass());
		if (base_data) {
			base_hp += base_data->base_hp + (base_data->hp_factor * stats);
			base_hp += ((uint64)GetHeroicSTA() * 10.0);
		}
	}
	else {
		uint64 Post255;
		uint64 lm = GetClassLevelFactor();
		if ((GetSTA() - 255) / 2 > 0) {
			Post255 = (GetSTA() - 255) / 2;
		}
		else {
			Post255 = 0;
		}
		base_hp = (5) + (GetLevel() * lm / 10) + (((GetSTA() - Post255) * (double)GetLevel() * lm / 3000)) + ((Post255 * (double)GetLevel()) * lm / 6000);
	}
	EQ::ItemInstance* inst = CastToClient()->GetInv().GetItem(EQ::invslot::slotCharm);
	if (inst)
	{
		if (inst->GetID() == RaceCharmIDs::CharmDwarf || inst->GetID() == CharmWoodElf || inst->GetID() == CharmOgre || inst->GetID() == CharmTroll)
		{
			base_hp *= 1.05;
		}
		if (inst->GetID() == RaceCharmIDs::CharmBarbarian)
		{
			base_hp *= 1.10;
		}
		if (inst->GetID() == RaceCharmIDs::CharmHighElf)
		{
			base_hp *= 0.80;
		}
	}

	return base_hp;
}

// This is for calculating Base HPs + STA bonus for SoD or later clients.
uint64 Client::GetClassHPFactor()
{
	int factor;
	// Note: Base HP factor under level 41 is equal to factor / 12, and from level 41 to 80 is factor / 6.
	// Base HP over level 80 is factor / 10
	// HP per STA point per level is factor / 30 for level 80+
	// HP per STA under level 40 is the level 80 HP Per STA / 120, and for over 40 it is / 60.
	switch (GetClass()) {
		case DRUID:
		case ENCHANTER:
		case NECROMANCER:
		case MAGICIAN:
		case WIZARD:
			factor = 240;
			break;
		case BEASTLORD:
		case BERSERKER:
		case MONK:
		case ROGUE:
		case SHAMAN:
			factor = 255;
			break;
		case BARD:
		case CLERIC:
			factor = 264;
			break;
		case SHADOWKNIGHT:
		case PALADIN:
			factor = 288;
			break;
		case RANGER:
			factor = 276;
			break;
		case WARRIOR:
			factor = 300;
			break;
		default:
			factor = 240;
			break;
	}
	return factor;
}

// This should return the combined AC of all the items the player is wearing.
int32 Client::GetRawItemAC()
{
	int32 Total = 0;
	// this skips MainAmmo..add an '=' conditional if that slot is required (original behavior)
	for (int16 slot_id = EQ::invslot::BONUS_BEGIN; slot_id <= EQ::invslot::BONUS_STAT_END; slot_id++) {
		const EQ::ItemInstance* inst = m_inv[slot_id];
		if (inst && inst->IsClassCommon()) {
			Total += inst->GetItem()->AC;
		}
	}
	return Total;
}

int64 Client::CalcMaxMana()
{
	switch (GetCasterClass()) {
		case 'I':
		case 'W': {
				max_mana = (CalcBaseMana() + itembonuses.Mana + spellbonuses.Mana + aabonuses.Mana + GroupLeadershipAAManaEnhancement());
				break;
			}
		case 'N': {
				max_mana = 0;
				break;
			}
		default: {
				LogSpells("Invalid Class [{}] in CalcMaxMana", GetCasterClass());
				max_mana = 0;
				break;
			}
	}
	if (max_mana < 0) {
		max_mana = 0;
	}
	if (current_mana > max_mana) {
		current_mana = max_mana;
	}
	int mana_perc_cap = spellbonuses.ManaPercCap[SBIndex::RESOURCE_PERCENT_CAP];
	if (mana_perc_cap) {
		int curMana_cap = (max_mana * mana_perc_cap) / 100;
		if (current_mana > curMana_cap || (spellbonuses.ManaPercCap[SBIndex::RESOURCE_AMOUNT_CAP] && current_mana > spellbonuses.ManaPercCap[SBIndex::RESOURCE_AMOUNT_CAP])) {
			current_mana = curMana_cap;
		}
	}
	LogSpells("Client::CalcMaxMana() called for [{}] - returning [{}]", GetName(), max_mana);
	return max_mana;
}

int64 Client::CalcBaseMana()
{
	int ConvertedWisInt = 0;
	int MindLesserFactor, MindFactor;
	int WisInt = 0;
	int64 base_mana = 0;
	int wisint_mana = 0;
	int64 max_m = 0;
	switch (GetCasterClass()) {
		case 'I':
			WisInt = GetINT();
			if (ClientVersion() >= EQ::versions::ClientVersion::SoF && RuleB(Character, SoDClientUseSoDHPManaEnd)) {
				ConvertedWisInt = WisInt;
				int over200 = WisInt;
				if (WisInt > 100) {
					if (WisInt > 200) {
						over200 = (WisInt - 200) / -2 + WisInt;
					}
					ConvertedWisInt = (3 * over200 - 300) / 2 + over200;
				}
				auto base_data = database.GetBaseData(GetLevel(), GetClass());
				if (base_data) {
					max_m = base_data->base_mana + (ConvertedWisInt * base_data->mana_factor) + (GetHeroicINT() * 10);
				}
			}
			else {
				if ((( WisInt - 199 ) / 2) > 0) {
					MindLesserFactor = ( WisInt - 199 ) / 2;
				}
				else {
					MindLesserFactor = 0;
				}
				MindFactor = WisInt - MindLesserFactor;
				if (WisInt > 100) {
					max_m = (((5 * (MindFactor + 20)) / 2) * 3 * GetLevel() / 40);
				}
				else {
					max_m = (((5 * (MindFactor + 200)) / 2) * 3 * GetLevel() / 100);
				}
			}
			break;
		case 'W':
			WisInt = GetWIS();
			if (ClientVersion() >= EQ::versions::ClientVersion::SoF && RuleB(Character, SoDClientUseSoDHPManaEnd)) {
				ConvertedWisInt = WisInt;
				int over200 = WisInt;
				if (WisInt > 100) {
					if (WisInt > 200) {
						over200 = (WisInt - 200) / -2 + WisInt;
					}
					ConvertedWisInt = (3 * over200 - 300) / 2 + over200;
				}
				auto base_data = database.GetBaseData(GetLevel(), GetClass());
				if (base_data) {
					max_m = base_data->base_mana + (ConvertedWisInt * base_data->mana_factor) + (GetHeroicWIS() * 10);
				}
			}
			else {
				if ((( WisInt - 199 ) / 2) > 0) {
					MindLesserFactor = ( WisInt - 199 ) / 2;
				}
				else {
					MindLesserFactor = 0;
				}
				MindFactor = WisInt - MindLesserFactor;
				if (WisInt > 100) {
					max_m = (((5 * (MindFactor + 20)) / 2) * 3 * GetLevel() / 40);
				}
				else {
					max_m = (((5 * (MindFactor + 200)) / 2) * 3 * GetLevel() / 100);
				}
			}
			break;
		case 'N': {
				max_m = 0;
				break;
			}
		default: {
				LogDebug("Invalid Class [{}] in CalcMaxMana", GetCasterClass());
				max_m = 0;
				break;
			}
	}

	EQ::ItemInstance* inst = CastToClient()->GetInv().GetItem(EQ::invslot::slotCharm);
	if (inst)
	{
		switch (inst->GetID())
		{
		case RaceCharmIDs::CharmErudite:
			max_m *= 2;
			break;
		case RaceCharmIDs::CharmWoodElf:
			max_m *= 1.05;
			break;
		}
	}

	#if EQDEBUG >= 11
	LogDebug("Client::CalcBaseMana() called for [{}] - returning [{}]", GetName(), max_m);
	#endif
	return max_m;
}

int64 Client::CalcBaseManaRegen()
{
	uint8 clevel = GetLevel();
	int32 regen = 0;
	/*if (IsSitting() || (GetHorseId() != 0)) {*/
		if (HasSkill(EQ::skills::SkillMeditate)) {
			regen = (((GetSkill(EQ::skills::SkillMeditate) / 10) + (clevel - (clevel / 4))) / 4) + 4;
		}
		else {
			regen = 2;
		}
	//}
	//else {
	//	regen = 2;
	//}
	return regen;
}

int64 Client::CalcManaRegen(bool bCombat)
{
	int regen = 0;
	auto level = GetLevel();
	// so the new formulas break down with older skill caps where you don't have the skill until 4 or 8
	// so for servers that want to use the old skill progression they can set this rule so they
	// will get at least 1 for standing and 2 for sitting.
	bool old = RuleB(Character, OldMinMana);
	if (!IsStarved()) {
		// client does some base regen for shrouds here
		//if (IsSitting() || CanMedOnHorse()) {
			// kind of weird to do it here w/e
			// client does some base medding regen for shrouds here
			//if (GetClass() != BARD) {
			//	auto skill = GetSkill(EQ::skills::SkillMeditate);
			//	if (skill > 0) {
			//		regen++;
			//		if (skill > 1)
			//			regen++;
			//		if (skill >= 15)
			//			regen += skill / 15;
			//	}
			//}
		//	if (old)
		//		regen = std::max(regen, 2);
		///*}*/ else if (old) {
		//	regen = std::max(regen, 1);
		/*}*/
		regen = CalcBaseManaRegen();
	}

	if (level > 61) {
		regen++;
		if (level > 63)
			regen++;
	}

	EQ::ItemInstance* inst = CastToClient()->GetInv().GetItem(EQ::invslot::slotCharm);
	if (inst)
	{
		switch (inst->GetID())
		{
		case RaceCharmIDs::CharmHighElf:
			regen += 6;
			break;
		case RaceCharmIDs::CharmErudite:
			regen = std::ceil(regen / 2.0f);
			break;
		}
	}

	regen += aabonuses.ManaRegen;
	// add in + 1 bonus for SE_CompleteHeal, but we don't do anything for it yet?

	int item_bonus = itembonuses.ManaRegen; // this is capped already
	int heroic_bonus = 0;

	switch (GetCasterClass()) {
	case 'W':
		heroic_bonus = GetHeroicWIS();
		break;
	default:
		heroic_bonus = GetHeroicINT();
		break;
	}

	item_bonus += heroic_bonus / 25;
	regen += item_bonus;

	if (level <= 70 && regen > 65)
		regen = 65;

	regen = regen * 100.0f * AreaManaRegen * 0.01f + 0.5f;

	if (!bCombat && CanFastRegen() && (IsSitting() || CanMedOnHorse())) {
		auto max_mana = GetMaxMana();
		int fast_regen = 6 * (max_mana / zone->newzone_data.FastRegenMana);
		if (regen < fast_regen) // weird, but what the client is doing
			regen = fast_regen;
	}

	regen += spellbonuses.ManaRegen; // TODO: live does this in buff tick
	return (regen * RuleI(Character, ManaRegenMultiplier) / 100);
}

int64 Client::CalcManaRegenCap()
{
	int64 cap = RuleI(Character, ItemManaRegenCap) + aabonuses.ItemManaRegenCap + itembonuses.ItemManaRegenCap + spellbonuses.ItemManaRegenCap;
	return (cap * RuleI(Character, ManaRegenMultiplier) / 100);
}

uint32 Client::CalcCurrentWeight()
{
	const EQ::ItemData* TempItem = nullptr;
	EQ::ItemInstance* ins = nullptr;
	uint32 Total = 0;
	int x;
	for (x = EQ::invslot::POSSESSIONS_BEGIN; x <= EQ::invslot::POSSESSIONS_END; x++) {
		TempItem = 0;
		ins = GetInv().GetItem(x);
		if (ins) {
			TempItem = ins->GetItem();
		}
		if (TempItem) {
			Total += TempItem->Weight;
		}
	}
	//TODO: coin weight reduction (from purses, etc), since client already calculates it
	/*  From the Wiki http://www.eqemulator.net/wiki/wikka.php?wakka=EQEmuDBSchemaitems under bagwr (thanks Trevius):
	    Interestingly, you can also have bags that reduce coin weight. However, in order to set bags to reduce coin weight, you MUST set the Item ID somewhere between 17201 and 17230. This is hard coded into the client.
	    The client is set to have certain coin weight reduction on a per Item ID basis within this range. The best way to create an new item to reduce coin weight is to examine existing bags in this range.
	    Search for the words "coin purse" with the #finditem command in game and the Bag WR setting on those bags is the amount they will reduce coin weight. It is easiest to overwrite one of those bags if you wish to create one with the
	    same weight reduction amount for coins. You can use other Item IDs in this range for setting coin weight reduction, but by using an existing item, at least you will know the amount the client will reduce it by before you create it.
	    This is the ONLY instance I have seen where the client is hard coded to particular Item IDs to set a certain property for an item. It is very odd.
	*/
	// SoD+ client has no weight for coin
	if (EQ::behavior::StaticLookup(EQ::versions::ConvertClientVersionToMobVersion(ClientVersion()))->CoinHasWeight) {
		Total += (m_pp.platinum + m_pp.gold + m_pp.silver + m_pp.copper) / 4;
	}
	float Packrat = (float)spellbonuses.Packrat + (float)aabonuses.Packrat + (float)itembonuses.Packrat;
	if (Packrat > 0) {
		Total = (uint32)((float)Total * (1.0f - ((Packrat * 1.0f) / 100.0f)));    //AndMetal: 1% per level, up to 5% (calculated from Titanium client). verified thru client that it reduces coin weight by the same %
	}
	//without casting to float & back to uint32, this didn't work right
	return Total;
}

int32 Client::CalcAlcoholPhysicalEffect()
{
	if (m_pp.intoxication <= 55) {
		return 0;
	}
	return (m_pp.intoxication - 40) / 16;
}

int32 Client::GetClassSpecificStats(uint32 class_id, int32 statIndex)
{

	switch(statIndex)
	{
		case 0: // STR
		{
			switch (GetClass())
			{
				case WARRIOR:
					return 10;
				case CLERIC:
					return 5;
				case PALADIN:
					return 10;
				case RANGER:
					return 5;
				case SHADOWKNIGHT:
					return 10;
				case DRUID:
					return 0;
				case MONK:
					return 5;
				case BARD:
					return 5;
				case ROGUE:
					return 0;
				case SHAMAN:
					return 0;
				case NECROMANCER:
					return 0;
				case WIZARD:
					return 0;
				case MAGICIAN:
					return 0;
				case ENCHANTER:
					return 0;
				case BEASTLORD:
					return 0;
				default:
					return 0;

			}
			break;
		}
		case 1: // STA
		{
			switch (GetClass())
			{
			case WARRIOR:
				return 10;
			case CLERIC:
				return 5;
			case PALADIN:
				return 5;
			case RANGER:
				return 10;
			case SHADOWKNIGHT:
				return 5;
			case DRUID:
				return 10;
			case MONK:
				return 5;
			case BARD:
				return 0;
			case ROGUE:
				return 0;
			case SHAMAN:
				return 5;
			case NECROMANCER:
				return 0;
			case WIZARD:
				return 10;
			case MAGICIAN:
				return 10;
			case ENCHANTER:
				return 0;
			case BEASTLORD:
				return 10;
			default:
				return 0;
			}
			break;
		}
		case 2: // AGI
		{
			switch (GetClass())
			{
			case WARRIOR:
				return 5;
			case CLERIC:
				return 0;
			case PALADIN:
				return 0;
			case RANGER:
				return 10;
			case SHADOWKNIGHT:
				return 0;
			case DRUID:
				return 0;
			case MONK:
				return 10;
			case BARD:
				return 0;
			case ROGUE:
				return 10;
			case SHAMAN:
				return 0;
			case NECROMANCER:
				return 0;
			case WIZARD:
				return 0;
			case MAGICIAN:
				return 0;
			case ENCHANTER:
				return 0;
			case BEASTLORD:
				return 5;
			default:
				return 0;
			}
			break;
		}
		case 3: // DEX
		{
			switch (GetClass())
			{
			case WARRIOR:
				return 0;
			case CLERIC:
				return 0;
			case PALADIN:
				return 0;
			case RANGER:
				return 0;
			case SHADOWKNIGHT:
				return 0;
			case DRUID:
				return 0;
			case MONK:
				return 10;
			case BARD:
				return 10;
			case ROGUE:
				return 10;
			case SHAMAN:
				return 0;
			case NECROMANCER:
				return 10;
			case WIZARD:
				return 0;
			case MAGICIAN:
				return 0;
			case ENCHANTER:
				return 0;
			case BEASTLORD:
				return 0;
			default:
				return 0;
			}
			break;
		}
		case 4: // WIS
		{
			switch (GetClass())
			{
			case WARRIOR:
				return 0;
			case CLERIC:
				return 10;
			case PALADIN:
				return 5;
			case RANGER:
				return 5;
			case SHADOWKNIGHT:
				return 0;
			case DRUID:
				return 10;
			case MONK:
				return 0;
			case BARD:
				return 0;
			case ROGUE:
				return 0;
			case SHAMAN:
				return 10;
			case NECROMANCER:
				return 0;
			case WIZARD:
				return 0;
			case MAGICIAN:
				return 0;
			case ENCHANTER:
				return 0;
			case BEASTLORD:
				return 10;
			default:
				return 0;
			}
			break;
		}
		case 5: // INT
		{
			switch (GetClass())
			{
			case WARRIOR:
				return 0;
			case CLERIC:
				return 0;
			case PALADIN:
				return 0;
			case RANGER:
				return 0;
			case SHADOWKNIGHT:
				return 10;
			case DRUID:
				return 0;
			case MONK:
				return 0;
			case BARD:
				return 0;
			case ROGUE:
				return 0;
			case SHAMAN:
				return 0;
			case NECROMANCER:
				return 10;
			case WIZARD:
				return 10;
			case MAGICIAN:
				return 10;
			case ENCHANTER:
				return 10;
			case BEASTLORD:
				return 0;
			default:
				return 0;
			}
			break;
		}
		case 6: // CHA
		{
			switch (GetClass())
			{
			case WARRIOR:
				return 0;
			case CLERIC:
				return 0;
			case PALADIN:
				return 10;
			case RANGER:
				return 0;
			case SHADOWKNIGHT:
				return 5;
			case DRUID:
				return 0;
			case MONK:
				return 0;
			case BARD:
				return 10;
			case ROGUE:
				return 0;
			case SHAMAN:
				return 5;
			case NECROMANCER:
				return 0;
			case WIZARD:
				return 0;
			case MAGICIAN:
				return 0;
			case ENCHANTER:
				return 10;
			case BEASTLORD:
				return 5;
			default:
				return 0;
			}
			break;
		}
	}
	return 0;
}

int32 Client::GetRaceSpecificStats(int32 statIndex)
{
	EQ::ItemInstance* inst = CastToClient()->GetInv().GetItem(EQ::invslot::slotCharm);

	if (inst)
	{

		switch (statIndex)
		{
		case 0: // STR
		{
			switch (inst->GetID())
			{
			case CharmHuman:
				return 75;
			case CharmBarbarian:
				return 103;
			case CharmErudite:
				return 60;
			case CharmWoodElf:
				return 65;
			case CharmHighElf:
				return 55;
			case CharmDarkElf:
				return 60;
			case CharmHalfElf:
				return 70;
			case CharmDwarf:
				return 90;
			case CharmTroll:
				return 108;
			case CharmOgre:
				return 130;
			case CharmHalfling:
				return 70;
			case CharmGnome:
				return 60;
			case CharmIksar:
				return 60;
			case CharmVahShir:
				return 90;
			case CharmFroglok:
				return 70;
			default:
				return 75;
			}
			break;
		}
		case 1: // STA
		{
			switch (inst->GetID())
			{
			case CharmHuman:
				return 75;
			case CharmBarbarian:
				return 95;
			case CharmErudite:
				return 70;
			case CharmWoodElf:
				return 65;
			case CharmHighElf:
				return 65;
			case CharmDarkElf:
				return 65;
			case CharmHalfElf:
				return 70;
			case CharmDwarf:
				return 90;
			case CharmTroll:
				return 114;
			case CharmOgre:
				return 127;
			case CharmHalfling:
				return 75;
			case CharmGnome:
				return 70;
			case CharmIksar:
				return 70;
			case CharmVahShir:
				return 75;
			case CharmFroglok:
				return 80;
			default:
				return 75;
			}
		}
		case 2: // AGI
		{
			switch (inst->GetID())
			{
			case CharmHuman:
				return 75;
			case CharmBarbarian:
				return 82;
			case CharmErudite:
				return 70;
			case CharmWoodElf:
				return 95;
			case CharmHighElf:
				return 85;
			case CharmDarkElf:
				return 90;
			case CharmHalfElf:
				return 90;
			case CharmDwarf:
				return 70;
			case CharmTroll:
				return 83;
			case CharmOgre:
				return 70;
			case CharmHalfling:
				return 95;
			case CharmGnome:
				return 85;
			case CharmIksar:
				return 90;
			case CharmVahShir:
				return 90;
			case CharmFroglok:
				return 100;
			default:
				return 75;
			}
			break;
		}
		case 3: // DEX
		{
			switch (inst->GetID())
			{
			case CharmHuman:
				return 75;
			case CharmBarbarian:
				return 70;
			case CharmErudite:
				return 70;
			case CharmWoodElf:
				return 80;
			case CharmHighElf:
				return 70;
			case CharmDarkElf:
				return 75;
			case CharmHalfElf:
				return 85;
			case CharmDwarf:
				return 90;
			case CharmTroll:
				return 75;
			case CharmOgre:
				return 70;
			case CharmHalfling:
				return 90;
			case CharmGnome:
				return 85;
			case CharmIksar:
				return 85;
			case CharmVahShir:
				return 70;
			case CharmFroglok:
				return 100;
			default:
				return 75;
			}
			break;
		}
		case 4: // WIS
		{
			switch (inst->GetID())
			{
			case CharmHuman:
				return 75;
			case CharmBarbarian:
				return 70;
			case CharmErudite:
				return 83;
			case CharmWoodElf:
				return 80;
			case CharmHighElf:
				return 95;
			case CharmDarkElf:
				return 83;
			case CharmHalfElf:
				return 60;
			case CharmDwarf:
				return 83;
			case CharmTroll:
				return 60;
			case CharmOgre:
				return 67;
			case CharmHalfling:
				return 80;
			case CharmGnome:
				return 67;
			case CharmIksar:
				return 80;
			case CharmVahShir:
				return 70;
			case CharmFroglok:
				return 75;
			default:
				return 75;
			}
		}
		case 5: // INT
		{
			switch (inst->GetID())
			{
			case CharmHuman:
				return 75;
			case CharmBarbarian:
				return 60;
			case CharmErudite:
				return 107;
			case CharmWoodElf:
				return 75;
			case CharmHighElf:
				return 92;
			case CharmDarkElf:
				return 99;
			case CharmHalfElf:
				return 75;
			case CharmDwarf:
				return 60;
			case CharmTroll:
				return 52;
			case CharmOgre:
				return 60;
			case CharmHalfling:
				return 67;
			case CharmGnome:
				return 98;
			case CharmIksar:
				return 75;
			case CharmVahShir:
				return 65;
			case CharmFroglok:
				return 75;
			default:
				return 75;
			}
			break;
		}
		case 6: // CHA
		{
			switch (inst->GetID())
			{
			case CharmHuman:
				return 75;
			case CharmBarbarian:
				return 55;
			case CharmErudite:
				return 70;
			case CharmWoodElf:
				return 75;
			case CharmHighElf:
				return 80;
			case CharmDarkElf:
				return 60;
			case CharmHalfElf:
				return 75;
			case CharmDwarf:
				return 45;
			case CharmTroll:
				return 40;
			case CharmOgre:
				return 37;
			case CharmHalfling:
				return 50;
			case CharmGnome:
				return 60;
			case CharmIksar:
				return 55;
			case CharmVahShir:
				return 65;
			case CharmFroglok:
				return 50;
			default:
				return 75;
			}
			break;
		}
		}
	}
	return 75;
}

int32 Client::GetPrimaryPreferredStatAllocationIndex()
{
	switch (GetClass())
	{
	case WARRIOR:
		return 1;
	case CLERIC:
		return 4;
	case PALADIN:
		return 1;
	case RANGER:
		return 3;
	case SHADOWKNIGHT:
		return 1;
	case DRUID:
		return 4;
	case MONK:
		return 0;
	case BARD:
		return 10;
	case ROGUE:
		return 0;
	case SHAMAN:
		return 1;
	case NECROMANCER:
		return 1;
	case WIZARD:
		return 5;
	case MAGICIAN:
		return 5;
	case ENCHANTER:
		return 6;
	case BEASTLORD:
		return 2;
	default:
		return -1;
	}
}

int32 Client::GetSecondaryPreferredStatAllocationIndex()
{
	switch (GetClass())
	{
	case WARRIOR:
		return -1;
	case CLERIC:
		return 0;
	case PALADIN:
		return -1;
	case RANGER:
		return -1;
	case SHADOWKNIGHT:
		return -1;
	case DRUID:
		return 1;
	case MONK:
		return -1;
	case BARD:
		return -1;
	case ROGUE:
		return 1;
	case SHAMAN:
		return 4;
	case NECROMANCER:
		return 5;
	case WIZARD:
		return 1;
	case MAGICIAN:
		return 1;
	case ENCHANTER:
		return 5;
	case BEASTLORD:
		return -1;
	default:
		return -1;
	}
}

int32 Client::GetBaseAllocationPointsByClass()
{
	switch (GetClass())
	{
	case WARRIOR:
		return 25;
	case CLERIC:
		return 30;
	case PALADIN:
		return 20;
	case RANGER:
		return 20;
	case SHADOWKNIGHT:
		return 20;
	case DRUID:
		return 30;
	case MONK:
		return 20;
	case BARD:
		return 25;
	case ROGUE:
		return 30;
	case SHAMAN:
		return 30;
	case NECROMANCER:
		return 30;
	case WIZARD:
		return 30;
	case MAGICIAN:
		return 30;
	case ENCHANTER:
		return 30;
	case BEASTLORD:
		return 20;
	default:
		return 30;
	}
}


void Client::CalcBaseStatAllocations()
{

	int allocPoints = GetBaseAllocationPointsByClass();

	int32 primaryAllocationIndex = GetPrimaryPreferredStatAllocationIndex();
	int32 secondaryAllocationIndex = GetSecondaryPreferredStatAllocationIndex();


	STR = 0;
	STA = 0;
	AGI = 0;
	DEX = 0;
	INT = 0;
	WIS = 0;
	CHA = 0;

	if (GetRaceSpecificStats(2) + GetClassSpecificStats(GetClass(), 2) < 75)
	{
		allocPoints -= 75 - GetRaceSpecificStats(2) + GetClassSpecificStats(GetClass(), 2);
	}

	if (secondaryAllocationIndex == -1)
	{
		switch (primaryAllocationIndex)
		{
		case 0:
		{
			STR += allocPoints;
			break;
		}
		case 1:
		{
			STA += allocPoints;
			break;
		}
		case 2:
		{
			AGI += allocPoints;
			break;
		}
		case 3:
		{
			DEX += allocPoints;
			break;
		}
		case 4:
		{
			WIS += allocPoints;
			break;
		}
		case 5:
		{
			INT += allocPoints;
			break;
		}
		case 6:
		{
			CHA += allocPoints;
			break;
		}
		default:
		{

			break;
		}
		}
	}
	else
	{
		switch (primaryAllocationIndex)
		{
		case 0:
		{
			STR += 25;
			break;
		}
		case 1:
		{
			STA += 25;
			break;
		}
		case 2:
		{
			AGI += 25;
			break;
		}
		case 3:
		{
			DEX += 25;
			break;
		}
		case 4:
		{
			WIS += 25;
			break;
		}
		case 5:
		{
			INT += 25;
			break;
		}
		case 6:
		{
			CHA += 25;
			break;
		}
		default:
		{
			break;
		}
		}
		if (allocPoints > 25)
		{
			switch (secondaryAllocationIndex)
			{
			case 0:
			{
				STR += allocPoints - 25;
				break;
			}
			case 1:
			{
				STA += allocPoints - 25;
				break;
			}
			case 2:
			{
				AGI += allocPoints - 25;
				break;
			}
			case 3:
			{
				DEX += allocPoints - 25;
				break;
			}
			case 4:
			{
				WIS += allocPoints - 25;
				break;
			}
			case 5:
			{
				INT += allocPoints - 25;
				break;
			}
			case 6:
			{
				CHA += allocPoints - 25;
				break;
			}
			default:
			{

				break;
			}
			}
		}
	}
}

int32 Client::CalcSTR()
{
	int32 val = GetRaceSpecificStats(0) + GetClassSpecificStats(GetClass(), 0) + itembonuses.STR + spellbonuses.STR + CalcAlcoholPhysicalEffect();
	int32 mod = aabonuses.STR;
	STR += val + mod;
	if (STR < 1) {
		STR = 1;
	}
	int m = GetMaxSTR();
	if (STR > m) {
		STR = m;
	}
	return (STR);
}

int32 Client::CalcSTA()
{
	int32 val = GetRaceSpecificStats(1) + GetClassSpecificStats(GetClass(), 1) + itembonuses.STA + spellbonuses.STA + CalcAlcoholPhysicalEffect();
	int32 mod = aabonuses.STA;
	STA += val + mod;
	if (STA < 1) {
		STA = 1;
	}
	int m = GetMaxSTA();
	if (STA > m) {
		STA = m;
	}
	return (STA);
}

int32 Client::CalcAGI()
{
	int32 val = GetRaceSpecificStats(2) + GetClassSpecificStats(GetClass(), 2);
	if (val < 75) // we'll always be /at least/ 75 base. we smert.
		val = 75;
	val += itembonuses.AGI + spellbonuses.AGI + CalcAlcoholPhysicalEffect();
	int32 mod = aabonuses.AGI;
	int32 str = GetSTR();
	//Encumbered penalty
	if (weight > (str * 10)) {
		//AGI is halved when we double our weight, zeroed (defaults to 1) when we triple it. this includes AGI from AAs
		float total_agi = float(val + mod);
		float str_float = float(str);
		AGI += (int32)(((-total_agi) / (str_float * 2)) * (((float)weight / 10) - str_float) + total_agi);	//casting to an int assumes this will be floor'd. without using floats & casting to int16, the calculation doesn't work right
	}
	else {
		AGI += val + mod;
	}
	if (AGI < 1) {
		AGI = 1;
	}
	int m = GetMaxAGI();
	if (AGI > m) {
		AGI = m;
	}
	return (AGI);
}

int32 Client::CalcDEX()
{
	int32 val = GetRaceSpecificStats(3) + GetClassSpecificStats(GetClass(), 3) + itembonuses.DEX + spellbonuses.DEX - CalcAlcoholPhysicalEffect();
	int32 mod = aabonuses.DEX;
	DEX += val + mod;
	if (DEX < 1) {
		DEX = 1;
	}
	int m = GetMaxDEX();
	if (DEX > m) {
		DEX = m;
	}
	return (DEX);
}

int32 Client::CalcINT()
{
	int32 val = GetRaceSpecificStats(5) + GetClassSpecificStats(GetClass(), 5) + itembonuses.INT + spellbonuses.INT;
	int32 mod = aabonuses.INT;
	INT += val + mod;
	if (m_pp.intoxication) {
		int32 AlcINT = INT - (int32)((float)m_pp.intoxication / 200.0f * (float)INT) - 1;
		if ((AlcINT < (int)(0.2 * INT))) {
			INT = (int)(0.2f * (float)INT);
		}
		else {
			INT = AlcINT;
		}
	}
	if (INT < 1) {
		INT = 1;
	}
	int m = GetMaxINT();
	if (INT > m) {
		INT = m;
	}
	return (INT);
}

int32 Client::CalcWIS()
{
	int32 val = GetRaceSpecificStats(4) + GetClassSpecificStats(GetClass(), 4) + itembonuses.WIS + spellbonuses.WIS;
	int32 mod = aabonuses.WIS;
	WIS += val + mod;
	if (m_pp.intoxication) {
		int32 AlcWIS = WIS - (int32)((float)m_pp.intoxication / 200.0f * (float)WIS) - 1;
		if ((AlcWIS < (int)(0.2 * WIS))) {
			WIS = (int)(0.2f * (float)WIS);
		}
		else {
			WIS = AlcWIS;
		}
	}
	if (WIS < 1) {
		WIS = 1;
	}
	int m = GetMaxWIS();
	if (WIS > m) {
		WIS = m;
	}
	return (WIS);
}

int32 Client::CalcCHA()
{
	int32 val = GetRaceSpecificStats(6) + GetClassSpecificStats(GetClass(), 6) + itembonuses.CHA + spellbonuses.CHA;
	int32 mod = aabonuses.CHA;
	CHA += val + mod;
	if (CHA < 1) {
		CHA = 1;
	}
	int m = GetMaxCHA();
	if (CHA > m) {
		CHA = m;
	}
	return (CHA);
}

int Client::CalcHaste()
{
	/*  Tests: (based on results in newer char window)
	    68 v1 + 46 item + 25 over + 35 inhib = 204%
	    46 item + 5 v2 + 25 over + 35 inhib = 65%
	    68 v1 + 46 item + 5 v2 + 25 over + 35 inhib = 209%
	    75% slow + 35 inhib = 25%
	    35 inhib = 65%
	    75% slow = 25%
	    Conclusions:
	    the bigger effect in slow v. inhib wins
	    slow negates all other hastes
	    inhib will only negate all other hastes if you don't have v1 (ex. VQ)
	*/
	// slow beats all! Besides a better inhibit
	if (spellbonuses.haste < 0) {
		if (-spellbonuses.haste <= spellbonuses.inhibitmelee) {
			Haste = 100 - spellbonuses.inhibitmelee;
		}
		else {
			Haste = 100 + spellbonuses.haste;
		}
		return Haste;
	}
	// No haste and inhibit, kills all other hastes
	if (spellbonuses.haste == 0 && spellbonuses.inhibitmelee) {
		Haste = 100 - spellbonuses.inhibitmelee;
		return Haste;
	}
	int h = 0;
	int cap = 0;
	int level = GetLevel();
	// we know we have a haste spell and not slowed, no extra inhibit melee checks needed
	if (spellbonuses.haste) {
		h += spellbonuses.haste - spellbonuses.inhibitmelee;
	}
	if (spellbonuses.hastetype2 && level > 49) { // type 2 is capped at 10% and only available to 50+
		h += spellbonuses.hastetype2 > 10 ? 10 : spellbonuses.hastetype2;
	}
	// 26+ no cap, 1-25 10
	if (level > 25) { // 26+
		h += itembonuses.haste;
	}
	else {   // 1-25
		h += itembonuses.haste > 10 ? 10 : itembonuses.haste;
	}
	// 60+ 100, 51-59 85, 1-50 level+25
	if (level > 59) { // 60+
		cap = RuleI(Character, HasteCap);
	}
	else if (level > 50) {  // 51-59
		cap = 85;
	}
	else {   // 1-50
		cap = level + 25;
	}
	cap = mod_client_haste_cap(cap);
	if (h > cap) {
		h = cap;
	}
	// 51+ 25 (despite there being higher spells...), 1-50 10
	if (level > 50) { // 51+
		cap = RuleI(Character, Hastev3Cap);
		if (spellbonuses.hastetype3 > cap) {
			h += cap;
		} else {
			h += spellbonuses.hastetype3;
		}
	}
	else {   // 1-50
		h += spellbonuses.hastetype3 > 10 ? 10 : spellbonuses.hastetype3;
	}
	h += ExtraHaste;	//GM granted haste.
	h = mod_client_haste(h);
	Haste = 100 + h;
	return Haste;
}

//The AA multipliers are set to be 5, but were 2 on WR
//The resistant discipline which I think should be here is implemented
//in Mob::ResistSpell
int32	Client::CalcMR()
{
	//racial bases
	switch (GetBaseRace()) {
		case HUMAN:
			MR = 25;
			break;
		case BARBARIAN:
			MR = 25;
			break;
		case ERUDITE:
			MR = 30;
			break;
		case WOOD_ELF:
			MR = 25;
			break;
		case HIGH_ELF:
			MR = 25;
			break;
		case DARK_ELF:
			MR = 25;
			break;
		case HALF_ELF:
			MR = 25;
			break;
		case DWARF:
			MR = 30;
			break;
		case TROLL:
			MR = 25;
			break;
		case OGRE:
			MR = 25;
			break;
		case HALFLING:
			MR = 25;
			break;
		case GNOME:
			MR = 25;
			break;
		case IKSAR:
			MR = 25;
			break;
		case VAHSHIR:
			MR = 25;
			break;
		case FROGLOK:
			MR = 30;
			break;
		case DRAKKIN:
		{
			MR = 25;
			if (GetDrakkinHeritage() == 2)
				MR += 10;
			else if (GetDrakkinHeritage() == 5)
				MR += 2;
			break;
		}
		default:
			MR = 20;
	}
	MR += itembonuses.MR + spellbonuses.MR + aabonuses.MR;
	if (GetClass() == WARRIOR || GetClass() == BERSERKER) {
		MR += GetLevel() / 2;
	}
	if (MR < 1) {
		MR = 1;
	}
	if (MR > GetMaxMR()) {
		MR = GetMaxMR();
	}
	return (MR);
}

int32	Client::CalcFR()
{
	//racial bases
	switch (GetBaseRace()) {
		case HUMAN:
			FR = 25;
			break;
		case BARBARIAN:
			FR = 25;
			break;
		case ERUDITE:
			FR = 25;
			break;
		case WOOD_ELF:
			FR = 25;
			break;
		case HIGH_ELF:
			FR = 25;
			break;
		case DARK_ELF:
			FR = 25;
			break;
		case HALF_ELF:
			FR = 25;
			break;
		case DWARF:
			FR = 25;
			break;
		case TROLL:
			FR = 5;
			break;
		case OGRE:
			FR = 25;
			break;
		case HALFLING:
			FR = 25;
			break;
		case GNOME:
			FR = 25;
			break;
		case IKSAR:
			FR = 30;
			break;
		case VAHSHIR:
			FR = 25;
			break;
		case FROGLOK:
			FR = 25;
			break;
		case DRAKKIN:
		{
			FR = 25;
			if (GetDrakkinHeritage() == 0)
				FR += 10;
			else if (GetDrakkinHeritage() == 5)
				FR += 2;
			break;
		}
		default:
			FR = 20;
	}
	int c = GetClass();
	if (c == RANGER) {
		FR += 4;
		int l = GetLevel();
		if (l > 49) {
			FR += l - 49;
		}
	}
	if (c == MONK) {
		FR += 8;
		int l = GetLevel();
		if (l > 49) {
			FR += l - 49;
		}
	}
	FR += itembonuses.FR + spellbonuses.FR + aabonuses.FR;
	if (FR < 1) {
		FR = 1;
	}
	if (FR > GetMaxFR()) {
		FR = GetMaxFR();
	}
	return (FR);
}

int32	Client::CalcDR()
{
	//racial bases
	switch (GetBaseRace()) {
		case HUMAN:
			DR = 15;
			break;
		case BARBARIAN:
			DR = 15;
			break;
		case ERUDITE:
			DR = 10;
			break;
		case WOOD_ELF:
			DR = 15;
			break;
		case HIGH_ELF:
			DR = 15;
			break;
		case DARK_ELF:
			DR = 15;
			break;
		case HALF_ELF:
			DR = 15;
			break;
		case DWARF:
			DR = 15;
			break;
		case TROLL:
			DR = 15;
			break;
		case OGRE:
			DR = 15;
			break;
		case HALFLING:
			DR = 20;
			break;
		case GNOME:
			DR = 15;
			break;
		case IKSAR:
			DR = 15;
			break;
		case VAHSHIR:
			DR = 15;
			break;
		case FROGLOK:
			DR = 15;
			break;
		case DRAKKIN:
		{
			DR = 15;
			if (GetDrakkinHeritage() == 1)
				DR += 10;
			else if (GetDrakkinHeritage() == 5)
				DR += 2;
			break;
		}
		default:
			DR = 15;
	}
	int c = GetClass();
	// the monk one is part of base resist
	if (c == MONK) {
		int l = GetLevel();
		if (l > 50)
			DR += l - 50;
	}
	if (c == PALADIN) {
		DR += 8;
		int l = GetLevel();
		if (l > 49) {
			DR += l - 49;
		}
	}
	else if (c == SHADOWKNIGHT || c == BEASTLORD) {
		DR += 4;
		int l = GetLevel();
		if (l > 49) {
			DR += l - 49;
		}
	}
	DR += itembonuses.DR + spellbonuses.DR + aabonuses.DR;
	if (DR < 1) {
		DR = 1;
	}
	if (DR > GetMaxDR()) {
		DR = GetMaxDR();
	}
	return (DR);
}

int32	Client::CalcPR()
{
	//racial bases
	switch (GetBaseRace()) {
		case HUMAN:
			PR = 15;
			break;
		case BARBARIAN:
			PR = 15;
			break;
		case ERUDITE:
			PR = 15;
			break;
		case WOOD_ELF:
			PR = 15;
			break;
		case HIGH_ELF:
			PR = 15;
			break;
		case DARK_ELF:
			PR = 15;
			break;
		case HALF_ELF:
			PR = 15;
			break;
		case DWARF:
			PR = 20;
			break;
		case TROLL:
			PR = 15;
			break;
		case OGRE:
			PR = 15;
			break;
		case HALFLING:
			PR = 20;
			break;
		case GNOME:
			PR = 15;
			break;
		case IKSAR:
			PR = 15;
			break;
		case VAHSHIR:
			PR = 15;
			break;
		case FROGLOK:
			PR = 30;
			break;
		case DRAKKIN:
		{
			PR = 15;
			if (GetDrakkinHeritage() == 3)
				PR += 10;
			else if (GetDrakkinHeritage() == 5)
				PR += 2;
			break;
		}
		default:
			PR = 15;
	}
	int c = GetClass();
	// this monk bonus is part of the base
	if (c == MONK) {
		int l = GetLevel();
		if (l > 50)
			PR += l - 50;
	}
	if (c == ROGUE) {
		PR += 8;
		int l = GetLevel();
		if (l > 49) {
			PR += l - 49;
		}
	}
	else if (c == SHADOWKNIGHT) {
		PR += 4;
		int l = GetLevel();
		if (l > 49) {
			PR += l - 49;
		}
	}
	PR += itembonuses.PR + spellbonuses.PR + aabonuses.PR;
	if (PR < 1) {
		PR = 1;
	}
	if (PR > GetMaxPR()) {
		PR = GetMaxPR();
	}
	return (PR);
}

int32	Client::CalcCR()
{
	//racial bases
	switch (GetBaseRace()) {
		case HUMAN:
			CR = 25;
			break;
		case BARBARIAN:
			CR = 35;
			break;
		case ERUDITE:
			CR = 25;
			break;
		case WOOD_ELF:
			CR = 25;
			break;
		case HIGH_ELF:
			CR = 25;
			break;
		case DARK_ELF:
			CR = 25;
			break;
		case HALF_ELF:
			CR = 25;
			break;
		case DWARF:
			CR = 25;
			break;
		case TROLL:
			CR = 25;
			break;
		case OGRE:
			CR = 25;
			break;
		case HALFLING:
			CR = 25;
			break;
		case GNOME:
			CR = 25;
			break;
		case IKSAR:
			CR = 15;
			break;
		case VAHSHIR:
			CR = 25;
			break;
		case FROGLOK:
			CR = 25;
			break;
		case DRAKKIN:
		{
			CR = 25;
			if (GetDrakkinHeritage() == 4)
				CR += 10;
			else if (GetDrakkinHeritage() == 5)
				CR += 2;
			break;
		}
		default:
			CR = 25;
	}
	int c = GetClass();
	if (c == RANGER || c == BEASTLORD) {
		CR += 4;
		int l = GetLevel();
		if (l > 49) {
			CR += l - 49;
		}
	}
	CR += itembonuses.CR + spellbonuses.CR + aabonuses.CR;
	if (CR < 1) {
		CR = 1;
	}
	if (CR > GetMaxCR()) {
		CR = GetMaxCR();
	}
	return (CR);
}

int32	Client::CalcCorrup()
{
	Corrup = GetBaseCorrup() + itembonuses.Corrup + spellbonuses.Corrup + aabonuses.Corrup;
	if (Corrup > GetMaxCorrup()) {
		Corrup = GetMaxCorrup();
	}
	return (Corrup);
}

int32 Client::CalcATK()
{
	ATK = itembonuses.ATK + spellbonuses.ATK + aabonuses.ATK + GroupLeadershipAAOffenseEnhancement();
	return (ATK);
}

uint32 Mob::GetInstrumentMod(uint16 spell_id)
{
	if (GetClass() != BARD) {
		//Other classes can get a base effects mod using SPA 413
		if (HasBaseEffectFocus()) {
			return (10 + (GetFocusEffect(focusFcBaseEffects, spell_id) / 10));//TODO: change action->instrument mod to float to support < 10% focus values
		}
		return 10;
	}

	//AA's click effects that use instrument/singing skills don't apply modifiers (Confirmed on live 11/24/21 ~Kayen)
	if (casting_spell_aa_id) {
		return 10;
	}

	uint32 effectmod = 10;
	int effectmodcap = 0;
	if (RuleB(Character, UseSpellFileSongCap)) {
		effectmodcap = spells[spell_id].song_cap / 10;
		if (effectmodcap) {
			effectmodcap += 10; //Actual calculated cap is 100 greater than songcap value.
		}
	}
	else {
		effectmodcap = RuleI(Character, BaseInstrumentSoftCap);
	}
	// this should never use spell modifiers...
	// if a spell grants better modifers, they are copied into the item mods
	// because the spells are supposed to act just like having the intrument.
	// item mods are in 10ths of percent increases
	// clickies (Symphony of Battle) that have a song skill don't get AA bonus for some reason
	// but clickies that are songs (selo's on Composers Greaves) do get AA mod as well

	/*Mechanics: updated 10/19/21 ~Kayen
		Bard Spell Effects

		Mod uses the highest bonus from either of these for each instrument
		SPA 179 SE_AllInstrumentMod is used for instrument spellbonus.______Mod. This applies to ALL instrument mods (Puretones Discipline)
		SPA 260 SE_AddSingingMod is used for instrument spellbonus.______Mod. This applies to indiviual instrument mods. (Instrument mastery AA)
			-Example usage: From AA a value of 4 = 40%

		SPA 118 SE_Amplification is a stackable singing mod, on live it exists as both spell and AA bonus (stackable)
			- Live Behavior: Amplifcation can be modified by singing mods and amplification itself, thus on the second cast of Amplification you will recieve
			  the mod from the first cast, this continues until you reach the song mod cap.

		SPA 261 SE_SongModCap raises song focus cap (No longer used on live)
		SPA 270 SE_BardSongRange increase range of beneficial bard songs (Sionachie's Crescendo)

		SPA 413 SE_FcBaseEffects focus effect that replaced item instrument mods

		Issues 10-15-21:
		Bonuses are not applied, unless song is stopped and restarted due to pulse keeping it continues. -> Need to recode songs to recast when duration ends.

		Formula Live Bards:
		mod = (10 + (aabonus.____Mod [SPA 260 AA Instrument Mastery]) + (SE_FcBaseEffect[SPA 413])/10 + (spellbonus.______Mod [SPA 179 Puretone Disc]) + (Amplication [SPA 118])/10

		TODO: Spell Table Fields that need to be implemented
		Field 225	//float base_effects_focus_slope;  // -- BASE_EFFECTS_FOCUS_SLOPE
		Field 226	//float base_effects_focus_offset; // -- BASE_EFFECTS_FOCUS_OFFSET (35161	Ruaabri's Reckless Renewal -120)
		Based on description possibly works as a way to quickly balance instrument mods to a song.
		Using a standard slope formula: y = mx + b
		modified_base_value = (base_effects_focus_slope x effectmod)(base_value) + (base_effects_focus_offset)
		Will need to confirm on live before implementing.
	*/

	switch (spells[spell_id].skill) {
	case EQ::skills::SkillPercussionInstruments:
		if (itembonuses.percussionMod == 0 && spellbonuses.percussionMod == 0)
			effectmod = 10;
		else if (GetSkill(EQ::skills::SkillPercussionInstruments) == 0)
			effectmod = 10;
		else if (itembonuses.percussionMod > spellbonuses.percussionMod)
			effectmod = itembonuses.percussionMod;
		else
			effectmod = spellbonuses.percussionMod;
		if (IsBardSong(spell_id))
			effectmod += aabonuses.percussionMod;
		break;
	case EQ::skills::SkillStringedInstruments:
		if (itembonuses.stringedMod == 0 && spellbonuses.stringedMod == 0)
			effectmod = 10;
		else if (GetSkill(EQ::skills::SkillStringedInstruments) == 0)
			effectmod = 10;
		else if (itembonuses.stringedMod > spellbonuses.stringedMod)
			effectmod = itembonuses.stringedMod;
		else
			effectmod = spellbonuses.stringedMod;
		if (IsBardSong(spell_id))
			effectmod += aabonuses.stringedMod;
		break;
	case EQ::skills::SkillWindInstruments:
		if (itembonuses.windMod == 0 && spellbonuses.windMod == 0)
			effectmod = 10;
		else if (GetSkill(EQ::skills::SkillWindInstruments) == 0)
			effectmod = 10;
		else if (itembonuses.windMod > spellbonuses.windMod)
			effectmod = itembonuses.windMod;
		else
			effectmod = spellbonuses.windMod;
		if (IsBardSong(spell_id))
			effectmod += aabonuses.windMod;
		break;
	case EQ::skills::SkillBrassInstruments:
		if (itembonuses.brassMod == 0 && spellbonuses.brassMod == 0)
			effectmod = 10;
		else if (GetSkill(EQ::skills::SkillBrassInstruments) == 0)
			effectmod = 10;
		else if (itembonuses.brassMod > spellbonuses.brassMod)
			effectmod = itembonuses.brassMod;
		else
			effectmod = spellbonuses.brassMod;
		if (IsBardSong(spell_id))
			effectmod += aabonuses.brassMod;
		break;
	case EQ::skills::SkillSinging:
		if (itembonuses.singingMod == 0 && spellbonuses.singingMod == 0)
			effectmod = 10;
		else if (itembonuses.singingMod > spellbonuses.singingMod)
			effectmod = itembonuses.singingMod;
		else
			effectmod = spellbonuses.singingMod;
		if (IsBardSong(spell_id))
			effectmod += aabonuses.singingMod + (spellbonuses.Amplification + itembonuses.Amplification + aabonuses.Amplification); //SPA 118 SE_Amplification
		break;
	default:
		effectmod = 10;
		return effectmod;
	}

	if (HasBaseEffectFocus()) {
		effectmod += (GetFocusEffect(focusFcBaseEffects, spell_id) / 10);
	}

	if (effectmod < 10) {
		effectmod = 10;
	}

	if (effectmodcap) {

		effectmodcap += aabonuses.songModCap + spellbonuses.songModCap + itembonuses.songModCap; //SPA 261 SE_SongModCap (not used on live)

		//Incase a negative modifier is used.
		if (effectmodcap <= 0) {
			effectmodcap = 10;
		}

		if (effectmod > effectmodcap) { // if the cap is calculated to be 0 using new rules, no cap.
			effectmod = effectmodcap;
		}
	}

	LogSpells("[{}]::GetInstrumentMod() spell=[{}] mod=[{}] modcap=[{}]\n", GetName(), spell_id, effectmod, effectmodcap);

	return effectmod;
}

void Client::CalcMaxEndurance()
{
	max_end = CalcBaseEndurance() + spellbonuses.Endurance + itembonuses.Endurance + aabonuses.Endurance;
	if (max_end < 0) {
		max_end = 0;
	}
	if (current_endurance > max_end) {
		current_endurance = max_end;
	}
	int end_perc_cap = spellbonuses.EndPercCap[SBIndex::RESOURCE_PERCENT_CAP];
	if (end_perc_cap) {
		int curEnd_cap = (max_end * end_perc_cap) / 100;
		if (current_endurance > curEnd_cap || (spellbonuses.EndPercCap[SBIndex::RESOURCE_AMOUNT_CAP] && current_endurance > spellbonuses.EndPercCap[SBIndex::RESOURCE_AMOUNT_CAP])) {
			current_endurance = curEnd_cap;
		}
	}
}

int64 Client::CalcBaseEndurance()
{
	int64 base_end = 0;
	if (ClientVersion() >= EQ::versions::ClientVersion::SoF && RuleB(Character, SoDClientUseSoDHPManaEnd)) {
		double heroic_stats = (GetHeroicSTR() + GetHeroicSTA() + GetHeroicDEX() + GetHeroicAGI()) / 4.0f;
		double stats = (GetSTR() + GetSTA() + GetDEX() + GetAGI()) / 4.0f;
		if (stats > 201.0f) {
			stats = 1.25f * (stats - 201.0f) + 352.5f;
		}
		else if (stats > 100.0f) {
			stats = 2.5f * (stats - 100.0f) + 100.0f;
		}
		auto base_data = database.GetBaseData(GetLevel(), GetClass());
		if (base_data) {
			base_end = base_data->base_end + (heroic_stats * 10.0f) + (base_data->endurance_factor * static_cast<int>(stats));
		}
	}
	else {
		int Stats = GetSTR() + GetSTA() + GetDEX() + GetAGI();
		int LevelBase = GetLevel() * 15;
		int at_most_800 = Stats;
		if (at_most_800 > 800) {
			at_most_800 = 800;
		}
		int Bonus400to800 = 0;
		int HalfBonus400to800 = 0;
		int Bonus800plus = 0;
		int HalfBonus800plus = 0;
		int BonusUpto800 = int( at_most_800 / 4 ) ;
		if (Stats > 400) {
			Bonus400to800 = int( (at_most_800 - 400) / 4 );
			HalfBonus400to800 = int( std::max( ( at_most_800 - 400 ), 0 ) / 8 );
			if (Stats > 800) {
				Bonus800plus = int( (Stats - 800) / 8 ) * 2;
				HalfBonus800plus = int( (Stats - 800) / 16 );
			}
		}
		int64 bonus_sum = BonusUpto800 + Bonus400to800 + HalfBonus400to800 + Bonus800plus + HalfBonus800plus;
		base_end = LevelBase;
		//take all of the sums from above, then multiply by level*0.075
		base_end += ( bonus_sum * 3 * GetLevel() ) / 40;
	}
	return base_end;
}

int64 Client::CalcEnduranceRegen(bool bCombat)
{
	int64 base = 0;
	if (!IsStarved()) {
		auto base_data = database.GetBaseData(GetLevel(), GetClass());
		if (base_data) {
			base = static_cast<int>(base_data->end_regen);
			if (!auto_attack && base > 0)
				base += base / 2;
		}
	}

	// so when we are mounted, our local client SpeedRun is always 0, so this is always false, but the packets we process it to our own shit :P
	bool is_running = runmode && animation != 0 && GetHorseId() == 0; // TODO: animation is really what MQ2 calls SpeedRun

	int weight_limit = GetSTR();
	auto level = GetLevel();
	if (GetClass() == MONK) {
		if (level > 99)
			weight_limit = 58;
		else if (level > 94)
			weight_limit = 57;
		else if (level > 89)
			weight_limit = 56;
		else if (level > 84)
			weight_limit = 55;
		else if (level > 79)
			weight_limit = 54;
		else if (level > 64)
			weight_limit = 53;
		else if (level > 63)
			weight_limit = 50;
		else if (level > 61)
			weight_limit = 47;
		else if (level > 59)
			weight_limit = 45;
		else if (level > 54)
			weight_limit = 40;
		else if (level > 50)
			weight_limit = 38;
		else if (level > 44)
			weight_limit = 36;
		else if (level > 29)
			weight_limit = 34;
		else if (level > 14)
			weight_limit = 32;
		else
			weight_limit = 30;
	}

	bool encumbered = (CalcCurrentWeight() / 10) >= weight_limit;

	if (is_running)
		base += level / -15;

	if (encumbered)
		base += level / -15;

	auto item_bonus = GetHeroicAGI() + GetHeroicDEX() + GetHeroicSTA() + GetHeroicSTR();
	item_bonus = item_bonus / 4 / 50;
	item_bonus += itembonuses.EnduranceRegen; // this is capped already
	base += item_bonus;

	base = base * AreaEndRegen + 0.5f;

	auto aa_regen = aabonuses.EnduranceRegen;

	int64 regen = base;
	if (!bCombat && CanFastRegen() && (IsSitting() || CanMedOnHorse())) {
		auto max_end = GetMaxEndurance();
		int fast_regen = 6 * (max_end / zone->newzone_data.FastRegenEndurance);
		if (aa_regen < fast_regen) // weird, but what the client is doing
			aa_regen = fast_regen;
	}

	regen += aa_regen;
	regen += spellbonuses.EnduranceRegen; // TODO: client does this in buff tick

	return (regen * RuleI(Character, EnduranceRegenMultiplier) / 100);
}

int64 Client::CalcEnduranceRegenCap()
{
	int64 cap = RuleI(Character, ItemEnduranceRegenCap) + aabonuses.ItemEnduranceRegenCap + itembonuses.ItemEnduranceRegenCap + spellbonuses.ItemEnduranceRegenCap;
	return (cap * RuleI(Character, EnduranceRegenMultiplier) / 100);
}

int32 Client::CalcItemATKCap()
{
	int cap = RuleI(Character, ItemATKCap) + itembonuses.ItemATKCap + spellbonuses.ItemATKCap + aabonuses.ItemATKCap;
	return cap;
}

int Client::GetRawACNoShield(int &shield_ac) const
{
	int ac = itembonuses.AC + spellbonuses.AC + aabonuses.AC;
	shield_ac = 0;
	const EQ::ItemInstance *inst = m_inv.GetItem(EQ::invslot::slotSecondary);
	if (inst) {
		if (inst->GetItem()->ItemType == EQ::item::ItemTypeShield) {
			ac -= inst->GetItem()->AC;
			shield_ac = inst->GetItem()->AC;
			for (uint8 i = EQ::invaug::SOCKET_BEGIN; i <= EQ::invaug::SOCKET_END; i++) {
				if (inst->GetAugment(i)) {
					ac -= inst->GetAugment(i)->GetItem()->AC;
					shield_ac += inst->GetAugment(i)->GetItem()->AC;
				}
			}
		}
	}
	return ac;
}
