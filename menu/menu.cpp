#include <errno.h>

#include "sal.h"
#include "menu.h"
#include "snapshot.h"
#include "snes9x.h"
#include "gfx.h"
#include "memmap.h"
#include "soundux.h"

#define MAX_DISPLAY_CHARS			40
#define ROM_SELECTOR_SAVE_DEFAULT_DIR	0
#define ROM_SELECTOR_MAIN_MENU			1
#define ROM_SELECTOR_DEFAULT_FOCUS		2
#define ROM_SELECTOR_ROM_START			3

static u16 mMenuBackground[SAL_SCREEN_WIDTH * SAL_SCREEN_HEIGHT];

static s32 mMenutileXscroll=0;
static s32 mMenutileYscroll=0;
static s32 mTileCounter=0;
static s32 mQuickSavePresent=0;
static u32 mPreviewingState=0;

static s8 mMenuText[30][MAX_DISPLAY_CHARS];

static struct SAL_DIRECTORY_ENTRY *mRomList=NULL;
static s32 mRomCount;
static s8 mRomDir[SAL_MAX_PATH]={""};

struct SAVE_STATE mSaveState[10];  // holds the filenames for the savestate and "inuse" flags
static s8 mSaveStateName[SAL_MAX_PATH]={""};       // holds the last filename to be scanned for save states
static s8 mRomName[SAL_MAX_PATH]={""};
static s8 mSystemDir[SAL_MAX_PATH];
static struct MENU_OPTIONS *mMenuOptions=NULL;
static u16 mTempFb[SNES_WIDTH*SNES_HEIGHT_EXTENDED*2];

static char errormsg[MAX_DISPLAY_CHARS];

extern volatile bool argv_rom_loaded;
extern "C" void S9xSaveSRAM(int showWarning);

u8 menuGameSettings=0, menuGlobalSettings=0;

bool file_exists(char *path) {
	struct stat s;
	return (stat(path, &s) == 0 && s.st_mode & S_IFREG); // exists and is file
}
int hwscale;

void DefaultMenuOptions(void)
{
	mMenuOptions->frameSkip = 0;   //auto
	mMenuOptions->soundEnabled = 1;
	mMenuOptions->volume = 25;
	mMenuOptions->cpuSpeed = 336;
	mMenuOptions->country = 0;
	mMenuOptions->showFps = 0;
	mMenuOptions->soundRate = 48000;
	mMenuOptions->stereo = 0;
	mMenuOptions->fullScreen = hwscale ? 3 : 1;
	mMenuOptions->autoSaveSram = 1;
	mMenuOptions->soundSync = 1;
}

s32 LoadMenuOptions(const char *path, const char *filename, const char *ext, const char *optionsmem, s32 maxSize, s32 showMessage)
{
	s8 fullFilename[SAL_MAX_PATH];
	s8 _filename[SAL_MAX_PATH];
	s8 _ext[SAL_MAX_PATH];
	s8 _path[SAL_MAX_PATH];
	s32 size = 0;

	if (showMessage) {
		PrintTitle("");
		sal_VideoPrint(8,120,"Loading...", SAL_RGB(31, 31, 31));
		sal_VideoFlip(1);
	}

	sal_DirectorySplitFilename(filename, _path, _filename, _ext);
	sprintf(fullFilename, "%s%s%s.%s", path, SAL_DIR_SEP, _filename,ext);
	return sal_FileLoad(fullFilename, (u8*)optionsmem, maxSize, (u32*)&size);
}

s32 SaveMenuOptions(const char *path, const char *filename, const char *ext, const char *optionsmem, s32 maxSize, s32 showMessage)
{
	s8 fullFilename[SAL_MAX_PATH];
	s8 _filename[SAL_MAX_PATH];
	s8 _ext[SAL_MAX_PATH];
	s8 _path[SAL_MAX_PATH];

	if (showMessage) {
		PrintTitle("");
		sal_VideoPrint(8, 120, "Saving...", SAL_RGB(31, 31, 31));
		sal_VideoFlip(1);
	}

	sal_DirectorySplitFilename(filename, _path, _filename, _ext);
	sprintf(fullFilename, "%s%s%s.%s", path, SAL_DIR_SEP, _filename, ext);
	return sal_FileSave(fullFilename, (u8*)optionsmem, maxSize);
}

s32 DeleteMenuOptions(const char *path, const char *filename, const char *ext, s32 showMessage)
{
	s8 fullFilename[SAL_MAX_PATH];
	s8 _filename[SAL_MAX_PATH];
	s8 _ext[SAL_MAX_PATH];
	s8 _path[SAL_MAX_PATH];

	if (showMessage) {
		PrintTitle("");
		sal_VideoPrint(8, 120, "Deleting...", SAL_RGB(31, 31, 31));
		sal_VideoFlip(1);
	}

	sal_DirectorySplitFilename(filename, _path, _filename, _ext);
	sprintf(fullFilename, "%s%s%s.%s", path, SAL_DIR_SEP, _filename, ext);
	sal_FileDelete(fullFilename);
	return SAL_OK;
}

s32 LoadLastSelectedRomPos() // Try to get the last selected rom position from a config file
{
	char lastselfile[SAL_MAX_PATH];
	s32 savedval = ROM_SELECTOR_DEFAULT_FOCUS;
	strcpy(lastselfile, sal_DirectoryGetHome());
	sal_DirectoryCombine(lastselfile, "lastselected.opt");
	FILE * pFile;
	pFile = fopen (lastselfile, "r+");
	if (pFile != NULL) {
		fscanf (pFile, "%i", &savedval);
		fclose (pFile);
	}
	return savedval;
}

void SaveLastSelectedRomPos(s32 pospointer) // Save the last selected rom position in a config file
{
	char lastselfile[SAL_MAX_PATH];
	strcpy(lastselfile, sal_DirectoryGetHome());
	sal_DirectoryCombine(lastselfile, "lastselected.opt");
	FILE *pFile;
	pFile = fopen(lastselfile, "w+");
	fprintf(pFile, "%i", pospointer);
	fclose(pFile);
}

void DelLastSelectedRomPos() // Remove the last selected rom position config file
{
	char lastselfile[SAL_MAX_PATH];
	strcpy(lastselfile, sal_DirectoryGetHome());
	sal_DirectoryCombine(lastselfile, "lastselected.opt");
	remove(lastselfile);
}

void MenuPause()
{
	sal_InputWaitForPress();
	sal_InputWaitForRelease();
}

s32 MenuMessageBox(const char *message1, const char *message2, const char *message3, enum MENU_MESSAGE_BOX_MODE mode)
{
	s32 select = 0;
	s32 subaction = -1;
	u32 keys = 0;

	printf("MessageBox: %s %s %s\n", message1, message2, message3);

	sal_InputIgnore();
	while (subaction == -1) {
		keys = sal_InputPollRepeat(0);
		if (keys & SAL_INPUT_UP) {
			select = SAL_OK; // Up
		}
		else if (keys & SAL_INPUT_DOWN) {
			select = SAL_ERROR; // Down
		}
		if ((keys&INP_BUTTON_MENU_SELECT) || (keys&INP_BUTTON_MENU_CANCEL)) {
			subaction = select;
		}
		PrintTitle("Message Box");
		sal_VideoPrint(8, 50, message1, SAL_RGB(31, 31, 31));
		sal_VideoPrint(8, 60, message2, SAL_RGB(31, 31, 31));
		sal_VideoPrint(8, 70, message3, SAL_RGB(31, 31, 31));
		switch (mode) {
			case MENU_MESSAGE_BOX_MODE_YESNO: // yes no input
				if (select == SAL_OK) {
					PrintBar(120 - 4);
					sal_VideoPrint(8, 120, "YES", SAL_RGB(31, 31, 31));
					sal_VideoPrint(8, 140,  "NO", SAL_RGB(31, 31, 31));
				} else {
					PrintBar(140 - 4);
					sal_VideoPrint(8, 120, "YES", SAL_RGB(31, 31, 31));
					sal_VideoPrint(8, 140,  "NO", SAL_RGB(31, 31, 31));
				}
				break;
			case MENU_MESSAGE_BOX_MODE_PAUSE:
				PrintBar(120 - 4);
				sal_VideoPrint(8, 120, "Press button to continue", SAL_RGB(31, 31, 31));
				break;
			case MENU_MESSAGE_BOX_MODE_MSG:
				subaction = SAL_OK;
				break;
		}
		sal_VideoFlip(1);
	}
	sal_InputIgnore();
	return(subaction);
}

void PrintTitle(const char *title)
{
	sal_ImageDraw(mMenuBackground, SAL_SCREEN_WIDTH, SAL_SCREEN_HEIGHT, 0, 0);
	sal_VideoPrint(8, 4, title, SAL_RGB(31, 8, 8));
}

void PrintBar(u32 givenY)
{
	//sal_ImageDraw(mHighLightBar,HIGHLIGHT_BAR_WIDTH, HIGHLIGHT_BAR_HEIGHT,0,givenY);
	sal_HighlightBar(262, HIGHLIGHT_BAR_HEIGHT, 0, givenY);
}

void freeRomLists()
{
	//free rom list buffers
	if (mRomList != NULL) free(mRomList);
	mRomList = NULL;
}

void DefaultRomListItems()
{
	s32 i;
	strcpy(mRomList[ROM_SELECTOR_SAVE_DEFAULT_DIR].displayName, "Save default directory");
	strcpy(mRomList[ROM_SELECTOR_MAIN_MENU].displayName, "Main menu");
	strcpy(mRomList[ROM_SELECTOR_DEFAULT_FOCUS].displayName, "/");
	strcpy(mRomList[ROM_SELECTOR_DEFAULT_FOCUS].filename, "/");
	mRomList[ROM_SELECTOR_DEFAULT_FOCUS].type = SAL_FILE_TYPE_DIRECTORY;
	// mRomList[ROM_SELECTOR_DEFAULT_FOCUS].displayName[0]=0;
}

static
void SwapDirectoryEntry(struct SAL_DIRECTORY_ENTRY *salFrom, struct SAL_DIRECTORY_ENTRY *salTo)
{
	struct SAL_DIRECTORY_ENTRY temp;

	//Copy salFrom to temp entry
	strcpy(temp.displayName, salFrom->displayName);
	strcpy(temp.filename, salFrom->filename);
	temp.type = salFrom->type;

	//Copy salTo to salFrom
	strcpy(salFrom->displayName, salTo->displayName);
	strcpy(salFrom->filename, salTo->filename);
	salFrom->type = salTo->type;

	//Copy temp entry to salTo
	strcpy(salTo->displayName, temp.displayName);
	strcpy(salTo->filename, temp.filename);
	salTo->type = temp.type;
}

int FileScan()
{
	s32 itemCount = 0, fileCount = 0, dirCount = 0;
	s32 x, a, b, startIndex = ROM_SELECTOR_DEFAULT_FOCUS + 1;
	s8 text[50];
	s8 filename[SAL_MAX_PATH];
	s8 path[SAL_MAX_PATH];
	s8 ext[SAL_MAX_PATH];
	struct SAL_DIR d;

	freeRomLists();

#if 0
	PrintTitle("File Scan");
	sal_VideoPrint(8,120,"Scanning Directory...",SAL_RGB(31,31,31));
	sal_VideoFlip(1);
#endif

	if (sal_DirectoryGetItemCount(mRomDir,&itemCount) == SAL_ERROR) {
		return SAL_ERROR;
	}

	mRomCount = ROM_SELECTOR_ROM_START + itemCount;
	mRomList = (SAL_DIRECTORY_ENTRY*)malloc(sizeof(struct SAL_DIRECTORY_ENTRY) * mRomCount);

	//was there enough memory?
	if (mRomList == NULL) {
		MenuMessageBox("Could not allocate memory", "Too many files", "", MENU_MESSAGE_BOX_MODE_PAUSE);
		//not enough memory - try the minimum
		mRomList = (SAL_DIRECTORY_ENTRY*)malloc(sizeof(struct SAL_DIRECTORY_ENTRY)*ROM_SELECTOR_ROM_START);
		mRomCount = ROM_SELECTOR_ROM_START;
		if (mRomList == NULL) {
			//still no joy
			MenuMessageBox("Dude, I'm really broken now", "Restart system", "never do this again", MENU_MESSAGE_BOX_MODE_PAUSE);
			mRomCount = -1;
			return SAL_ERROR;
		}
	}

	//Add default items
	DefaultRomListItems();

	if (itemCount > 0) {
		if (sal_DirectoryOpen(mRomDir, &d) == SAL_OK) {
			//Dir opened, now stream out details
			x = 0;
			while (sal_DirectoryRead(&d, &mRomList[x+startIndex], mRomDir) == SAL_OK) {
				//Dir entry read
#if 0
				PrintTitle("File Scan");
				sprintf(text,"Fetched item %d of %d",x, itemCount-1);
				sal_VideoPrint(8,120,text,SAL_RGB(31,31,31));
				PrintBar(228-4);
				sal_VideoPrint(0,228,mRomDir,SAL_RGB(0,0,0));
				sal_VideoFlip(1);
#endif
				if (mRomList[x+startIndex].type == SAL_FILE_TYPE_FILE) {
					sal_DirectorySplitFilename(mRomList[x + startIndex].filename, path, filename, ext);
					if (
						sal_StringCompare(ext, "zip") == 0 ||
						sal_StringCompare(ext, "smc") == 0 ||
						sal_StringCompare(ext, "sfc") == 0 ||
						sal_StringCompare(ext, "fig") == 0 /* Super WildCard dump */ ||
						sal_StringCompare(ext, "swc") == 0 /* Super WildCard dump */)
					{
						fileCount++;
						x++;
					}
				} else {
					dirCount++;
					x++;
				}

			}
			mRomCount = ROM_SELECTOR_ROM_START + dirCount + fileCount;
			sal_DirectoryClose(&d);
		} else {
			return SAL_ERROR;
		}

#if 0
		PrintTitle("File Scan");
		sal_VideoPrint(8,120,"Sorting items...",SAL_RGB(31,31,31));
		sal_VideoFlip(1);
#endif
		int lowIndex = 0;
		//Put all directory entries at the top
		for (a = startIndex; a < startIndex + dirCount; a++) {
			if (mRomList[a].type == SAL_FILE_TYPE_FILE) {
				for (b = a + 1; b < mRomCount; b++) {
					if (mRomList[b].type == SAL_FILE_TYPE_DIRECTORY) {
						SwapDirectoryEntry(&mRomList[a],&mRomList[b]);
						break;
					}
				}
			}
		}

		//Now sort directory entries
		for (a = startIndex; a < startIndex + dirCount; a++) {
			lowIndex = a;
			for (b = a + 1; b < startIndex + dirCount; b++) {
				if (sal_StringCompare(mRomList[b].displayName, mRomList[lowIndex].displayName) < 0) {
					//this index is lower
					lowIndex = b;
				}
			}
			//lowIndex should index next lowest value
			SwapDirectoryEntry(&mRomList[lowIndex], &mRomList[a]);
		}

		//Now sort file entries
		for (a = startIndex + dirCount; a < mRomCount; a++) {
			lowIndex = a;
			for (b = a + 1; b < mRomCount; b++) {
				if (sal_StringCompare(mRomList[b].displayName, mRomList[lowIndex].displayName) < 0) {
					//this index is lower
					lowIndex = b;
				}
			}
			//lowIndex should index next lowest value
			SwapDirectoryEntry(&mRomList[lowIndex], &mRomList[a]);
		}
	}

	return SAL_OK;
}

s32 UpdateRomCache()
{
	s8 filename[SAL_MAX_PATH];
	PrintTitle("CRC Lookup");
	sal_VideoPrint(8, 120, "Saving cache to disk...", SAL_RGB(31, 31, 31));
	sal_VideoFlip(1);

	strcpy(filename, mRomDir);
	sal_DirectoryCombine(filename, "romcache.dat");
	sal_FileSave(filename, (u8*)&mRomList[0], sizeof(struct SAL_DIRECTORY_ENTRY)*(mRomCount));

	return SAL_OK;
}

s32 FileSelect()
{
	s8 text[SAL_MAX_PATH];
	s8 previewPath[SAL_MAX_PATH];
	s8 previousRom[SAL_MAX_PATH];
	u16 romPreview[262 * 186];
	bool8 havePreview = FALSE;
	s32 action = 0;
	s32 smooth = 0;
	u16 color = 0;
	s32 i = 0;
	s32 focus = ROM_SELECTOR_DEFAULT_FOCUS;
	s32 menuExit = 0;
	s32 scanstart = 0, scanend = 0;
	u32 keys = 0;
	s32 size = 0, check = SAL_OK;

	previousRom[0] = '\0';

	if (FileScan() != SAL_OK) {
		strcpy(mRomDir, sal_DirectoryGetUser());
		if (FileScan() != SAL_OK) {
			MenuMessageBox("Home directory inaccessible", "", "", MENU_MESSAGE_BOX_MODE_PAUSE);
			mRomCount = ROM_SELECTOR_DEFAULT_FOCUS;
			menuExit = 1;
			return 0;
		}
	}

	focus = LoadLastSelectedRomPos(); //try to load a saved position in the romlist

	smooth = focus << 8;
	sal_InputIgnore();
	while (menuExit == 0) {
		keys = sal_InputPollRepeat(0);

		if (keys & INP_BUTTON_MENU_SELECT) {
			switch (focus) {
				case ROM_SELECTOR_SAVE_DEFAULT_DIR: //Save default directory
					DelLastSelectedRomPos(); //delete any previously saved position in the romlist
					SaveMenuOptions(mSystemDir, DEFAULT_ROM_DIR_FILENAME, DEFAULT_ROM_DIR_EXT, mRomDir, strlen(mRomDir), 1);
					break;

				case ROM_SELECTOR_MAIN_MENU: //Return to menu
					action = 0;
					menuExit = 1;
					break;

				// case ROM_SELECTOR_DEFAULT_FOCUS: //blank space - do nothing
					// break;

				default:
					// normal file or dir selected
					if (mRomList[focus].type == SAL_FILE_TYPE_DIRECTORY) {
						//Check for special directory names "." and ".."
						if (sal_StringCompare(mRomList[focus].filename, ".") == 0) {
							//goto root directory

						}
						else if (sal_StringCompare(mRomList[focus].filename, "..") == 0) {
							// up a directory
							//Remove a directory from RomPath and rescan
							//Code below will never let you go further up than \SD Card\ on the Gizmondo
							//This is by design.
							sal_DirectoryGetParent(mRomDir);
							FileScan();
							focus = ROM_SELECTOR_DEFAULT_FOCUS; // default menu to non menu item
														// just to stop directory scan being started
							smooth = focus << 8;
							sal_InputIgnore();
							break;
						} else {
							if (sal_StringCompare(mRomList[focus].filename, "/") == 0) {
								strcpy(mRomDir, "/");
							} else {
								sal_DirectoryCombine(mRomDir, mRomList[focus].filename); //go to sub directory
							}

							FileScan();
							focus = ROM_SELECTOR_DEFAULT_FOCUS; // default menu to non menu item
														// just to stop directory scan being started
							smooth = focus << 8;
						}
					} else {
						// user has selected a rom, so load it
						SaveLastSelectedRomPos(focus); // save the current position in the romlist
						strcpy(mRomName, mRomDir);
						sal_DirectoryCombine(mRomName, mRomList[focus].filename);
						mQuickSavePresent = 0;  // reset any quick saves
						action = 1;
						menuExit = 1;
					}
					sal_InputIgnore();
					break;
			}
		}
		else if (keys & INP_BUTTON_MENU_CANCEL) {
			sal_InputWaitForRelease();
			action = 0;
			menuExit = 1;
		}
		else if (keys & SAL_INPUT_UP) {
			focus--; // Up
		}
		else if (keys & SAL_INPUT_DOWN) {
			focus++; // Down
		}
		else if (keys & SAL_INPUT_LEFT) {
			focus -= 12;
		}
		else if (keys & SAL_INPUT_RIGHT) {
			focus += 12;
		}

		if (focus > mRomCount - 1) {
			focus = 0;
			smooth = (focus << 8) - 1;
		}
		else if (focus < 0)
		{
			focus = mRomCount - 1;
			smooth = (focus << 8) - 1;
		}

		// Draw screen:
		PrintTitle("ROM selection");

		if (strcmp(mRomList[focus].displayName, previousRom) != 0) {
			char dummy[SAL_MAX_PATH], fileNameNoExt[SAL_MAX_PATH];
			sal_DirectorySplitFilename(mRomList[focus].filename, dummy, fileNameNoExt, dummy);
			sprintf(previewPath, "%s/previews/%s.%s", sal_DirectoryGetHome(), fileNameNoExt, "png");
			strcpy(previousRom, mRomList[focus].displayName);
			havePreview = sal_ImageLoad(previewPath, &romPreview, 262, 186) != SAL_ERROR;
			if (havePreview) {
				sal_VideoBitmapDim(romPreview, 262 * 186);
			}
		}

		if (havePreview) {
			sal_ImageDraw(romPreview, 262, 186, 0, 16);
		}

		smooth = smooth * 7 + (focus << 8);
		smooth >>= 3;

		scanstart = focus - 15;
		if (scanstart < 0) scanstart = 0;

		scanend = focus + 15;
		if (scanend > mRomCount) scanend = mRomCount;

		for (i = scanstart; i < scanend; i++) {
			s32 x = 0, y = 0;

			y = (i << 4) - (smooth >> 4);
			x = 0;
			y += 112 - 28;
			if (y <= 48 - 28 || y >= 232 - 36) continue;

			if (i == focus) {
				color = SAL_RGB(31, 31, 31);
				PrintBar(y - 4);
			} else {
				color = SAL_RGB(31, 31, 31);
			}

			// Draw Directory icon if current entry is a directory
			if (mRomList[i].type == SAL_FILE_TYPE_DIRECTORY) {
				sprintf(text, "<%s>", mRomList[i].displayName);
				sal_VideoPrint(x, y, text, color);
			} else {
				sal_VideoPrint(x, y, mRomList[i].displayName, color);
			}
		}

		sal_VideoPrint(0, 4, mRomDir, SAL_RGB(31, 8, 8));

		sal_VideoFlip(1);
		usleep(10000);
	}
	sal_InputIgnore();

	freeRomLists();

	return action;
}

static void ScanSaveStates(s8 *romname)
{
	s32 i = 0;
	s8 savename[SAL_MAX_PATH];
	s8 filename[SAL_MAX_PATH];
	s8 ext[SAL_MAX_PATH];
	s8 path[SAL_MAX_PATH];

	if (!strcmp(romname, mSaveStateName)) return; // is current save state rom so exit

	sal_DirectorySplitFilename(romname, path, filename, ext);

	sprintf(savename, "%s.%s", filename, SAVESTATE_EXT);

	for (i = 0; i < 10; i++) {
		/*
		need to build a save state filename
		all saves are held in current working directory (lynxSaveStateDir)
		save filename has following format
		shortname(minus file ext) + SV + saveno ( 0 to 9 )
		*/
		sprintf(mSaveState[i].filename, "%s%d", savename, i);
		sprintf(mSaveState[i].fullFilename, "%s%s%s", mSystemDir, SAL_DIR_SEP, mSaveState[i].filename);
		if (sal_FileExists(mSaveState[i].fullFilename) == SAL_TRUE) {
			// we have a savestate
			mSaveState[i].inUse = 1;
		} else {
			// no save state
			mSaveState[i].inUse = 0;
		}
	}
	strcpy(mSaveStateName, romname);  // save the last scanned romname
}

static
bool8 LoadStateTemp()
{
	char name[SAL_MAX_PATH];
	bool8 ret;
	sprintf(name, "%s%s%s", sal_DirectoryGetTemp(), SAL_DIR_SEP, ".svt");
	if (!(ret = S9xUnfreezeGame(name))) {
		fprintf(stderr, "Failed to read saved state at %s: %s\n", name, strerror(errno));
	}
	return ret;
}

static
void SaveStateTemp()
{
	char name[SAL_MAX_PATH];
	sprintf(name, "%s%s%s", sal_DirectoryGetTemp(), SAL_DIR_SEP, ".svt");
	if (!S9xFreezeGame(name)) {
		fprintf(stderr, "Failed to write saved state at %s: %s\n", name, strerror(errno));
	}
}

static
void DeleteStateTemp()
{
	char name[SAL_MAX_PATH];
	sprintf(name, "%s%s%s", sal_DirectoryGetTemp(), SAL_DIR_SEP, ".svt");
	sal_FileDelete(name);
}

// static
bool LoadStateFile(s8 *filename)
{
	bool ret;
	if (!(ret = S9xUnfreezeGame(filename))) {
		fprintf(stderr, "Failed to read saved state at %s: %s\n", filename, strerror(errno));
	}
	return ret;
}

// static
bool SaveStateFile(s8 *filename)
{
	bool ret;
	if (!(ret = S9xFreezeGame(filename))) {
		fprintf(stderr, "Failed to write saved state at %s: %s\n", filename, strerror(errno));
	}
	return ret;
}

u32 IsPreviewingState()
{
	return mPreviewingState;
}

s32 saveno = 0; // save state number
static s32 SaveStateSelect(s32 mode)
{
	s8 text[128];
	s32 action = 11;
	u32 keys = 0;
	u16 *pixTo, *pixFrom;

	if (mRomName[0] == 0) {
		// no rom loaded
		// display error message and exit
		return(0);
	}
	// Allow the emulator to back out of loading a saved state for previewing.
	SaveStateTemp();
	ScanSaveStates(mRomName);
	sal_InputIgnore();

	while (action != 0 && action != 100) {
		keys = sal_InputPollRepeat(0);

		if (keys & (SAL_INPUT_UP | SAL_INPUT_LEFT)) {
			saveno--;
			if (saveno < 0) saveno = 9;
			action = 1;
		}
		else if (keys & (SAL_INPUT_DOWN | SAL_INPUT_RIGHT)) {
			saveno++;
			if (saveno > 9) saveno = 0;
			action = 1;
		}

		if (keys & INP_BUTTON_MENU_CANCEL) {
			action = 0; // exit
		}
		else if ((keys & INP_BUTTON_MENU_PREVIEW_SAVESTATE) && (action == 12)) {
			action = 3;  // preview slot mode
		}
		else if (keys & INP_BUTTON_MENU_SELECT) {
			if (saveno == -1) {
				action = 0; // exit
			}
			else if ((mode == 0) && ((action == 2) || (action == 5) || (action == 12))) {
				action = 6;  // pre-save mode
			}
			else if ((mode == 1) && ((action == 5) || (action == 12))) {
				action = 8;  // pre-load mode
			}
			else if (((mode == 2) && ((action == 5) || (action == 12))) || ((keys & SAL_INPUT_X) && (mode == 0))) {
				if (MenuMessageBox("Are you sure you want to delete", "this save?", "", MENU_MESSAGE_BOX_MODE_YESNO) == SAL_OK) {
					action = 13;  //delete slot with no preview
				}
			}
		}

		PrintTitle("Choose a slot");

		if (saveno == -1) {
			if (action != 10 && action != 0) {
				action = 10;
			}
		} else {
			sal_VideoDrawRect(0, 16, 262, 16, SAL_RGB(22, 0, 0));
			sprintf(text, "SLOT %d", saveno);
			sal_VideoPrint(107, 20, text, SAL_RGB(31, 31, 31));
		}

		switch (action) {
			case 1:
				//sal_VideoPrint(112,145 - 36,14,"Checking....",(unsigned short)SAL_RGB(31,31,31));
				break;
			case 2:
				sal_VideoPrint(115, 145 - 36, "FREE", SAL_RGB(31, 31, 31));
				break;
			case 3:
				sal_VideoPrint(75, 145 - 36, "Previewing...", SAL_RGB(31, 31, 31));
				break;
			case 4:
				sal_VideoPrint(59, 145 - 36, "Previewing failed", SAL_RGB(31, 8, 8));
				snprintf(errormsg, sizeof(errormsg), "%s",strerror(errno));
				sal_VideoPrint((320 - strlen(errormsg) * 8) / 2, 145 - 20, errormsg, SAL_RGB(31, 8, 8));
				sal_VideoDrawRect(0, 186, 262, 16, SAL_RGB(22, 0, 0));
				if (mode == 0) {
					sal_VideoPrint((262 - (strlen(MENU_TEXT_DELETE_SAVESTATE) << 3)) >> 1, 190, MENU_TEXT_DELETE_SAVESTATE, SAL_RGB(31, 31, 31));
				}
				break;
			case 5: {
				u32 DestWidth = 205, DestHeight = 154;
				sal_VideoBitmapScale(0, 0, SNES_WIDTH, SNES_HEIGHT, DestWidth, DestHeight, SAL_SCREEN_WIDTH - DestWidth, &mTempFb[0], (u16*)sal_VideoGetBuffer() + (SAL_SCREEN_WIDTH * (((202 + 16) - DestHeight) / 2)) + ((262 - DestWidth) / 2));
				sal_VideoDrawRect(0, 186, 262, 16, SAL_RGB(22, 0, 0));
				switch (mode) {
					case 1:
						sal_VideoPrint((262 - (strlen(MENU_TEXT_LOAD_SAVESTATE) << 3)) >> 1, 190, MENU_TEXT_LOAD_SAVESTATE, SAL_RGB(31, 31, 31));
						break;
					case 2:
						sal_VideoPrint((262 - (strlen(MENU_TEXT_DELETE_SAVESTATE) << 3)) >> 1, 190, MENU_TEXT_DELETE_SAVESTATE, SAL_RGB(31, 31, 31));
						break;
					default:
						sal_VideoPrint((262 - (strlen(MENU_TEXT_OVERWRITE_SAVESTATE) << 3)) >> 1, 190, MENU_TEXT_OVERWRITE_SAVESTATE, SAL_RGB(31, 31, 31));
						break;
				}
				break;
			}
			case 6:
				sal_VideoPrint(95, 145 - 36, "Saving...", SAL_RGB(31, 31, 31));
				break;
			case 7:
				sal_VideoPrint(95, 145 - 36, "Saving failed", SAL_RGB(31, 8, 8));
				snprintf(errormsg, sizeof(errormsg), "%s", strerror(errno));
				sal_VideoPrint((320 - strlen(errormsg) * 8) / 2, 145 - 20, errormsg, SAL_RGB(31, 8, 8));
				break;
			case 8:
				sal_VideoPrint(87, 145 - 36, "Loading...", SAL_RGB(31, 31, 31));
				break;
			case 9:
				sal_VideoPrint(87, 145 - 36, "Loading failed", SAL_RGB(31, 8, 8));
				snprintf(errormsg, sizeof(errormsg), "%s", strerror(errno));
				sal_VideoPrint((320 - strlen(errormsg) * 8) / 2, 145 - 20, errormsg, SAL_RGB(31, 8, 8));
				break;
			case 12:
				sal_VideoPrint(95, 145 - 36, "Slot used", SAL_RGB(31, 31, 31));
				sal_VideoPrint((262 - (strlen(MENU_TEXT_PREVIEW_SAVESTATE) << 3)) >> 1, 165, MENU_TEXT_PREVIEW_SAVESTATE, SAL_RGB(31, 31, 31));
				switch (mode) {
					case 1:
						sal_VideoPrint((262-(strlen(MENU_TEXT_LOAD_SAVESTATE) << 3)) >> 1, 175, MENU_TEXT_LOAD_SAVESTATE, SAL_RGB(31, 31, 31));
						break;
					case 2:
						sal_VideoPrint((262-(strlen(MENU_TEXT_DELETE_SAVESTATE) << 3)) >> 1, 175, MENU_TEXT_DELETE_SAVESTATE, SAL_RGB(31, 31, 31));
						break;
					default:
						sal_VideoPrint((262-(strlen(MENU_TEXT_OVERWRITE_SAVESTATE) << 3)) >> 1, 175, MENU_TEXT_OVERWRITE_SAVESTATE, SAL_RGB(31, 31, 31));
						break;
				}
				break;
			case 13:
				sal_VideoPrint(87, 145 - 36, "Deleting...", SAL_RGB(31, 31, 31));
				break;
		}

		sal_VideoFlip(1);

		switch (action) {
			case 1:
				if (mSaveState[saveno].inUse) {
					action = 3;
				} else {
					action = 2;
				}
				break;
			case 3:
				if (LoadStateFile(mSaveState[saveno].fullFilename)) {
					// Loaded OK. Preview it by running the state for one frame.
					mPreviewingState = 1;
					sal_AudioSetMuted(1);
					GFX.Screen = (uint8 *) &mTempFb[0];
					// GFX.Screen = (uint8 *) sal_VideoGetBuffer();
					IPPU.RenderThisFrame = TRUE;
					unsigned int fullScreenSave = mMenuOptions->fullScreen;
					mMenuOptions->fullScreen = 0;
					S9xMainLoop();
					mMenuOptions->fullScreen = fullScreenSave;
					sal_AudioSetMuted(0);
					mPreviewingState = 0;
					action = 5;
				} else {
					action = 4; // did not load correctly; report an error
				}
				break;
			case 6:
				//Reload state in case user has been previewing
				LoadStateTemp();
				if (SaveStateFile(mSaveState[saveno].fullFilename)) {
					mSaveState[saveno].inUse = 1;
					action = 1;
				} else {
					action = 7; // did not saved correctly; report an error
				}
				break;
			case 8:
				if (LoadStateFile(mSaveState[saveno].fullFilename)) {
					action = 100;  // loaded ok so exit
				} else {
					action = 9; // did not load correctly; report an error
				}
				break;
			case 11:
				action = 1;
				break;
			case 13:
				sal_FileDelete(mSaveState[saveno].fullFilename);
				mSaveState[saveno].inUse = 0;
				action = 1;
				break;
		}

		usleep(10000);
	}

	if (action != 100) {
		LoadStateTemp();
	}

	GFX.Screen = (uint8 *) sal_VideoGetBuffer();
	DeleteStateTemp();
	sal_InputIgnore();
	return(action);
}

static
void RenderMenu(const char *menuName, s32 menuCount, s32 menuSmooth, s32 menufocus)
{
	s32 i = 0;
	u16 color = 0;
	PrintTitle(menuName);

	for (i = 0; i < menuCount; i++) {
		int x = 8;
		int y = (i << 4) - (menuSmooth >> 4);
		y += 88 - 28;

		if (y <= 48 - 28 || y >= 232 - 36) {
			continue;
		}

		if (i == menufocus) {
			color = SAL_RGB(31, 31, 31);
			PrintBar(y - 4);
		} else {
			color = SAL_RGB(31, 31, 31);
		}

		sal_VideoPrint(x,y,mMenuText[i],color);
	}
}


void ShowCredits()
{
	s32 menuExit = 0, menuCount = 0, menufocus = 0, menuSmooth = 0;
	u32 keys = 0;

	strcpy(mMenuText[menuCount++], "PocketSNES - built " __DATE__);
	strcpy(mMenuText[menuCount++], "-------------------------------------");
	strcpy(mMenuText[menuCount++], "Based on Snes9x version " VERSION /* snes9x.h */);
	strcpy(mMenuText[menuCount++], "PocketSNES created by Scott Ramsby");
	strcpy(mMenuText[menuCount++], "Initial port to the Dingoo by Reesy");
	strcpy(mMenuText[menuCount++], "Ported to OpenDingux by pcercuei");
	strcpy(mMenuText[menuCount++], "Optimizations and fixes by Nebuleon");
	strcpy(mMenuText[menuCount++], "Port to RetroGame by Steward-Fu");
	strcpy(mMenuText[menuCount++], "RetroGame optimizations by Sauce,");
	strcpy(mMenuText[menuCount++], "msx and pingflood");

	sal_InputIgnore();

	while (!menuExit) {
		// Draw screen:
		menuSmooth = menuSmooth * 7 + (menufocus << 8); menuSmooth >>= 3;
		RenderMenu("Credits", menuCount, menuSmooth, menufocus);
		sal_VideoFlip(1);

		keys = sal_InputPollRepeat(0);
		menuExit = (keys & INP_BUTTON_MENU_CANCEL);

		if (keys & SAL_INPUT_UP) {
			menufocus--; // Up
			if (menufocus < 0) {
				menufocus = menuCount - 1;
				menuSmooth = (menufocus << 8) - 1;
			}
		}
		else if (keys & SAL_INPUT_DOWN) {
			menufocus++; // Down
			if (menufocus > menuCount - 1) {
				menufocus = 0;
				menuSmooth = (menufocus << 8) - 1;
			}
		}

		usleep(10000);
	}
	sal_InputIgnore();
}

static
void MainMenuUpdateText(s32 menu_index)
{
	switch (menu_index) {
		case MENU_ROM_SELECT:
			strcpy(mMenuText[menu_index], "选择ROM");
			break;
		case SAVESTATE_MENU_LOAD:
			strcpy(mMenuText[menu_index], "加载状态");
			break;
		case SAVESTATE_MENU_SAVE:
			strcpy(mMenuText[menu_index], "保存状态");
			break;
		case MENU_RESET_GAME:
			strcpy(mMenuText[menu_index], "重置");
			break;
		case MENU_EXIT_APP:
			strcpy(mMenuText[menu_index], "退出");
			break;
		case MENU_SETTINGS:
			strcpy(mMenuText[menu_index], "设置");
			break;
	}
}

static
void VideoSettingsMenuUpdateText(s32 menu_index)
{
	switch (menu_index) {
		case VIDEO_SETTINGS_MENU_FRAMESKIP:
			switch (mMenuOptions->frameSkip) {
				case 0:
					strcpy(mMenuText[menu_index],  "Frameskip                  AUTO");
					break;
				default:
					sprintf(mMenuText[menu_index], "Frameskip                     %1d", mMenuOptions->frameSkip - 1);
					break;
			}
			break;

		case VIDEO_SETTINGS_MENU_FPS:
			switch (mMenuOptions->showFps) {
				case 1:
					strcpy(mMenuText[menu_index], "Show FPS                     ON");
					break;
				default:
					strcpy(mMenuText[menu_index], "Show FPS                    OFF");
					break;
			}
			break;

		case VIDEO_SETTINGS_MENU_FULLSCREEN:
			switch (mMenuOptions->fullScreen) {
				case 1:
					strcpy(mMenuText[menu_index], "Video scaling              FAST");
					break;
				case 2:
					strcpy(mMenuText[menu_index], "Video scaling            SMOOTH");
					break;
				case 3:
					strcpy(mMenuText[menu_index], "Video scaling          HARDWARE");
					break;
				case 4:
					strcpy(mMenuText[menu_index], "Video scaling              CROP");
					break;
				default:
					strcpy(mMenuText[menu_index], "Video scaling          ORIGINAL");
					break;
			}
	}
}

static
void AudioSettingsMenuUpdateText(s32 menu_index)
{
	switch (menu_index) {
		case AUDIO_SETTINGS_MENU_SOUND_SYNC:
			switch (mMenuOptions->soundSync) {
				case 0:
					strcpy(mMenuText[menu_index], "Prefer fluid              Video");
					break;
				case 1:
					strcpy(mMenuText[menu_index], "Prefer fluid               Both");
					break;
				default:
					strcpy(mMenuText[menu_index], "Prefer fluid              Audio");
					break;
			}
			break;

		case AUDIO_SETTINGS_MENU_SOUND_ON:
			sprintf(mMenuText[menu_index], "Sound                       %s", mMenuOptions->soundEnabled ? " ON" : "OFF");
			break;

		case AUDIO_SETTINGS_MENU_SOUND_RATE:
			sprintf(mMenuText[menu_index], "Sound rate                %5d", mMenuOptions->soundRate);
			break;

		case AUDIO_SETTINGS_MENU_SOUND_STEREO:
			sprintf(mMenuText[menu_index], "Stereo                      %s", mMenuOptions->stereo ? " ON" : "OFF");
			break;
		// case SETTINGS_MENU_SOUND_VOL:
		// 	sprintf(mMenuText[menu_index], "Volume:                     %d", mMenuOptions->volume);
		// 	break;
	}
}

static
void SettingsMenuUpdateText(s32 menu_index)
{
	switch (menu_index) {
		case VIDEO_MENU_SETTINGS:
			strcpy(mMenuText[menu_index], "Video Settings");
			break;

		case AUDIO_MENU_SETTINGS:
			strcpy(mMenuText[menu_index], "Audio Settings");
			break;

		case SETTINGS_MENU_AUTO_SAVE_SRAM:
			sprintf(mMenuText[menu_index], "Save SRAM                %s", mMenuOptions->autoSaveSram ? "  AUTO" : "MANUAL");
			break;

#if 0
		case SETTINGS_MENU_CPU_SPEED:
			sprintf(mMenuText[menu_index], "Cpu Speed:                  %d", mMenuOptions->cpuSpeed);
			break;
#endif
		case SETTINGS_MENU_SAVE_GLOBAL_SETTINGS:
			sprintf(mMenuText[menu_index], "Global settings            %s", menuGlobalSettings ? "LOAD" : "SAVE");
			break;

		case SETTINGS_MENU_SAVE_CURRENT_SETTINGS:
			switch (menuGameSettings) {
				case 1:
					strcpy(mMenuText[menu_index], "Game settings              LOAD");
					break;
				case 2:
					strcpy(mMenuText[menu_index], "Game settings            DELETE");
					break;
				default:
					strcpy(mMenuText[menu_index], "Game settings              SAVE");
					break;
			}
			break;

		case MENU_CREDITS:
			strcpy(mMenuText[menu_index], "Credits");
			break;
	}
}

static
void SettingsMenuUpdateTextAll(void)
{
	SettingsMenuUpdateText(VIDEO_MENU_SETTINGS);
	SettingsMenuUpdateText(AUDIO_MENU_SETTINGS);
//	SettingsMenuUpdateText(SETTINGS_MENU_CPU_SPEED);
	SettingsMenuUpdateText(SETTINGS_MENU_SAVE_GLOBAL_SETTINGS);
	SettingsMenuUpdateText(SETTINGS_MENU_SAVE_CURRENT_SETTINGS);
	SettingsMenuUpdateText(SETTINGS_MENU_AUTO_SAVE_SRAM);
	SettingsMenuUpdateText(MENU_CREDITS);
}

static
void AudioSettingsMenuUpdateTextAll(void)
{
	AudioSettingsMenuUpdateText(AUDIO_SETTINGS_MENU_SOUND_ON);
	AudioSettingsMenuUpdateText(AUDIO_SETTINGS_MENU_SOUND_STEREO);
	AudioSettingsMenuUpdateText(AUDIO_SETTINGS_MENU_SOUND_RATE);
	AudioSettingsMenuUpdateText(AUDIO_SETTINGS_MENU_SOUND_SYNC);
}

static
void VideoSettingsMenuUpdateTextAll(void)
{
	VideoSettingsMenuUpdateText(VIDEO_SETTINGS_MENU_FRAMESKIP);
	VideoSettingsMenuUpdateText(VIDEO_SETTINGS_MENU_FPS);
	VideoSettingsMenuUpdateText(VIDEO_SETTINGS_MENU_FULLSCREEN);
}

static
void MainMenuUpdateTextAll(void)
{
	MainMenuUpdateText(SAVESTATE_MENU_LOAD);
	MainMenuUpdateText(SAVESTATE_MENU_SAVE);
	MainMenuUpdateText(MENU_RESET_GAME);
	MainMenuUpdateText(MENU_ROM_SELECT);
	MainMenuUpdateText(MENU_SETTINGS);
	MainMenuUpdateText(MENU_EXIT_APP);
}

void LoadCurrentOptions()
{
	if (mRomName[0] != 0) {
		LoadMenuOptions(mSystemDir, mRomName, MENU_OPTIONS_EXT, (char*)mMenuOptions, sizeof(struct MENU_OPTIONS), 1);
	}
	return;
}

void MenuReloadOptions()
{
	if (mRomName[0] != 0) {
		//Load settings for game
		if (LoadMenuOptions(mSystemDir, mRomName, MENU_OPTIONS_EXT, (s8*)mMenuOptions, sizeof(struct MENU_OPTIONS), 0) == SAL_OK) {
			return;
		}
	}

	//Load global settings
	if (LoadMenuOptions(mSystemDir, MENU_OPTIONS_FILENAME, MENU_OPTIONS_EXT, (s8*)mMenuOptions, sizeof(struct MENU_OPTIONS), 0) == SAL_OK) {
		return;
	}

	DefaultMenuOptions();
}

void MenuInit(const char *systemDir, struct MENU_OPTIONS *menuOptions)
{
	s8 filename[SAL_MAX_PATH];
	u16 *pix;
	s32 x;

	strcpy(mSystemDir,systemDir);
	mMenuOptions=menuOptions;

	if (LoadMenuOptions(mSystemDir, DEFAULT_ROM_DIR_FILENAME, DEFAULT_ROM_DIR_EXT, mRomDir, SAL_MAX_PATH, 0) != SAL_OK) {
		strcpy(mRomDir,systemDir);
	}

	pix = &mMenuBackground[0];
	for (x = 0; x < SAL_SCREEN_WIDTH * SAL_SCREEN_HEIGHT; x++) {
		*pix++ = SAL_RGB(0,0,0);
	}

	sal_ImageLoad("backdrop.png", &mMenuBackground, SAL_SCREEN_WIDTH, SAL_SCREEN_HEIGHT);

	MenuReloadOptions();
}

static
s32 VideoSettingsMenu(void)
{
	s32 menuExit = 0, menuCount = VIDEO_SETTINGS_MENU_COUNT, menufocus = 0, menuSmooth = 0;
	s32 action = 0;
	s32 subaction = 0;
	u32 keys = 0;

	VideoSettingsMenuUpdateTextAll();

	sal_InputIgnore();

	while (!menuExit) {
		// Draw screen:
		menuSmooth = menuSmooth * 7 + (menufocus << 8); menuSmooth >>= 3;
		RenderMenu("Video Settings", menuCount, menuSmooth, menufocus);
		sal_VideoFlip(1);

		keys = sal_InputPollRepeat(0);
		menuExit = (keys & INP_BUTTON_MENU_CANCEL);

		if (keys & (SAL_INPUT_LEFT | SAL_INPUT_RIGHT)) {
			switch (menufocus) {
				case VIDEO_SETTINGS_MENU_FRAMESKIP:
					if (keys & SAL_INPUT_RIGHT) {
						mMenuOptions->frameSkip++;
						if (mMenuOptions->frameSkip > 6) mMenuOptions->frameSkip = 0;
					} else {
						mMenuOptions->frameSkip--;
						if (mMenuOptions->frameSkip > 6) mMenuOptions->frameSkip = 6;
					}
					break;

				case VIDEO_SETTINGS_MENU_FPS:
					mMenuOptions->showFps ^= 1;
					break;

				case VIDEO_SETTINGS_MENU_FULLSCREEN:
					int max_val = hwscale ? 4 : 2;
					if (keys & SAL_INPUT_RIGHT) {
						mMenuOptions->fullScreen++;
						if (mMenuOptions->fullScreen > max_val) mMenuOptions->fullScreen = 0;
					} else {
						mMenuOptions->fullScreen--;
						if (mMenuOptions->fullScreen > max_val) mMenuOptions->fullScreen = max_val;
					}
					break;
			}
			VideoSettingsMenuUpdateText(menufocus);
		}
		else if (keys & SAL_INPUT_UP) {
			menufocus--; // Up
			if (menufocus < 0) {
				menufocus = menuCount - 1;
				menuSmooth = (menufocus << 8) - 1;
			}
		}
		else if (keys & SAL_INPUT_DOWN) {
			menufocus++; // Down
			if (menufocus > menuCount - 1) {
				menufocus = 0;
				menuSmooth = (menufocus << 8) - 1;
			}
		}
		usleep(10000);
	}
	sal_InputIgnore();
	return action;
}

static
s32 AudioSettingsMenu(void)
{
	s32 menuExit=0,menuCount=AUDIO_SETTINGS_MENU_COUNT,menufocus=0,menuSmooth=0;
	s32 action=0;
	s32 subaction=0;
	u32 keys=0;

	AudioSettingsMenuUpdateTextAll();

	sal_InputIgnore();

	while (!menuExit) {
		// Draw screen:
		menuSmooth = menuSmooth * 7 + (menufocus << 8); menuSmooth >>= 3;
		RenderMenu("Audio Settings", menuCount, menuSmooth, menufocus);
		sal_VideoFlip(1);

		keys = sal_InputPollRepeat(0);
		menuExit = (keys & INP_BUTTON_MENU_CANCEL);

		if (keys & (SAL_INPUT_LEFT | SAL_INPUT_RIGHT)) {
			switch (menufocus) {
				case AUDIO_SETTINGS_MENU_SOUND_ON:
					mMenuOptions->soundEnabled ^= 1;
					break;

				case AUDIO_SETTINGS_MENU_SOUND_STEREO:
					mMenuOptions->stereo ^= 1;
					break;

				case AUDIO_SETTINGS_MENU_SOUND_SYNC:
					if (keys & SAL_INPUT_RIGHT) {
						mMenuOptions->soundSync++;
						if (mMenuOptions->soundSync > 2) mMenuOptions->soundSync = 0;
					} else {
						mMenuOptions->soundSync--;
						if (mMenuOptions->soundSync > 2) mMenuOptions->soundSync = 2;
					}
					break;

				case AUDIO_SETTINGS_MENU_SOUND_RATE:
					if (keys & SAL_INPUT_RIGHT) {
						mMenuOptions->soundRate = sal_AudioRateNext(mMenuOptions->soundRate);
					} else {
						mMenuOptions->soundRate = sal_AudioRatePrevious(mMenuOptions->soundRate);
					}
					break;

#if 0
				case SETTINGS_MENU_SOUND_VOL:
					if (keys & SAL_INPUT_RIGHT)
					{
						mMenuOptions->volume+=1;
						if (mMenuOptions->volume>31) mMenuOptions->volume=0;
					}
					else
					{
						mMenuOptions->volume-=1;
						if (mMenuOptions->volume>31) mMenuOptions->volume=31;

					}
					SettingsMenuUpdateText(SETTINGS_MENU_SOUND_VOL);
					break;
#endif
			}
			AudioSettingsMenuUpdateText(menufocus);
		}
		else if (keys & SAL_INPUT_UP) {
			menufocus--; // Up
			if (menufocus < 0) {
				menufocus = menuCount - 1;
				menuSmooth = (menufocus << 8) - 1;
			}
		}
		else if (keys & SAL_INPUT_DOWN) {
			menufocus++; // Down
			if (menufocus > menuCount - 1) {
				menufocus = 0;
				menuSmooth = (menufocus << 8) - 1;
			}
		}

		usleep(10000);
	}
	sal_InputIgnore();
	return action;
}

static
s32 SettingsMenu(void)
{
	s32 menuExit = 0, menuCount = SETTINGS_MENU_COUNT, menufocus = 0, menuSmooth = 0;
	s32 action = 0;
	s32 subaction = 0;
	u32 keys = 0;

	SettingsMenuUpdateTextAll();

	sal_InputIgnore();

	while (!menuExit) {
		// Draw screen:
		menuSmooth = menuSmooth * 7 + (menufocus << 8); menuSmooth >>= 3;
		RenderMenu("Settings", menuCount, menuSmooth, menufocus);

		switch (menufocus) {
			case SETTINGS_MENU_AUTO_SAVE_SRAM:
				sal_VideoPrint(60, 180, "Press A to save now", SAL_RGB(31, 31, 31));
				break;

			case SETTINGS_MENU_SAVE_CURRENT_SETTINGS:
				if (mRomName[0] != 0) {
					switch (menuGameSettings) {
						case 1:
							sal_VideoPrint(70, 180, "Press A to load", SAL_RGB(31, 31, 31));
							break;
						case 2:
							sal_VideoPrint(66, 180, "Press A to delete", SAL_RGB(31, 31, 31));
							break;
						default:
							sal_VideoPrint(70, 180, "Press A to save", SAL_RGB(31, 31, 31));
					}
				}
				break;

			case SETTINGS_MENU_SAVE_GLOBAL_SETTINGS:
				if (menuGlobalSettings) {
					sal_VideoPrint(70, 180, "Press A to load", SAL_RGB(31, 31, 31));
				} else {
					sal_VideoPrint(70, 180, "Press A to save", SAL_RGB(31, 31, 31));
				}
				break;
		}

		sal_VideoFlip(1);

		keys = sal_InputPollRepeat(0);
		menuExit = (keys & INP_BUTTON_MENU_CANCEL);

		if (keys & INP_BUTTON_MENU_SELECT) {
			switch (menufocus) {
				case VIDEO_MENU_SETTINGS:
					VideoSettingsMenu();
					SettingsMenuUpdateTextAll();
					break;
				case AUDIO_MENU_SETTINGS:
					AudioSettingsMenu();
					SettingsMenuUpdateTextAll();
					break;
				case SETTINGS_MENU_AUTO_SAVE_SRAM:
					if (mRomName[0] != 0) {
						MenuMessageBox("", "", "Saving SRAM...", MENU_MESSAGE_BOX_MODE_MSG);
						S9xSaveSRAM(1);
						usleep(3e6);
					}
					break;
				case SETTINGS_MENU_SAVE_GLOBAL_SETTINGS:
					if (menuGlobalSettings) {
						LoadMenuOptions(mSystemDir, MENU_OPTIONS_FILENAME, MENU_OPTIONS_EXT, (char*)mMenuOptions, sizeof(struct MENU_OPTIONS), 1);
					} else {
						SaveMenuOptions(mSystemDir, MENU_OPTIONS_FILENAME, MENU_OPTIONS_EXT, (char*)mMenuOptions, sizeof(struct MENU_OPTIONS), 1);
					}
					usleep(3e5);
					break;
				case SETTINGS_MENU_SAVE_CURRENT_SETTINGS:
					if (mRomName[0] != 0) {
						switch (menuGameSettings) {
							case 1:
								LoadMenuOptions(mSystemDir, mRomName, MENU_OPTIONS_EXT, (char*)mMenuOptions, sizeof(struct MENU_OPTIONS), 1);
								break;
							case 2:
								DeleteMenuOptions(mSystemDir, mRomName, MENU_OPTIONS_EXT, 1);
								break;
							default:
								SaveMenuOptions(mSystemDir, mRomName, MENU_OPTIONS_EXT, (char*)mMenuOptions, sizeof(struct MENU_OPTIONS), 1);
						}
						usleep(3e5);
						SettingsMenuUpdateTextAll();
					}
					break;
				case MENU_CREDITS:
					ShowCredits();
					SettingsMenuUpdateTextAll();
					break;
			}
		}
		else if (keys & (SAL_INPUT_LEFT | SAL_INPUT_RIGHT)) {
			switch (menufocus) {
				case SETTINGS_MENU_AUTO_SAVE_SRAM:
					mMenuOptions->autoSaveSram ^= 1;
					break;

				case SETTINGS_MENU_SAVE_GLOBAL_SETTINGS:
					menuGlobalSettings = menuGlobalSettings > 0 ? 0 : 1;
					break;

				case SETTINGS_MENU_SAVE_CURRENT_SETTINGS:
					if (keys & SAL_INPUT_RIGHT) {
						menuGameSettings++;
						if (menuGameSettings > 2) menuGameSettings = 0;
					} else {
						menuGameSettings--;
						if (menuGameSettings > 2) menuGameSettings = 2;
					}
					break;

#if 0
				case SETTINGS_MENU_CPU_SPEED:

					if (keys & SAL_INPUT_RIGHT)
					{
						if (keys&INP_BUTTON_MENU_SELECT)
						{
							mMenuOptions->cpuSpeed=sal_CpuSpeedNextFast(mMenuOptions->cpuSpeed);
						}
						else
						{
							mMenuOptions->cpuSpeed=sal_CpuSpeedNext(mMenuOptions->cpuSpeed);
						}
					}
					else
					{
						if (keys&INP_BUTTON_MENU_SELECT)
						{
							mMenuOptions->cpuSpeed=sal_CpuSpeedPreviousFast(mMenuOptions->cpuSpeed);
						}
						else
						{
							mMenuOptions->cpuSpeed=sal_CpuSpeedPrevious(mMenuOptions->cpuSpeed);
						}
					}
					SettingsMenuUpdateText(SETTINGS_MENU_CPU_SPEED);
					break;
#endif
			}
			SettingsMenuUpdateText(menufocus);
		}
		else if (keys & SAL_INPUT_UP) {
			menufocus--; // Up
			if (menufocus < 0) {
				menufocus = menuCount - 1;
				menuSmooth = (menufocus << 8) - 1;
			}
		}
		else if (keys & SAL_INPUT_DOWN) {
			menufocus++; // Down
			if (menufocus > menuCount - 1) {
				menufocus = 0;
				menuSmooth = (menufocus << 8) - 1;
			}
		}

		usleep(10000);
	}
	sal_InputIgnore();
	return action;
}

s32 MenuRun(s8 *romName)
{
	s32 menuExit = 0, menuCount = MENU_COUNT, menufocus = 0, menuSmooth = 0;
	s32 action = EVENT_NONE;
	s32 subaction = 0;
	u32 keys = 0;

	hwscale = file_exists("/sys/devices/platform/jz-lcd.0/keep_aspect_ratio") || file_exists("/proc/jz/ipu");

	sal_CpuSpeedSet(MENU_NORMAL_CPU_SPEED);

	if (sal_StringCompare(mRomName, romName) != 0) {
		action = EVENT_LOAD_ROM;
		strcpy(mRomName, romName);
		return action;
	}

	if (argv_rom_loaded)
		menuCount = MENU_COUNT - 1;

#if 0
	if ((mMenuOptions->autoSaveSram) && (mRomName[0]!=0))
	{
		MenuMessageBox("Saving SRAM...","","",MENU_MESSAGE_BOX_MODE_MSG);
		S9xSaveSRAM(0);
	}
#endif

	MainMenuUpdateTextAll();
	sal_InputIgnore();

	while (!menuExit) {
		// Draw screen:
		menuSmooth = menuSmooth * 7 + (menufocus << 8); menuSmooth >>= 3;
		RenderMenu("Main Menu", menuCount, menuSmooth, menufocus);
		sal_VideoFlip(1);

		keys = sal_InputPollRepeat(0);

		if (keys & INP_BUTTON_MENU_SELECT) {
			switch (menufocus) {
				case MENU_ROM_SELECT:
					subaction = FileSelect();
					if (subaction == 1) {
						action = EVENT_LOAD_ROM;
						strcpy(romName, mRomName);
						MenuReloadOptions();
						if (!hwscale && mMenuOptions->fullScreen > 2) mMenuOptions->fullScreen = 1;
						menuExit = 1;
					}
					break;
				case SAVESTATE_MENU_LOAD:
					subaction = SaveStateSelect(SAVESTATE_MODE_LOAD);
					if (subaction == 100) {
						menuExit = 1;
						action = EVENT_RUN_ROM;
					}
					break;
				case SAVESTATE_MENU_SAVE:
					SaveStateSelect(SAVESTATE_MODE_SAVE);
					break;
				case MENU_SETTINGS:
					SettingsMenu();
					MainMenuUpdateTextAll();
					break;
				case MENU_RESET_GAME:
					if (mRomName[0] != 0) {
						action=EVENT_RESET_ROM;
						menuExit=1;
					}
					break;
				case MENU_EXIT_APP:
					action = EVENT_EXIT_APP;
					menuExit = 1;
					break;
			}
		}
		else if (keys & INP_BUTTON_MENU_CANCEL) {
			if (mRomName[0] != 0) {
				action = EVENT_RUN_ROM;
				menuExit = 1;
			}
		}
		else if (keys & SAL_INPUT_UP) {
			menufocus--; // Up
			if (menufocus < 0) {
				menufocus = menuCount - 1;
				menuSmooth = (menufocus << 8) - 1;
			}
		}
		else if (keys & SAL_INPUT_DOWN) {
			menufocus++; // Down
			if (menufocus > menuCount - 1) {
				menufocus = 0;
				menuSmooth = (menufocus << 8) - 1;
			}
		}

		usleep(10000);
	}

	sal_InputWaitForRelease();

	return action;
}
