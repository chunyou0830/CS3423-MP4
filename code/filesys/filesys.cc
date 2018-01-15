// filesys.cc 
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk 
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them 
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "copyright.h"
#include "debug.h"
#include "disk.h"
#include "pbitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known 
// sectors, so that they can be located on boot-up.
#define FreeMapSector 		0
#define DirectorySector 	1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number 
// of files that can be loaded onto the disk.
#define FreeMapFileSize 	(NumSectors / BitsInByte)
#define NumDirEntries 		64	// MP4 MODIFIED
#define DirectoryFileSize 	(sizeof(DirectoryEntry) * NumDirEntries)

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).  
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{ 
	DEBUG(dbgFile, "Initializing the file system.");
	if (format) {
		PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
		Directory *directory = new Directory(NumDirEntries);
		FileHeader *mapHdr = new FileHeader;
		FileHeader *dirHdr = new FileHeader;

		DEBUG(dbgFile, "Formatting the file system.");

		// First, allocate space for FileHeaders for the directory and bitmap
		// (make sure no one else grabs these!)
		freeMap->Mark(FreeMapSector);	    
		freeMap->Mark(DirectorySector);

		// Second, allocate space for the data blocks containing the contents
		// of the directory and bitmap files.  There better be enough space!

		ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
		ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

		// Flush the bitmap and directory FileHeaders back to disk
		// We need to do this before we can "Open" the file, since open
		// reads the file header off of disk (and currently the disk has garbage
		// on it!).

		DEBUG(dbgFile, "Writing headers back to disk.");
		mapHdr->WriteBack(FreeMapSector);    
		dirHdr->WriteBack(DirectorySector);

		// OK to open the bitmap and directory files now
		// The file system operations assume these two files are left open
		// while Nachos is running.

		freeMapFile = new OpenFile(FreeMapSector);
		directoryFile = new OpenFile(DirectorySector);
	 
		// Once we have the files "open", we can write the initial version
		// of each file back to disk.  The directory at this point is completely
		// empty; but the bitmap has been changed to reflect the fact that
		// sectors on the disk have been allocated for the file headers and
		// to hold the file data for the directory and bitmap.

		DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
		freeMap->WriteBack(freeMapFile);	 // flush changes to disk
		directory->WriteBack(directoryFile);

		if (debug->IsEnabled('f')) {
			freeMap->Print();
			directory->Print();
		}
		delete freeMap; 
		delete directory; 
		delete mapHdr; 
		delete dirHdr;
	} else {
		// if we are not formatting the disk, just open the files representing
		// the bitmap and directory; these are left open while Nachos is running
		freeMapFile = new OpenFile(FreeMapSector);
		directoryFile = new OpenFile(DirectorySector);
	}
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileSystem::~FileSystem
//----------------------------------------------------------------------
FileSystem::~FileSystem()
{
	delete freeMapFile;
	delete directoryFile;
}

//----------------------------------------------------------------------
// FileSystem::Create
//  MP4 MODIFIED.
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk 
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file 
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

bool FileSystem::Create(char *name, int initialSize)
{
	if(!CheckFileLength(name)){
		return FALSE;
	}
	OpenFile *parentDirectoryFile;

	Directory *parentDirectory;
	PersistentBitmap *freeMap;
	FileHeader *hdr;
	int sector;
	bool success;

	DEBUG(dbgFile, "Creating file " << name << " size " << initialSize);

	char *fileName = GetFileName(name);
	char *dirName = GetDirectoryName(name);
	//cout << "name: " << name << endl << "fileName: " << fileName << endl << "dirName: " << dirName << endl;
	// Default root dir as parent
	parentDirectory = new Directory(NumDirEntries);
	parentDirectoryFile = directoryFile;
	// If not in root dir, change the parent directory
	if(dirName!=NULL){
		Directory *rootDirectory = new Directory(NumDirEntries);
		rootDirectory->FetchFrom(directoryFile);
		int parentDirectoryFileSector = rootDirectory->Find(dirName, true);
		if(parentDirectoryFileSector == -1){
			return FALSE;
		}
		//cout << "Parent Directory File Sector is " << parentDirectoryFileSector << endl;
		parentDirectoryFile = new OpenFile(parentDirectoryFileSector);
		delete rootDirectory;
	}
	parentDirectory->FetchFrom(parentDirectoryFile);
	//parentDirectory->Print();
	
	if (parentDirectory->Find(fileName, false) != -1){
		success = FALSE;			// file is already in directory
	}
	else {	
		freeMap = new PersistentBitmap(freeMapFile,NumSectors);
		sector = freeMap->FindAndSet();	// find a sector to hold the file header
		if (sector == -1) {
			success = FALSE;		// no free block for file header 
		}		
		else if (!parentDirectory->Add(fileName, sector, FILE)){
			success = FALSE;	// no space in directory
		}
		else {
			hdr = new FileHeader;
			if (!hdr->Allocate(freeMap, initialSize)){
				success = FALSE;	// no space on disk for data
			}
			else {	
				success = TRUE;
			// everthing worked, flush all changes back to disk
				hdr->WriteBack(sector);
				parentDirectory->WriteBack(parentDirectoryFile);
				freeMap->WriteBack(freeMapFile);
			}
			delete hdr;
		}
		delete freeMap;
	}
	if(success){
		ASSERT(parentDirectory->Find(fileName, false) != -1);
		//cout << "Pass opening test after file creation" << endl;
	}
	else{
		cout << "File creation not success" << endl;
		success = FALSE;
	}
	delete parentDirectory;
	return success;
}

//----------------------------------------------------------------------
// FileSystem::Open
//  MP4 MODIFIED.
// 	Open a file for reading and writing.  
//	To open a file:
//	  Find the location of the file's header, using the directory 
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile *
FileSystem::Open(char *name)
{ 
	Directory *parentDirectory = new Directory(NumDirEntries);
	OpenFile *openFile = NULL;

	char *fileName = GetFileName(name);
	char *dirName = GetDirectoryName(name);

	DEBUG(dbgFile, "Opening file" << name);

	// Default root dir as parent
	parentDirectory->FetchFrom(directoryFile);
	//parentDirectory->Print();
	// If not in root dir, change the parent directory
	if(dirName != NULL){
		int parentDirectoryFileSector = parentDirectory->Find(dirName, true);
		OpenFile *parentDirectoryFile = new OpenFile(parentDirectoryFileSector);
		parentDirectory->FetchFrom(parentDirectoryFile);
		delete parentDirectoryFile;
	}

	int sector = parentDirectory->Find(fileName, false); 
	//cout << "FOUNDED FILE SECTOR NUMBER " << sector << endl;
	if (sector >= 0){
		openFile = new OpenFile(sector);	// name was found in directory 
		//cout << "File founded" << endl;
	}
	delete parentDirectory;
	return openFile;				// return NULL if not found
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(char *name, bool recursiveflag)
{ 
	Directory *directory;
	PersistentBitmap *freeMap;
	FileHeader *fileHdr;
    OpenFile *of;
	int sector;
	
	directory = new Directory(NumDirEntries);
	directory->FetchFrom(directoryFile);
    char *fileName = GetFileName(name);
    char *dirName = GetDirectoryName(name);
    of = directoryFile;
    if (recursiveflag==false){
        if(dirName != NULL){
            of = new OpenFile(directory->Find(dirName,true));
            directory->FetchFrom(of);
        }
	sector = directory->Find(fileName, false);
	if (sector == -1) {
	   delete directory;
	   return FALSE;			 // file not found 
	}
	fileHdr = new FileHeader;
	fileHdr->FetchFrom(sector);

	freeMap = new PersistentBitmap(freeMapFile,NumSectors);

	fileHdr->Deallocate(freeMap);  		// remove data blocks
	freeMap->Clear(sector);			// remove header block
	directory->Remove(filename);

	freeMap->WriteBack(freeMapFile);		// flush to disk
	directory->WriteBack(of);        // flush to disk
}else{
    freeMap = new PersistentBitmap(freeMapFile, NumSectors);
    sector = directory->Find(fileName,true);
    if (sector == -1) {
       delete directory;
       return FALSE;             // file not found 
    }
    Directory *dirtemp = new Directory(NumDirEntries);
    OpenFile *temp = directoryFile;
    dirtemp->FetchFrom(directoryFile);
    if(dirName != NULL){
        of = new OpenFile(directory->Find(dirName,true));
        directory->FetchFrom(temp);
    }

    of = new OpenFile(sector);
    directory->FetchFrom(of);
    directory->RemoveAll(freeMap,of);

    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    fileHdr->Deallocate(freeMap);
    freeMap->Clear(sector);

    dirtemp->Remove(fileName);

    freeMap->WriteBack(freeMapFile);
    dirtemp->WriteBack(temp);
}
	delete fileHdr;
	delete directory;
	delete freeMap;
    delete of;
	return TRUE;
} 

//----------------------------------------------------------------------
// FileSystem::List
//  MP4 MODIFIED.
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void FileSystem::List(char *name, bool recursively)
{
	Directory *rootDirectory = new Directory(NumDirEntries);
	rootDirectory->FetchFrom(directoryFile);

	char *dirName = name;

	// If not listing for root directory
	if(strlen(name)>1){
		dirName = GetFileName(name);
		OpenFile *childDirectoryFile = new OpenFile(rootDirectory->Find(dirName, true));
		Directory *childDirectory = new Directory(NumDirEntries);
		childDirectory->FetchFrom(childDirectoryFile);
		childDirectory->List(0, recursively);

		delete childDirectory;
		delete childDirectoryFile;
	}
	// If listing for root directory
	else{
		rootDirectory->List(0, recursively);
	}

	delete rootDirectory;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
	FileHeader *bitHdr = new FileHeader;
	FileHeader *dirHdr = new FileHeader;
	PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile,NumSectors);
	Directory *directory = new Directory(NumDirEntries);

	printf("Bit map file header:\n");
	bitHdr->FetchFrom(FreeMapSector);
	bitHdr->Print();

	printf("Directory file header:\n");
	dirHdr->FetchFrom(DirectorySector);
	dirHdr->Print();

	freeMap->Print();

	directory->FetchFrom(directoryFile);
	directory->Print();

	delete bitHdr;
	delete dirHdr;
	delete freeMap;
	delete directory;
} 

//----------------------------------------------------------------------
// FileSystem::GetFileName
//  MP4 MODIFIED. Get the filename from full file path.
//----------------------------------------------------------------------

char* FileSystem::GetFileName(char *fullpath)
{
	//DEBUG(dbgFile, "Get file name");
	char *filename;
	filename = strrchr(fullpath, '/');
	filename++;
	//DEBUG(dbgFile, "Get file name finished");
	//cout << "GetFileName " << filename << endl;
	return filename;
}

//----------------------------------------------------------------------
// FileSystem::GetDirectoryName
//  MP4 MODIFIED. Get the last parent directory from full file path.
//----------------------------------------------------------------------

char* FileSystem::GetDirectoryName(char *fullpath)
{
	char *fullpath_cp = (char*)malloc(sizeof(char) * (strlen(fullpath)+1));
	//cout << "Input dir name is " << fullpath << endl;
	memcpy(fullpath_cp, fullpath, strlen(fullpath)+1);
	//cout << "Copied content" << fullpath_cp << endl;
	//DEBUG(dbgFile, "Get directory name");
	char *filename = GetFileName(fullpath_cp);
	char *dirname = strtok(fullpath_cp, "/");
	char *parent = NULL;
	while(dirname != filename){
		parent = dirname;
		dirname = strtok(NULL, "/");
	}
	//cout << "GetDirName " << parent << endl;
	return parent;
}

//----------------------------------------------------------------------
// FileSystem::CheckFileLength
//  MP4 MODIFIED. Check if the length of the path and filename satisfies the work request.
//----------------------------------------------------------------------

bool FileSystem::CheckFileLength(char *fullpath)
{
	DEBUG(dbgFile, "Checking file length");
	char *filename = GetFileName(fullpath);
	//cout <<	"FILENAME" << strlen(filename) << endl << "FULLPATH" << strlen(fullpath) << endl;
	if(strlen(filename)>9){
		cout << "File name too long." << endl;
		return FALSE;
	}
	if(strlen(fullpath)>255){
		cout << "File path too long." << endl;
		return FALSE;
	}
	DEBUG(dbgFile, "Checking file length passed");
	return TRUE;
}

//----------------------------------------------------------------------
// FileSystem::CreateDirectory
//  MP4 MODIFIED. Check if the length of the path and filename satisfies the work request.
//----------------------------------------------------------------------

void FileSystem::CreateDirectory(char *fullpath)
{
	if(!CheckFileLength(fullpath)){
		return;
	}
	PersistentBitmap * freeMap = new PersistentBitmap(freeMapFile, NumSectors);
	OpenFile *freeMapFile = new OpenFile(FreeMapSector);
	
	char *fileName = GetFileName(fullpath);
	//cout << "Getted file name" << endl;
	char *dirName = GetDirectoryName(fullpath);
	//cout << "Getted dir name" << endl;
	FileHeader *hdr = new FileHeader;
	hdr->Allocate(freeMap, DirectoryFileSize);

	Directory *rootDirectory = new Directory(NumDirEntries);
	//cout << "fetcfh root dir" << endl;
	rootDirectory->FetchFrom(directoryFile);
	//cout << "Create new dir instance" << endl;
	Directory *newDirectory = new Directory(NumDirEntries);
	//cout << "Before inside if section" << endl;
	// Creating directory in root
	if(dirName == NULL){
		int sector = freeMap->FindAndSet();
		//cout << "Allocated new dir at sector " << sector << endl;
		hdr->WriteBack(sector);
		//cout << "new openfile at sector" << endl;
		OpenFile *newDirectoryFile = new OpenFile(sector);
		//cout << "Write Back" << endl;
		newDirectory->WriteBack(newDirectoryFile);
		//cout << "root dir add" << endl;
		rootDirectory->Add(fileName, sector, DIR);
		//cout << "root dir writeback" << endl;
		rootDirectory->WriteBack(directoryFile);

		delete newDirectoryFile;
	}
	// Creating directory in a directory
	else{
		int parentDirectoryFileSector = rootDirectory->Find(dirName, true);
		//cout << "Parent Directory File Sector is " << parentDirectoryFileSector << endl;
		// If cannot find parent dir, then return
		if(parentDirectoryFileSector == -1){
			cout << "Invalid path" << endl;
			return;
		}
		else{
			Directory *parentDirectory = new Directory(NumDirEntries);
			OpenFile *parentDirectoryFile = new OpenFile(parentDirectoryFileSector);
			parentDirectory->FetchFrom(parentDirectoryFile);

			int sector = freeMap->FindAndSet();
			//cout << "Allocated new dir at sector " << sector << endl;
			hdr->WriteBack(sector);
			OpenFile *newDirectoryFile = new OpenFile(sector);
			newDirectory->WriteBack(newDirectoryFile);

			parentDirectory->Add(fileName, sector, DIR);
			parentDirectory->WriteBack(parentDirectoryFile);

			delete parentDirectory;
			delete parentDirectoryFile;
			delete newDirectoryFile;
		}
	}

	delete rootDirectory;
	//newDirectory->Print();
	delete newDirectory;

	freeMap->WriteBack(freeMapFile);
	delete freeMapFile;
	delete freeMap;
	delete hdr;
}
	
int FileSystem::Write(char *buf, int len, int id){
	OpenFile* of;
	if(fileDescriptorTable[id-1]==NULL||(id-1)<0||(id-1)>=20){
		return -1;
	}
	of = fileDescriptorTable[id-1];
	int result;
	result = of->Write(buf,len);
	return result;
}

int FileSystem::Read(char *buf, int len, int id){
	OpenFile* of;
	if(fileDescriptorTable[id-1]==NULL||(id-1)<0||(id-1)>=20){
		return -1;
	}
	of = fileDescriptorTable[id-1];
	int result;
	result = of->Read(buf,len);
	return result;
}

int FileSystem::Close(int id){
	if(fileDescriptorTable[id-1]==NULL||(id-1)<0||(id-1)>=20){
		return 0;
	}
	fileDescriptorTable[id-1]==NULL;
	return 1;
}

#endif // FILESYS_STUB
