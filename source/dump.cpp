#include "dump.hpp"
#include <tesla.hpp>

//enum class Result {
//    Success = 0,
//    NotImplemented = 1,
//    InvalidArgument = 2,
//    InProgress = 3,
//    NoAsyncOperation = 4,
//    InvalidAsyncOperation = 5,
//    NotPermitted = 6,
//    NotInitialized = 7,
//};

static char pathBuffer[FS_MAX_PATH] = { 0 };
Result rc;

/**
 * @brief Dumps dream town
 *
 * @param progress : pointer to a progressbar progress
 * @param *status : pointer to a progressbar status string (const char*)
 * @param *logelm : pointer to a Log Element
 */
void Dumper(u8* progress, const char** status, tsl::elm::Log** logelm) {
	*progress = 69;
	*status = "optaining pointers...";
	//[[[main+3D96720]+10]+130]+60
	u64 mainAddr = util::FollowPointerMain(0x3D96720, 0x10, 0x130, 0xFFFFFFFFFFFFFFFF) + 0x60;
	if (mainAddr == 0x60) {
		*status = "Error: mainAddr";
		*progress = 0;
		return;
	}
	//[[[[main+3D96720]+10]+140]+08]
	u64 playerAddr = util::FollowPointerMain(0x3D96720, 0x10, 0x140, 0x08, 0xFFFFFFFFFFFFFFFF);
	if (playerAddr == 0x00) {
		*status = "Error: playerAddr";
		*progress = 0;
		return;
	}

	*progress = 20;
	*status = "pointers optained";

	//max amount of players in a dream town
	std::vector<bool> players(0x8, false);

	*status = "checking players...";
	//check existing players
	for (u8 i = 0; i < 8; i++) {
		u64 offset = i * GSavePlayerVillagerAccountSize;
		u128 AccountUID = 0;
		dmntchtReadCheatProcessMemory(mainAddr + GSavePlayerVillagerAccountOffset + offset, &AccountUID, 0x10);
		if (AccountUID != 0) players[i] = true;
	}
	*status = "players checked";
	//time for fun
	fsdevMountSdmc();
	FsFileSystem fsSdmc;
	rc = fsOpenSdCardFileSystem(&fsSdmc);
	if (R_FAILED(rc)) {
		*progress = 0;
		*status = "Error: opening SD Card";
		fsdevUnmountDevice("sdmc");
		return;
	}
	FsFile main;
	FsFile personal;

	TimeCalendarTime dumpdreamtime = util::getDreamTime(mainAddr);
	char dreamtime[128];
	const char* date_format = "%02d.%02d.%04d @ %02d-%02d";
	sprintf(dreamtime, date_format, dumpdreamtime.day, dumpdreamtime.month, dumpdreamtime.year, dumpdreamtime.hour, dumpdreamtime.minute);
	
	(*logelm)->addLine("Dream Time: " + std::string(dreamtime));

	(*logelm)->addLine("DA-" + util::getDreamAddrString(mainAddr));

#if DEBUG
	*status = dumptime;
	std::this_thread::sleep_for(std::chrono::seconds(2));
#endif

	std::string newdumppath = "/config/luna/dump/[DA-" + util::getDreamAddrString(mainAddr) + "]";
	std::string strislandname = util::getIslandNameASCII(playerAddr);
	if (!strislandname.empty()) newdumppath += " " + strislandname;

	*status = "starting dump...";
	//make dir on SD
	if (access(newdumppath.c_str(), F_OK) == -1) {
		mkdir(newdumppath.c_str(), 0777);
	}
	newdumppath += "/" + std::string(dreamtime);
	mkdir(newdumppath.c_str(), 0777);
	for (u8 i = 1; i < 8; i++) {
		fs::addPathFilter("/config/luna/template/Villager" + std::to_string(i));
	}
	//copy template to new directory recursively
	fs::copyDirToDir(&fsSdmc, "/config/luna/template/", newdumppath + "/", logelm);
	fs::freePathFilters();
	(*logelm)->addLine("copied template.");
	*progress = 40;
	*status = "finished copying template";
	size_t bufferSize = BUFF_SIZE;
	u8 *buffer = new u8[bufferSize];
	//opening main write
	std::snprintf(pathBuffer, FS_MAX_PATH, std::string(newdumppath + "/main.dat").c_str());
	rc = fsFsOpenFile(&fsSdmc, pathBuffer, FsOpenMode_Write, &main);
	if (R_FAILED(rc)) {
		*progress = 0;
		*status = "Error: opening main file";
		fsFileClose(&main);
		fsFsClose(&fsSdmc);
		fsdevUnmountDevice("sdmc");
		return;
	}

	*status = "writing main.dat...";
	for (u64 offset = 0; offset < mainSize; offset += bufferSize) {
		if (bufferSize > mainSize - offset)
			bufferSize = mainSize - offset;

		*progress = 40 + (20 * (offset / mainSize));

		dmntchtReadCheatProcessMemory(mainAddr + offset, buffer, bufferSize);
		rc = fsFileWrite(&main, SaveHeaderSize + offset, buffer, bufferSize, FsWriteOption_Flush);
#if DEBUG
		char out[sizeof(Result)];
		snprintf(out, sizeof(Result), "%u", rc);
		*status = (const char*)out;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
	}
	u16 IsDreamingBed = 0; //346
	u16 TapDreamEnable = 1; //354
	u16 EnableMyDream = 0; //362
	u16 DreamUploadPlayerHaveCreatorID = 0; //364

	//removes the dream bed and gyroid on Plaza
	fsFileWrite(&main, SaveHeaderSize + EventFlagOffset + (346 * 2), &IsDreamingBed, sizeof(u16), FsWriteOption_Flush);
	//should allow you to dream without introduction to it etc
	fsFileWrite(&main, SaveHeaderSize + EventFlagOffset + (354 * 2), &TapDreamEnable, sizeof(u16), FsWriteOption_Flush);
	//removes link to dream town
	fsFileWrite(&main, SaveHeaderSize + EventFlagOffset + (362 * 2), &EnableMyDream, sizeof(u16), FsWriteOption_Flush);
	//removes panel
	fsFileWrite(&main, SaveHeaderSize + EventFlagOffset + (364 * 2), &DreamUploadPlayerHaveCreatorID, sizeof(u16), FsWriteOption_Flush);

	//remove DreamInfo in dumped file
	u8 DreamInfoBuffer[DreamInfoSize] = { 0 };
	fsFileWrite(&main, SaveHeaderSize + DreamIDOffset, DreamInfoBuffer, DreamInfoSize, FsWriteOption_Flush);


	(*logelm)->addLine("applied fixes to main.");

	*progress = 60;
	*status = "wrote main.dat";

	//done writing main
	fsFileClose(&main);

	FsFile landname;
	//clear our path buffer or bad things will happen
	memset(pathBuffer, 0, FS_MAX_PATH);
	std::snprintf(pathBuffer, FS_MAX_PATH, std::string(newdumppath + "/landname.dat").c_str());
	u16 islandname[0xB];
	memcpy(islandname, util::getIslandName(playerAddr).name, sizeof(islandname));
	//in case user doesn't submit a valid landname.dat file or the file at all
	fsFsDeleteFile(&fsSdmc, pathBuffer);
	fsFsCreateFile(&fsSdmc, pathBuffer, 0xB * sizeof(u16), 0);
	fsFsOpenFile(&fsSdmc, pathBuffer, FsOpenMode_Write, &landname);
	fsFileWrite(&landname, 0, &islandname, 0x16, FsWriteOption_Flush);
	fsFileClose(&landname);


	std::string Villager0 = newdumppath + "/Villager0/";

	u8 playercount = 0;

	for (u8 j = 0; j < 8; j++)
		if (players[j]) playercount++;

	u8 percentageperplayer = 40 / playercount;

	for (u8 i = 0; i < 8; i++) {
		//if this player (i) doesn't exist, return
		if (!players[i]) continue;

		std::string player = "/Villager" + std::to_string(i) + "/";
#if DEBUG
		*status = player.c_str();
		std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
		std::string currentplayer = newdumppath + player;
#if DEBUG
		*status = currentplayer.c_str();
		std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
		//copy the original villager for the existing villagers
		if (i != 0) {
			mkdir(currentplayer.c_str(), 0777);
			fs::copyDirToDir(&fsSdmc, Villager0, currentplayer, logelm);
			(*logelm)->addLine("finished copying player template Villager" + std::to_string(i) + ".");
		}
		//reset size in-case it got changed in the latter for loop
		bufferSize = BUFF_SIZE;
		//clear our path buffer or bad things will happen
		memset(pathBuffer, 0, FS_MAX_PATH);
		//opening personal
		std::snprintf(pathBuffer, FS_MAX_PATH, std::string(currentplayer + "personal.dat").c_str());
		rc = fsFsOpenFile(&fsSdmc, pathBuffer, FsOpenMode_Write, &personal);
		if (R_FAILED(rc)) {
			*progress = 0;
			*status = "Error: opening player file";
			fsFileClose(&personal);
			fsFsClose(&fsSdmc);
			fsdevUnmountDevice("sdmc");
			return;
		}

		*status = "writing player...";
		for (u64 offset = 0; offset < playerSize; offset += bufferSize) {
			if (bufferSize > playerSize - offset)
				bufferSize = playerSize - offset;

			dmntchtReadCheatProcessMemory(playerAddr + offset + (i * playersOffset), buffer, bufferSize);
			fsFileWrite(&personal, SaveHeaderSize + offset, buffer, bufferSize, FsWriteOption_Flush);
		}
		(*logelm)->addLine("wrote player " + std::to_string(i + 1) + ".");

		//applying fix for pocket size
		*status = "applying fixes to player...";
		u8 houselvl = 0;
		u16 BuiltTownOffice = 0;
		//using default sizes
		u32 storageSize = 80;
		u32 pocket2Size = 0;

		dmntchtReadCheatProcessMemory(mainAddr + houseLvlOffset + (i * houseSize), &houselvl, sizeof(u8));
		dmntchtReadCheatProcessMemory(mainAddr + EventFlagOffset + (59 * 2), &BuiltTownOffice, sizeof(u16));

		switch (houselvl) {
			//no need to change defaults
			case 1: break;
			case 2: storageSize = 120;
					break;
			case 3: storageSize = 240;
					break;
			case 4: storageSize = 320;
					break;
			case 5: storageSize = 400;
					break;
			case 6: storageSize = 800;
					break;
			case 7: storageSize = 1600;
					break;
			case 8: storageSize = 2400;
					break;
		}
		//those are just assumptions; the pocket upgrades are available at those points tho, so most people got them
		if (houselvl > 0x1) {
			pocket2Size += 0xA;
			if (BuiltTownOffice == 0x1) pocket2Size += 0xA;
		}

		fsFileWrite(&personal, StorageSizeOffset, &storageSize, sizeof(u32), FsWriteOption_Flush);
		fsFileWrite(&personal, Pocket2SizeOffset, &pocket2Size, sizeof(u32), FsWriteOption_Flush);

		fsFileClose(&personal);
		(*logelm)->addLine("applied fixes to player " + std::to_string(i + 1) + ".");
		*progress += percentageperplayer;
	}

	//dump succeeded

	*progress = 100;
	*status = "DONE!";
	fsFsClose(&fsSdmc);
	fsdevUnmountDevice("sdmc");
	return;
}
