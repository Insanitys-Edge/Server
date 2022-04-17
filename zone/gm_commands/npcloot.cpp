#include "../client.h"
#include "../corpse.h"
#include "../../common/data_verification.h"

void command_npcloot(Client *c, const Seperator *sep)
{
	if (!c->GetTarget() || (!c->GetTarget()->IsNPC() && !c->GetTarget()->IsCorpse())) {
		c->Message(Chat::White, "You must target an NPC or a Corpse to use this command.");
		return;
	}

	if (c->GetTarget()->IsNPC()) {
		c->GetTarget()->CastToNPC()->QueryLoot(c);
	} else if (c->GetTarget()->IsCorpse()) {
		c->GetTarget()->CastToCorpse()->QueryLoot(c);
	}
}

