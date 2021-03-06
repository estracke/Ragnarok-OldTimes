// Copyright (c) Athena Dev Teams - Licensed under GNU GPL
// For more information, see LICENCE in the main folder

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include "../common/cbasetypes.h"
#include "../common/malloc.h"
#include "../common/socket.h"
#include "../common/timer.h"
#include "../common/nullpo.h"
#include "../common/showmsg.h"

#include "map.h"
#include "battle.h"
#include "chrif.h"
#include "clif.h"
#include "intif.h"
#include "npc.h"
#include "pc.h"
#include "status.h"
#include "mercenary.h"
#ifndef TXT_ONLY
#include "charsave.h"
#endif
//Updated table (only doc^^) [Sirius]
//Used Packets: U->2af8
//Free Packets: F->2af8

struct dbt *auth_db;

static const int packet_len_table[0x3d] = {
	60, 3,-1,27,10,-1, 6,-1,	// 2af8-2aff: U->2af8, U->2af9, U->2afa, U->2afb, U->2afc, U->2afd, U->2afe, U->2aff
	 6,-1,18, 7,-1,49,30,10,	// 2b00-2b07: U->2b00, U->2b01, U->2b02, U->2b03, U->2b04, U->2b05, U->2b06, U->2b07
	 6,30,-1,10,86, 7,44,34,	// 2b08-2b0f: U->2b08, U->2b09, U->2b0a, U->2b0b, U->2b0c, U->2b0d, U->2b0e, U->2b0f
	 0,-1,10, 6,11,-1, 0, 0,	// 2b10-2b17: U->2b10, U->2b11, U->2b12, U->2b13, U->2b14, U->2b15, U->2b16, U->2b17
	-1,-1,-1,-1,-1,-1, 2, 7,		// 2b18-2b1f: U->2b18, U->2b19, U->2b1a, U->2b1b, U->2b1c, U->2b1d, U->2b1e, U->2b1f
	-1,10, 8,-1,-1,-1,-1,-1,	// 2b20-2b27: U->2b20, U->2b21, U->2b22, U->2b23, F->2b24, F->2b25, F->2b26, F->2b27
};

//Used Packets:
//2af8: Outgoing, chrif_connect -> 'connect to charserver / auth @ charserver'
//2af9: Incomming, chrif_connectack -> 'answer of the 2af8 login(ok / fail)'
//2afa: Outgoing, chrif_sendmap -> 'sending our maps'
//2afb: Incomming, chrif_sendmapack -> 'Maps received successfully / or not ..'
//2afc: Outgoing, chrif_scdata_request -> request sc_data for pc_authok'ed char. <- new command reuses previous one.
//2afd: Incomming, chrif_authok -> 'character selected, add to auth db'
//2afe: Outgoing, send_usercount_tochar -> 'sends player count of this map server to charserver'
//2aff: Outgoing, send_users_tochar -> 'sends all actual connected character ids to charserver'
//2b00: Incomming, map_setusers -> 'set the actual usercount? PACKET.2B COUNT.L.. ?' (not sure)
//2b01: Outgoing, chrif_save -> 'charsave of char XY account XY (complete struct)'
//2b02: Outgoing, chrif_charselectreq -> 'player returns from ingame to charserver to select another char.., this packets includes sessid etc' ? (not 100% sure)
//2b03: Incomming, clif_charselectok -> '' (i think its the packet after enterworld?) (not sure)
//2b04: Incomming, chrif_recvmap -> 'getting maps from charserver of other mapserver's'
//2b05: Outgoing, chrif_changemapserver -> 'Tell the charserver the mapchange / quest for ok...'
//2b06: Incomming, chrif_changemapserverack -> 'awnser of 2b05, ok/fail, data: dunno^^'
//2b07: Incoming, clif_updatemaxid -> Received when updating the max account/char known
//2b08: Outgoing, chrif_searchcharid -> '...'
//2b09: Incomming, map_addchariddb -> 'Adds a name to the nick db'
//2b0a: Outgoing, chrif_changegm -> 'level change of acc/char XY'
//2b0b: Incomming, chrif_changedgm -> 'answer of 2b0a..'
//2b0c: Outgoing, chrif_changeemail -> 'change mail address ...'
//2b0d: Incomming, chrif_changedsex -> 'Change sex of acc XY'
//2b0e: Outgoing, chrif_char_ask_name -> 'Do some operations (change sex, ban / unban etc)'
//2b0f: Incomming, chrif_char_ask_name_answer -> 'answer of the 2b0e'
//2b10: Outgoing, chrif_updatefamelist -> 'Update the fame ranking lists and send them'
//2b11: Outgoing, chrif_changesex -> 'change sex of acc X'
//2b12: Incomming, chrif_divorce -> 'divorce a wedding of charid X and partner id X'
//2b13: Incomming, chrif_accountdeletion -> 'Delete acc XX, if the player is on, kick ....'
//2b14: Incomming, chrif_accountban -> 'not sure: kick the player with message XY'
//2b15: Incomming, chrif_recvgmaccounts -> 'recieve gm accs from charserver (seems to be incomplete !)'
//2b16: Outgoing, chrif_ragsrvinfo -> 'sends motd / rates ....'
//2b17: Outgoing, chrif_char_offline -> 'tell the charserver that the char is now offline'
//2b18: Outgoing, chrif_char_reset_offline -> 'set all players OFF!'
//2b19: Outgoing, chrif_char_online -> 'tell the charserver that the char .. is online'
//2b1a: Outgoing, chrif_buildfamelist -> 'Build the fame ranking lists and send them'
//2b1b: Incomming, chrif_recvfamelist -> 'Receive fame ranking lists'
//2b1c: Outgoing, chrif_save_scdata -> 'Send sc_data of player for saving.'
//2b1d: Incomming, chrif_load_scdata -> 'received sc_data of player for loading.'
//2b1e: Incoming, chrif_update_ip -> 'Reqest forwarded from char-server for interserver IP sync.' [Lance]
//2b1f: Incomming, chrif_disconnectplayer -> 'disconnects a player (aid X) with the message XY ... 0x81 ..' [Sirius]
//2b20: Incomming, chrif_removemap -> 'remove maps of a server (sample: its going offline)' [Sirius]
//2b21: Incomming, chrif_save_ack. Returned after a character has been "final saved" on the char-server. [Skotlex]
//2b22: Incomming, chrif_updatefamelist_ack. Updated one position in the fame list.
//2b24-2b27: FREE

int chrif_connected = 0;
int char_fd = 0; //Using 0 instead of -1 is safer against crashes. [Skotlex]
int srvinfo;
static char char_ip_str[128];
static in_addr_t char_ip= 0;
static int char_port = 6121;
static char userid[NAME_LENGTH], passwd[NAME_LENGTH];
static int chrif_state = 0;
static int char_init_done = 0;
int other_mapserver_count=0; //Holds count of how many other map servers are online (apart of this instance) [Skotlex]

//Interval at which map server updates online listing. [Valaris]
#define CHECK_INTERVAL 3600000
//Interval at which map server sends number of connected users. [Skotlex]
#define UPDATE_INTERVAL 10000
//This define should spare writing the check in every function. [Skotlex]
#define chrif_check(a) { if(!chrif_isconnect()) return a; }

// 設定ファイル読み込み関係
/*==========================================
 *
 *------------------------------------------
 */
void chrif_setuserid(char *id)
{
	memcpy(userid, id, NAME_LENGTH);
}

/*==========================================
 *
 *------------------------------------------
 */
void chrif_setpasswd(char *pwd)
{
	memcpy(passwd, pwd, NAME_LENGTH);
}

/*==========================================
 *
 *------------------------------------------
 */
void chrif_checkdefaultlogin(void)
{
	if (strcmp(userid, "s1")==0 && strcmp(passwd, "p1")==0) {
		ShowError("Using the default user/password s1/p1 is NOT RECOMMENDED.\n");
#ifdef TXT_ONLY
		ShowNotice("Please edit your save/account.txt file to create a proper inter-server user/password (gender 'S')\n");
#else
		ShowNotice("Please edit your 'login' table to create a proper inter-server user/password (gender 'S')\n");
#endif
		ShowNotice("and then edit your user/password in conf/map_athena.conf (or conf/import/map_conf.txt)\n");
	}
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_setip(char *ip)
{
	char ip_str[16];
	char_ip = resolve_hostbyname(ip,NULL,ip_str);

	if (!char_ip) {
		ShowWarning("Failed to Resolve Char Server Address! (%s)\n", ip);
		return 0;
	}
	strncpy(char_ip_str, ip, sizeof(char_ip_str));
	ShowInfo("Char Server IP Address : '"CL_WHITE"%s"CL_RESET"' -> '"CL_WHITE"%s"CL_RESET"'.\n", ip, ip_str);
	return 1;
}

/*==========================================
 *
 *------------------------------------------
 */
void chrif_setport(int port)
{
	char_port = port;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_isconnect(void)
{
	return (char_fd > 0 && session[char_fd] != NULL && chrif_state == 2);
}

/*==========================================
 * Saves char.
 * Flag = 1: Character is quitting.
 * Flag = 2: Character is changing map-servers
 *------------------------------------------
 */
int chrif_save(struct map_session_data *sd, int flag)
{
	nullpo_retr(-1, sd);
	
	pc_makesavestatus(sd);
	if(!chrif_isconnect())
  	{
		if (flag) sd->state.finalsave = 1; //Will save character on reconnect.
		return -1;
	}

	if (sd->state.finalsave)
		return -1; //Refuse to save a char already tagged for final saving. [Skotlex]
	//For data sync
	if (sd->state.storage_flag == 1)
		storage_storage_save(sd->status.account_id, flag);
	else if (sd->state.storage_flag == 2)
		storage_guild_storagesave(sd->status.account_id, sd->status.guild_id, flag);
	if (flag) sd->state.storage_flag = 0; //Force close it.

	//Saving of registry values. 
	if (sd->state.reg_dirty&4)
		intif_saveregistry(sd, 3); //Save char regs
	if (sd->state.reg_dirty&2)
		intif_saveregistry(sd, 2); //Save account regs
	if (sd->state.reg_dirty&1)
		intif_saveregistry(sd, 1); //Save account2 regs
#ifndef TXT_ONLY
	if(charsave_method){ //New 'Local' save
		charsave_savechar(sd->status.char_id, &sd->status);
		if (flag) //Character final saved.
			sd->state.finalsave = 1;
		if (flag == 1)
			chrif_char_offline(sd); //Tell char server that character went offline.
		return 0;	
	}
#endif
	WFIFOHEAD(char_fd, sizeof(sd->status) + 13);
	WFIFOW(char_fd,0) = 0x2b01;
	WFIFOW(char_fd,2) = sizeof(sd->status) + 13;
	WFIFOL(char_fd,4) = sd->status.account_id;
	WFIFOL(char_fd,8) = sd->status.char_id;
	WFIFOB(char_fd,12) = (flag==1)?1:0; //Flag to tell char-server this character is quitting.
	memcpy(WFIFOP(char_fd,13), &sd->status, sizeof(sd->status));
	WFIFOSET(char_fd, WFIFOW(char_fd,2));

	if (sd->hd && merc_is_hom_active(sd->hd))
		merc_save(sd->hd);

	if (flag)
		sd->state.finalsave = 1; //Mark the last save as done.
	return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_connect(int fd)
{
	ShowStatus("Logging in to char server...\n", char_fd);
	WFIFOHEAD(fd, 60);
	WFIFOW(fd,0) = 0x2af8;
	memcpy(WFIFOP(fd,2), userid, NAME_LENGTH);
	memcpy(WFIFOP(fd,26), passwd, NAME_LENGTH);
	WFIFOL(fd,50) = 0;
	WFIFOL(fd,54) = clif_getip_long();
	WFIFOW(fd,58) = clif_getport();	// [Valaris] thanks to fov
	WFIFOSET(fd,60);

	return 0;
}

/*==========================================
 * マップ送信
 *------------------------------------------
 */
int chrif_sendmap(int fd)
{
	int i;
	ShowStatus("Sending maps to char server...\n");
	WFIFOHEAD(fd, 4 + map_num * 4);
	WFIFOW(fd,0) = 0x2afa;
	for(i = 0; i < map_num; i++)
		WFIFOW(fd,4+i*4) = map[i].index;
	WFIFOW(fd,2) = 4 + i * 4;
	WFIFOSET(fd,WFIFOW(fd,2));

	return 0;
}

/*==========================================
 * マップ受信
 *------------------------------------------
 */
int chrif_recvmap(int fd)
{
	int i, j, ip, port;
	unsigned char *p = (unsigned char *)&ip;
	RFIFOHEAD(fd);
	ip = RFIFOL(fd,4);
	port = RFIFOW(fd,8);
	for(i = 10, j = 0; i < RFIFOW(fd,2); i += 4, j++) {
		map_setipport(RFIFOW(fd,i), ip, port);
//		if (battle_config.etc_log)
//			printf("recv map %d %s\n", j, RFIFOP(fd,i));
	}
	if (battle_config.etc_log)
		ShowStatus("recv map on %d.%d.%d.%d:%d (%d maps)\n", p[0], p[1], p[2], p[3], port, j);

	other_mapserver_count++;
	return 0;
}

/*==========================================
 * Delete maps of other servers, (if an other mapserver is going OFF)
 *------------------------------------------
 */
int chrif_removemap(int fd){
	int i, j, ip, port;
	unsigned char *p = (unsigned char *)&ip;
	RFIFOHEAD(fd);

	ip = RFIFOL(fd, 4);
	port = RFIFOW(fd, 8);

	for(i = 10, j = 0; i < RFIFOW(fd, 2); i += 4, j++){
		map_eraseipport(RFIFOW(fd, i), ip, port);
	}

	other_mapserver_count--;
	if(battle_config.etc_log)
		ShowStatus("remove map of server %d.%d.%d.%d:%d (%d maps)\n", p[0], p[1], p[2], p[3], port, j);
	return 0;
}

int chrif_save_ack(int fd) {
	struct map_session_data *sd;
	RFIFOHEAD(fd);
	sd = map_id2sd(RFIFOL(fd,2));

	if (sd && sd->status.char_id == RFIFOL(fd,6))
		map_quit_ack(sd);
	return 0;
}

/*==========================================
 * マップ鯖間移動のためのデータ準備要求
 *------------------------------------------
 */
int chrif_changemapserver(struct map_session_data *sd, short map, int x, int y, int ip, short port)
{
	int s_ip;

	nullpo_retr(-1, sd);

	chrif_check(-1);

	if (other_mapserver_count < 1)
	{	//No other map servers are online!
		clif_authfail_fd(sd->fd, 0);
		return -1;
	}

	if (sd->fd && sd->fd < fd_max && session[sd->fd])
		s_ip = session[sd->fd]->client_addr.sin_addr.s_addr;
	else //Not connected? Can't retrieve IP
		s_ip = 0;

	WFIFOHEAD(char_fd, 35);
	WFIFOW(char_fd, 0) = 0x2b05;
	WFIFOL(char_fd, 2) = sd->bl.id;
	WFIFOL(char_fd, 6) = sd->login_id1;
	WFIFOL(char_fd,10) = sd->login_id2;
	WFIFOL(char_fd,14) = sd->status.char_id;
	WFIFOW(char_fd,18) = map;
	WFIFOW(char_fd,20) = x;
	WFIFOW(char_fd,22) = y;
	WFIFOL(char_fd,24) = ip;
	WFIFOW(char_fd,28) = port;
	WFIFOB(char_fd,30) = sd->status.sex;
	WFIFOL(char_fd,31) = s_ip;
	WFIFOSET(char_fd,35);

	return 0;
}

/*==========================================
 * マップ鯖間移動ack
 *------------------------------------------
 */
int chrif_changemapserverack(int fd)
{
	struct map_session_data *sd;
	RFIFOHEAD(fd);
	sd = map_id2sd(RFIFOL(fd,2));

	if (sd == NULL || sd->status.char_id != RFIFOL(fd,14))
		return -1;

	if (RFIFOL(fd,6) == 1) {
		if (battle_config.error_log)
			ShowError("map server change failed.\n");
		clif_authfail_fd(sd->fd, 0);
		return 0;
	}
	clif_changemapserver(sd, (char*)mapindex_id2name(RFIFOW(fd,18)), RFIFOW(fd,20), RFIFOW(fd,22), RFIFOL(fd,24), RFIFOW(fd,28));

	//Player has been saved already, remove him from memory. [Skotlex]	
	map_quit(sd);
	map_quit_ack(sd);
	return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_connectack(int fd)
{
	RFIFOHEAD(fd);
	if (RFIFOB(fd,2)) {
		ShowFatalError("Connection to char-server failed %d.\n", RFIFOB(fd,2));
		exit(1);
	}
	ShowStatus("Successfully logged on to Char Server (Connection: '"CL_WHITE"%d"CL_RESET"').\n",fd);
	chrif_state = 1;
	chrif_connected=1;

	chrif_sendmap(fd);

	ShowStatus("Event '"CL_WHITE"OnCharIfInit"CL_RESET"' executed with '"CL_WHITE"%d"CL_RESET"' NPCs.\n", npc_event_doall("OnCharIfInit"));
	ShowStatus("Event '"CL_WHITE"OnInterIfInit"CL_RESET"' executed with '"CL_WHITE"%d"CL_RESET"' NPCs.\n", npc_event_doall("OnInterIfInit"));
	if(!char_init_done) {
		char_init_done = 1;
		ShowStatus("Event '"CL_WHITE"OnInterIfInitOnce"CL_RESET"' executed with '"CL_WHITE"%d"CL_RESET"' NPCs.\n", npc_event_doall("OnInterIfInitOnce"));
	}

	return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_sendmapack(int fd)
{
	RFIFOHEAD(fd);
	if (RFIFOB(fd,2)) {
		ShowFatalError("chrif : send map list to char server failed %d\n", RFIFOB(fd,2));
		exit(1);
	}
	memcpy(wisp_server_name, RFIFOP(fd,3), NAME_LENGTH);
	ShowStatus("Map sending complete. Map Server is now online.\n");
	chrif_state = 2;

	//If there are players online, send them to the char-server. [Skotlex]
	send_users_tochar(-1, gettick(), 0, 0);
	
	//Re-save any storages that were modified in the disconnection time. [Skotlex]
	do_reconnect_map();
	do_reconnect_storage();

	return 0;
}

/*==========================================
 * Request sc_data from charserver [Skotlex]
 *------------------------------------------
 */
int chrif_scdata_request(int account_id, int char_id)
{
#ifdef ENABLE_SC_SAVING
#ifndef TXT_ONLY
	if (charsave_method)
		return charsave_load_scdata(account_id, char_id);
#endif
	chrif_check(-1);

	WFIFOHEAD(char_fd, 10);
	WFIFOW(char_fd, 0) = 0x2afc;
	WFIFOL(char_fd, 2) = account_id;
	WFIFOL(char_fd, 6) = char_id;
	WFIFOSET(char_fd,10);
#endif
	return 0;
}

/*==========================================
 * new auth system [Kevin]
 *------------------------------------------
 */
void chrif_authreq(struct map_session_data *sd)
{
	struct auth_node *auth_data;
	auth_data=idb_get(auth_db, sd->bl.id);

	if(auth_data) {
		if(auth_data->char_dat &&
			auth_data->account_id== sd->bl.id &&
			auth_data->login_id1 == sd->login_id1)
		{	//auth ok
			pc_authok(sd, auth_data->login_id2, auth_data->connect_until_time, auth_data->char_dat);
		} else { //auth failed
			pc_authfail(sd);
			chrif_char_offline(sd); //Set him offline, the char server likely has it set as online already.
		}
		if (auth_data->char_dat)
			aFree(auth_data->char_dat);
		idb_remove(auth_db, sd->bl.id);
	} else { //data from char server has not arrived yet.
		auth_data = aCalloc(1,sizeof(struct auth_node));
		auth_data->sd = sd;
		auth_data->fd = sd->fd;
		auth_data->account_id = sd->bl.id;
		auth_data->login_id1 = sd->login_id1;
		auth_data->node_created = gettick();
		uidb_put(auth_db, sd->bl.id, auth_data);
	}
	return;
}

//character selected, insert into auth db
void chrif_authok(int fd) {
	struct auth_node *auth_data;
	RFIFOHEAD(fd);
	
	if ((auth_data =uidb_get(auth_db, RFIFOL(fd, 4))) != NULL)
	{	//Is the character already awaiting authorization?
		if (auth_data->sd)
		{
			//First, check to see if the session data still exists (avoid dangling pointers)
			if(session[auth_data->fd] && session[auth_data->fd]->session_data == auth_data->sd)
			{	
				if (auth_data->char_dat == NULL &&
					auth_data->account_id == RFIFOL(fd, 4) &&
					auth_data->login_id1 == RFIFOL(fd, 8))
				{ //Auth Ok
					pc_authok(auth_data->sd, RFIFOL(fd, 16), RFIFOL(fd, 12), (struct mmo_charstatus*)RFIFOP(fd, 20));
				} else { //Auth Failed
					pc_authfail(auth_data->sd);
					chrif_char_offline(auth_data->sd); //Set him offline, the char server likely has it set as online already.
				}
			} //else: Character no longer exists, just go through.
		}
		//Delete the data of this node...
		if (auth_data->char_dat)
			aFree (auth_data->char_dat);
		uidb_remove(auth_db, RFIFOL(fd, 4));
		return;
	}
	// Awaiting for client to connect.
	auth_data = (struct auth_node *)aCalloc(1,sizeof(struct auth_node));
	auth_data->char_dat = (struct mmo_charstatus *) aCalloc(1,sizeof(struct mmo_charstatus));

	auth_data->account_id=RFIFOL(fd, 4);
	auth_data->login_id1=RFIFOL(fd, 8);
	auth_data->connect_until_time=RFIFOL(fd, 12);
	auth_data->login_id2=RFIFOL(fd, 16);
	memcpy(auth_data->char_dat,RFIFOP(fd, 20),sizeof(struct mmo_charstatus));
	auth_data->node_created=gettick();
	uidb_put(auth_db, RFIFOL(fd, 4), auth_data);
}

int auth_db_cleanup_sub(DBKey key,void *data,va_list ap)
{
	struct auth_node *node=(struct auth_node*)data;

	if(DIFF_TICK(gettick(),node->node_created)>30000) {
		ShowNotice("Character (aid: %d) not authed within 30 seconds of character select!\n", node->account_id);
		if (node->char_dat)
			aFree(node->char_dat);
		db_remove(auth_db, key);
		return 1;
	}
	return 0;
}

int auth_db_cleanup(int tid, unsigned int tick, int id, int data) {
	auth_db->foreach(auth_db, auth_db_cleanup_sub);
	return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_charselectreq(struct map_session_data *sd, unsigned long s_ip)
{
	nullpo_retr(-1, sd);

	if( !sd || !sd->bl.id || !sd->login_id1 )
		return -1;
	chrif_check(-1);

	WFIFOHEAD(char_fd, 18);
	WFIFOW(char_fd, 0) = 0x2b02;
	WFIFOL(char_fd, 2) = sd->bl.id;
	WFIFOL(char_fd, 6) = sd->login_id1;
	WFIFOL(char_fd,10) = sd->login_id2;
	WFIFOL(char_fd,14) = s_ip;
	WFIFOSET(char_fd,18);

	return 0;
}

/*==========================================
 * キャラ名問い合わせ
 *------------------------------------------
 */
int chrif_searchcharid(int char_id)
{
	if( !char_id )
		return -1;
	chrif_check(-1);

	WFIFOHEAD(char_fd, 6);
	WFIFOW(char_fd,0) = 0x2b08;
	WFIFOL(char_fd,2) = char_id;
	WFIFOSET(char_fd,6);

	return 0;
}

/*==========================================
 * GMに変化要求
 *------------------------------------------
 */
int chrif_changegm(int id, const char *pass, int len)
{
	if (battle_config.etc_log)
		ShowInfo("chrif_changegm: account: %d, password: '%s'.\n", id, pass);

	chrif_check(-1);

	WFIFOHEAD(char_fd, len + 8);
	WFIFOW(char_fd,0) = 0x2b0a;
	WFIFOW(char_fd,2) = len + 8;
	WFIFOL(char_fd,4) = id;
	memcpy(WFIFOP(char_fd,8), pass, len);
	WFIFOSET(char_fd, len + 8);

	return 0;
}

/*==========================================
 * Change Email
 *------------------------------------------
 */
int chrif_changeemail(int id, const char *actual_email, const char *new_email)
{
	if (battle_config.etc_log)
		ShowInfo("chrif_changeemail: account: %d, actual_email: '%s', new_email: '%s'.\n", id, actual_email, new_email);

	chrif_check(-1);

        WFIFOHEAD(char_fd, 86);
	WFIFOW(char_fd,0) = 0x2b0c;
	WFIFOL(char_fd,2) = id;
	memcpy(WFIFOP(char_fd,6), actual_email, 40);
	memcpy(WFIFOP(char_fd,46), new_email, 40);
	WFIFOSET(char_fd,86);

	return 0;
}

/*==========================================
 * Send message to char-server with a character name to do some operations (by Yor)
 * Used to ask Char-server about a character name to have the account number to modify account file in login-server.
 * type of operation:
 *   1: block
 *   2: ban
 *   3: unblock
 *   4: unban
 *   5: changesex
 *------------------------------------------
 */
int chrif_char_ask_name(int id, char * character_name, short operation_type, int year, int month, int day, int hour, int minute, int second)
{
	chrif_check(-1);

	WFIFOHEAD(char_fd, 44);
	WFIFOW(char_fd, 0) = 0x2b0e;
	WFIFOL(char_fd, 2) = id; // account_id of who ask (for answer) -1 if nobody
	memcpy(WFIFOP(char_fd,6), character_name, NAME_LENGTH);
	WFIFOW(char_fd, 30) = operation_type; // type of operation
	if (operation_type == 2) {
		WFIFOW(char_fd, 32) = year;
		WFIFOW(char_fd, 34) = month;
		WFIFOW(char_fd, 36) = day;
		WFIFOW(char_fd, 38) = hour;
		WFIFOW(char_fd, 40) = minute;
		WFIFOW(char_fd, 42) = second;
	}
//	ShowInfo("chrif : sent 0x2b0e\n");
	WFIFOSET(char_fd,44);

	return 0;
}

/*==========================================
 * 性別変化要求
 *------------------------------------------
 */
int chrif_changesex(int id, int sex) {
	chrif_check(-1);

        WFIFOHEAD(char_fd, 9);
	WFIFOW(char_fd,0) = 0x2b11;
	WFIFOW(char_fd,2) = 9;
	WFIFOL(char_fd,4) = id;
	WFIFOB(char_fd,8) = sex;
//	ShowInfo("chrif : sent 0x3000(changesex)\n");
	WFIFOSET(char_fd,9);
	return 0;
}

/*==========================================
 * Answer after a request about a character name to do some operations (by Yor)
 * Used to answer of chrif_char_ask_name.
 * type of operation:
 *   1: block
 *   2: ban
 *   3: unblock
 *   4: unban
 *   5: changesex
 * type of answer:
 *   0: login-server resquest done
 *   1: player not found
 *   2: gm level too low
 *   3: login-server offline
 *------------------------------------------
 */
int chrif_char_ask_name_answer(int fd)
{
	int acc;
	struct map_session_data *sd;
	char output[256];
	char player_name[NAME_LENGTH];
	RFIFOHEAD(fd);

	acc = RFIFOL(fd,2); // account_id of who has asked (-1 if nobody)
	memcpy(player_name, RFIFOP(fd,6), NAME_LENGTH-1);
	player_name[NAME_LENGTH-1] = '\0';

	sd = map_id2sd(acc);
	if (acc >= 0 && sd != NULL) {
		if (RFIFOW(fd, 32) == 1) // player not found
			sprintf(output, "The player '%s' doesn't exist.", player_name);
		else {
			switch(RFIFOW(fd, 30)) {
			case 1: // block
				switch(RFIFOW(fd, 32)) {
				case 0: // login-server resquest done
					sprintf(output, "Login-server has been asked to block the player '%s'.", player_name);
					break;
				//case 1: // player not found
				case 2: // gm level too low
					sprintf(output, "Your GM level don't authorise you to block the player '%s'.", player_name);
					break;
				case 3: // login-server offline
					sprintf(output, "Login-server is offline. Impossible to block the the player '%s'.", player_name);
					break;
				}
				break;
			case 2: // ban
				switch(RFIFOW(fd, 32)) {
				case 0: // login-server resquest done
					sprintf(output, "Login-server has been asked to ban the player '%s'.", player_name);
					break;
				//case 1: // player not found
				case 2: // gm level too low
					sprintf(output, "Your GM level don't authorise you to ban the player '%s'.", player_name);
					break;
				case 3: // login-server offline
					sprintf(output, "Login-server is offline. Impossible to ban the the player '%s'.", player_name);
					break;
				}
				break;
			case 3: // unblock
				switch(RFIFOW(fd, 32)) {
				case 0: // login-server resquest done
					sprintf(output, "Login-server has been asked to unblock the player '%s'.", player_name);
					break;
				//case 1: // player not found
				case 2: // gm level too low
					sprintf(output, "Your GM level don't authorise you to unblock the player '%s'.", player_name);
					break;
				case 3: // login-server offline
					sprintf(output, "Login-server is offline. Impossible to unblock the the player '%s'.", player_name);
					break;
				}
				break;
			case 4: // unban
				switch(RFIFOW(fd, 32)) {
				case 0: // login-server resquest done
					sprintf(output, "Login-server has been asked to unban the player '%s'.", player_name);
					break;
				//case 1: // player not found
				case 2: // gm level too low
					sprintf(output, "Your GM level don't authorise you to unban the player '%s'.", player_name);
					break;
				case 3: // login-server offline
					sprintf(output, "Login-server is offline. Impossible to unban the the player '%s'.", player_name);
					break;
				}
				break;
			case 5: // changesex
				switch(RFIFOW(fd, 32)) {
				case 0: // login-server resquest done
					sprintf(output, "Login-server has been asked to change the sex of the player '%s'.", player_name);
					break;
				//case 1: // player not found
				case 2: // gm level too low
					sprintf(output, "Your GM level don't authorise you to change the sex of the player '%s'.", player_name);
					break;
				case 3: // login-server offline
					sprintf(output, "Login-server is offline. Impossible to change the sex of the the player '%s'.", player_name);
					break;
				}
				break;
			}
		}
		if (output[0] != '\0') {
			output[sizeof(output)-1] = '\0';
			clif_displaymessage(sd->fd, output);
		}
	} else
		ShowError("chrif_char_ask_name_answer failed - player not online.\n");

	return 0;
}

/*==========================================
 * End of GM change (@GM) (modified by Yor)
 *------------------------------------------
 */
int chrif_changedgm(int fd)
{
	int acc, level;
	struct map_session_data *sd = NULL;
	RFIFOHEAD(fd);

	acc = RFIFOL(fd,2);
	level = RFIFOL(fd,6);

	sd = map_id2sd(acc);

	if (battle_config.etc_log)
		ShowNotice("chrif_changedgm: account: %d, GM level 0 -> %d.\n", acc, level);
	if (sd != NULL) {
		if (level > 0)
			clif_displaymessage(sd->fd, "GM modification success.");
		else
			clif_displaymessage(sd->fd, "Failure of GM modification.");
	}

	return 0;
}

/*==========================================
 * 性別変化終了 (modified by Yor)
 *------------------------------------------
 */
int chrif_changedsex(int fd)
{
	int acc, sex, i;
	struct map_session_data *sd;
	RFIFOHEAD(fd);

	acc = RFIFOL(fd,2);
	sex = RFIFOL(fd,6);
	if (battle_config.etc_log)
		ShowNotice("chrif_changedsex %d.\n", acc);
	sd = map_id2sd(acc);
	if (acc > 0) {
		if (sd != NULL && sd->status.sex != sex) {
			sd->status.sex = !sd->status.sex;

			// to avoid any problem with equipment and invalid sex, equipment is unequiped.
			for (i = 0; i < MAX_INVENTORY; i++) {
				if (sd->status.inventory[i].nameid && sd->status.inventory[i].equip)
					pc_unequipitem((struct map_session_data*)sd, i, 2);
			}
			// reset skill of some job
			if ((sd->class_&MAPID_UPPERMASK) == MAPID_BARDDANCER) {
				// remove specifical skills of Bard classes 
				for(i = 315; i <= 322; i++) {
					if (sd->status.skill[i].id > 0 && !sd->status.skill[i].flag) {
						if (sd->status.skill_point > USHRT_MAX - sd->status.skill[i].lv)
							sd->status.skill_point = USHRT_MAX;
						else
							sd->status.skill_point += sd->status.skill[i].lv;
						sd->status.skill[i].id = 0;
						sd->status.skill[i].lv = 0;
					}
				}
				// remove specifical skills of Dancer classes 
				for(i = 323; i <= 330; i++) {
					if (sd->status.skill[i].id > 0 && !sd->status.skill[i].flag) {
						if (sd->status.skill_point > USHRT_MAX - sd->status.skill[i].lv)
							sd->status.skill_point = USHRT_MAX;
						else
							sd->status.skill_point += sd->status.skill[i].lv;
						sd->status.skill[i].id = 0;
						sd->status.skill[i].lv = 0;
					}
				}
				clif_updatestatus(sd, SP_SKILLPOINT);
				// change job if necessary
				if (sd->status.sex) //Changed from Dancer
					sd->status.class_ -= 1;
				else	//Changed from Bard
					sd->status.class_ += 1;
				//sd->class_ needs not be updated as both Dancer/Bard are the same.
			}
			// save character
			//chrif_save(sd,1); Character will be saved on session closed -> map_quit
			sd->login_id1++; // change identify, because if player come back in char within the 5 seconds, he can change its characters
			                 // do same modify in login-server for the account, but no in char-server (it ask again login_id1 to login, and don't remember it)
			clif_displaymessage(sd->fd, "Your sex has been changed (need disconnection by the server)...");
			clif_setwaitclose(sd->fd); // forced to disconnect for the change
		}
	} else {
		if (sd != NULL) {
			ShowError("chrif_changedsex failed.\n");
		}
	}

	return 0;
}

/*==========================================
 * 離婚情報同期要求
 *------------------------------------------
 */
int chrif_divorce(int char_id, int partner_id)
{
	struct map_session_data *sd = NULL;

	if (!char_id || !partner_id)
		return 0;

	nullpo_retr(0, sd = map_charid2sd(partner_id));
	if (sd->status.partner_id == char_id) {
		int i;
		//離婚(相方は既にキャラが消えている筈なので)
		sd->status.partner_id = 0;

		//相方の結婚指輪を剥奪
		for(i = 0; i < MAX_INVENTORY; i++)
			if (sd->status.inventory[i].nameid == WEDDING_RING_M || sd->status.inventory[i].nameid == WEDDING_RING_F)
				pc_delitem(sd, i, 1, 0);
	}

	return 0;
}

/*==========================================
 * Disconnection of a player (account has been deleted in login-server) by [Yor]
 *------------------------------------------
 */
int chrif_accountdeletion(int fd)
{
	int acc;
	struct map_session_data *sd;
	RFIFOHEAD(fd);

	acc = RFIFOL(fd,2);
	if (battle_config.etc_log)
		ShowNotice("chrif_accountdeletion %d.\n", acc);
	sd = map_id2sd(acc);
	if (acc > 0) {
		if (sd != NULL) {
			sd->login_id1++; // change identify, because if player come back in char within the 5 seconds, he can change its characters
			clif_displaymessage(sd->fd, "Your account has been deleted (disconnection)...");
			clif_setwaitclose(sd->fd); // forced to disconnect for the change
		}
	} else {
		if (sd != NULL)
			ShowError("chrif_accountdeletion failed - player not online.\n");
	}

	return 0;
}

/*==========================================
 * Disconnection of a player (account has been banned of has a status, from login-server) by [Yor]
 *------------------------------------------
 */
int chrif_accountban(int fd)
{
	int acc;
	struct map_session_data *sd;
	RFIFOHEAD(fd);

	acc = RFIFOL(fd,2);
	if (battle_config.etc_log)
		ShowNotice("chrif_accountban %d.\n", acc);
	sd = map_id2sd(acc);
	if (acc > 0) {
		if (sd != NULL) {
			sd->login_id1++; // change identify, because if player come back in char within the 5 seconds, he can change its characters
			if (RFIFOB(fd,6) == 0) { // 0: change of statut, 1: ban
				switch (RFIFOL(fd,7)) { // status or final date of a banishment
				case 1:   // 0 = Unregistered ID
					clif_displaymessage(sd->fd, "Your account has 'Unregistered'.");
					break;
				case 2:   // 1 = Incorrect Password
					clif_displaymessage(sd->fd, "Your account has an 'Incorrect Password'...");
					break;
				case 3:   // 2 = This ID is expired
					clif_displaymessage(sd->fd, "Your account has expired.");
					break;
				case 4:   // 3 = Rejected from Server
					clif_displaymessage(sd->fd, "Your account has been rejected from server.");
					break;
				case 5:   // 4 = You have been blocked by the GM Team
					clif_displaymessage(sd->fd, "Your account has been blocked by the GM Team.");
					break;
				case 6:   // 5 = Your Game's EXE file is not the latest version
					clif_displaymessage(sd->fd, "Your Game's EXE file is not the latest version.");
					break;
				case 7:   // 6 = Your are Prohibited to log in until %s
					clif_displaymessage(sd->fd, "Your account has been prohibited to log in.");
					break;
				case 8:   // 7 = Server is jammed due to over populated
					clif_displaymessage(sd->fd, "Server is jammed due to over populated.");
					break;
				case 9:   // 8 = No MSG (actually, all states after 9 except 99 are No MSG, use only this)
					clif_displaymessage(sd->fd, "Your account has not more authorised.");
					break;
				case 100: // 99 = This ID has been totally erased
					clif_displaymessage(sd->fd, "Your account has been totally erased.");
					break;
				default:
					clif_displaymessage(sd->fd, "Your account has not more authorised.");
					break;
				}
			} else if (RFIFOB(fd,6) == 1) { // 0: change of statut, 1: ban
				time_t timestamp;
				char tmpstr[2048];
				timestamp = (time_t)RFIFOL(fd,7); // status or final date of a banishment
				strcpy(tmpstr, "Your account has been banished until ");
				strftime(tmpstr + strlen(tmpstr), 24, "%d-%m-%Y %H:%M:%S", localtime(&timestamp));
				clif_displaymessage(sd->fd, tmpstr);
			}
			clif_setwaitclose(sd->fd); // forced to disconnect for the change
		}
	} else {
		if (sd != NULL)
			ShowError("chrif_accountban failed - player not online.\n");
	}

	return 0;
}

//Disconnect the player out of the game, simple packet
//packet.w AID.L WHY.B 2+4+1 = 7byte
int chrif_disconnectplayer(int fd){
	struct map_session_data *sd;
	RFIFOHEAD(fd);

	sd = map_id2sd(RFIFOL(fd, 2));

	if(sd == NULL){
		return -1;
	}

	if (!sd->fd)
	{	//No connection
		if (sd->state.autotrade)
			map_quit(sd); //Remove it.
		//Else we don't remove it because the char should have a timer to remove the player because it force-quit before,
		//and we don't want them kicking their previous instance before the 10 secs penalty time passes. [Skotlex]
		return 0;
	}

	switch(RFIFOB(fd, 6)){
		//clif_authfail_fd
		case 1: //server closed
			clif_authfail_fd(sd->fd, 1);
		break;

		case 2: //someone else logged in
			clif_authfail_fd(sd->fd, 2);
		break;

		case 3: //server overpopulated
			clif_authfail_fd(sd->fd, 4);
		break;

		case 4: //out of time payd for .. (avail)
			clif_authfail_fd(sd->fd, 10);
		break;

		case 5: //forced to dc by gm
			clif_authfail_fd(sd->fd, 15);
		break;
	}
	return 0;
}

/*==========================================
 * Request to reload GM accounts and their levels: send to char-server by [Yor]
 *------------------------------------------
 */
int chrif_reloadGMdb(void)
{
	chrif_check(-1);

	WFIFOHEAD(char_fd, 2);
	WFIFOW(char_fd,0) = 0x2af7;
	WFIFOSET(char_fd, 2);

	return 0;
}

/*==========================================
 * Receiving GM accounts and their levels from char-server by [Yor]
 *------------------------------------------
 */
int chrif_recvgmaccounts(int fd)
{
	ShowInfo("From login-server: receiving information of '"CL_WHITE"%d"CL_RESET"' GM accounts.\n", pc_read_gm_account(fd));
	return 0;
}

/*==========================================
 * Request/Receive top 10 Fame character list
 *------------------------------------------
 */

int chrif_updatefamelist(struct map_session_data *sd)
{
	char type;
	chrif_check(-1);

	switch(sd->class_&MAPID_UPPERMASK) {
		case MAPID_BLACKSMITH:
			type = 1;
			break;
		case MAPID_ALCHEMIST:
			type = 2;
			break;
		case MAPID_TAEKWON:
			type = 3;
			break;
		default:
			return 0;
	}

	WFIFOHEAD(char_fd, 12);
	WFIFOW(char_fd, 0) = 0x2b10;
	WFIFOL(char_fd, 2) = sd->status.char_id;
	WFIFOL(char_fd, 6) = sd->status.fame;
	WFIFOB(char_fd, 10) = type;
	WFIFOB(char_fd, 11) = pc_famerank(sd->status.char_id, sd->class_&MAPID_UPPERMASK);
	WFIFOSET(char_fd, 12);

	return 0;
}

int chrif_buildfamelist(void)
{
	chrif_check(-1);

	WFIFOHEAD(char_fd, 2);
	WFIFOW(char_fd, 0) = 0x2b1a;
	WFIFOSET(char_fd, 2);

	return 0;
}

int chrif_recvfamelist(int fd)
{
	int num, size;
	int total = 0, len = 8;
	RFIFOHEAD(fd);

	memset (smith_fame_list, 0, sizeof(smith_fame_list));
	memset (chemist_fame_list, 0, sizeof(chemist_fame_list));
	memset (taekwon_fame_list, 0, sizeof(taekwon_fame_list));

	size = RFIFOW(fd, 6); //Blacksmith block size
	for (num = 0; len < size && num < MAX_FAME_LIST; num++) {
		memcpy(&smith_fame_list[num], RFIFOP(fd,len), sizeof(struct fame_list));
 		len += sizeof(struct fame_list);
	}
	total += num;

	size = RFIFOW(fd, 4); //Alchemist block size
	for (num = 0; len < size && num < MAX_FAME_LIST; num++) {
		memcpy(&chemist_fame_list[num], RFIFOP(fd,len), sizeof(struct fame_list));
 		len += sizeof(struct fame_list);
	}
	total += num;

	size = RFIFOW(fd, 2); //Total packet length
	for (num = 0; len < size && num < MAX_FAME_LIST; num++) {
		memcpy(&taekwon_fame_list[num], RFIFOP(fd,len), sizeof(struct fame_list));
 		len += sizeof(struct fame_list);
	}
	total += num;

	ShowInfo("Received Fame List of '"CL_WHITE"%d"CL_RESET"' characters.\n", total);

	return 0;
}

int chrif_updatefamelist_ack(int fd)
{
	struct fame_list *list;
	char index;
	RFIFOHEAD(fd);
	switch (RFIFOB(fd, 2))
	{
		case 1:
			list = smith_fame_list;
			break;
		case 2:
			list = chemist_fame_list;
			break;
		case 3:
			list = taekwon_fame_list;
			break;
		default:
			return 0;
	}
	index = RFIFOB(fd, 3);
	if (index < 0 || index >= MAX_FAME_LIST)
		return 0;
	list[(int)index].fame = RFIFOL(fd, 4);
	return 1;
}

int chrif_save_scdata(struct map_session_data *sd)
{	//parses the sc_data of the player and sends it to the char-server for saving. [Skotlex]
#ifdef ENABLE_SC_SAVING
	int i, count=0;
	unsigned int tick;
	struct status_change_data data;
	struct TimerData *timer;

	if (sd->state.finalsave) //Character was already saved?
		return -1;
#ifndef TXT_ONLY
	if(charsave_method) //New 'Local' save
	{
		charsave_save_scdata(sd->status.account_id, sd->status.char_id, &sd->sc, MAX_STATUSCHANGE);
		return 0;
	}
#endif
	
	chrif_check(-1);
	tick = gettick();
	
	WFIFOHEAD(char_fd, 14 + SC_MAX*sizeof(struct status_change_data));
	WFIFOW(char_fd,0) = 0x2b1c;
	WFIFOL(char_fd,4) = sd->status.account_id;
	WFIFOL(char_fd,8) = sd->status.char_id;
	for (i = 0; i < SC_MAX; i++)
	{
		if (sd->sc.data[i].timer == -1)
			continue;
		timer = get_timer(sd->sc.data[i].timer);
		if (timer == NULL || timer->func != status_change_timer || DIFF_TICK(timer->tick,tick) < 0)
			continue;
		data.tick = DIFF_TICK(timer->tick,tick); //Duration that is left before ending.
		data.type = i;
		data.val1 = sd->sc.data[i].val1;
		data.val2 = sd->sc.data[i].val2;
		data.val3 = sd->sc.data[i].val3;
		data.val4 = sd->sc.data[i].val4;
		memcpy(WFIFOP(char_fd,14 +count*sizeof(struct status_change_data)),
			&data, sizeof(struct status_change_data));
		count++;
	}
	if (count == 0)
		return 0; //Nothing to save.
	WFIFOW(char_fd,12) = count;
	WFIFOW(char_fd,2) = 14 +count*sizeof(struct status_change_data); //Total packet size
	WFIFOSET(char_fd,WFIFOW(char_fd,2));
#endif
	return 0;
}

int chrif_load_scdata(int fd)
{	//Retrieve and load sc_data for a player. [Skotlex]
#ifdef ENABLE_SC_SAVING
	struct map_session_data *sd;
	struct status_change_data *data;
	int aid, cid, i, count;
	RFIFOHEAD(fd);

	aid = RFIFOL(fd,4); //Player Account ID
	cid = RFIFOL(fd,8); //Player Char ID
	
	sd = map_id2sd(aid);
	if (!sd)
	{
		ShowError("chrif_load_scdata: Player of AID %d not found!\n", aid);
		return -1;
	}
	if (sd->status.char_id != cid)
	{
		ShowError("chrif_load_scdata: Receiving data for account %d, char id does not matches (%d != %d)!\n", aid, sd->status.char_id, cid);
		return -1;
	}
	count = RFIFOW(fd,12); //sc_count
	for (i = 0; i < count; i++)
	{
		data = (struct status_change_data*)RFIFOP(fd,14 + i*sizeof(struct status_change_data));
		if (data->tick < 1)
		{	//Protection against invalid tick values. [Skotlex]
			ShowWarning("chrif_load_scdata: Received invalid duration (%d ms) for status change %d (character %s)\n", data->tick, data->type, sd->status.name);
			continue;
		}
		status_change_start(&sd->bl, data->type, 10000, data->val1, data->val2, data->val3, data->val4, data->tick, 15);
	}
#endif
	return 0;
}

/*==========================================
 * Send rates and motd to char server [Wizputer]
 *------------------------------------------
 */
 int chrif_ragsrvinfo(int base_rate, int job_rate, int drop_rate)
{
	char buf[256];
	FILE *fp;
	int i;

	chrif_check(-1);

	WFIFOHEAD(char_fd, sizeof(buf) + 10);
	WFIFOW(char_fd,0) = 0x2b16;
	WFIFOW(char_fd,2) = base_rate;
	WFIFOW(char_fd,4) = job_rate;
	WFIFOW(char_fd,6) = drop_rate;

	if ((fp = fopen(motd_txt, "r")) != NULL) {
		if (fgets(buf, 250, fp) != NULL) {
			for(i = 0; buf[i]; i++) {
				if (buf[i] == '\r' || buf[i] == '\n') {
					buf[i] = 0;
					break;
				}
			}
			WFIFOW(char_fd,8) = sizeof(buf) + 10;
			memcpy(WFIFOP(char_fd,10), buf, sizeof(buf));
		}
		fclose(fp);
	} else {
		memset(buf, 0, sizeof(buf)); //No data found, send empty packets?
		WFIFOW(char_fd,8) = sizeof(buf) + 10;
		memcpy(WFIFOP(char_fd,10), buf, sizeof(buf));
	}
	WFIFOSET(char_fd,WFIFOW(char_fd,8));
	return 0;
}


/*=========================================
 * Tell char-server charcter disconnected [Wizputer]
 *-----------------------------------------
 */

int chrif_char_offline(struct map_session_data *sd)
{
	chrif_check(-1);

	WFIFOHEAD(char_fd, 10);
	WFIFOW(char_fd,0) = 0x2b17;
	WFIFOL(char_fd,2) = sd->status.char_id;
	WFIFOL(char_fd,6) = sd->status.account_id;
	WFIFOSET(char_fd,10);

	return 0;
}

/*=========================================
 * Tell char-server to reset all chars offline [Wizputer]
 *-----------------------------------------
 */
int chrif_flush_fifo(void) {
	chrif_check(-1);

	set_nonblocking(char_fd, 0);
	flush_fifos();
	set_nonblocking(char_fd, 1);

	return 0;
}

/*=========================================
 * Tell char-server to reset all chars offline [Wizputer]
 *-----------------------------------------
 */
int chrif_char_reset_offline(void) {
	chrif_check(-1);

	WFIFOHEAD(char_fd, 2);
	WFIFOW(char_fd,0) = 0x2b18;
	WFIFOSET(char_fd,2);

	return 0;
}

/*=========================================
 * Tell char-server charcter is online [Wizputer]
 *-----------------------------------------
 */

int chrif_char_online(struct map_session_data *sd)
{
	chrif_check(-1);

        WFIFOHEAD(char_fd, 10);
	WFIFOW(char_fd,0) = 0x2b19;
	WFIFOL(char_fd,2) = sd->status.char_id;
	WFIFOL(char_fd,6) = sd->status.account_id;
	WFIFOSET(char_fd,10);

	return 0;
}

int chrif_disconnect(int fd) {
	if(fd == char_fd) {
		char_fd = 0;
		ShowWarning("Map Server disconnected from Char Server.\n\n");
		chrif_connected = 0;
		
	 	other_mapserver_count=0; //Reset counter. We receive ALL maps from all map-servers on reconnect.
		map_eraseallipport();

		//Attempt to reconnect in a second. [Skotlex]
		add_timer(gettick() + 1000, check_connect_char_server, 0, 0);
	}
	return 0;
}

void chrif_update_ip(int fd){
	unsigned long new_ip;
	WFIFOHEAD(fd, 6);
	new_ip = resolve_hostbyname(char_ip_str, NULL, NULL);
	if (new_ip && new_ip != char_ip)
		char_ip = new_ip; //Update char_ip

	new_ip = clif_refresh_ip();
	if (!new_ip) return; //No change
	WFIFOW(fd, 0) = 0x2736;
	WFIFOL(fd, 2) = new_ip;
	WFIFOSET(fd, 6);
}

/*==========================================
 *
 *------------------------------------------
 */
int chrif_parse(int fd)
{
	int packet_len, cmd;
	// only char-server can have an access to here.
	// so, if it isn't the char-server, we disconnect the session (fd != char_fd).
	if (fd != char_fd || session[fd]->eof) {
		if (fd == char_fd && chrif_connected == 1) {
			chrif_disconnect (fd);
		}
		else if (fd != char_fd)
			ShowDebug("chrif_parse: Disconnecting invalid session #%d (is not the char-server)\n", fd);

		do_close(fd);
		return 0;
	}

	while (RFIFOREST(fd) >= 2 && !session[fd]->eof) { //Infinite loop on broken pipe fix. [Skotlex]
		RFIFOHEAD(fd);
		cmd = RFIFOW(fd,0);
		if (cmd < 0x2af8 || cmd >= 0x2af8 + (sizeof(packet_len_table) / sizeof(packet_len_table[0])) ||
		    packet_len_table[cmd-0x2af8] == 0) {

			int r = intif_parse(fd); // intifに渡す

			if (r == 1) continue;	// intifで処理した
			if (r == 2) return 0;	// intifで処理したが、データが足りない

			session[fd]->eof = 1;
			ShowWarning("chrif_parse: session #%d, intif_parse failed -> disconnected.\n", fd);
			return 0;
		}
		packet_len = packet_len_table[cmd-0x2af8];
		if (packet_len == -1) {
			if (RFIFOREST(fd) < 4)
				return 0;
			packet_len = RFIFOW(fd,2);
		}
		if ((int)RFIFOREST(fd) < packet_len)
			return 0;

		switch(cmd) {
		case 0x2af9: chrif_connectack(fd); break;
		case 0x2afb: chrif_sendmapack(fd); break;
		case 0x2afd: chrif_authok(fd); break;
		case 0x2b00: map_setusers(fd); break;
		case 0x2b03: clif_charselectok(RFIFOL(fd,2)); break;
		case 0x2b04: chrif_recvmap(fd); break;
		case 0x2b06: chrif_changemapserverack(fd); break;
		case 0x2b07: clif_updatemaxid(RFIFOL(fd,2), RFIFOL(fd,6)); break;
		case 0x2b09: map_addchariddb(RFIFOL(fd,2), (char*)RFIFOP(fd,6)); break;
		case 0x2b0b: chrif_changedgm(fd); break;
		case 0x2b0d: chrif_changedsex(fd); break;
		case 0x2b0f: chrif_char_ask_name_answer(fd); break;
		case 0x2b12: chrif_divorce(RFIFOL(fd,2), RFIFOL(fd,6)); break;
		case 0x2b13: chrif_accountdeletion(fd); break;
		case 0x2b14: chrif_accountban(fd); break;
		case 0x2b15: chrif_recvgmaccounts(fd); break;
		case 0x2b1b: chrif_recvfamelist(fd); break;
		case 0x2b1d: chrif_load_scdata(fd); break;
		case 0x2b1e: chrif_update_ip(fd); break;
		case 0x2b1f: chrif_disconnectplayer(fd); break;
		case 0x2b20: chrif_removemap(fd); break;
		case 0x2b21: chrif_save_ack(fd); break;
		case 0x2b22: chrif_updatefamelist_ack(fd); break;

		default:
			if (battle_config.error_log)
				ShowError("chrif_parse : unknown packet (session #%d): 0x%x. Disconnecting.\n", fd, cmd);
			session[fd]->eof = 1;
			return 0;
		}
		if (fd == char_fd) //There's the slight chance we lost the connection during parse, in which case this would segfault if not checked [Skotlex]
			RFIFOSKIP(fd, packet_len);
	}

	return 0;
}

int send_usercount_tochar(int tid, unsigned int tick, int id, int data) {
	int count;
	static int last_count = 0;

	chrif_check(-1);
	
	map_getallusers(&count);
	
	if (count == last_count) //No need to waste packets.
		return 0;
	last_count = count;

	WFIFOHEAD(char_fd, 4);
	WFIFOW(char_fd,0) = 0x2afe;
	WFIFOW(char_fd,2) = count;
	WFIFOSET(char_fd,4);
	return 0;
}

/*==========================================
 * timer関数
 * 今このmap鯖に繋がっているクライアント人数をchar鯖へ送る
 *------------------------------------------
 */
int send_users_tochar(int tid, unsigned int tick, int id, int data) {
	int count, users=0, i;
	struct map_session_data **all_sd;

	chrif_check(-1);

	all_sd = map_getallusers(&count);
	WFIFOHEAD(char_fd, 6+8*users);
	WFIFOW(char_fd,0) = 0x2aff;
	for (i = 0; i < count; i++) {
		WFIFOL(char_fd,6+8*users) = all_sd[i]->status.account_id;
		WFIFOL(char_fd,6+8*users+4) = all_sd[i]->status.char_id;
		users++;
	}
	WFIFOW(char_fd,2) = 6 + 8 * users;
	WFIFOW(char_fd,4) = users;
	WFIFOSET(char_fd,6+8*users);

	return 0;
}

/*==========================================
 * timer関数
 * char鯖との接続を確認し、もし切れていたら再度接続する
 *------------------------------------------
 */
int check_connect_char_server(int tid, unsigned int tick, int id, int data) {
	static int displayed = 0;
	if (char_fd <= 0 || session[char_fd] == NULL) {
		if (!displayed) {
			ShowStatus("Attempting to connect to Char Server. Please wait.\n");
			displayed = 1;
		}
		chrif_state = 0;
		char_fd = make_connection(char_ip, char_port);
		if (char_fd == -1)
		{	//Attempt to connect later. [Skotlex]
			char_fd = 0;
			return 0;
		}
		session[char_fd]->func_parse = chrif_parse;
		realloc_fifo(char_fd, FIFOSIZE_SERVERLINK, FIFOSIZE_SERVERLINK);

		chrif_connect(char_fd);
		chrif_connected = (chrif_state == 2);
#ifndef TXT_ONLY
		srvinfo = 0;
#endif /* not TXT_ONLY */
	} else {
#ifndef TXT_ONLY
		if (srvinfo == 0) {
			chrif_ragsrvinfo(battle_config.base_exp_rate, battle_config.job_exp_rate, battle_config.item_rate_common);
			srvinfo = 1;
		}
#endif /* not TXT_ONLY */
/*		There is no need, the connection is TCP, so the packet is assured to arrive unless the connection dies [Skotlex]
		//If for some reason the next iteration (10 secs) we are still not connected,
		//assume the packets got lost, so we need to resend them. [Skotlex]
		if (chrif_state == 0)
			chrif_connect(char_fd);
		else if (chrif_state == 1)
			chrif_sendmap(char_fd);
*/
	}
	if (chrif_isconnect()) displayed = 0;
	return 0;
}

int auth_db_final(DBKey k,void *d,va_list ap) {
	struct auth_node *node=(struct auth_node*)d;
	if (node->char_dat)
		aFree(node->char_dat);
	return 0;
}

/*==========================================
 * 終了
 *------------------------------------------
 */
int do_final_chrif(void)
{
	if (char_fd > 0)
		do_close(char_fd);
	auth_db->destroy(auth_db, auth_db_final);
	return 0;
}

/*==========================================
 *
 *------------------------------------------
 */
int do_init_chrif(void)
{
	add_timer_func_list(check_connect_char_server, "check_connect_char_server");
	add_timer_func_list(send_usercount_tochar, "send_usercount_tochar");
	add_timer_func_list(send_users_tochar, "send_users_tochar");
	add_timer_func_list(auth_db_cleanup, "auth_db_cleanup");
	add_timer_interval(gettick() + 1000, check_connect_char_server, 0, 0, 10 * 1000);
#ifdef TXT_ONLY
	//Txt needs this more frequently because it is used for the online.html file.
	add_timer_interval(gettick() + 1000, send_users_tochar, 0, 0, UPDATE_INTERVAL);
#else
	add_timer_interval(gettick() + 1000, send_users_tochar, 0, 0, CHECK_INTERVAL);
	add_timer_interval(gettick() + 1000, send_usercount_tochar, 0, 0, UPDATE_INTERVAL);
#endif
	add_timer_interval(gettick() + 1000, auth_db_cleanup, 0, 0, 30 * 1000);

	auth_db = db_alloc(__FILE__,__LINE__,DB_INT,DB_OPT_RELEASE_DATA,sizeof(int));

	return 0;
}
