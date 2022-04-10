/*	EQEMu: Everquest Server Emulator
	Copyright (C) 2001-2016 EQEMu Development Team (http://eqemulator.org)

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

/*

	To add a new bot command 3 things must be done:

	1.	At the bottom of bot_command.h you must add a prototype for it.
	2.	Add the function in this file.
	3.	In the bot_command_init function you must add a call to bot_command_add
		for your function.

	Notes: If you want an alias for your bot command, add an entry to the
	`bot_command_settings` table in your database. The access level you
	set with bot_command_add is the default setting if the bot command isn't
	listed in the `bot_command_settings` db table.

*/
